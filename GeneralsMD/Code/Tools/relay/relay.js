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
const BROADCAST = 0xffffffff;

const DEFAULT_PORT = 8094;
const MAX_PACKET = 1400;            // bytes, whole datagram, either direction
const HELLO_HEADER = 15;            // magic(2) type(1) exeCRC(4) iniCRC(4) session(4)
const DATA_IN_HEADER = 15;          // magic(2) type(1) session(4) dstVip(4) dstPort(2) srcPort(2)
const DATA_OUT_HEADER = 11;         // magic(2) type(1) srcVip(4) srcPort(2) dstPort(2)
const MAX_NAME = 32;                // bytes of UTF-8
const IDLE_MS = 30000;              // a session silent this long is dropped
const MAX_SESSIONS = 4096;
const RATE_BYTES_PER_SEC = 512 * 1024;  // sustained, per source endpoint
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

	const sock = dgram.createSocket('udp4');
	const sessions = new Map();   // session id -> session
	const byVip = new Map();      // vip -> session
	const byEndpoint = new Map(); // "ip:port" -> session
	const buckets = new Map();    // "ip:port" -> { tokens, at }
	const stats = { pktIn: 0, bytesIn: 0, pktOut: 0, bytesOut: 0, dropped: 0 };
	let nextVip = 1;
	let timers = [];

	function allocVip() {
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
			const id = crypto.randomBytes(4).readUInt32BE(0);
			if (id !== 0 && !sessions.has(id)) return id;
		}
	}

	function allow(key, len) {
		const t = now();
		let b = buckets.get(key);
		if (!b) buckets.set(key, (b = { tokens: burst, at: t }));
		b.tokens = Math.min(burst, b.tokens + ((t - b.at) * rate) / 1000);
		b.at = t;
		const cost = Math.max(len, PACKET_COST_MIN);
		if (b.tokens < cost) return false;
		b.tokens -= cost;
		return true;
	}

	function send(buf, s) {
		stats.pktOut++;
		stats.bytesOut += buf.length;
		sock.send(buf, s.port, s.address);
	}

	function remove(s, reason) {
		sessions.delete(s.id);
		byVip.delete(s.vip);
		if (byEndpoint.get(s.key) === s) byEndpoint.delete(s.key);
		log(`leave room=${s.room} vip=${vipString(s.vip)} reason=${reason}`);
	}

	// One endpoint belongs to one session. A session moving to a new endpoint is NAT rebinding;
	// a session already sitting on that endpoint is a restarted client and goes.
	function bind(s, rinfo, key) {
		if (s.key === key) return;
		if (byEndpoint.get(s.key) === s) byEndpoint.delete(s.key);
		const other = byEndpoint.get(key);
		if (other && other !== s) remove(other, 'replaced');
		s.key = key;
		s.address = rinfo.address;
		s.port = rinfo.port;
		byEndpoint.set(key, s);
	}

	function hello(msg, rinfo, key) {
		const exe = msg.readUInt32BE(3);
		const ini = msg.readUInt32BE(7);
		const id = msg.readUInt32BE(11);
		const name = msg.subarray(HELLO_HEADER, HELLO_HEADER + MAX_NAME).toString('utf8');
		let s = id ? sessions.get(id) : undefined;
		if (s && (s.exe !== exe || s.ini !== ini)) {
			remove(s, 'version changed');
			s = undefined;
		}
		if (!s) {
			if (sessions.size >= MAX_SESSIONS) return false;
			const vip = allocVip();
			if (!vip) return false;
			s = { id: newSessionId(), vip, exe, ini, room: `${hex(exe)}:${hex(ini)}`, name, key: null, seen: 0 };
			sessions.set(s.id, s);
			byVip.set(vip, s);
			bind(s, rinfo, key);
			log(`join room=${s.room} vip=${vipString(vip)} name=${JSON.stringify(name)}`);
		} else {
			bind(s, rinfo, key);
		}
		s.seen = now();
		const w = Buffer.alloc(11);
		w[0] = MAGIC0;
		w[1] = MAGIC1;
		w[2] = T_WELCOME;
		w.writeUInt32BE(s.id, 3);
		w.writeUInt32BE(s.vip, 7);
		send(w, s);
		return true;
	}

	function data(msg, rinfo, key) {
		const s = sessions.get(msg.readUInt32BE(3));
		if (!s) return false;
		bind(s, rinfo, key);
		s.seen = now();
		const dst = msg.readUInt32BE(7);
		const out = Buffer.allocUnsafe(msg.length - DATA_IN_HEADER + DATA_OUT_HEADER);
		out[0] = MAGIC0;
		out[1] = MAGIC1;
		out[2] = T_DATA;
		out.writeUInt32BE(s.vip, 3);
		out.writeUInt16BE(msg.readUInt16BE(13), 7);  // srcPort
		out.writeUInt16BE(msg.readUInt16BE(11), 9);  // dstPort
		msg.copy(out, DATA_OUT_HEADER, DATA_IN_HEADER);
		if (dst === BROADCAST) {
			// ponytail: scans every session per broadcast; keep a per-room set if sessions reach thousands
			for (const t of sessions.values()) if (t !== s && t.room === s.room) send(out, t);
			return true;
		}
		const t = byVip.get(dst);
		if (!t || t.room !== s.room) return false;
		send(out, t);
		return true;
	}

	function onMessage(msg, rinfo) {
		stats.pktIn++;
		stats.bytesIn += msg.length;
		const key = `${rinfo.address}:${rinfo.port}`;
		let ok = false;
		if (msg.length >= 3 && msg.length <= MAX_PACKET && msg[0] === MAGIC0 && msg[1] === MAGIC1 && allow(key, msg.length)) {
			if (msg[2] === T_HELLO && msg.length >= HELLO_HEADER) ok = hello(msg, rinfo, key);
			else if (msg[2] === T_DATA && msg.length >= DATA_IN_HEADER) ok = data(msg, rinfo, key);
		}
		if (!ok) stats.dropped++;
	}

	function sweep() {
		const t = now();
		for (const s of sessions.values()) if (t - s.seen > IDLE_MS) remove(s, 'idle');
		for (const [k, b] of buckets) if (t - b.at > IDLE_MS) buckets.delete(k);
	}

	function report() {
		log(`stats sessions=${sessions.size} in=${stats.pktIn}p/${stats.bytesIn}B out=${stats.pktOut}p/${stats.bytesOut}B dropped=${stats.dropped}p`);
		for (const k of Object.keys(stats)) stats[k] = 0;
	}

	sock.on('message', onMessage);
	sock.on('error', (e) => log(`socket error ${e.message}`));

	return {
		sessions,
		sweep,
		report,
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

module.exports = { createRelay, MAX_PACKET, IDLE_MS, DATA_IN_HEADER, DATA_OUT_HEADER, vipString };

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
