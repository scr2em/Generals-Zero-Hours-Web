import test from 'node:test';
import assert from 'node:assert/strict';
import { createHmac } from 'node:crypto';
import { Hub, buildIceServers, sanitizeName, normalizeRoomCode, generateRoomCode, ROOM_CODE_RE, FRAME_DATA, TO_ALL } from '../hub.mjs';

const config = (extra = {}) => ({
	subnet: '10.77.0', maxPlayers: 4, maxRooms: 3,
	stun: ['stun:stun.example:19302'], turn: [], turnUser: '', turnPass: '', turnSecret: '', turnTtl: 3600, ...extra,
});

function fakeClient() {
	const inbox = [];
	return {
		inbox,
		send(data, isBinary) { inbox.push(isBinary ? { binary: Buffer.from(data) } : JSON.parse(data)); },
		close() { this.closed = true; },
		last(type) { return [...inbox].reverse().find((m) => m.t === type); },
		all(type) { return inbox.filter((m) => m.t === type); },
	};
}

function join(hub, room, name) {
	const client = fakeClient();
	const peer = hub.createPeer(client);
	assert.equal(hub.handleText(peer, JSON.stringify({ t: 'join', room, name, v: 1 })), true);
	return { client, peer, welcome: client.last('welcome') };
}

test('players get consecutive virtual addresses and learn about each other', () => {
	const hub = new Hub(config());
	const a = join(hub, 'abcd', 'Alice');
	assert.equal(a.welcome.room, 'ABCD');
	assert.equal(a.welcome.ip, '10.77.0.2');
	assert.equal(a.welcome.id, 2);
	assert.deepEqual(a.welcome.peers, []);
	assert.deepEqual(a.welcome.ice.iceServers, [{ urls: ['stun:stun.example:19302'] }]);
	const b = join(hub, 'ABCD', 'Bob');
	assert.equal(b.welcome.ip, '10.77.0.3');
	assert.deepEqual(b.welcome.peers, [{ id: 2, ip: '10.77.0.2', name: 'Alice' }]);
	assert.deepEqual(a.client.last('peer-joined').peer, { id: 3, ip: '10.77.0.3', name: 'Bob' });
});

test('a freed address is reused and the others are told', () => {
	const hub = new Hub(config());
	const a = join(hub, 'ROOM', 'A');
	const b = join(hub, 'ROOM', 'B');
	hub.removePeer(a.peer);
	assert.deepEqual(b.client.last('peer-left'), { t: 'peer-left', id: 2 });
	const c = join(hub, 'ROOM', 'C');
	assert.equal(c.welcome.ip, '10.77.0.2');
	hub.removePeer(b.peer);
	hub.removePeer(c.peer);
	assert.equal(hub.rooms.size, 0, 'an empty room is closed');
});

test('a room code is generated when none is given', () => {
	const hub = new Hub(config());
	const a = join(hub, undefined, 'A');
	assert.match(a.welcome.room, ROOM_CODE_RE);
	const b = join(hub, '', 'B');
	assert.notEqual(a.welcome.room, b.welcome.room);
});

test('limits: room size, room count, bad codes, double join', () => {
	const hub = new Hub(config());
	for (let i = 0; i < 4; ++i) join(hub, 'FULL', 'P' + i);
	const client = fakeClient();
	const peer = hub.createPeer(client);
	hub.handleText(peer, JSON.stringify({ t: 'join', room: 'FULL' }));
	assert.equal(client.last('error').code, 'room-full');
	hub.handleText(peer, JSON.stringify({ t: 'join', room: 'no way!' }));
	assert.equal(client.last('error').code, 'bad-room');
	join(hub, 'ROOM2', 'x');
	join(hub, 'ROOM3', 'x');
	hub.handleText(peer, JSON.stringify({ t: 'join', room: 'ROOM4' }));
	assert.equal(client.last('error').code, 'server-full');
	const a = join(hub, 'ROOM2', 'y');
	hub.handleText(a.peer, JSON.stringify({ t: 'join', room: 'ROOM2' }));
	assert.equal(a.client.last('error').code, 'already-joined');
});

