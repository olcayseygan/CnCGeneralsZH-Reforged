'use strict';
// Self-check for relay.js: node test.js   (exit code 0 = pass)

const assert = require('assert');
const dgram = require('dgram');
const { createRelay, IDLE_MS, MAX_PACKET } = require('./relay');

const HOST = '127.0.0.1';
let clock = 1000000;
const logLines = [];
const relay = createRelay({ now: () => clock, log: (l) => logLines.push(l), burstBytes: 64 * 1024 });
let relayPort;

function client() {
	const sock = dgram.createSocket('udp4');
	const queue = [];
	let waiter = null;
	sock.on('message', (m) => {
		if (waiter) { const w = waiter; waiter = null; w(m); } else queue.push(m);
	});
	return new Promise((resolve) => sock.bind(0, HOST, () => resolve({
		sock,
		send: (buf) => sock.send(buf, relayPort, HOST),
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

function hello(exe, ini, session, name = '') {
	const n = Buffer.from(name, 'utf8');
	const b = Buffer.alloc(15 + n.length);
	b.write('ZR', 0, 'latin1');
	b[2] = 1;
	b.writeUInt32BE(exe, 3);
	b.writeUInt32BE(ini, 7);
	b.writeUInt32BE(session, 11);
	n.copy(b, 15);
	return b;
}

function data(session, dstVip, dstPort, srcPort, payload) {
	const p = Buffer.from(payload);
	const b = Buffer.alloc(15 + p.length);
	b.write('ZR', 0, 'latin1');
	b[2] = 3;
	b.writeUInt32BE(session, 3);
	b.writeUInt32BE(dstVip >>> 0, 7);
	b.writeUInt16BE(dstPort, 11);
	b.writeUInt16BE(srcPort, 13);
	p.copy(b, 15);
	return b;
}

async function join(c, exe, ini, session = 0, name = '') {
	c.send(hello(exe, ini, session, name));
	const w = await c.next();
	assert.ok(w, 'no WELCOME');
	assert.strictEqual(w.length, 11);
	assert.strictEqual(w.toString('latin1', 0, 2), 'ZR');
	assert.strictEqual(w[2], 2);
	const s = { session: w.readUInt32BE(3), vip: w.readUInt32BE(7) };
	assert.notStrictEqual(s.session, 0);
	assert.strictEqual(s.vip >>> 24, 10, 'vip not in 10/8');
	return s;
}

function parseData(m) {
	assert.ok(m, 'expected DATA, got nothing');
	assert.strictEqual(m.toString('latin1', 0, 2), 'ZR');
	assert.strictEqual(m[2], 3);
	return { srcVip: m.readUInt32BE(3), srcPort: m.readUInt16BE(7), dstPort: m.readUInt16BE(9), payload: m.subarray(11).toString() };
}

async function main() {
	relayPort = await relay.start(0, HOST);
	const [a, b, c, a2, d] = await Promise.all([client(), client(), client(), client(), client()]);

	// hello / welcome: two in one room get distinct vips, a third in another room
	const A = await join(a, 0x1111, 0x2222, 0, 'Alpha');
	const B = await join(b, 0x1111, 0x2222);
	const C = await join(c, 0x1111, 0x3333, 0, 'Charlie');
	assert.notStrictEqual(A.vip, B.vip);
	assert.notStrictEqual(A.session, B.session);
	assert.ok(logLines.some((l) => l.startsWith('join room=00001111:00002222') && l.includes('"Alpha"')));

	// keepalive hello with the session keeps session and vip
	assert.deepStrictEqual(await join(a, 0x1111, 0x2222, A.session, 'Alpha'), A);

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
	b.send(data(0xdeadbeef, A.vip, 8086, 8086, 'forged'));
	const bad = data(B.session, A.vip, 8086, 8086, 'x');
	bad[0] = 0x41;
	b.send(bad);
	b.send(data(B.session, A.vip, 8086, 8086, Buffer.alloc(MAX_PACKET - 14)));
	assert.strictEqual(await a.next(150), null, 'bad packet forwarded');

	// largest legal packet goes through
	b.send(data(B.session, A.vip, 8086, 8086, Buffer.alloc(MAX_PACKET - 15, 0x61)));
	assert.strictEqual(parseData(await a.next()).payload.length, MAX_PACKET - 15);

	// NAT rebinding: same session from a new endpoint keeps the vip and moves delivery there
	assert.deepStrictEqual(await join(a2, 0x1111, 0x2222, A.session), A);
	b.send(data(B.session, A.vip, 8086, 8086, 'moved'));
	assert.strictEqual(parseData(await a2.next()).payload, 'moved');
	assert.strictEqual(await a.next(150), null, 'old endpoint still served');
	// DATA from yet another endpoint rebinds too
	a.send(data(A.session, B.vip, 8086, 8086, 'back'));
	assert.strictEqual(parseData(await b.next()).payload, 'back');
	b.send(data(B.session, A.vip, 8086, 8086, 'to a'));
	assert.strictEqual(parseData(await a.next()).payload, 'to a');

	// idle expiry: B keeps alive, A and C go silent
	clock += IDLE_MS - 10000;
	await join(b, 0x1111, 0x2222, B.session);
	clock += 15000;
	relay.sweep();
	assert.strictEqual(relay.sessions.size, 1);
	assert.ok(logLines.some((l) => l.startsWith('leave room=00001111:00003333') && l.endsWith('reason=idle')));
	b.send(data(B.session, A.vip, 8086, 8086, 'gone?'));
	assert.strictEqual(await a.next(150), null, 'expired vip still delivered');
	const A3 = await join(a, 0x1111, 0x2222, A.session);
	assert.notStrictEqual(A3.session, A.session, 'expired session revived');

	// rate limit: a fresh source gets its burst (64 KiB in this test) and then nothing until the clock moves
	const D = await join(d, 0x1111, 0x2222);
	const big = data(D.session, B.vip, 1, 1, Buffer.alloc(1000));
	for (let i = 0; i < 80; i++) d.send(big);
	let got = 0;
	while (await b.next(200)) got++;
	assert.ok(got >= 50 && got < 80, `rate limit let ${got} of 80 through`);

	relay.report();
	assert.ok(/^stats sessions=\d+ in=\d+p\/\d+B out=\d+p\/\d+B dropped=\d+p$/.test(logLines[logLines.length - 1]));

	for (const x of [a, b, c, a2, d]) x.sock.close();
	relay.stop();
	console.log('relay test: all checks passed');
}

main().catch((e) => {
	console.error(e);
	process.exit(1);
});
