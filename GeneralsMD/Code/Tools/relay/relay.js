'use strict';
// Virtual LAN relay: players behind NAT each get a virtual 10.x.y.z address and the
// relay forwards the game's LAN packets between them. Wire format in README.md.
// Run: node relay.js   (UDP port from RELAY_PORT, default 8094)

const dgram = require('dgram');
const crypto = require('crypto');

const MAGIC0 = 0x5a; // 'Z'
const MAGIC1 = 0x52; // 'R'
const T_HELLO = 1;
const T_WELCOME = 2;
const T_DATA = 3;
const T_HELLO_AUTH = 4;
const BROADCAST = 0xffffffff;
const NO_SESSION = '0000000000000000';

const DEFAULT_PORT = 8094;
const MAX_PACKET = 1400;            // bytes, whole datagram, either direction
const HELLO_HEADER = 23;            // magic(2) type(1) exeCRC(4) iniCRC(4) session(8) wantVip(4)
const HELLO_AUTH_HEADER = 39;       // HELLO's header, then ticket(16)
const WELCOME_SIZE = 15;            // magic(2) type(1) session(8) vip(4)
const DATA_IN_HEADER = 19;          // magic(2) type(1) session(8) dstVip(4) dstPort(2) srcPort(2)
const DATA_OUT_HEADER = 11;         // magic(2) type(1) srcVip(4) srcPort(2) dstPort(2)
const MAX_NAME = 32;                // bytes of UTF-8
const IDLE_MS = 120000;             // a session silent this long is dropped; outlasts one long blocking step of a map load
const MAX_SESSIONS = 4096;
const MAX_SESSIONS_PER_IP = 8;      // one host cannot take the whole table
const MAX_ROOM = 16;                // a broadcast reaches at most 15 others
const MAX_BUCKETS = 65536;          // rate limit entries; new source addresses are dropped while it is full
const RATE_BYTES_PER_SEC = 512 * 1024;  // sustained, per source IP
const BURST_BYTES = 2 * 1024 * 1024;    // a map transfer goes out in one burst
const PACKET_COST_MIN = 64;         // a tiny packet still costs this much, so floods of them drain the bucket
const SWEEP_MS = 5000;
const STATS_MS = 60000;

const vipString = (v) => `${v >>> 24}.${(v >>> 16) & 255}.${(v >>> 8) & 255}.${v & 255}`;
const hex = (n) => n.toString(16).padStart(8, '0');