test('signals are forwarded to the addressed player only', () => {
	const hub = new Hub(config());
	const a = join(hub, 'SIG', 'A');
	const b = join(hub, 'SIG', 'B');
	const c = join(hub, 'SIG', 'C');
	hub.handleText(b.peer, JSON.stringify({ t: 'signal', to: 2, data: { sdp: { type: 'offer', sdp: 'x' } } }));
	assert.deepEqual(a.client.last('signal'), { t: 'signal', from: 3, data: { sdp: { type: 'offer', sdp: 'x' } } });
	assert.equal(c.client.last('signal'), undefined);
	hub.handleText(b.peer, JSON.stringify({ t: 'signal', to: 99, data: {} }));
	assert.equal(b.client.last('error').code, 'no-such-peer');
	// not across rooms
	const other = join(hub, 'OTHER', 'Z');
	hub.handleText(other.peer, JSON.stringify({ t: 'signal', to: 2, data: {} }));
	assert.equal(other.client.last('error').code, 'no-such-peer');
});

test('relayed datagrams: unicast and broadcast get the sender stamped in', () => {
	const hub = new Hub(config());
	const a = join(hub, 'REL', 'A');
	const b = join(hub, 'REL', 'B');
	const c = join(hub, 'REL', 'C');
	const frame = (to, payload) => Buffer.concat([Buffer.from([FRAME_DATA, to, 0x1f, 0x96, 0x1f, 0x96]), Buffer.from(payload)]);
	assert.equal(hub.handleBinary(a.peer, frame(3, 'hello')), true);
	assert.deepEqual([...b.client.inbox.at(-1).binary], [...frame(2, 'hello')]);
	assert.equal(c.client.inbox.filter((m) => m.binary).length, 0);
	hub.handleBinary(c.peer, frame(TO_ALL, 'all'));
	assert.deepEqual([...a.client.inbox.at(-1).binary], [...frame(4, 'all')]);
	assert.deepEqual([...b.client.inbox.at(-1).binary], [...frame(4, 'all')]);
	assert.equal(c.client.inbox.filter((m) => m.binary).length, 0, 'not echoed to the sender');
	// garbage and strangers
	assert.equal(hub.handleBinary(a.peer, Buffer.from([9, 9, 9])), true);
	const stranger = hub.createPeer(fakeClient());
	assert.equal(hub.handleBinary(stranger, frame(2, 'x')), false);
});

test('flooding is dropped silently, not forwarded', () => {
	const hub = new Hub(config());
	const a = join(hub, 'FLOOD', 'A');
	const b = join(hub, 'FLOOD', 'B');
	const frame = Buffer.concat([Buffer.from([FRAME_DATA, 3, 0, 1, 0, 2]), Buffer.alloc(1000)]);
	for (let i = 0; i < 20000; ++i) hub.handleBinary(a.peer, frame);
	const got = b.client.inbox.filter((m) => m.binary).length;
	assert.ok(got < 20000 && got > 1000, `forwarded ${got}`);
});

test('malformed control messages', () => {
	const hub = new Hub(config());
	const client = fakeClient();
	const peer = hub.createPeer(client);
	assert.equal(hub.handleText(peer, 'not json'), false);
	assert.equal(hub.handleText(peer, '[1]'), false);
	assert.equal(hub.handleText(peer, JSON.stringify({ t: 'nope' })), true);
	assert.equal(client.last('error').code, 'bad-message');
	hub.handleText(peer, JSON.stringify({ t: 'ping', n: 5 }));
	assert.deepEqual(client.last('pong'), { t: 'pong', n: 5 });
	hub.handleText(peer, JSON.stringify({ t: 'join', room: 'V', v: 2 }));
	assert.equal(client.last('error').code, 'version');
});

test('names are cleaned and codes normalised', () => {
	assert.equal(sanitizeName('  Al\u0000ice\n ', 'x'), 'Alice');
	assert.equal(sanitizeName('', 'Player7'), 'Player7');
	assert.equal(sanitizeName('x'.repeat(100), 'y').length, 24);
	assert.equal(normalizeRoomCode(' k7m2q '), 'K7M2Q');
});

