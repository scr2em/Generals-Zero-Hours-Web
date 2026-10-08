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
// If the connection to the signaling server drops (a restart, a network change) the data channels carry on and the
// module reconnects in the background, asking for the same address; the page shows the problem meanwhile.
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
	const RECONNECT_DELAYS_MS = [400, 1000, 2000, 4000, 8000, 15000, 30000];   // then every 30 s, for as long as the page stays in the room
	const ICE_RETRY_MS = 3000;              // a failed WebRTC connection is made again after this long
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
			// ?ice=relay lets WebRTC use TURN candidates only (tests of TURN, strict networks);
			// dropRx / dropTx are fractions of datagrams to throw away, latencyMs / jitterMs delay what arrives (tests
			// of a bad network; a jitter larger than the gap between datagrams reorders them, as the internet does).
			const query = global.location ? new URLSearchParams(global.location.search) : new URLSearchParams();
			this.debug = {
				dropRx: 0, dropTx: 0, latencyMs: 0, jitterMs: 0,
				forceRelay: query.get('net') === 'relay', iceRelay: query.get('ice') === 'relay', log: query.has('netlog'),
			};
			this.onDatagram = null;     // (from {id, ip}, srcPort, dstPort, Uint8Array) for pages without an engine (tests)
			this._listeners = new Set();
			this._reset();
			this._counters = { sentP2P: 0, sentRelay: 0, recvP2P: 0, recvRelay: 0, noRoute: 0, simDropped: 0, rxRingFull: 0, serverErrors: 0, reconnects: 0 };
			this._scratch = new Uint8Array(4 + MAX_DATAGRAM);
			this._engine = null;
			this._status = 'offline';
			this._error = null;
		}

		_reset() {
			this._ws = null;
			this._session = null;        // {room, name, server, id} of the room we are in, for reconnecting
			this._reconnect = null;      // {timer, attempt} while the server connection is being restored
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
				reconnecting: !!this._reconnect,
				peers: [...this._peers.values()].map((p) => ({ id: p.id, ip: p.ip, name: p.name, mode: p.mode, route: p.route })),
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
		async join(room, options = {}) {
			if (this._ws || this._session) this.leave();
			this._signalUrl = resolveSignalUrl(options.server);
			this._reset();
			this._setStatus('connecting');
			let welcome;
			try {
				welcome = await this._openSocket(room, options.name, 0);
			} catch (e) {
				this._teardown();
				this._setStatus('failed', e.message);
				throw e;
			}
			this._session = { room: welcome.room, name: options.name, server: options.server, id: welcome.id };
			this._onWelcome(welcome);
			return this.state;
		}

		/** Opens the WebSocket and sends the join request; resolves with the server's welcome. After that every
		 *  message goes to _onMessage, and a closed socket to _onSocketClosed. */
		_openSocket(room, name, want) {
			return new Promise((resolve, reject) => {
				let settled = false;
				let ws = null;
				const fail = (message) => {
					if (settled) return;
					settled = true;
					clearTimeout(timer);
					if (ws) { ws.onclose = ws.onmessage = ws.onopen = null; try { ws.close(); } catch (e) { /* ignore */ } }
					if (this._ws === ws) this._ws = null;
					reject(new Error(message));
				};
				const timer = setTimeout(() => fail('The server did not answer (' + this._signalUrl + ')'), JOIN_TIMEOUT_MS);
				try {
					ws = new WebSocket(this._signalUrl);
				} catch (e) {
					fail('Cannot reach the server: ' + (e && e.message || e));
					return;
				}
				ws.binaryType = 'arraybuffer';
				this._ws = ws;
				ws.onopen = () => ws.send(JSON.stringify({
					t: 'join', v: PROTOCOL_VERSION, room: room || undefined, name: name || undefined, want: want || undefined,
				}));
				ws.onerror = () => { /* onclose follows */ };
				ws.onclose = (event) => {
					if (!settled) { fail(event && event.code === 1008 ? 'The server refused the connection' : 'Cannot reach the server (' + this._signalUrl + ')'); return; }
					this._onSocketClosed(ws);
				};
				ws.onmessage = (event) => {
					if (typeof event.data !== 'string') { this._onRelayFrame(new Uint8Array(event.data)); return; }
					let msg;
					try { msg = JSON.parse(event.data); } catch (e) { return; }
					if (!settled) {
						if (msg.t === 'welcome') { settled = true; clearTimeout(timer); resolve(msg); }
						else if (msg.t === 'error') fail(msg.message || msg.code);
						return;
					}
					this._onMessage(msg);
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
			if (this._reconnect) clearTimeout(this._reconnect.timer);
			this._reconnect = null;
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

		/** The signaling connection ended. Before the join it is a failure (see _openSocket); afterwards the game
		 *  keeps running on the data channels while we try to get the server back. */
		_onSocketClosed(ws) {
			if (ws !== this._ws) return;
			this._ws = null;
			if (!this._session) return;
			this._log('signaling connection closed');
			for (const p of this._peers.values()) this._updateMode(p);
			this._error = 'The connection to the server was lost; reconnecting…';
			this._scheduleReconnect();
			this._changed();
		}

		_scheduleReconnect() {
			const attempt = this._reconnect ? this._reconnect.attempt : 0;
			if (this._reconnect) clearTimeout(this._reconnect.timer);
			const delay = RECONNECT_DELAYS_MS[Math.min(attempt, RECONNECT_DELAYS_MS.length - 1)] * (0.75 + Math.random() * 0.5);
			this._reconnect = { attempt: attempt + 1, timer: setTimeout(() => this._tryReconnect(), delay) };
		}

		async _tryReconnect() {
			const session = this._session;
			if (!session || this._ws) return;
			let welcome;
			try {
				welcome = await this._openSocket(session.room, session.name, session.id);
			} catch (e) {
				if (!this._session) return;
				this._log('reconnect failed:', e.message);
				this._error = 'The connection to the server was lost; reconnecting… (' + e.message + ')';
				this._scheduleReconnect();
				this._changed();
				return;
			}
			if (this._session !== session) { try { this._ws.close(); } catch (e) { /* ignore */ } return; }
			this._reconnect = null;
			this._counters.reconnects++;
			this._onRejoined(welcome);
		}

		/** The server knows us again (possibly a new instance with no rooms). Keep every working connection. */
		_onRejoined(msg) {
			const self = this._self;
			this._ice = msg.ice || this._ice;
			if (msg.id !== self.id || msg.room !== this._room) {
				// Somebody else took our address while we were away: the engine's idea of its address is wrong now.
				this._log('address changed from', self.ip, 'to', msg.ip);
				for (const p of this._peers.values()) this._closePeer(p);
				this._peers.clear();
				this._session.id = msg.id;
				this._onWelcome(msg);
				this._error = 'Your address in the room changed to ' + msg.ip + '. Leave the game and open the lobby again.';
				this._changed();
				return;
			}
			self.name = msg.name;
			this._error = null;
			const listed = new Map(msg.peers.map((info) => [info.id, info]));
			for (const peer of this._peers.values()) {
				const info = listed.get(peer.id);
				// A player we are connected to that has not reconnected yet stays (its data channel still works);
				// if it is gone for good the channel closes and _updateMode removes it.
				peer.stale = !info;
				if (info && info.ip !== peer.ip) { this._closePeer(peer); this._peers.delete(peer.id); }
			}
			for (const info of msg.peers) {
				const existing = this._peers.get(info.id);
				if (existing && existing.dc && existing.dc.readyState === 'open') { existing.name = info.name; continue; }
				const peer = existing || this._addPeer(info);
				peer.name = info.name;
				if (existing) this._closePeer(peer);
				this._connect(peer, true);
			}
			this._log('rejoined room', msg.room, 'as', msg.ip, 'with', msg.peers.length, 'others');
			this._changed();
		}

		_onMessage(msg) {
			switch (msg.t) {
				case 'peer-joined': {
					const known = this._peers.get(msg.peer.id);
					if (known && known.ip === msg.peer.ip) { known.stale = false; known.name = msg.peer.name; }
					else {
						if (known) { this._closePeer(known); this._peers.delete(known.id); }
						this._addPeer(msg.peer);
					}
					this._changed();
					break;
				}
				case 'peer-left': {
					const peer = this._peers.get(msg.id);
					if (peer) { this._closePeer(peer); this._peers.delete(msg.id); this._changed(); }
					break;
				}
				case 'signal': this._onSignal(msg.from, msg.data).catch((e) => this._log('signal error', e)); break;
				case 'error':
					// Nothing the server says after the join is fatal (rate limits, a signal for a player that just left).
					this._counters.serverErrors++;
					this._log('server:', msg.code, msg.message);
					break;
				default: break;
			}
		}

		_addPeer(info) {
			let peer = this._peers.get(info.id);
			if (peer) return peer;
			peer = {
				id: info.id, ip: info.ip, ipNum: ipToNumber(info.ip), name: info.name, mode: 'relay', route: null, stale: false,
				pc: null, dc: null, pending: [], timer: null, retry: null, offerer: false,
			};
			this._peers.set(info.id, peer);
			return peer;
		}

		_closePeer(peer) {
			clearTimeout(peer.timer);
			clearTimeout(peer.retry);
			peer.timer = peer.retry = null;
			if (peer.dc) { peer.dc.onopen = peer.dc.onclose = peer.dc.onmessage = null; try { peer.dc.close(); } catch (e) { /* ignore */ } }
			if (peer.pc) { peer.pc.onicecandidate = peer.pc.onconnectionstatechange = null; try { peer.pc.close(); } catch (e) { /* ignore */ } }
			peer.dc = peer.pc = null;
			peer.pending = [];
			peer.mode = 'relay';
			peer.route = null;
		}

		// ---- WebRTC -------------------------------------------------------------------------------

		_connect(peer, offerer) {
			if (peer.pc || this.debug.forceRelay || typeof RTCPeerConnection === 'undefined') return;
			const config = { ...this._ice };
			if (this.debug.iceRelay) config.iceTransportPolicy = 'relay';
			let pc;
			try {
				pc = new RTCPeerConnection(config);
			} catch (e) {
				// Browsers refuse a whole configuration for one bad ICE server (a TURN URL without credentials, a typo).
				this._log('ICE configuration refused:', e && e.message, '- using the usable servers only');
				const usable = (config.iceServers || []).filter((s) => !/^turns?:/i.test([].concat(s.urls)[0]) || (s.username && s.credential));
				try { pc = new RTCPeerConnection({ ...config, iceServers: usable }); } catch (e2) { return; }
			}
			peer.pc = pc;
			peer.offerer = offerer;
			// Both ends create the channel with the same id: no negotiation of the channel itself.
			const dc = pc.createDataChannel(DC_LABEL, { negotiated: true, id: 0, ordered: false, maxRetransmits: 0 });
			dc.binaryType = 'arraybuffer';
			peer.dc = dc;
			dc.onopen = () => { this._log('data channel open to', peer.ip); this._updateMode(peer); this._findRoute(peer, pc); this._changed(); };
			dc.onclose = () => { if (peer.dc !== dc) return; this._updateMode(peer); this._changed(); };
			dc.onmessage = (event) => this._onChannelMessage(peer, event.data);
			pc.onicecandidate = (event) => {
				if (event.candidate) this._signal(peer, { candidate: event.candidate.toJSON() });
			};
			pc.onconnectionstatechange = () => {
				if (peer.pc !== pc) return;
				this._log('connection to', peer.ip, pc.connectionState);
				if (pc.connectionState === 'failed') {
					this._updateMode(peer);
					this._changed();
					// The side that made the offer tries again; the other accepts the new offer.
					if (peer.offerer && this._peers.get(peer.id) === peer) {
						clearTimeout(peer.retry);
						peer.retry = setTimeout(() => {
							if (peer.pc !== pc || this._peers.get(peer.id) !== peer) return;
							this._log('connecting to', peer.ip, 'again');
							this._closePeer(peer);
							this._connect(peer, true);
						}, ICE_RETRY_MS);
					}
				}
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

		/** Which kind of path the data channel ended up on: 'direct' (host/peer-reflexive), 'stun' (server-reflexive) or 'turn'. */
		_findRoute(peer, pc) {
			if (!pc.getStats) return;
			pc.getStats().then((report) => {
				if (peer.pc !== pc) return;
				let pair = null;
				report.forEach((stat) => { if (stat.type === 'transport' && stat.selectedCandidatePairId) pair = report.get(stat.selectedCandidatePairId); });
				if (!pair) report.forEach((stat) => { if (stat.type === 'candidate-pair' && stat.nominated && stat.state === 'succeeded') pair = stat; });
				if (!pair) return;
				const local = report.get(pair.localCandidateId);
				const remote = report.get(pair.remoteCandidateId);
				const kinds = [local && local.candidateType, remote && remote.candidateType];
				peer.route = kinds.includes('relay') ? 'turn' : kinds.includes('srflx') ? 'stun' : 'direct';
				this._log('route to', peer.ip, peer.route, kinds.join('/'));
				this._changed();
			}).catch(() => {});
		}

		_updateMode(peer) {
			peer.mode = peer.dc && peer.dc.readyState === 'open' ? 'p2p' : 'relay';
			if (peer.mode === 'relay') peer.route = null;
			// A player that did not come back after our server connection was lost, and whose channel died: gone.
			if (peer.stale && peer.mode === 'relay' && this._peers.get(peer.id) === peer) {
				this._closePeer(peer);
				this._peers.delete(peer.id);
			}
		}

		_signal(peer, data) {
			if (this._ws && this._ws.readyState === WebSocket.OPEN) this._ws.send(JSON.stringify({ t: 'signal', to: peer.id, data }));
		}

		async _onSignal(from, data) {
			const peer = this._peers.get(from);
			if (!peer || this.debug.forceRelay || !data) return;
			// A new offer means the other side starts over (it reloaded, or its connection failed): so do we. If both of
			// us made an offer at the same time, the lower id wins.
			if (data.sdp && data.sdp.type === 'offer' && peer.pc) {
				if (peer.offerer && peer.pc.signalingState === 'have-local-offer' && from > this._self.id) return;
				this._closePeer(peer);
			}
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
			const delay = this.debug.latencyMs + (this.debug.jitterMs ? (Math.random() * 2 - 1) * this.debug.jitterMs : 0);
			if (delay >= 1) {
				// (the payload may be a view into a buffer that is reused)
				const copy = payload.slice();
				setTimeout(() => this._deliverNow(peer, srcPort, dstPort, copy), delay);
				return;
			}
			this._deliverNow(peer, srcPort, dstPort, payload);
		}

		_deliverNow(peer, srcPort, dstPort, payload) {
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
