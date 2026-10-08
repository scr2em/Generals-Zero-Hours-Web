// The browser end of the game's network: a virtual LAN room over WebRTC.
//
// The game believes it is on a LAN: it broadcasts to find games, sends datagrams to "IP:port" addresses and
// reads them back. Browsers have no UDP, so the engine thread (WebDevice/Network/WebNet.cpp) puts every datagram
// into a ring in shared memory and this module, on the page's main thread (RTCPeerConnection is not available
// in workers), delivers it:
//
//   - every player of the room gets a virtual address (10.77.0.n) from the signaling server (webnet/server);
//   - the players are connected as a full mesh of RTCDataChannels in UDP mode (unordered, no retransmits);
//     the server only brokers offers, answers and ICE candidates;
//   - a datagram to an address goes to that player, one to a broadcast address (255.255.255.255 or
//     10.77.0.255) to all the others;
//   - until a data channel is open, or if it never opens (symmetric NATs without a TURN server), the datagram
//     is relayed through the signaling server's WebSocket instead, so the lobby works at once and a game
//     still works, at a higher latency, behind any firewall.
//
// Wire format of a datagram on a data channel / relayed: [src port u16 BE][dst port u16 BE][payload]
// (the relay frame has [0x01][peer id] in front, see server/hub.mjs).
//
// API (window.zhNet): join(room, {name, server}) / leave() / state / on('change', fn) / stats() / debug.
// The engine attaches by calling attachEngine(pointer, buffer) from WebNet.cpp.
(function (global) {
	'use strict';

	const PROTOCOL_VERSION = 1;
	const DEFAULT_SIGNAL_PATH = '/signal';
	const DC_LABEL = 'zh-udp';
	const DC_HIGH_WATER = 256 * 1024;       // bytes queued in a data channel before datagrams are dropped
	const P2P_GIVE_UP_MS = 15000;           // no open data channel after this long: stay on the relay (and say so)
	const JOIN_TIMEOUT_MS = 8000;
	const MAX_DATAGRAM = 1524;

	// Layout of WebDevice/Network/WebNetShared.h
	const H = {
		MAGIC: 0, VERSION: 1, SLOTS: 2, SLOT_BYTES: 3, STATE: 4, LOCAL_IP: 5, PEER_COUNT: 6, EPOCH: 7,
		TX_HEAD: 8, TX_TAIL: 9, TX_SEQ: 10, RX_HEAD: 11, RX_TAIL: 12, TX_DROPPED: 13, RX_DROPPED: 14, TX_PACKETS: 15,
		RX_PACKETS: 16, RX_NO_SOCKET: 17, RX_QUEUE_DROPPED: 18, LOOP_PACKETS: 19,
	};
	const MAGIC = 0x544e485a;
	const HEADER_BYTES = 256;
	const NAME_OFFSET = 128;
	const NAME_BYTES = 64;
	const SLOT_HEADER = 12;
	const STATE_CODE = { offline: 0, connecting: 1, online: 2, failed: 3 };

	const ipToNumber = (ip) => ip.split('.').reduce((n, o) => ((n << 8) | Number(o)) >>> 0, 0) >>> 0;

	/** The WebSocket URL of the signaling server: an explicit one, <meta name="zh-signal">, or this page's own host. */
	function resolveSignalUrl(explicit) {
		let value = explicit || '';
		if (!value) {
			try { value = new URLSearchParams(global.location.search).get('signal') || ''; } catch (e) { /* ignore */ }
		}
		if (!value) {
			const meta = global.document && global.document.querySelector('meta[name="zh-signal"]');
			value = meta ? meta.content : '';
		}
		if (!value) {
			const loc = global.location;
			return (loc.protocol === 'https:' ? 'wss:' : 'ws:') + '//' + loc.host + DEFAULT_SIGNAL_PATH;
		}
		if (!/^[a-z]+:\/\//i.test(value)) value = (global.location.protocol === 'https:' ? 'wss://' : 'ws://') + value;
		value = value.replace(/^http/i, 'ws');
		const url = new URL(value);
		if (url.pathname === '/' || url.pathname === '') url.pathname = DEFAULT_SIGNAL_PATH;
		return url.toString();
	}

	class ZhNet {
		constructor() {
			this.version = PROTOCOL_VERSION;
			// ?net=relay sends everything through the server (for firewalls that block WebRTC, and for tests);
			// dropRx / dropTx are fractions of datagrams to throw away (tests of loss).
			const query = global.location ? new URLSearchParams(global.location.search) : new URLSearchParams();
			this.debug = { dropRx: 0, dropTx: 0, forceRelay: query.get('net') === 'relay', log: query.has('netlog') };
			this.onDatagram = null;     // (from {id, ip}, srcPort, dstPort, Uint8Array) for pages without an engine (tests)
			this._listeners = new Set();
			this._reset();
			this._counters = { sentP2P: 0, sentRelay: 0, recvP2P: 0, recvRelay: 0, noRoute: 0, simDropped: 0, rxRingFull: 0 };
			this._scratch = new Uint8Array(4 + MAX_DATAGRAM);
			this._engine = null;
			this._status = 'offline';
			this._error = null;
		}

		_reset() {
			this._ws = null;
			this._joined = false;
			this._self = null;           // {id, ip, ipNum, name}
			this._room = null;
			this._maxPlayers = 0;
			this._ice = { iceServers: [] };
			this._peers = new Map();     // id -> peer
		}

		// ---- state --------------------------------------------------------------------------------

		get state() {
			return {
				status: this._status,
				error: this._error,
				room: this._room,
				self: this._self && { id: this._self.id, ip: this._self.ip, name: this._self.name },
				maxPlayers: this._maxPlayers,
				signalUrl: this._signalUrl || null,
				engineAttached: !!this._engine,
				peers: [...this._peers.values()].map((p) => ({ id: p.id, ip: p.ip, name: p.name, mode: p.mode })),
			};
		}

		on(event, fn) { if (event === 'change') this._listeners.add(fn); return () => this._listeners.delete(fn); }

		_log(...args) { if (this.debug.log) console.log('[zhnet]', ...args); }

		_setStatus(status, error = null) {
			this._status = status;
			this._error = error;
			this._changed();
		}

		_changed() {
			this._publish();
			for (const fn of this._listeners) {
				try { fn(this.state); } catch (e) { console.error(e); }
			}
		}

		/** Counters for tests and diagnostics. */
		stats() {
			const out = { ...this._counters };
			if (this._engine) {
				const { i32 } = this._engine;
				const get = (i) => Atomics.load(i32, i) >>> 0;
				Object.assign(out, {
					engineTx: get(H.TX_PACKETS), engineTxDropped: get(H.TX_DROPPED), engineRx: get(H.RX_PACKETS),
					engineRxNoSocket: get(H.RX_NO_SOCKET), engineRxQueueDropped: get(H.RX_QUEUE_DROPPED),
					engineLoop: get(H.LOOP_PACKETS), ringRxDropped: get(H.RX_DROPPED),
				});
			}
			return out;
		}

		// ---- joining a room -----------------------------------------------------------------------

		/** Joins (and creates, if need be) the room; a falsy code lets the server pick one. Resolves with the state. */
		join(room, options = {}) {
			if (this._ws) this.leave();
			this._signalUrl = resolveSignalUrl(options.server);
			this._reset();
			this._setStatus('connecting');
			return new Promise((resolve, reject) => {
				let settled = false;
				const fail = (message) => {
					if (settled) return;
					settled = true;
					clearTimeout(timer);
					this._teardown();
					this._setStatus('failed', message);
					reject(new Error(message));
				};
				let ws;
				try {
					ws = new WebSocket(this._signalUrl);
				} catch (e) {
					this._status = 'failed';
					fail('Cannot reach the server: ' + (e && e.message || e));
					return;
				}
				ws.binaryType = 'arraybuffer';
				this._ws = ws;
				const timer = setTimeout(() => fail('The server did not answer (' + this._signalUrl + ')'), JOIN_TIMEOUT_MS);
				ws.onopen = () => ws.send(JSON.stringify({ t: 'join', v: PROTOCOL_VERSION, room: room || undefined, name: options.name || undefined }));
				ws.onerror = () => { /* onclose follows */ };
				ws.onclose = () => {
					if (ws !== this._ws) return;
					if (!settled) { fail('Cannot reach the server (' + this._signalUrl + ')'); return; }
					// The data channels keep working; datagrams to players without one are lost from now on.
					this._log('signaling connection closed');
					this._ws = null;
					for (const p of this._peers.values()) this._updateMode(p);
					this._error = 'The connection to the server was lost';
					this._changed();
				};
				ws.onmessage = (event) => {
					if (typeof event.data !== 'string') { this._onRelayFrame(new Uint8Array(event.data)); return; }
					let msg;
					try { msg = JSON.parse(event.data); } catch (e) { return; }
					if (msg.t === 'welcome') {
						if (settled) return;
						settled = true;
						clearTimeout(timer);
						this._onWelcome(msg);
						resolve(this.state);
					} else if (msg.t === 'error' && !settled) {
						fail(msg.message || msg.code);
					} else {
						this._onMessage(msg);
					}
				};
			});
		}

		leave() {
			if (this._ws) { try { this._ws.send(JSON.stringify({ t: 'leave' })); } catch (e) { /* closing anyway */ } }
			this._teardown();
			this._setStatus('offline');
		}

		_teardown() {
			const ws = this._ws;
			this._ws = null;
			if (ws) { ws.onclose = null; ws.onmessage = null; try { ws.close(); } catch (e) { /* ignore */ } }
			for (const p of this._peers.values()) this._closePeer(p);
			this._reset();
		}

		_onWelcome(msg) {
			this._joined = true;
			this._room = msg.room;
			this._maxPlayers = msg.maxPlayers;
			this._ice = msg.ice || { iceServers: [] };
			this._self = { id: msg.id, ip: msg.ip, ipNum: ipToNumber(msg.ip), name: msg.name };
			this._status = 'online';
			this._error = null;
			this._epoch = (this._epoch || 0) + 1;
			this._log('joined room', msg.room, 'as', msg.ip, 'with', msg.peers.length, 'others');
			for (const info of msg.peers) {
				const peer = this._addPeer(info);
				this._connect(peer, true);    // the newcomer makes the offers
			}
			this._changed();
		}

		_onMessage(msg) {
			switch (msg.t) {
				case 'peer-joined': this._addPeer(msg.peer); this._changed(); break;
				case 'peer-left': {
					const peer = this._peers.get(msg.id);
					if (peer) { this._closePeer(peer); this._peers.delete(msg.id); this._changed(); }
					break;
				}
				case 'signal': this._onSignal(msg.from, msg.data).catch((e) => this._log('signal error', e)); break;
				case 'error': this._error = msg.message || msg.code; this._changed(); break;
				default: break;
			}
		}

		_addPeer(info) {
			let peer = this._peers.get(info.id);
			if (peer) return peer;
			peer = { id: info.id, ip: info.ip, ipNum: ipToNumber(info.ip), name: info.name, mode: 'relay', pc: null, dc: null, pending: [], timer: null };
			this._peers.set(info.id, peer);
			return peer;
		}

		_closePeer(peer) {
			clearTimeout(peer.timer);
			if (peer.dc) { peer.dc.onopen = peer.dc.onclose = peer.dc.onmessage = null; try { peer.dc.close(); } catch (e) { /* ignore */ } }
			if (peer.pc) { peer.pc.onicecandidate = peer.pc.onconnectionstatechange = null; try { peer.pc.close(); } catch (e) { /* ignore */ } }
			peer.dc = peer.pc = null;
		}

		// ---- WebRTC -------------------------------------------------------------------------------

		_connect(peer, offerer) {
			if (peer.pc || this.debug.forceRelay || typeof RTCPeerConnection === 'undefined') return;
			const pc = new RTCPeerConnection(this._ice);
			peer.pc = pc;
			// Both ends create the channel with the same id: no negotiation of the channel itself.
			const dc = pc.createDataChannel(DC_LABEL, { negotiated: true, id: 0, ordered: false, maxRetransmits: 0 });
			dc.binaryType = 'arraybuffer';
			peer.dc = dc;
			dc.onopen = () => { this._log('data channel open to', peer.ip); this._updateMode(peer); this._changed(); };
			dc.onclose = () => { this._updateMode(peer); this._changed(); };
			dc.onmessage = (event) => this._onChannelMessage(peer, event.data);
			pc.onicecandidate = (event) => {
				if (event.candidate) this._signal(peer, { candidate: event.candidate.toJSON() });
			};
			pc.onconnectionstatechange = () => {
				this._log('connection to', peer.ip, pc.connectionState);
				if (pc.connectionState === 'failed') { this._updateMode(peer); this._changed(); }
			};
			peer.timer = setTimeout(() => {
				if (!peer.dc || peer.dc.readyState !== 'open') this._log('no direct connection to', peer.ip, 'yet; relaying through the server');
			}, P2P_GIVE_UP_MS);
			if (offerer) {
				pc.createOffer()
					.then((offer) => pc.setLocalDescription(offer))
					.then(() => this._signal(peer, { sdp: pc.localDescription.toJSON() }))
					.catch((e) => this._log('offer failed', e));
			}
		}

		_updateMode(peer) {
			peer.mode = peer.dc && peer.dc.readyState === 'open' ? 'p2p' : 'relay';
		}

		_signal(peer, data) {
			if (this._ws && this._ws.readyState === WebSocket.OPEN) this._ws.send(JSON.stringify({ t: 'signal', to: peer.id, data }));
		}

		async _onSignal(from, data) {
			const peer = this._peers.get(from);
			if (!peer || this.debug.forceRelay) return;
			if (!peer.pc) this._connect(peer, false);
			const pc = peer.pc;
			if (!pc) return;
			if (data.sdp) {
				await pc.setRemoteDescription(data.sdp);
				for (const candidate of peer.pending.splice(0)) await pc.addIceCandidate(candidate).catch(() => {});
				if (data.sdp.type === 'offer') {
					await pc.setLocalDescription(await pc.createAnswer());
					this._signal(peer, { sdp: pc.localDescription.toJSON() });
				}
			} else if (data.candidate) {
				if (pc.remoteDescription) await pc.addIceCandidate(data.candidate).catch(() => {});
				else peer.pending.push(data.candidate);
			}
		}

		// ---- datagrams from the network -------------------------------------------------------------

		_onChannelMessage(peer, data) {
			if (!(data instanceof ArrayBuffer) || data.byteLength < 5) return;
			const bytes = new Uint8Array(data);
			this._counters.recvP2P++;
			this._deliver(peer, (bytes[0] << 8) | bytes[1], (bytes[2] << 8) | bytes[3], bytes.subarray(4));
		}

		_onRelayFrame(frame) {
			if (frame.length < 7 || frame[0] !== 0x01) return;
			const peer = this._peers.get(frame[1]);
			if (!peer) return;
			this._counters.recvRelay++;
			this._deliver(peer, (frame[2] << 8) | frame[3], (frame[4] << 8) | frame[5], frame.subarray(6));
		}

		_deliver(peer, srcPort, dstPort, payload) {
			if (this.debug.dropRx > 0 && Math.random() < this.debug.dropRx) { this._counters.simDropped++; return; }
			if (payload.length > MAX_DATAGRAM) return;
			if (this.onDatagram) this.onDatagram(peer, srcPort, dstPort, payload);
			const e = this._engine;
			if (!e) return;
			const head = Atomics.load(e.i32, H.RX_HEAD) >>> 0;
			const tail = Atomics.load(e.i32, H.RX_TAIL) >>> 0;
			if (((head - tail) >>> 0) >= e.slots) {
				this._counters.rxRingFull++;
				Atomics.add(e.i32, H.RX_DROPPED, 1);
				return;
			}
			const off = e.rxBase + (head % e.slots) * e.slotBytes;
			e.dv.setUint32(off, peer.ipNum, true);
			e.dv.setUint16(off + 4, srcPort, true);
			e.dv.setUint16(off + 6, dstPort, true);
			e.dv.setUint16(off + 8, payload.length, true);
			e.dv.setUint16(off + 10, 0, true);
			e.u8.set(payload, off + SLOT_HEADER);
			Atomics.store(e.i32, H.RX_HEAD, (head + 1) | 0);
		}

		// ---- datagrams to the network ---------------------------------------------------------------

		/** Sends a datagram from this player: dstIp is the address as a number (host order) or dotted string. */
		sendDatagram(dstIp, srcPort, dstPort, payload) {
			if (typeof dstIp === 'string') dstIp = ipToNumber(dstIp);
			if (!this._self || payload.length > MAX_DATAGRAM) { this._counters.noRoute++; return; }
			if (dstIp === 0xffffffff || (dstIp & 0xff) === 0xff) {
				for (const peer of this._peers.values()) this._sendTo(peer, srcPort, dstPort, payload);
				return;
			}
			const peer = (dstIp >>> 8) === (this._self.ipNum >>> 8) ? this._peers.get(dstIp & 0xff) : null;
			if (!peer) { this._counters.noRoute++; return; }
			this._sendTo(peer, srcPort, dstPort, payload);
		}

		_sendTo(peer, srcPort, dstPort, payload) {
			if (this.debug.dropTx > 0 && Math.random() < this.debug.dropTx) { this._counters.simDropped++; return; }
			const dc = peer.dc;
			if (dc && dc.readyState === 'open' && !this.debug.forceRelay) {
				if (dc.bufferedAmount > DC_HIGH_WATER) return;     // congested: dropped, like UDP
				const s = this._scratch;
				s[0] = srcPort >> 8; s[1] = srcPort & 0xff; s[2] = dstPort >> 8; s[3] = dstPort & 0xff;
				s.set(payload, 4);      // also takes the data out of shared memory, which send() would refuse
				try { dc.send(s.subarray(0, 4 + payload.length)); this._counters.sentP2P++; } catch (e) { /* closing */ }
				return;
			}
			const ws = this._ws;
			if (ws && ws.readyState === WebSocket.OPEN) {
				const frame = new Uint8Array(6 + payload.length);
				frame[0] = 0x01; frame[1] = peer.id;
				frame[2] = srcPort >> 8; frame[3] = srcPort & 0xff; frame[4] = dstPort >> 8; frame[5] = dstPort & 0xff;
				frame.set(payload, 6);
				ws.send(frame);
				this._counters.sentRelay++;
			}
		}

		// ---- the engine thread -----------------------------------------------------------------------

		/** Called by WebNet.cpp (on this thread, through MAIN_THREAD_EM_ASM) with the shared block. */
		attachEngine(pointer, buffer) {
			if (this._engine) return true;
			if (!buffer || typeof Atomics === 'undefined') return false;
			const i32 = new Int32Array(buffer, pointer, HEADER_BYTES / 4);
			if ((i32[H.MAGIC] >>> 0) !== MAGIC || i32[H.VERSION] !== 1) { console.error('zhnet: unexpected engine network block'); return false; }
			const slots = i32[H.SLOTS];
			const slotBytes = i32[H.SLOT_BYTES];
			const total = HEADER_BYTES + 2 * slots * slotBytes;
			this._engine = {
				i32, slots, slotBytes,
				u8: new Uint8Array(buffer, pointer, total),
				dv: new DataView(buffer, pointer, total),
				txBase: HEADER_BYTES,
				rxBase: HEADER_BYTES + slots * slotBytes,
			};
			this._publish();
			this._armTx();
			// A safety net for a wake-up that never comes (or a browser without Atomics.waitAsync).
			this._txPoll = setInterval(() => this._drainTx(), Atomics.waitAsync ? 250 : 4);
			this._log('engine attached');
			this._changed();
			return true;
		}

		/** Writes the state the engine reads (address, room state, name). */
		_publish() {
			const e = this._engine;
			if (!e) return;
			const online = this._status === 'online' && this._self;
			Atomics.store(e.i32, H.STATE, STATE_CODE[this._status] ?? 0);
			Atomics.store(e.i32, H.LOCAL_IP, online ? this._self.ipNum | 0 : 0);
			Atomics.store(e.i32, H.PEER_COUNT, this._peers.size);
			Atomics.store(e.i32, H.EPOCH, this._epoch | 0);
			const name = online ? this._self.name : (this._desiredName || '');
			const bytes = new TextEncoder().encode(name).subarray(0, NAME_BYTES - 1);
			e.u8.fill(0, NAME_OFFSET, NAME_OFFSET + NAME_BYTES);
			e.u8.set(bytes, NAME_OFFSET);
		}

		/** The player name the engine reports before a room is joined. */
		setName(name) { this._desiredName = name; this._publish(); }

		_armTx() {
			const e = this._engine;
			if (!e) return;
			const seq = Atomics.load(e.i32, H.TX_SEQ);
			this._drainTx();
			if (!Atomics.waitAsync) return;
			const result = Atomics.waitAsync(e.i32, H.TX_SEQ, seq);
			if (result.async) result.value.then(() => this._armTx());
			else setTimeout(() => this._armTx(), 0);
		}

		_drainTx() {
			const e = this._engine;
			if (!e) return;
			let tail = Atomics.load(e.i32, H.TX_TAIL) >>> 0;
			const head = Atomics.load(e.i32, H.TX_HEAD) >>> 0;
			if (tail === head) return;
			while (tail !== head) {
				const off = e.txBase + (tail % e.slots) * e.slotBytes;
				const ip = e.dv.getUint32(off, true);
				const srcPort = e.dv.getUint16(off + 4, true);
				const dstPort = e.dv.getUint16(off + 6, true);
				const length = e.dv.getUint16(off + 8, true);
				if (length <= e.slotBytes - SLOT_HEADER) {
					try { this.sendDatagram(ip, srcPort, dstPort, e.u8.subarray(off + SLOT_HEADER, off + SLOT_HEADER + length)); }
					catch (err) { console.error('zhnet send', err); }
				}
				tail = (tail + 1) >>> 0;
			}
			Atomics.store(e.i32, H.TX_TAIL, tail | 0);
		}
	}

	global.zhNet = new ZhNet();
	global.zhNet.resolveSignalUrl = resolveSignalUrl;
})(typeof globalThis !== 'undefined' ? globalThis : window);
