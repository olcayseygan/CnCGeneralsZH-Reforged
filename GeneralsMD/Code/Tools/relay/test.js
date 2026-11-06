'use strict';
// Self-check for relay.js: node test.js   (exit code 0 = pass)

const assert = require('assert');
const dgram = require('dgram');
const { createRelay, IDLE_MS, MAX_PACKET, NO_SESSION, HELLO_HEADER, DATA_IN_HEADER } = require('./relay');

let clock = 1000000;
const logLines = [];
const log = (l) => logLines.push(l);
const sockets = [];

// a client socket on host (any 127.x address is loopback, which gives the relay distinct source IPs)
function client(relayPort, host = '127.0.0.1') {
	const sock = dgram.createSocket('udp4');
	sockets.push(sock);
	const queue = [];
	let waiter = null;
	sock.on('message', (m) => {
		if (waiter) { const w = waiter; waiter = null; w(m); } else queue.push(m);
	});
	return new Promise((resolve) => sock.bind(0, host, () => resolve({
		send: (buf) => sock.send(buf, relayPort, '127.0.0.1'),
		// next datagram, or null after ms
		next(ms = 300) {
			if (queue.length) return Promise.resolve(queue.shift());
			return new Promise((res) => {
				const t = setTimeout(() => { waiter = null; res(null); }, ms);
				waiter = (m) => { clearTimeout(t); res(m); };
			});
		},
	})));
}

function hello(exe, ini, session = NO_SESSION, name = '', want = 0) {
	const n = Buffer.from(name, 'utf8');
	const b = Buffer.alloc(HELLO_HEADER + n.length);
	b.write('ZR', 0, 'latin1');
	b[2] = 1;
	b.writeUInt32BE(exe, 3);
	b.writeUInt32BE(ini, 7);
	b.write(session, 11, 'hex');
	b.writeUInt32BE(want >>> 0, 19);
	n.copy(b, HELLO_HEADER);
	return b;
}

function data(session, dstVip, dstPort, srcPort, payload) {
	const p = Buffer.from(payload);
	const b = Buffer.alloc(DATA_IN_HEADER + p.length);
	b.write('ZR', 0, 'latin1');
	b[2] = 3;
	b.write(session, 3, 'hex');
	b.writeUInt32BE(dstVip >>> 0, 11);
	b.writeUInt16BE(dstPort, 15);
	b.writeUInt16BE(srcPort, 17);
	p.copy(b, DATA_IN_HEADER);
	return b;
}

// the WELCOME a HELLO gets, or null when the relay says nothing
async function join(c, exe, ini, session = NO_SESSION, name = '', want = 0) {
	c.send(hello(exe, ini, session, name, want));
	const w = await c.next(200);
	if (!w) return null;
	assert.strictEqual(w.length, 15);
	assert.strictEqual(w.toString('latin1', 0, 2), 'ZR');
	assert.strictEqual(w[2], 2);
	const s = { session: w.toString('hex', 3, 11), vip: w.readUInt32BE(11) };
	assert.notStrictEqual(s.session, NO_SESSION);
	assert.strictEqual(s.vip >>> 24, 10, 'vip not in 10/8');
	return s;
}

// join, then answer with the id as the game does, which is what lets the relay forward to the endpoint
async function member(c, exe, ini, name = '', want = 0) {
	const s = await join(c, exe, ini, NO_SESSION, name, want);
	assert.ok(s, 'no WELCOME');
	assert.deepStrictEqual(await join(c, exe, ini, s.session, name), s);
	return s;
}

function parseData(m) {
	assert.ok(m, 'expected DATA, got nothing');
	assert.strictEqual(m.toString('latin1', 0, 2), 'ZR');
	assert.strictEqual(m[2], 3);
	return { srcVip: m.readUInt32BE(3), srcPort: m.readUInt16BE(7), dstPort: m.readUInt16BE(9), payload: m.subarray(11).toString() };
}

