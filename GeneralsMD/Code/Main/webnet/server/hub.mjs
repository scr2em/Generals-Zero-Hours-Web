// The rooms of the virtual LAN: who is in which room, which virtual IPv4 address each player has, and the
// routing of signaling messages and relayed datagrams between them. No networking in here (server.mjs owns the
// sockets); a "client" is anything with send(data, isBinary) and close(code, reason).
//
// Wire protocol (version 1), JSON text frames unless noted.
//
//  client -> server
//    {t:'join', room?, name?, want?, v:1}  join (or create) a room; without a code the server picks one. `want` is
//                                          the id the client had before a reconnect: given back if it is free
//    {t:'signal', to:<id>, data:<object>}  WebRTC offer/answer/candidate for one peer, forwarded as is
//    {t:'ping', n}                         answered with {t:'pong', n}
//    {t:'leave'}                           leave the room (the socket stays open)
//    binary: [0x01][to: 1 byte, 0xFF = all other players][src port u16 BE][dst port u16 BE][payload]
//            a datagram relayed through the server, for peers without a direct connection
//
//  server -> client
//    {t:'welcome', v:1, id, ip, room, name, maxPlayers, peers:[{id,ip,name}], ice:{iceServers:[...], iceTransportPolicy?}}
//    {t:'peer-joined', peer:{id,ip,name}}   {t:'peer-left', id}
//    {t:'signal', from:<id>, data}          {t:'pong', n}
//    {t:'error', code, message}
//    binary: [0x01][from: 1 byte][src port u16 BE][dst port u16 BE][payload]
//
// The id of a player is the last octet of its virtual address (<subnet>.<id>), so the two never disagree.
//
// Everything a client sends is untrusted: sizes are bounded, signals are rebuilt from the fields we know (never
// forwarded as is), names are cleaned, and every kind of traffic has a per client rate limit.

import { createHmac, randomInt } from 'node:crypto';

export const PROTOCOL_VERSION = 1;
export const FRAME_DATA = 0x01;
export const TO_ALL = 0xff;
export const FIRST_ID = 2;     // .1 is left to the "gateway"
export const LAST_ID = 254;    // .255 is the broadcast address
export const DATAGRAM_HEADER = 6;
export const MAX_DATAGRAM = 1536;
export const MAX_TEXT = 16 * 1024;        // the largest JSON message (an SDP with a dozen candidates is ~2 kB)
export const MAX_SDP = 12 * 1024;
export const MAX_CANDIDATE = 1024;

const CODE_ALPHABET = 'ABCDEFGHJKMNPQRSTUVWXYZ23456789';   // no 0/O/1/I/L: 31 symbols
export const ROOM_CODE_LENGTH = 6;                          // 31^6 = 8.9e8 (29.7 bits) for the codes the server makes
export const ROOM_CODE_RE = /^[A-Z0-9]{3,16}$/;

export const DEFAULTS = {
	subnet: '10.77.0', maxPlayers: 16, maxRooms: 500, maxRoomsPerIp: 8,
	stun: [], turn: [], turnUser: '', turnPass: '', turnSecret: '', turnTtl: 86400, iceRelayOnly: false,
	// per client: messages (and bytes of them) per second / burst, relayed datagrams and bytes per second / burst
	controlRate: 60, controlBurst: 400, controlBytesRate: 200_000, controlBytesBurst: 600_000,
	datagramRate: 3000, datagramBurst: 6000, byteRate: 3_000_000, byteBurst: 6_000_000,
	// per client address: joins per second / burst (room code guessing, churn)
	joinRate: 0.5, joinBurst: 20,
};

export function normalizeRoomCode(code) {
	return String(code ?? '').trim().toUpperCase();
}

export function sanitizeName(name, fallback) {
	// Printable ASCII and Latin-1 letters only: the game's player names are shown in a bitmap font.
	const clean = String(name ?? '').replace(/[^\x20-\x7e¡-ÿ]/g, '').replace(/\s+/g, ' ').trim().slice(0, 24).trim();
	return clean || fallback;
}