function createRelay(opts = {}) {
	const now = opts.now || Date.now;
	const log = opts.log || ((line) => console.log(`${new Date().toISOString()} ${line}`));
	const rate = opts.rateBytesPerSec ?? RATE_BYTES_PER_SEC;
	const burst = opts.burstBytes ?? BURST_BYTES;
	const maxPerIp = opts.maxPerIp ?? MAX_SESSIONS_PER_IP;
	const maxRoom = opts.maxRoom ?? MAX_ROOM;
	const maxBuckets = opts.maxBuckets ?? MAX_BUCKETS;
	const bucketFullMs = (burst / rate) * 1000;  // an untouched bucket is full again after this, as good as new
	const authorize = opts.authorize;             // ticket hex -> account id, or null; without it HELLO_AUTH is refused
	const onAuthorized = opts.onAuthorized || (() => {});
	const onLeave = opts.onLeave || (() => {});

	const sock = dgram.createSocket('udp4');
	const sessions = new Map();   // session id (16 hex digits) -> session
	const byVip = new Map();      // vip -> session
	const byEndpoint = new Map(); // "ip:port" -> session
	const rooms = new Map();      // "exe:ini", "acct:<id>" or whatever setRoom names -> Set of sessions
	const accounts = new Map();   // account id -> its one session
	const perIp = new Map();      // ip that opened them -> session count
	const buckets = new Map();    // source ip -> { tokens, at }
	const stats = { pktIn: 0, bytesIn: 0, pktOut: 0, bytesOut: 0, dropped: 0 };
	let nextVip = 1;
	let timers = [];

	// A client may ask for an address (a -netgame slot list names each copy's, and a client whose
	// session was forgotten asks for the one it had); it gets it when it is a usable 10.x.y.z nobody
	// holds, the next free one otherwise.
	function allocVip(want) {
		const low = want & 255;
		if (want >>> 24 === 10 && low !== 0 && low !== 255 && !byVip.has(want)) return want;
		for (let i = 0; i < 0xffffff; i++) {
			const n = nextVip;
			nextVip = nextVip >= 0xfffffe ? 1 : nextVip + 1;
			const low = n & 255;
			if (low === 0 || low === 255) continue;
			const vip = (0x0a000000 | n) >>> 0;
			if (!byVip.has(vip)) return vip;
		}
		return 0;
	}

	function newSessionId() {
		for (;;) {
			const id = crypto.randomBytes(8).toString('hex');
			if (id !== NO_SESSION && !sessions.has(id)) return id;
		}
	}

	function allow(ip, cost) {
		const t = now();
		let b = buckets.get(ip);
		if (!b) {
			if (buckets.size >= maxBuckets) return false;
			buckets.set(ip, (b = { tokens: burst, at: t }));
		}
		b.tokens = Math.min(burst, b.tokens + ((t - b.at) * rate) / 1000);
		b.at = t;
		if (b.tokens < cost) return false;
		b.tokens -= cost;
		return true;
	}

	function send(buf, s) {
		stats.pktOut++;
		stats.bytesOut += buf.length;
		sock.send(buf, s.port, s.address);
	}

	function leaveRoom(s) {
		const members = rooms.get(s.room);
		members.delete(s);
		if (!members.size) rooms.delete(s.room);
	}

	function remove(s, reason) {
		sessions.delete(s.id);
		byVip.delete(s.vip);
		if (byEndpoint.get(s.key) === s) byEndpoint.delete(s.key);
		leaveRoom(s);
		const n = perIp.get(s.ip) - 1;
		if (n) perIp.set(s.ip, n); else perIp.delete(s.ip);
		log(`leave room=${s.room} vip=${vipString(s.vip)} reason=${reason}`);
		if (s.account !== null) {
			if (accounts.get(s.account) === s) accounts.delete(s.account);
			onLeave(s.account, s.seen);
		}
	}

	function place(s, rinfo, key) {
		if (byEndpoint.get(s.key) === s) byEndpoint.delete(s.key);
		s.key = key;
		s.address = rinfo.address;
		s.port = rinfo.port;
		byEndpoint.set(key, s);
	}

	function create(exe, ini, want, name, rinfo, key, room = `${hex(exe)}:${hex(ini)}`) {
		const members = rooms.get(room) || new Set();
		if (sessions.size >= MAX_SESSIONS || (perIp.get(rinfo.address) || 0) >= maxPerIp || members.size >= maxRoom) return null;
		const vip = allocVip(want);
		if (!vip) return null;
		const s = { id: newSessionId(), vip, exe, ini, room, name, ip: rinfo.address, key: null, seen: 0, verified: false, account: null };
		sessions.set(s.id, s);
		byVip.set(vip, s);
		rooms.set(room, members.add(s));
		perIp.set(s.ip, (perIp.get(s.ip) || 0) + 1);
		place(s, rinfo, key);
		log(`join room=${room} vip=${vipString(vip)} name=${JSON.stringify(name)}`);
		return s;
	}

	// A new session for whoever the ticket belongs to, in a room of its own. An account holds one session:
	// logging in again replaces the old one. authorize is asked once per new session, never on a retry
	// or keepalive, so the server may make a ticket single-use.
	function login(exe, ini, want, name, rinfo, key, ticket) {
		let account = null;
		try {
			account = authorize(ticket) ?? null;
		} catch (e) {
			log(`authorize threw ${e.message}`);
		}
		if (account === null) return null;
		const old = accounts.get(account);
		if (old) remove(old, 'replaced');
		const s = create(exe, ini, want, name, rinfo, key, `acct:${account}`);
		if (!s) return null;
		s.account = account;
		accounts.set(account, s);
		onAuthorized(account, s.vip);
		return s;
	}

	/* A session is forwarded to only once a packet carrying its id came back from its endpoint, and the
		 id only ever goes out in a WELCOME to that endpoint. A HELLO with a forged source address therefore
		 opens a session nothing reaches, and the relay cannot be aimed at someone else. */
	function hello(msg, rinfo, key, auth) {
		const exe = msg.readUInt32BE(3);
		const ini = msg.readUInt32BE(7);
		const id = msg.toString('hex', 11, 19);
		const want = msg.readUInt32BE(19);
		const nameAt = auth ? HELLO_AUTH_HEADER : HELLO_HEADER;
		const name = msg.subarray(nameAt, nameAt + MAX_NAME).toString('utf8');
		let s = id !== NO_SESSION ? sessions.get(id) : undefined;
		if (s && (s.exe !== exe || s.ini !== ini)) {
			remove(s, 'version changed');
			s = undefined;
		}
		if (s && s.key !== key) {
			// NAT moved the client. Never onto an endpoint another session holds, and under a new id, which
			// only whoever reads the new endpoint learns.
			if (byEndpoint.has(key)) return false;
			sessions.delete(s.id);
			s.id = newSessionId();
			sessions.set(s.id, s);
			s.verified = false;
			place(s, rinfo, key);
		} else if (s) {
			s.verified = true;
		} else {
			// A HELLO the endpoint's session did not sign: the client retrying before our WELCOME reached
			// it, or one that lost its id. Same CRCs get that session; other CRCs are dropped, so a forged
			// HELLO from someone's endpoint cannot end their session. A HELLO_AUTH gets back only a session that
			// is already authorised.
			s = byEndpoint.get(key);
			if (s && (s.exe !== exe || s.ini !== ini || (auth && s.account === null))) return false;
			if (!s) s = auth ? login(exe, ini, want, name, rinfo, key, msg.toString('hex', HELLO_HEADER, HELLO_AUTH_HEADER)) : create(exe, ini, want, name, rinfo, key);
			if (!s) return false;
		}
		s.seen = now();
		const w = Buffer.alloc(WELCOME_SIZE);
		w[0] = MAGIC0;
		w[1] = MAGIC1;
		w[2] = T_WELCOME;
		w.write(s.id, 3, 'hex');
		w.writeUInt32BE(s.vip, 11);
		send(w, s);
		return true;
	}

	function data(msg, ip, key) {
		const s = sessions.get(msg.toString('hex', 3, 11));
		if (!s || s.key !== key) return false;  // only a HELLO moves a session to another endpoint
		s.verified = true;
		s.seen = now();
		const dst = msg.readUInt32BE(11);
		const targets = [];
		if (dst === BROADCAST) {
			for (const t of rooms.get(s.room)) if (t !== s && t.verified) targets.push(t);
		} else {
			const t = byVip.get(dst);
			if (!t || t === s || t.room !== s.room || !t.verified) return false;
			targets.push(t);
		}
		// the sender pays for every copy, so a broadcast is no cheaper than sending each one itself
		if (!allow(ip, Math.max(msg.length, PACKET_COST_MIN) * Math.max(targets.length, 1))) return false;
		const out = Buffer.allocUnsafe(msg.length - DATA_IN_HEADER + DATA_OUT_HEADER);
		out[0] = MAGIC0;
		out[1] = MAGIC1;
		out[2] = T_DATA;
		out.writeUInt32BE(s.vip, 3);
		out.writeUInt16BE(msg.readUInt16BE(17), 7);  // srcPort
		out.writeUInt16BE(msg.readUInt16BE(15), 9);  // dstPort
		msg.copy(out, DATA_OUT_HEADER, DATA_IN_HEADER);
		for (const t of targets) send(out, t);
		return true;
	}

	function onMessage(msg, rinfo) {
		stats.pktIn++;
		stats.bytesIn += msg.length;
		const key = `${rinfo.address}:${rinfo.port}`;
		let ok = false;
		if (msg.length >= 3 && msg.length <= MAX_PACKET && msg[0] === MAGIC0 && msg[1] === MAGIC1) {
			if (msg[2] === T_HELLO && msg.length >= HELLO_HEADER) ok = allow(rinfo.address, Math.max(msg.length, PACKET_COST_MIN)) && hello(msg, rinfo, key, false);
			else if (msg[2] === T_HELLO_AUTH && msg.length >= HELLO_AUTH_HEADER && authorize) ok = allow(rinfo.address, Math.max(msg.length, PACKET_COST_MIN)) && hello(msg, rinfo, key, true);
			else if (msg[2] === T_DATA && msg.length >= DATA_IN_HEADER) ok = data(msg, rinfo.address, key);
		}
		if (!ok) stats.dropped++;
	}

	function sweep() {
		const t = now();
		// oldest silence first, so onLeave reports who went quiet first
		const idle = [...sessions.values()].filter((s) => t - s.seen > IDLE_MS).sort((a, b) => a.seen - b.seen);
		for (const s of idle) remove(s, 'idle');
		for (const [k, b] of buckets) if (t - b.at > bucketFullMs) buckets.delete(k);
	}

	function report() {
		log(`stats sessions=${sessions.size} in=${stats.pktIn}p/${stats.bytesIn}B out=${stats.pktOut}p/${stats.bytesOut}B dropped=${stats.dropped}p`);
		for (const k of Object.keys(stats)) stats[k] = 0;
	}

	sock.on('message', onMessage);
	sock.on('error', (e) => log(`socket error ${e.message}`));

	return {
		sessions,
		buckets,
		sweep,
		report,
		// Moves an authorised session to another room; unicast and broadcast stay inside it. The caller is
		// trusted, so the room cap does not apply. False when the account holds no session.
		setRoom(account, room) {
			const s = accounts.get(account);
			if (!s) return false;
			log(`room vip=${vipString(s.vip)} from=${s.room} to=${room}`);
			leaveRoom(s);
			s.room = room;
			rooms.set(room, (rooms.get(room) || new Set()).add(s));
			return true;
		},
		vipOf: (account) => accounts.get(account)?.vip ?? null,
		start(port = DEFAULT_PORT, host) {
			return new Promise((resolve, reject) => {
				sock.once('error', reject);
				sock.bind(port, host, () => {
					sock.off('error', reject);
					timers = [setInterval(sweep, SWEEP_MS), setInterval(report, STATS_MS)];
					resolve(sock.address().port);
				});
			});
		},
		stop() {
			timers.forEach(clearInterval);
			sock.close();
		},
	};
}

module.exports = { createRelay, MAX_PACKET, IDLE_MS, NO_SESSION, HELLO_HEADER, HELLO_AUTH_HEADER, DATA_IN_HEADER, DATA_OUT_HEADER, vipString };

if (require.main === module) {
	const port = Number(process.env.RELAY_PORT) || DEFAULT_PORT;
	createRelay()
		.start(port)
		.then((p) => console.log(`${new Date().toISOString()} relay listening on udp/${p}`))
		.catch((e) => {
			console.error(`relay: cannot bind udp/${port}: ${e.message}`);
			process.exit(1);
		});
}