async function main() {
	const relay = createRelay({ now: () => clock, log });
	const port = await relay.start(0, '127.0.0.1');
	const [a, b, c, a2, e, f, g] = await Promise.all([...Array(7)].map(() => client(port)));

	// hello / welcome: distinct vips, a third in another room; a retried session-0 hello gets the same session
	const A = await member(a, 0x1111, 0x2222, 'Alpha');
	assert.deepStrictEqual(await join(a, 0x1111, 0x2222, NO_SESSION, 'Alpha'), A, 'retried hello replaced the session');
	const B = await join(b, 0x1111, 0x2222);
	const C = await member(c, 0x1111, 0x3333, 'Charlie');
	assert.notStrictEqual(A.vip, B.vip);
	assert.notStrictEqual(A.session, B.session);
	assert.ok(logLines.some((l) => l.startsWith('join room=00001111:00002222') && l.includes('"Alpha"')));

	// nothing is forwarded to an endpoint until it answers with its id: a forged hello reflects nothing
	a.send(data(A.session, 0xffffffff, 8086, 8086, 'early'));
	a.send(data(A.session, B.vip, 8086, 8086, 'early'));
	assert.strictEqual(await b.next(150), null, 'forwarded to an unverified session');
	assert.deepStrictEqual(await join(b, 0x1111, 0x2222, B.session), B);

	// unicast carries src vip, both ports and the payload
	a.send(data(A.session, B.vip, 8086, 8088, 'hi B'));
	assert.deepStrictEqual(parseData(await b.next()), { srcVip: A.vip, srcPort: 8088, dstPort: 8086, payload: 'hi B' });

	// broadcast reaches the room, not the sender and not another room
	a.send(data(A.session, 0xffffffff, 8086, 8086, 'lobby'));
	assert.deepStrictEqual(parseData(await b.next()), { srcVip: A.vip, srcPort: 8086, dstPort: 8086, payload: 'lobby' });
	assert.strictEqual(await c.next(150), null, 'broadcast crossed rooms');
	assert.strictEqual(await a.next(150), null, 'broadcast echoed to sender');

	// unicast into another room is dropped
	a.send(data(A.session, C.vip, 8086, 8086, 'nope'));
	assert.strictEqual(await c.next(150), null, 'unicast crossed rooms');

	// unknown session, bad magic and oversize packets are dropped
	b.send(data('deadbeefdeadbeef', A.vip, 8086, 8086, 'forged'));
	const bad = data(B.session, A.vip, 8086, 8086, 'x');
	bad[0] = 0x41;
	b.send(bad);
	b.send(data(B.session, A.vip, 8086, 8086, Buffer.alloc(MAX_PACKET - DATA_IN_HEADER + 1)));
	assert.strictEqual(await a.next(150), null, 'bad packet forwarded');

	// largest legal packet goes through
	b.send(data(B.session, A.vip, 8086, 8086, Buffer.alloc(MAX_PACKET - DATA_IN_HEADER, 0x61)));
	assert.strictEqual(parseData(await a.next()).payload.length, MAX_PACKET - DATA_IN_HEADER);

	// DATA carrying A's id from another endpoint neither moves A nor goes anywhere
	a2.send(data(A.session, B.vip, 8086, 8086, 'hijack'));
	assert.strictEqual(await b.next(150), null, 'DATA from a foreign endpoint forwarded');
	b.send(data(B.session, A.vip, 8086, 8086, 'still a'));
	assert.strictEqual(parseData(await a.next()).payload, 'still a');

	// a session-0 hello with other CRCs from A's endpoint does not end A
	a.send(hello(0x9999, 0x2222));
	assert.strictEqual(await a.next(150), null, 'answered a hello with other CRCs from a held endpoint');
	b.send(data(B.session, A.vip, 8086, 8086, 'kept'));
	assert.strictEqual(parseData(await a.next()).payload, 'kept');

	// a session cannot move onto an endpoint another session holds
	b.send(hello(0x1111, 0x2222, A.session));
	assert.strictEqual(await b.next(150), null, 'moved a session onto a held endpoint');
	b.send(data(B.session, A.vip, 8086, 8086, 'b still b'));
	assert.strictEqual(parseData(await a.next()).payload, 'b still b');

	// NAT rebinding: a hello with the id from a new endpoint moves the session there under a new id that
	// only the new endpoint has seen, and it is served once it answers with that id
	const A2 = await join(a2, 0x1111, 0x2222, A.session);
	assert.strictEqual(A2.vip, A.vip);
	assert.notStrictEqual(A2.session, A.session, 'moved session kept its id');
	b.send(data(B.session, A.vip, 8086, 8086, 'not yet'));
	assert.strictEqual(await a2.next(150), null, 'moved session served before it answered');
	assert.strictEqual(await a.next(150), null, 'old endpoint still served');
	assert.deepStrictEqual(await join(a2, 0x1111, 0x2222, A2.session), A2);
	b.send(data(B.session, A.vip, 8086, 8086, 'moved'));
	assert.strictEqual(parseData(await a2.next()).payload, 'moved');
	a.send(data(A.session, B.vip, 8086, 8086, 'old id'));
	assert.strictEqual(await b.next(150), null, 'old id still accepted');

	// a requested address is granted when free, refused when held, ignored outside 10/8
	const E = await join(e, 0x1111, 0x2222, NO_SESSION, 'Echo', 0x0a630005);
	assert.strictEqual(E.vip, 0x0a630005, 'free requested vip not granted');
	const F = await join(f, 0x1111, 0x2222, NO_SESSION, '', 0x0a630005);
	assert.notStrictEqual(F.vip, E.vip, 'held vip handed out twice');
	const G = await join(g, 0x1111, 0x2222, NO_SESSION, '', 0xc0a80105);
	assert.notStrictEqual(G.vip, 0xc0a80105);

	// idle expiry: B keeps alive, the rest go silent
	assert.ok(IDLE_MS >= 120000, 'idle timeout shorter than a long map load step');
	clock += IDLE_MS - 10000;
	await join(b, 0x1111, 0x2222, B.session);
	clock += 15000;
	relay.sweep();
	assert.strictEqual(relay.sessions.size, 1);
	assert.ok(logLines.some((l) => l.startsWith('leave room=00001111:00003333') && l.endsWith('reason=idle')));
	b.send(data(B.session, A.vip, 8086, 8086, 'gone?'));
	assert.strictEqual(await a2.next(150), null, 'expired vip still delivered');
	// the game's keepalive asks for the address it had, so a forgotten session comes back on it
	const A3 = await join(a2, 0x1111, 0x2222, A2.session, '', A.vip);
	assert.notStrictEqual(A3.session, A2.session, 'expired session revived');
	assert.strictEqual(A3.vip, A.vip, 'a forgotten client lost its address');

	relay.report();
	assert.ok(/^stats sessions=\d+ in=\d+p\/\d+B out=\d+p\/\d+B dropped=\d+p$/.test(logLines[logLines.length - 1]));
	relay.stop();

	// caps: sessions per source IP and per room
	const capped = createRelay({ log, maxPerIp: 2, maxRoom: 3 });
	const capPort = await capped.start(0, '127.0.0.1');
	const [k1, k2, k3, k4, k5, k6] = await Promise.all([
		client(capPort), client(capPort), client(capPort), client(capPort, '127.0.0.2'), client(capPort, '127.0.0.3'), client(capPort, '127.0.0.3')]);
	assert.ok(await join(k1, 1, 1));
	assert.ok(await join(k2, 1, 1));
	assert.strictEqual(await join(k3, 1, 1), null, 'per-IP session cap not enforced');
	assert.ok(await join(k4, 1, 1), 'another IP refused');
	assert.strictEqual(await join(k5, 1, 1), null, 'room cap not enforced');
	assert.ok(await join(k6, 1, 2), 'another room refused');
	capped.stop();

	// the rate limit table has a size: a new source IP is dropped while it is full
	const full = createRelay({ log, maxBuckets: 1 });
	const fullPort = await full.start(0, '127.0.0.1');
	const [q1, q2] = await Promise.all([client(fullPort), client(fullPort, '127.0.0.2')]);
	assert.ok(await join(q1, 1, 1));
	assert.strictEqual(await join(q2, 1, 1), null, 'rate table grew past its cap');
	assert.strictEqual(full.buckets.size, 1);
	full.stop();

	// rate limit per source IP (64 KiB burst here, the clock stands still), and a broadcast is charged
	// once for every copy: with two listeners each gets about half of what one would
	const limited = createRelay({ now: () => clock, log, burstBytes: 64 * 1024 });
	const limitPort = await limited.start(0, '127.0.0.1');
	const [x, y, z] = await Promise.all([client(limitPort, '127.0.0.4'), client(limitPort, '127.0.0.5'), client(limitPort, '127.0.0.5')]);
	const X = await member(x, 7, 7);
	await member(y, 7, 7);
	await member(z, 7, 7);
	const big = data(X.session, 0xffffffff, 1, 1, Buffer.alloc(1000));
	for (let i = 0; i < 80; i++) x.send(big);
	let got = 0;
	while (await y.next(200)) got++;
	assert.ok(got >= 25 && got <= 40, `two listeners: rate limit let ${got} of 80 through to each`);
	limited.stop();

	for (const s of sockets) s.close();
	console.log('relay test: all checks passed');
}

main().catch((err) => {
	console.error(err);
	process.exit(1);
});
