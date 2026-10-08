// The rooms of the virtual LAN: who is in which room, which virtual IPv4 address each player has, and the
// routing of signaling messages and relayed datagrams between them. No networking in here (server.mjs owns the
// sockets); a "client" is anything with send(data, isBinary) and close(code, reason).
//
// Wire protocol (version 1), JSON text frames unless noted.
//
//  client -> server
//    {t:'join', room?, name?, v:1}         join (or create) a room; without a code the server picks one
//    {t:'signal', to:<id>, data:<object>}  WebRTC offer/answer/candidate for one peer, forwarded as is
//    {t:'ping', n}                         answered with {t:'pong', n}
//    {t:'leave'}                           leave the room (the socket stays open)
//    binary: [0x01][to: 1 byte, 0xFF = all other players][src port u16 BE][dst port u16 BE][payload]
//            a datagram relayed through the server, for peers without a direct connection
//
//  server -> client
//    {t:'welcome', v:1, id, ip, room, name, maxPlayers, peers:[{id,ip,name}], ice:{iceServers:[...]}}
//    {t:'peer-joined', peer:{id,ip,name}}   {t:'peer-left', id}
//    {t:'signal', from:<id>, data}          {t:'pong', n}
//    {t:'error', code, message}
//    binary: [0x01][from: 1 byte][src port u16 BE][dst port u16 BE][payload]
//
// The id of a player is the last octet of its virtual address (<subnet>.<id>), so the two never disagree.

import { createHmac, randomInt } from 'node:crypto';

export const PROTOCOL_VERSION = 1;
export const FRAME_DATA = 0x01;
export const TO_ALL = 0xff;
export const FIRST_ID = 2;     // .1 is left to the "gateway"
export const LAST_ID = 254;    // .255 is the broadcast address
export const DATAGRAM_HEADER = 6;
export const MAX_DATAGRAM = 1536;

const CODE_ALPHABET = 'ABCDEFGHJKMNPQRSTUVWXYZ23456789';   // no 0/O/1/I/L
export const ROOM_CODE_RE = /^[A-Z0-9]{3,16}$/;

export function normalizeRoomCode(code) {
	return String(code ?? '').trim().toUpperCase();
}

export function sanitizeName(name, fallback) {
	// Printable ASCII and Latin-1 letters only: the game's player names are shown in a bitmap font.
	const clean = String(name ?? '').replace(/[^\x20-\x7e¡-ÿ]/g, '').trim().slice(0, 24);
	return clean || fallback;
}

export function generateRoomCode(length = 5) {
	let code = '';
	for (let i = 0; i < length; ++i) code += CODE_ALPHABET[randomInt(CODE_ALPHABET.length)];
	return code;
}

/** The ICE servers handed to the clients. TURN credentials are either fixed or, with a shared secret, the
 *  time-limited ones of coturn's `use-auth-secret` (REST API) mode. */
export function buildIceServers(config, peerLabel, now = Date.now()) {
	const servers = [];
	if (config.stun.length) servers.push({ urls: config.stun });
	if (config.turn.length) {
		if (config.turnSecret) {
			const username = `${Math.floor(now / 1000) + config.turnTtl}:${peerLabel}`;
			const credential = createHmac('sha1', config.turnSecret).update(username).digest('base64');
			servers.push({ urls: config.turn, username, credential });
		} else if (config.turnUser) {
			servers.push({ urls: config.turn, username: config.turnUser, credential: config.turnPass });
		} else {
			servers.push({ urls: config.turn });
		}
	}
	return { iceServers: servers };
}

class TokenBucket {
	constructor(ratePerSecond, burst) {
		this.rate = ratePerSecond;
		this.burst = burst;
		this.tokens = burst;
		this.last = Date.now();
	}
	take(cost = 1, now = Date.now()) {
		this.tokens = Math.min(this.burst, this.tokens + (now - this.last) * this.rate / 1000);
		this.last = now;
		if (this.tokens < cost) return false;
		this.tokens -= cost;
		return true;
	}
}

export class Peer {
	constructor(client) {
		this.client = client;
		this.room = null;
		this.id = 0;
		this.name = '';
		this.control = new TokenBucket(40, 120);          // JSON messages per second
		this.datagrams = new TokenBucket(3000, 6000);     // relayed datagrams per second
		this.bytes = new TokenBucket(3_000_000, 6_000_000);   // relayed bytes per second
	}
}

export class Hub {
	/** config: {subnet:'10.77.0', maxPlayers, maxRooms, stun:[], turn:[], turnUser, turnPass, turnSecret, turnTtl} */
	constructor(config, log = () => {}) {
		this.config = config;
		this.log = log;
		this.rooms = new Map();   // code -> {code, peers: Map<id, Peer>, created}
	}

	get peerCount() {
		let n = 0;
		for (const room of this.rooms.values()) n += room.peers.size;
		return n;
	}

	ipOf(id) { return `${this.config.subnet}.${id}`; }

	createPeer(client) { return new Peer(client); }