test('TURN credentials: fixed, and time limited from a shared secret (coturn REST API)', () => {
	assert.deepEqual(buildIceServers(config({ turn: ['turn:t:3478'], turnUser: 'u', turnPass: 'p' }), 'r-2').iceServers[1],
		{ urls: ['turn:t:3478'], username: 'u', credential: 'p' });
	const now = 1_700_000_000_000;
	const ice = buildIceServers(config({ turn: ['turn:t:3478'], turnSecret: 's3cret', turnTtl: 3600 }), 'r-2', now).iceServers[1];
	assert.equal(ice.username, `${1_700_000_000 + 3600}:r-2`);
	assert.equal(ice.credential, createHmac('sha1', 's3cret').update(ice.username).digest('base64'));
});

// ---- hardening ---------------------------------------------------------------------------------

test('names are unique in a room (the game refuses duplicates)', () => {
	const hub = new Hub(config());
	const a = join(hub, 'NAMES', 'Alice');
	const b = join(hub, 'NAMES', 'alice');
	const c = join(hub, 'NAMES', 'ALICE');
	assert.equal(a.welcome.name, 'Alice');
	assert.equal(b.welcome.name, 'alice 2');
	assert.equal(c.welcome.name, 'ALICE 3');
	const long = join(hub, 'NAMES2', 'x'.repeat(30));
	const long2 = join(hub, 'NAMES2', 'x'.repeat(30));
	assert.equal(long2.welcome.name.length, 24);
	assert.notEqual(long.welcome.name, long2.welcome.name);
});

test('a client that reconnects can ask for its old address', () => {
	const hub = new Hub(config());
	const a = join(hub, 'KEEP', 'A');     // .2
	const b = join(hub, 'KEEP', 'B');     // .3
	hub.removePeer(a.peer);
	const client = fakeClient();
	const peer = hub.createPeer(client);
	hub.handleText(peer, JSON.stringify({ t: 'join', room: 'KEEP', name: 'B2', want: 3 }));   // taken by b
	assert.equal(client.last('welcome').id, 2, 'a taken address is not given away');
	const again = fakeClient();
	const peer2 = hub.createPeer(again);
	hub.removePeer(b.peer);
	hub.handleText(peer2, JSON.stringify({ t: 'join', room: 'KEEP', name: 'B', want: 3 }));
	assert.equal(again.last('welcome').id, 3, 'a free address is given back');
	for (const bad of [0, 1, 255, 1000, -4, 2.5, '3', null]) {
		const c = fakeClient();
		const p = hub.createPeer(c);
		hub.handleText(p, JSON.stringify({ t: 'join', room: 'KEEP2', want: bad }));
		assert.equal(c.last('welcome').id, 2, `want ${JSON.stringify(bad)} is ignored`);
		hub.removePeer(p);
	}
	// beyond the room's size
	const c = fakeClient();
	hub.handleText(hub.createPeer(c), JSON.stringify({ t: 'join', room: 'KEEP3', want: 2 + 4 }));
	assert.equal(c.last('welcome').id, 2);
});

test('signals are rebuilt from known fields and bounded', () => {
	const hub = new Hub(config());
	const a = join(hub, 'SIG2', 'A');
	const b = join(hub, 'SIG2', 'B');
	const send = (data) => hub.handleText(b.peer, JSON.stringify({ t: 'signal', to: 2, data }));
	send({ sdp: { type: 'offer', sdp: 'v=0', evil: '<script>', __proto__: { x: 1 } }, extra: 1 });
	assert.deepEqual(a.client.last('signal').data, { sdp: { type: 'offer', sdp: 'v=0' } });
	send({ candidate: { candidate: 'candidate:1 1 udp 1 1.2.3.4 5 typ host', sdpMid: '0', sdpMLineIndex: 0, usernameFragment: 'abcd', evil: 1 } });
	assert.deepEqual(a.client.last('signal').data, { candidate: { candidate: 'candidate:1 1 udp 1 1.2.3.4 5 typ host', sdpMid: '0', sdpMLineIndex: 0, usernameFragment: 'abcd' } });
	const before = a.client.all('signal').length;
	for (const bad of [null, 5, 'x', [], {}, { sdp: null }, { sdp: { type: 'rollback', sdp: '' } }, { sdp: { type: 'offer', sdp: 5 } },
		{ sdp: { type: 'offer', sdp: 'x'.repeat(13000) } }, { candidate: 'x' }, { candidate: { candidate: 'x'.repeat(2000) } },
		{ candidate: { candidate: 'x', sdpMLineIndex: 'a' } }, { candidate: { candidate: 'x', sdpMid: {} } }]) {
		assert.equal(send(bad), true);
		assert.equal(b.client.last('error').code, 'bad-message', JSON.stringify(bad).slice(0, 40));
	}
	assert.equal(a.client.all('signal').length, before, 'nothing invalid is forwarded');
});

