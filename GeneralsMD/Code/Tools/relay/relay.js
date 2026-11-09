'use strict';
// Virtual LAN relay: players behind NAT each get a virtual 10.x.y.z address and the
// relay forwards the game's LAN packets between them. Wire format in README.md.
// Run: node relay.js   (UDP port from RELAY_PORT, default 8094)

const dgram = require('dgram');
const crypto = require('crypto');
const { Worker, isMainThread, parentPort, workerData } = require('worker_threads');

const MAGIC0 = 0x5a; // 'Z'
const MAGIC1 = 0x52; // 'R'
const T_HELLO = 1;
const T_WELCOME = 2;
const T_DATA = 3;
const T_HELLO_AUTH = 4;
const BROADCAST = 0xffffffff;
const NO_SESSION = '0000000000000000';
const HI = 0x100000000;
const ID_HI_MASK = 0x1fffff;        // ids the relay hands out stay under 2^53, so a session is keyed by a plain number

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
const endpoint = (rinfo) => `${rinfo.address}:${rinfo.port}`;
// The 8 session bytes at `at` as a number. Exact below 2^53, which is every id the relay hands out; a
// larger one comes out at 2^53 or above and matches nothing.
const sessionKey = (buf, at) => buf.readUInt32BE(at) * HI + buf.readUInt32BE(at + 4);
const defaultLog = (line) => console.log(`${new Date().toISOString()} ${line}`);

/* Why a datagram went nowhere. bad: wrong magic, type or length. hello: a HELLO refused. rate: over the
	 sender's bucket. nosession: an id the relay does not hold (a stale one after a NAT move or a replace).
	 endpoint: a held id from an endpoint that is not its own. notarget: no session on that address.
	 room: the target is in another room. unverified: the target has not answered its WELCOME yet.
	 alone: a broadcast nobody in the room could take; not counted in `dropped`, a lone lobby does it. */
const discards = () => ({ bad: 0, hello: 0, rate: 0, nosession: 0, endpoint: 0, notarget: 0, room: 0, unverified: 0, alone: 0 });
const CAUSES = Object.keys(discards());

