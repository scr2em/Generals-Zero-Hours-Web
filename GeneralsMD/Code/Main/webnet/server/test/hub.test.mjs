import test from 'node:test';
import assert from 'node:assert/strict';
import { createHmac } from 'node:crypto';
import { Hub, buildIceServers, sanitizeName, normalizeRoomCode, ROOM_CODE_RE, FRAME_DATA, TO_ALL } from '../hub.mjs';

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