test('oversized and rate limited control messages', () => {
	const hub = new Hub(config({ controlBurst: 10, controlRate: 1 }));
	assert.equal(hub.handleText(hub.createPeer(fakeClient()), ' '.repeat(20000)), false, 'too big: dropped');
	const client = fakeClient();
	const peer2 = hub.createPeer(client);
	for (let i = 0; i < 30; ++i) hub.handleText(peer2, JSON.stringify({ t: 'ping', n: i }));
	assert.equal(client.all('pong').length, 10, 'only the burst is answered');
	assert.equal(client.last('error').code, 'rate-limit');
	assert.equal(client.all('error').length, 1, 'one error per second, not one per message');
	// sustained flooding gets the client disconnected
	let keep = true;
	for (let i = 0; i < 300 && keep; ++i) keep = hub.handleText(peer2, JSON.stringify({ t: 'ping' }));
	assert.equal(keep, false);
	assert.ok(hub.stats.rateLimitedControl >= 90);
});

test('the size of control messages is limited per second as well', () => {
	const hub = new Hub(config({ controlBytesBurst: 5000, controlBytesRate: 1 }));
	const a = join(hub, 'BYTES', 'A');
	const b = join(hub, 'BYTES', 'B');
	const big = JSON.stringify({ t: 'signal', to: 2, data: { sdp: { type: 'offer', sdp: 'x'.repeat(3000) } } });
	hub.handleText(b.peer, big);
	hub.handleText(b.peer, big);
	assert.equal(a.client.all('signal').length, 1);
});

test('joins are limited per client address, and so are the rooms it opens', () => {
	const hub = new Hub(config({ joinBurst: 10, joinRate: 0.0001, maxRooms: 50, maxRoomsPerIp: 2 }));
	const attempt = (address, room) => {
		const client = fakeClient();
		const peer = hub.createPeer(client, address);
		hub.handleText(peer, JSON.stringify({ t: 'join', room }));
		return client.last('error')?.code ?? 'ok';
	};
	assert.equal(attempt('1.1.1.1', 'AAA'), 'ok');
	assert.equal(attempt('1.1.1.1', 'BBB'), 'ok');
	assert.equal(attempt('1.1.1.1', 'CCC'), 'rate-limit', 'a third room');
	assert.equal(attempt('1.1.1.1', 'AAA'), 'ok', 'joining an existing room is fine');
	assert.equal(attempt('2.2.2.2', 'CCC'), 'ok', 'other addresses are not affected');
});

test('join attempts: the bucket empties for a code guesser, others are not affected', () => {
	const hub = new Hub(config({ joinBurst: 5, joinRate: 0.0001, maxRoomsPerIp: 100, maxRooms: 100 }));
	const codes = [];
	for (let i = 0; i < 20; ++i) {
		const client = fakeClient();
		const peer = hub.createPeer(client, '6.6.6.6');
		hub.handleText(peer, JSON.stringify({ t: 'join', room: 'GUESS' + i }));
		codes.push(client.last('error')?.code ?? 'ok');
	}
	assert.equal(codes.filter((c) => c === 'ok').length, 5);
	const other = fakeClient();
	hub.handleText(hub.createPeer(other, '7.7.7.7'), JSON.stringify({ t: 'join', room: 'FINE' }));
	assert.equal(other.last('welcome').room, 'FINE');
	hub.prune(Date.now() + 1e9);
	assert.equal(hub.joinBuckets.size, 0, 'idle buckets are forgotten');
});