export function generateRoomCode(length = ROOM_CODE_LENGTH) {
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
	const ice = { iceServers: servers };
	if (config.iceRelayOnly && config.turn.length) ice.iceTransportPolicy = 'relay';   // only through TURN (tests, strict networks)
	return ice;
}

export class TokenBucket {
	constructor(ratePerSecond, burst, now = Date.now()) {
		this.rate = ratePerSecond;
		this.burst = burst;
		this.tokens = burst;
		this.last = now;
	}
	take(cost = 1, now = Date.now()) {
		this.tokens = Math.min(this.burst, this.tokens + (now - this.last) * this.rate / 1000);
		this.last = now;
		if (this.tokens < cost) return false;
		this.tokens -= cost;
		return true;
	}
	/** True when the bucket is back to full (nothing worth remembering). */
	idle(now = Date.now()) {
		return this.tokens + (now - this.last) * this.rate / 1000 >= this.burst;
	}
}

const clip = (value, n = 40) => String(value).replace(/[^\x20-\x7e]/g, '?').slice(0, n);

/** Rebuilds a WebRTC signal from the fields the clients use, or null: nothing else is ever forwarded. */
export function cleanSignal(data) {
	if (data === null || typeof data !== 'object' || Array.isArray(data)) return null;
	const sdp = data.sdp;
	if (sdp !== undefined) {
		if (sdp === null || typeof sdp !== 'object') return null;
		if (sdp.type !== 'offer' && sdp.type !== 'answer') return null;
		if (typeof sdp.sdp !== 'string' || sdp.sdp.length > MAX_SDP) return null;
		return { sdp: { type: sdp.type, sdp: sdp.sdp } };
	}
	const c = data.candidate;
	if (c !== undefined) {
		if (c === null || typeof c !== 'object' || typeof c.candidate !== 'string' || c.candidate.length > MAX_CANDIDATE) return null;
		const out = { candidate: c.candidate };
		if (c.sdpMid !== undefined && c.sdpMid !== null) {
			if (typeof c.sdpMid !== 'string' || c.sdpMid.length > 64) return null;
			out.sdpMid = c.sdpMid;
		} else if (c.sdpMid === null) out.sdpMid = null;
		if (c.sdpMLineIndex !== undefined && c.sdpMLineIndex !== null) {
			if (!Number.isInteger(c.sdpMLineIndex) || c.sdpMLineIndex < 0 || c.sdpMLineIndex > 255) return null;
			out.sdpMLineIndex = c.sdpMLineIndex;
		} else if (c.sdpMLineIndex === null) out.sdpMLineIndex = null;
		if (c.usernameFragment !== undefined && c.usernameFragment !== null) {
			if (typeof c.usernameFragment !== 'string' || c.usernameFragment.length > 256) return null;
			out.usernameFragment = c.usernameFragment;
		}
		return { candidate: out };
	}
	return null;
}

export class Peer {
	constructor(client, address, config) {
		this.client = client;
		this.address = address;
		this.room = null;
		this.id = 0;
		this.name = '';
		this.control = new TokenBucket(config.controlRate, config.controlBurst);                    // JSON messages
		this.controlBytes = new TokenBucket(config.controlBytesRate, config.controlBytesBurst);     // and their size
		this.datagrams = new TokenBucket(config.datagramRate, config.datagramBurst);                // relayed datagrams
		this.bytes = new TokenBucket(config.byteRate, config.byteBurst);                            // and the bytes sent out for them
		this.violations = 0;       // consecutive control messages over the limit
		this.lastRateError = 0;
	}
}

export class Hub {
	/** config: see DEFAULTS (everything is optional) */
	constructor(config, log = () => {}) {
		this.config = { ...DEFAULTS, ...config };
		this.log = log;
		this.rooms = new Map();    // code -> {code, peers: Map<id, Peer>, created, creator}
		this.joinBuckets = new Map();    // client address -> TokenBucket
		this.stats = { joins: 0, rooms: 0, rateLimitedControl: 0, rateLimitedDatagrams: 0, rejectedSignals: 0, relayedDatagrams: 0, relayedBytes: 0, refusedJoins: 0 };
	}