	#sendJson(peer, message) {
		peer.client.send(JSON.stringify(message), false);
	}

	#error(peer, code, message) {
		this.#sendJson(peer, { t: 'error', code, message });
	}

	#describe(p) { return { id: p.id, ip: this.ipOf(p.id), name: p.name }; }

	/** A JSON message from a client. Returns false when the client misbehaved and should be dropped. */
	handleText(peer, text) {
		if (!peer.control.take()) {
			this.#error(peer, 'rate-limit', 'Too many messages');
			return false;
		}
		let msg;
		try { msg = JSON.parse(text); } catch { this.#error(peer, 'bad-message', 'Not JSON'); return false; }
		if (!msg || typeof msg !== 'object' || typeof msg.t !== 'string') { this.#error(peer, 'bad-message', 'No message type'); return false; }
		switch (msg.t) {
			case 'join': this.#join(peer, msg); break;
			case 'leave': this.removePeer(peer); break;
			case 'ping': this.#sendJson(peer, { t: 'pong', n: msg.n }); break;
			case 'signal': this.#signal(peer, msg); break;
			default: this.#error(peer, 'bad-message', `Unknown message ${msg.t}`);
		}
		return true;
	}

	#join(peer, msg) {
		if (peer.room) { this.#error(peer, 'already-joined', 'Leave the room first'); return; }
		if (msg.v !== undefined && msg.v !== PROTOCOL_VERSION) { this.#error(peer, 'version', `Protocol version ${PROTOCOL_VERSION} expected`); return; }
		let code = normalizeRoomCode(msg.room);
		if (!code) {
			do { code = generateRoomCode(); } while (this.rooms.has(code));
		} else if (!ROOM_CODE_RE.test(code)) {
			this.#error(peer, 'bad-room', 'A room code is 3 to 16 letters and digits');
			return;
		}
		let room = this.rooms.get(code);
		if (!room) {
			if (this.rooms.size >= this.config.maxRooms) { this.#error(peer, 'server-full', 'Too many rooms'); return; }
			room = { code, peers: new Map(), created: Date.now() };
			this.rooms.set(code, room);
			this.log(`room ${code} created`);
		}
		if (room.peers.size >= this.config.maxPlayers) { this.#error(peer, 'room-full', `The room is full (${this.config.maxPlayers} players)`); return; }
		let id = FIRST_ID;
		while (room.peers.has(id)) ++id;
		if (id > LAST_ID) { this.#error(peer, 'room-full', 'The room is full'); return; }

		peer.room = room;
		peer.id = id;
		peer.name = sanitizeName(msg.name, `Player${id}`);
		const others = [...room.peers.values()].map((p) => this.#describe(p));
		room.peers.set(id, peer);
		this.#sendJson(peer, {
			t: 'welcome', v: PROTOCOL_VERSION, ...this.#describe(peer), room: code,
			maxPlayers: this.config.maxPlayers, peers: others,
			ice: buildIceServers(this.config, `${code}-${id}`),
		});
		for (const p of room.peers.values()) {
			if (p !== peer) this.#sendJson(p, { t: 'peer-joined', peer: this.#describe(peer) });
		}
		this.log(`room ${code}: ${peer.name} joined as ${this.ipOf(id)} (${room.peers.size} players)`);
	}

	#signal(peer, msg) {
		const target = peer.room?.peers.get(Number(msg.to));
		if (!target || target === peer) { this.#error(peer, 'no-such-peer', `No player ${msg.to}`); return; }
		if (msg.data === null || typeof msg.data !== 'object') { this.#error(peer, 'bad-message', 'Signal without data'); return; }
		this.#sendJson(target, { t: 'signal', from: peer.id, data: msg.data });
	}

	/** A binary frame (a relayed datagram) from a client. Returns false when the client should be dropped. */
	handleBinary(peer, data) {
		const room = peer.room;
		if (!room || data.length < 1 + 1 + 4 || data[0] !== FRAME_DATA || data.length > 2 + 4 + MAX_DATAGRAM) return room ? true : false;
		if (!peer.datagrams.take() || !peer.bytes.take(data.length)) return true;   // over the limit: dropped, like UDP would
		const to = data[1];
		const out = Buffer.from(data);   // a copy: the header's second byte becomes the sender
		out[1] = peer.id;
		if (to === TO_ALL) {
			for (const p of room.peers.values()) if (p !== peer) p.client.send(out, true);
		} else {
			const target = room.peers.get(to);
			if (target && target !== peer) target.client.send(out, true);
		}
		return true;
	}

	removePeer(peer) {
		const room = peer.room;
		if (!room) return;
		peer.room = null;
		room.peers.delete(peer.id);
		for (const p of room.peers.values()) this.#sendJson(p, { t: 'peer-left', id: peer.id });
		this.log(`room ${room.code}: ${peer.name} left (${room.peers.size} players)`);
		if (room.peers.size === 0) {
			this.rooms.delete(room.code);
			this.log(`room ${room.code} closed`);
		}
	}
}