test('a broadcast costs the sender what the server sends out', () => {
	const hub = new Hub(config({ maxPlayers: 8, byteBurst: 10000, byteRate: 1 }));
	const peers = [];
	for (let i = 0; i < 8; ++i) peers.push(join(hub, 'FAN', 'P' + i));
	const frame = Buffer.concat([Buffer.from([FRAME_DATA, TO_ALL, 0, 1, 0, 2]), Buffer.alloc(94)]);   // 100 bytes x 7 receivers
	for (let i = 0; i < 40; ++i) hub.handleBinary(peers[0].peer, frame);
	const got = peers[1].client.inbox.filter((m) => m.binary).length;
	assert.equal(got, Math.floor(10000 / 700), `${got} broadcasts of 100 bytes fit in the burst`);
});

test('room codes: 6 symbols without look-alikes, from a CSPRNG, and distinct', () => {
	const seen = new Set();
	for (let i = 0; i < 5000; ++i) {
		const c = generateRoomCode();
		assert.match(c, /^[ABCDEFGHJKMNPQRSTUVWXYZ2-9]{6}$/);
		seen.add(c);
	}
	assert.ok(seen.size > 4990, 'no repeats in 5000 codes (31^6 = 887 million)');
	const counts = new Map();
	for (let i = 0; i < 31 * 600; ++i) for (const ch of generateRoomCode(1)) counts.set(ch, (counts.get(ch) ?? 0) + 1);
	assert.equal(counts.size, 31);
	for (const n of counts.values()) assert.ok(n > 400 && n < 800, 'letters are about equally likely');
});

test('text echoed back to a client is clipped and printable', () => {
	const hub = new Hub(config());
	const a = join(hub, 'ECHO', 'A');
	hub.handleText(a.peer, JSON.stringify({ t: 'x'.repeat(5000) + '<script>' }));
	assert.ok(a.client.last('error').message.length < 80);
	hub.handleText(a.peer, JSON.stringify({ t: 'signal', to: '<img src=x onerror=1>\u0000'.repeat(50), data: {} }));
	const m = a.client.last('error').message;
	assert.ok(m.length < 80 && !/[\u0000-\u001f]/.test(m));
	hub.handleText(a.peer, JSON.stringify({ t: 'ping', n: { huge: 'x'.repeat(10000) } }));
	assert.deepEqual(a.client.last('pong'), { t: 'pong', n: 0 });
	const b = fakeClient();
	hub.handleText(hub.createPeer(b), JSON.stringify({ t: 'join', room: { $ne: 1 } }));
	assert.equal(b.last('error').code, 'bad-room');
});

test('hostile names are kept as plain text (escaping is the page\'s job) but stripped of control characters', () => {
	assert.equal(sanitizeName('<b onload=alert(1)>', 'x'), '<b onload=alert(1)>');
	assert.equal(sanitizeName('a‮b​c\u0007d', 'x'), 'abcd', 'bidi overrides, zero width and bell characters are removed');
	assert.equal(sanitizeName('  two   spaces  ', 'x'), 'two spaces');
	assert.equal(sanitizeName('\u0001\u0002', 'Player9'), 'Player9');
});

test('TURN: relay only policy is sent with the credentials', () => {
	const ice = buildIceServers(config({ turn: ['turn:t:3478'], turnSecret: 's', iceRelayOnly: true }), 'r-2');
	assert.equal(ice.iceTransportPolicy, 'relay');
	assert.equal(buildIceServers(config({ turn: [], iceRelayOnly: true }), 'r-2').iceTransportPolicy, undefined, 'never relay-only without TURN');
	assert.equal(buildIceServers(config({ turn: ['turn:t:3478'], turnSecret: 's' }), 'r-2').iceTransportPolicy, undefined);
});