	get peerCount() {
		let n = 0;
		for (const room of this.rooms.values()) n += room.peers.size;
		return n;
	}

	ipOf(id) { return `${this.config.subnet}.${id}`; }

	createPeer(client, address = '') { return new Peer(client, address, this.config); }

	/** Forgets what is no longer needed (call now and then). */
	prune(now = Date.now()) {
		for (const [address, bucket] of this.joinBuckets) if (bucket.idle(now)) this.joinBuckets.delete(address);
	}

	#sendJson(peer, message) {
		peer.client.send(JSON.stringify(message), false);
	}

	#error(peer, code, message) {
		this.#sendJson(peer, { t: 'error', code, message });
	}

	#describe(p) { return { id: p.id, ip: this.ipOf(p.id), name: p.name }; }

	/** A JSON message from a client. Returns false when the client misbehaved and should be dropped. */
	handleText(peer, text) {
		if (text.length > MAX_TEXT) { this.#error(peer, 'too-big', 'Message too large'); return false; }
		const now = Date.now();
		if (!peer.control.take(1, now) || !peer.controlBytes.take(text.length, now)) {
			// Dropped, not forwarded. Occasional bursts are forgiven; a client that keeps going is disconnected.
			this.stats.rateLimitedControl++;
			if (now - peer.lastRateError > 1000) { peer.lastRateError = now; this.#error(peer, 'rate-limit', 'Too many messages'); }
			return ++peer.violations < 100;
		}
		peer.violations = 0;
		let msg;
		try { msg = JSON.parse(text); } catch { this.#error(peer, 'bad-message', 'Not JSON'); return false; }
		if (!msg || typeof msg !== 'object' || typeof msg.t !== 'string') { this.#error(peer, 'bad-message', 'No message type'); return false; }
		switch (msg.t) {
			case 'join': this.#join(peer, msg); break;
			case 'leave': this.removePeer(peer); break;
			case 'ping': this.#sendJson(peer, { t: 'pong', n: Number.isFinite(msg.n) ? msg.n : 0 }); break;
			case 'signal': this.#signal(peer, msg); break;
			default: this.#error(peer, 'bad-message', `Unknown message ${clip(msg.t)}`);
		}
		return true;
	}

	#roomsCreatedBy(address) {
		let n = 0;
		for (const room of this.rooms.values()) if (room.creator === address) ++n;
		return n;
	}

	#join(peer, msg) {
		const cfg = this.config;
		if (peer.room) { this.#error(peer, 'already-joined', 'Leave the room first'); return; }
		if (msg.v !== undefined && msg.v !== PROTOCOL_VERSION) { this.#error(peer, 'version', `Protocol version ${PROTOCOL_VERSION} expected`); return; }
		if (peer.address) {
			let bucket = this.joinBuckets.get(peer.address);
			if (!bucket) this.joinBuckets.set(peer.address, bucket = new TokenBucket(cfg.joinRate, cfg.joinBurst));
			if (!bucket.take()) { this.stats.refusedJoins++; this.#error(peer, 'rate-limit', 'Too many joins from your address; wait a moment'); return; }
		}
		if (typeof msg.room !== 'string' && msg.room !== undefined && msg.room !== null) { this.#error(peer, 'bad-room', 'A room code is 3 to 16 letters and digits'); return; }
		let code = normalizeRoomCode(msg.room);
		if (!code) {
			do { code = generateRoomCode(); } while (this.rooms.has(code));
		} else if (!ROOM_CODE_RE.test(code)) {
			this.#error(peer, 'bad-room', 'A room code is 3 to 16 letters and digits');
			return;
		}
		let room = this.rooms.get(code);
		if (!room) {
			if (this.rooms.size >= cfg.maxRooms) { this.stats.refusedJoins++; this.#error(peer, 'server-full', 'Too many rooms'); return; }
			if (peer.address && this.#roomsCreatedBy(peer.address) >= cfg.maxRoomsPerIp) {
				this.stats.refusedJoins++;
				this.#error(peer, 'rate-limit', 'Too many rooms from your address');
				return;
			}
			room = { code, peers: new Map(), created: Date.now(), creator: peer.address };
			this.rooms.set(code, room);
			this.stats.rooms++;
			this.log(`room ${code} created`);
		}
		if (room.peers.size >= cfg.maxPlayers) { this.#error(peer, 'room-full', `The room is full (${cfg.maxPlayers} players)`); return; }
		let id = FIRST_ID;
		const want = msg.want;
		if (Number.isInteger(want) && want >= FIRST_ID && want < FIRST_ID + cfg.maxPlayers && want <= LAST_ID && !room.peers.has(want)) {
			id = want;     // a client that reconnects keeps its address
		} else {
			while (room.peers.has(id)) ++id;
		}
		if (id > LAST_ID) { this.#error(peer, 'room-full', 'The room is full'); return; }

		peer.room = room;
		peer.id = id;
		peer.name = this.#uniqueName(room, sanitizeName(msg.name, `Player${id}`));
		const others = [...room.peers.values()].map((p) => this.#describe(p));
		room.peers.set(id, peer);
		this.stats.joins++;
		this.#sendJson(peer, {
			t: 'welcome', v: PROTOCOL_VERSION, ...this.#describe(peer), room: code,
			maxPlayers: cfg.maxPlayers, peers: others,
			ice: buildIceServers(cfg, `${code}-${id}`),
		});
		for (const p of room.peers.values()) {
			if (p !== peer) this.#sendJson(p, { t: 'peer-joined', peer: this.#describe(peer) });
		}
		this.log(`room ${code}: ${peer.name} joined as ${this.ipOf(id)} (${room.peers.size} players)`);
	}

	/** The game refuses a second player with a name that is taken, so names are unique in a room. */
	#uniqueName(room, name) {
		const taken = new Set([...room.peers.values()].map((p) => p.name.toLowerCase()));
		if (!taken.has(name.toLowerCase())) return name;
		for (let n = 2; ; ++n) {
			const suffix = ` ${n}`;
			const candidate = name.slice(0, 24 - suffix.length).trimEnd() + suffix;
			if (!taken.has(candidate.toLowerCase())) return candidate;
		}
	}

	#signal(peer, msg) {
		const target = peer.room?.peers.get(Number(msg.to));
		if (!target || target === peer) { this.#error(peer, 'no-such-peer', `No player ${clip(msg.to)}`); return; }
		const data = cleanSignal(msg.data);
		if (!data) { this.stats.rejectedSignals++; this.#error(peer, 'bad-message', 'Not a valid signal'); return; }
		this.#sendJson(target, { t: 'signal', from: peer.id, data });
	}

	/** A binary frame (a relayed datagram) from a client. Returns false when the client should be dropped. */
	handleBinary(peer, data) {
		const room = peer.room;
		if (!room || data.length < 1 + 1 + 4 || data[0] !== FRAME_DATA || data.length > 2 + 4 + MAX_DATAGRAM) return room ? true : false;
		const to = data[1];
		// The cost is what the server has to send: a broadcast is sent to everybody else.
		const fanOut = to === TO_ALL ? room.peers.size - 1 : 1;
		if (fanOut < 1) return true;
		if (!peer.datagrams.take() || !peer.bytes.take(data.length * fanOut)) {
			this.stats.rateLimitedDatagrams++;
			return true;   // over the limit: dropped, like UDP would
		}
		const out = Buffer.from(data);   // a copy: the header's second byte becomes the sender
		out[1] = peer.id;
		this.stats.relayedDatagrams++;
		this.stats.relayedBytes += out.length * fanOut;
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