function createRelay(opts = {}) {
	if (opts.thread) return createRelayThread(opts);
	const now = opts.now || Date.now;
	const log = opts.log || defaultLog;
	const rate = opts.rateBytesPerSec ?? RATE_BYTES_PER_SEC;
	const burst = opts.burstBytes ?? BURST_BYTES;
	const maxPerIp = opts.maxPerIp ?? MAX_SESSIONS_PER_IP;
	const maxRoom = opts.maxRoom ?? MAX_ROOM;
	const maxBuckets = opts.maxBuckets ?? MAX_BUCKETS;
	const bucketFullMs = (burst / rate) * 1000;  // an untouched bucket is full again after this, as good as new
	const authorize = opts.authorize;             // ticket hex -> account id, null, or a promise of either; without it HELLO_AUTH is refused
	const onAuthorized = opts.onAuthorized || (() => {});
	const onLeave = opts.onLeave || (() => {});

	const sock = dgram.createSocket('udp4');
	const sessions = new Map();   // session id (sessionKey of its 8 bytes) -> session
	const byVip = new Map();      // vip -> session
	const byEndpoint = new Map(); // "ip:port" -> session
	const rooms = new Map();      // "exe:ini", "acct:<id>" or whatever setRoom names -> Set of sessions
	const accounts = new Map();   // account id -> its one session
	const perIp = new Map();      // ip that opened them -> session count
	const buckets = new Map();    // source ip -> { tokens, at }
	const authing = new Set();    // endpoints whose HELLO_AUTH waits on an authorize promise
	const stats = { pktIn: 0, bytesIn: 0, pktOut: 0, bytesOut: 0, drop: discards() };
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
			const b = crypto.randomBytes(8);
			const id = (b.readUInt32BE(0) & ID_HI_MASK) * HI + b.readUInt32BE(4);
			if (id !== 0 && !sessions.has(id)) return id;
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

	// s sent it, t was its target; either may be unknown
	function drop(cause, s, t) {
		stats.drop[cause]++;
		if (s) s.drop[cause]++;
		if (t) t.dropTo[cause]++;
	}

	// a session's counters since it entered its room, for the leave and room lines
	function tally(s, t) {
		let line = ` in=${s.inP}p/${s.inB}B out=${s.outP}p lastData=${s.lastData ? `${t - s.lastData}ms` : 'never'} seen=${t - s.seen}ms`;
		for (const c of CAUSES) if (s.drop[c]) line += ` drop.${c}=${s.drop[c]}`;
		for (const c of CAUSES) if (s.dropTo[c]) line += ` to.${c}=${s.dropTo[c]}`;
		return line;
	}

	function snapshot(s, t) {
		return {
			vip: vipString(s.vip), room: s.room, at: t, inP: s.inP, inB: s.inB, outP: s.outP,
			lastDataMs: s.lastData ? t - s.lastData : null, seenMs: t - s.seen, drop: { ...s.drop }, dropTo: { ...s.dropTo },
		};
	}

	function leaveRoom(s) {
		const members = rooms.get(s.room);
		members.delete(s);
		if (members.size) return;
		rooms.delete(s.room);
		if (s.room !== s.home) log(`empty room=${s.room}`);
	}

	function remove(s, reason) {
		log(`leave room=${s.room} vip=${vipString(s.vip)}${tally(s, now())} reason=${reason}`);
		sessions.delete(s.id);
		byVip.delete(s.vip);
		if (byEndpoint.get(s.key) === s) byEndpoint.delete(s.key);
		leaveRoom(s);
		const n = perIp.get(s.ip) - 1;
		if (n) perIp.set(s.ip, n); else perIp.delete(s.ip);
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
		const s = {
			id: newSessionId(), vip, exe, ini, room, home: room, name, ip: rinfo.address, key: null, seen: 0, verified: false, account: null,
			lastData: 0, inP: 0, inB: 0, outP: 0, drop: discards(), dropTo: discards(), last: null,
		};
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
	// or keepalive, so the server may make a ticket single-use. When it answers with a promise (the relay
	// in a thread asking the server's), a retry from that endpoint is ignored until it settles.
	function login(exe, ini, want, name, rinfo, key, ticket) {
		if (authing.has(key)) return true;
		let account = null;
		try {
			account = authorize(ticket) ?? null;
		} catch (e) {
			log(`authorize threw ${e.message}`);
		}
		if (account !== null && typeof account.then === 'function') {
			authing.add(key);
			account.then((a) => a ?? null, (e) => { log(`authorize threw ${e.message}`); return null; }).then((a) => {
				authing.delete(key);
				if (byEndpoint.has(key) || !admit(a, exe, ini, want, name, rinfo, key)) drop('hello');
			});
			return true;
		}
		return admit(account, exe, ini, want, name, rinfo, key);
	}

	function admit(account, exe, ini, want, name, rinfo, key) {
		if (account === null) return false;
		const old = accounts.get(account);
		if (old) remove(old, 'replaced');
		const s = create(exe, ini, want, name, rinfo, key, `acct:${account}`);
		if (!s) return false;
		s.account = account;
		accounts.set(account, s);
		onAuthorized(account, s.vip);
		welcome(s);
		return true;
	}

	function welcome(s) {
		s.seen = now();
		const w = Buffer.alloc(WELCOME_SIZE);
		w[0] = MAGIC0;
		w[1] = MAGIC1;
		w[2] = T_WELCOME;
		w.writeUInt32BE(Math.floor(s.id / HI), 3);
		w.writeUInt32BE(s.id >>> 0, 7);
		w.writeUInt32BE(s.vip, 11);
		send(w, s);
	}

	/* A session is forwarded to only once a packet carrying its id came back from its endpoint, and the
		 id only ever goes out in a WELCOME to that endpoint. A HELLO with a forged source address therefore
		 opens a session nothing reaches, and the relay cannot be aimed at someone else. */
	function hello(msg, rinfo, key, auth) {
		const exe = msg.readUInt32BE(3);
		const ini = msg.readUInt32BE(7);
		const id = sessionKey(msg, 11);
		const want = msg.readUInt32BE(19);
		const nameAt = auth ? HELLO_AUTH_HEADER : HELLO_HEADER;
		const name = msg.subarray(nameAt, nameAt + MAX_NAME).toString('utf8');
		let s = id !== 0 ? sessions.get(id) : undefined;
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
			if (!s && auth) return login(exe, ini, want, name, rinfo, key, msg.toString('hex', HELLO_HEADER, HELLO_AUTH_HEADER));
			if (!s) s = create(exe, ini, want, name, rinfo, key);
			if (!s) return false;
		}
		welcome(s);
		return true;
	}

	// The forwarding path, run for every game packet: no string, array or buffer is made here. The datagram
	// Node handed over is ours, so the out header is written over the end of the in header and the
	// payload goes back out from where it arrived.
	function data(msg, rinfo) {
		const s = sessions.get(sessionKey(msg, 3));
		if (!s) return drop('nosession', byEndpoint.get(endpoint(rinfo)));
		s.inP++;
		s.inB += msg.length;
		if (s.port !== rinfo.port || s.address !== rinfo.address) return drop('endpoint', s);  // only a HELLO moves a session to another endpoint
		s.verified = true;
		s.lastData = s.seen = now();
		const dst = msg.readUInt32BE(11);
		let t = null;
		let n = 0;
		if (dst === BROADCAST) {
			for (const m of rooms.get(s.room)) {
				if (m === s) continue;
				if (m.verified) n++;
				else m.dropTo.unverified++;  // its copy only; the others still get theirs
			}
		} else {
			t = byVip.get(dst);
			if (!t || t === s) return drop('notarget', s);
			if (t.room !== s.room) return drop('room', s, t);
			if (!t.verified) return drop('unverified', s, t);
			n = 1;
		}
		// the sender pays for every copy, so a broadcast is no cheaper than sending each one itself
		if (!allow(rinfo.address, Math.max(msg.length, PACKET_COST_MIN) * Math.max(n, 1))) {
			if (t) t.dropTo.rate++;
			else for (const m of rooms.get(s.room)) if (m !== s && m.verified) m.dropTo.rate++;
			return drop('rate', s);
		}
		if (!n) return drop('alone', s);
		const srcPort = msg.readUInt16BE(17);
		const dstPort = msg.readUInt16BE(15);
		const out = msg.subarray(DATA_IN_HEADER - DATA_OUT_HEADER);
		out[0] = MAGIC0;
		out[1] = MAGIC1;
		out[2] = T_DATA;
		out.writeUInt32BE(s.vip, 3);
		out.writeUInt16BE(srcPort, 7);
		out.writeUInt16BE(dstPort, 9);
		if (t) {
			t.outP++;
			return send(out, t);
		}
		for (const m of rooms.get(s.room)) {
			if (m === s || !m.verified) continue;
			m.outP++;
			send(out, m);
		}
	}

	function onMessage(msg, rinfo) {
		stats.pktIn++;
		stats.bytesIn += msg.length;
		if (msg.length < 3 || msg.length > MAX_PACKET || msg[0] !== MAGIC0 || msg[1] !== MAGIC1) return drop('bad', byEndpoint.get(endpoint(rinfo)));
		const type = msg[2];
		if (type === T_DATA && msg.length >= DATA_IN_HEADER) return data(msg, rinfo);
		const key = endpoint(rinfo);
		const auth = type === T_HELLO_AUTH && msg.length >= HELLO_AUTH_HEADER && !!authorize;
		if (!auth && !(type === T_HELLO && msg.length >= HELLO_HEADER)) return drop('bad', byEndpoint.get(key));
		if (!allow(rinfo.address, Math.max(msg.length, PACKET_COST_MIN))) return drop('rate');
		if (!hello(msg, rinfo, key, auth)) drop('hello');
	}

	function sweep() {
		const t = now();
		// oldest silence first, so onLeave reports who went quiet first
		const idle = [...sessions.values()].filter((s) => t - s.seen > IDLE_MS).sort((a, b) => a.seen - b.seen);
		for (const s of idle) remove(s, 'idle');
		for (const [k, b] of buckets) if (t - b.at > bucketFullMs) buckets.delete(k);
	}

	function report() {
		let dropped = 0;
		let causes = '';
		for (const c of CAUSES) {
			if (c !== 'alone') dropped += stats.drop[c];
			causes += ` ${c}=${stats.drop[c]}`;
		}
		log(`stats sessions=${sessions.size} in=${stats.pktIn}p/${stats.bytesIn}B out=${stats.pktOut}p/${stats.bytesOut}B dropped=${dropped}p${causes}`);
		stats.pktIn = stats.bytesIn = stats.pktOut = stats.bytesOut = 0;
		stats.drop = discards();
	}

	sock.on('message', onMessage);
	sock.on('error', (e) => log(`socket error ${e.message}`));

	return {
		sessions,
		buckets,
		sweep,
		report,
		// Moves an authorised session to another room; unicast and broadcast stay inside it. The caller is
		// trusted, so the room cap does not apply. False when the account holds no session. The session's
		// counters are logged for the room it leaves and start again from zero in the new one.
		setRoom(account, room) {
			const s = accounts.get(account);
			if (!s) return false;
			if (s.room === room) return true;
			const t = now();
			log(`room vip=${vipString(s.vip)} from=${s.room} to=${room}${tally(s, t)}`);
			s.last = snapshot(s, t);
			s.inP = s.inB = s.outP = 0;
			s.drop = discards();
			s.dropTo = discards();
			leaveRoom(s);
			s.room = room;
			rooms.set(room, (rooms.get(room) || new Set()).add(s));
			return true;
		},
		vipOf: (account) => accounts.get(account)?.vip ?? null,
		// Counters of the session an account (or a vip number) holds, since it entered its room, and `last`
		// for the room before; null when there is no such session.
		sessionStats(key) {
			const s = accounts.get(key) ?? byVip.get(key);
			return s ? { ...snapshot(s, now()), last: s.last } : null;
		},
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

/* createRelay({ thread: true, ... }): the same relay in a worker thread, so a packet is never queued behind
	 the host process's own work (a lobby tick, a TLS handshake, a collection of the host's heap); the
	 socket, the sessions and the forwarding live in the thread. The callbacks still run in the host: the
	 thread asks authorize by message and waits for the answer, and onAuthorized and onLeave arrive in the
	 order the thread called them. vipOf answers from a copy kept from those two; setRoom is sent over and
	 answers whether that copy holds the account. report() resolves once its stats line is logged. The
	 thread keeps the wall clock: `now` is not passed in, and sessions, buckets, sweep and sessionStats
	 are not offered. */
function createRelayThread(opts) {
	const log = opts.log || defaultLog;
	const authorize = opts.authorize;
	const onAuthorized = opts.onAuthorized || (() => {});
	const onLeave = opts.onLeave || (() => {});
	const plain = {};
	for (const [k, v] of Object.entries(opts)) if (typeof v !== 'function' && k !== 'thread') plain[k] = v;
	plain.authorize = !!authorize;
	const vips = new Map();  // account -> vip, from the thread's authorized and leave
	const waits = new Map(); // request id -> { resolve, reject }
	let nextWait = 0;
	let worker = null;
	let stopping = false;

	const ask = (m) => new Promise((resolve, reject) => {
		waits.set(++nextWait, { resolve, reject });
		worker.postMessage({ ...m, id: nextWait });
	});
	const settle = (id, fn) => {
		const w = waits.get(id);
		waits.delete(id);
		if (w) fn(w);
	};

	function onMessage(m) {
		if (m.t === 'log') log(m.line);
		else if (m.t === 'authorize') {
			Promise.resolve()
				.then(() => authorize(m.ticket))
				.then((a) => a ?? null, (e) => { log(`authorize threw ${e.message}`); return null; })
				.then((account) => worker?.postMessage({ t: 'account', id: m.id, account }));
		} else if (m.t === 'authorized') {
			vips.set(m.account, m.vip);
			onAuthorized(m.account, m.vip);
		} else if (m.t === 'leave') {
			vips.delete(m.account);
			onLeave(m.account, m.at);
		} else if (m.t === 'done') settle(m.id, (w) => w.resolve(m.value));
		else if (m.t === 'failed') settle(m.id, (w) => w.reject(new Error(m.message)));
	}

	return {
		setRoom(account, room) {
			worker?.postMessage({ t: 'setRoom', account, room });
			return vips.has(account);
		},
		vipOf: (account) => vips.get(account) ?? null,
		report: () => (worker ? ask({ t: 'report' }) : Promise.resolve()),
		start(port = DEFAULT_PORT, host) {
			worker = new Worker(__filename, { workerData: { zrRelay: plain } });
			worker.on('message', onMessage);
			// a relay that died takes the host with it, so a supervisor restarts both
			worker.on('error', (e) => {
				if (!waits.size) throw e;
				for (const id of [...waits.keys()]) settle(id, (w) => w.reject(e));
			});
			worker.on('exit', (code) => {
				if (!stopping) throw new Error(`relay thread exited with code ${code}`);
			});
			return ask({ t: 'start', port, host });
		},
		stop() {
			stopping = true;
			if (!worker) return Promise.resolve();
			const exited = new Promise((r) => worker.once('exit', r));
			worker.postMessage({ t: 'stop' });
			return exited;
		},
	};
}

// The thread's side of createRelayThread.
function runThread(o) {
	const post = (m) => parentPort.postMessage(m);
	const asked = new Map();  // request id -> resolve of an authorize promise
	let nextAsk = 0;
	const relay = createRelay({
		...o,
		log: (line) => post({ t: 'log', line }),
		authorize: o.authorize ? (ticket) => new Promise((resolve) => {
			asked.set(++nextAsk, resolve);
			post({ t: 'authorize', id: nextAsk, ticket });
		}) : undefined,
		onAuthorized: (account, vip) => post({ t: 'authorized', account, vip }),
		onLeave: (account, at) => post({ t: 'leave', account, at }),
	});
	parentPort.on('message', (m) => {
		if (m.t === 'account') {
			asked.get(m.id)?.(m.account);
			asked.delete(m.id);
		} else if (m.t === 'setRoom') relay.setRoom(m.account, m.room);
		else if (m.t === 'report') {
			relay.report();
			post({ t: 'done', id: m.id });
		} else if (m.t === 'start') {
			relay.start(m.port, m.host).then((value) => post({ t: 'done', id: m.id, value }), (e) => post({ t: 'failed', id: m.id, message: e.message }));
		} else if (m.t === 'stop') {
			relay.stop();
			parentPort.close();
		}
	});
}

module.exports = { createRelay, MAX_PACKET, IDLE_MS, NO_SESSION, HELLO_HEADER, HELLO_AUTH_HEADER, DATA_IN_HEADER, DATA_OUT_HEADER, vipString };

if (!isMainThread && workerData && workerData.zrRelay) runThread(workerData.zrRelay);
else if (require.main === module) {
	const port = Number(process.env.RELAY_PORT) || DEFAULT_PORT;
	createRelay()
		.start(port)
		.then((p) => console.log(`${new Date().toISOString()} relay listening on udp/${p}`))
		.catch((e) => {
			console.error(`relay: cannot bind udp/${port}: ${e.message}`);
			process.exit(1);
		});
}
