# Network play in the browser: a virtual LAN over WebRTC

The game's multiplayer is a LAN game: the lobby finds games by **UDP broadcast**, and during the game the
players exchange **UDP datagrams** (lockstep: every player runs the same simulation and only commands are
sent). Browsers have no UDP and cannot broadcast, so the web port gives every player of a *room* a virtual
network:

```
 player A's browser                         signaling server                       player B's browser
 ┌───────────────────────────┐          (webnet/server, Node.js)          ┌───────────────────────────┐
 │ engine thread (wasm)      │                                            │ engine thread (wasm)      │
 │  UDP class ─ WebNet.cpp   │      join room, addresses, offers/         │  UDP class ─ WebNet.cpp   │
 │     │ shared-memory rings │      answers/ICE, relay fallback           │     │ shared-memory rings │
 │ main thread: zhnet.js ────┼──────────── WebSocket ─────────────────────┼──── zhnet.js              │
 │     └── RTCDataChannel (unordered, maxRetransmits 0) ─────────────────────────┘                   │
 └───────────────────────────┘     the datagrams (peer to peer)           └───────────────────────────┘
```

* Everybody who opens the same **room code** is on one network. The server hands out the virtual addresses
  `10.77.0.2`, `10.77.0.3`, ... (the last octet is the player's id) and tells the players about each other.
* The players form a **full mesh of RTCDataChannels** in UDP mode (`ordered: false`, `maxRetransmits: 0`),
  negotiated through the server (offer/answer/ICE). Game traffic never touches the server once the channels
  are open. STUN is `stun:stun.l.google.com:19302` by default; TURN is optional (see below).
* A datagram to `10.77.0.n:port` goes to player *n*; one to `255.255.255.255` or `10.77.0.255` (the LAN
  lobby's discovery broadcast) goes to every other player. Datagrams carry the source and destination port,
  so the lobby (port 8086) and the game (8088) are separate sockets as on a real LAN.
* **Relay fallback.** Until a data channel is open, and for players that cannot be reached directly (symmetric
  NATs / firewalls that block WebRTC and no TURN server), the datagrams are relayed through the server's
  WebSocket. It is slower (TCP, through the server), but the game works. `?net=relay` forces it.
* **No change to the game's simulation.** The engine's own LAN code runs unchanged; only the bottom of the
  `UDP` class and the list of local addresses are replaced for the web build (`#ifdef __EMSCRIPTEN__`).

## Playing

1. Open the launcher page. Under *Play with friends* press **New room**, or type a code and press **Join room**.
   The page shows the room code, your address and who else is there.
2. Send the **invite link** (it contains `?room=CODE`) to your friends. Opening it joins the room at once.
3. Press **Play**. In the game open **Play with friends** (the stock *Multiplayer ▸ Network* button): the
   LAN lobby lists the other players and their games; host or join as on a LAN.

URL parameters of the launcher: `room=CODE`, `name=Alice` (default `Player<n>`), `signal=wss://host/signal`
(the server; default: `<meta name="zh-signal" content="...">`, else the page's own host, path `/signal`),
`net=relay` (no WebRTC), `netlog` (log to the browser console).

## Running the server

```sh
cd GeneralsMD/Code/Main/webnet/server
npm ci                                     # one dependency: ws
node server.mjs --port 8787                # ws://localhost:8787/signal
node server.mjs --static ../../../../../build/emscripten/GeneralsMD   # also serve the game build
npm test                                   # unit and integration tests
```

With `--static <dir>` one process serves the game (with the cross-origin-isolation headers it needs) *and*
the signaling, so `http://localhost:8787/z_generals.html?room=ABCD` just works. Otherwise host the game
anywhere that sends `Cross-Origin-Opener-Policy: same-origin` / `Cross-Origin-Embedder-Policy: require-corp`
(see `web/serve.py`) and point the page at the server with `?signal=` or the `zh-signal` meta tag. Both
pages must be on `https://` (or localhost) for WebRTC and SharedArrayBuffer; use `wss://` then.

Options (each also an environment variable):

| option | variable | default | |
|---|---|---|---|
| `--port` | `PORT` | 8787 | `0` = any free port |
| `--host` | `HOST` | 0.0.0.0 | |
| `--path` | `ZHNET_PATH` | `/signal` | WebSocket endpoint |
| `--static <dir>` | `ZHNET_STATIC` | | also serve the game build |
| `--origin <url>` (repeat) | `ZHNET_ORIGINS` | any | allowed `Origin` of WebSocket clients |
| `--trust-proxy` | `ZHNET_TRUST_PROXY` | off | client address from `X-Forwarded-For` (for the per-address limit) |
| `--subnet` | `ZHNET_SUBNET` | `10.77.0` | virtual addresses `<subnet>.2` ... |
| `--max-players` | `ZHNET_MAX_PLAYERS` | 16 | per room (the game plays 8) |
| `--max-rooms` | `ZHNET_MAX_ROOMS` | 500 | |
| `--max-connections-per-ip` | `ZHNET_MAX_PER_IP` | 24 | |
| `--stun <url>` (repeat) | `ZHNET_STUN` | `stun:stun.l.google.com:19302` | `none` = no STUN |
| `--turn <url>` (repeat) | `ZHNET_TURN` | | `turn:host:3478`, `turns:host:5349` |
| `--turn-user`, `--turn-pass` | `ZHNET_TURN_USER`, `ZHNET_TURN_PASS` | | fixed credentials |
| `--turn-secret` | `ZHNET_TURN_SECRET` | | coturn `use-auth-secret`: time-limited credentials per player |
| `--turn-ttl` | `ZHNET_TURN_TTL` | 86400 | seconds |

### Deployment

The server is one small stateless process (rooms live in memory; a restart drops the rooms, the players
reconnect by reloading the page). Put it behind a reverse proxy that terminates TLS and upgrades WebSockets.
nginx:

```nginx
location /signal {
    proxy_pass http://127.0.0.1:8787;
    proxy_http_version 1.1;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
    proxy_set_header X-Forwarded-For $remote_addr;
    proxy_read_timeout 1h;
}
```

(start the server with `--trust-proxy`). Caddy needs only `reverse_proxy /signal 127.0.0.1:8787`.
A systemd unit is `ExecStart=/usr/bin/node /srv/zhnet/server.mjs --trust-proxy --origin https://play.example.com`
with `Environment=ZHNET_TURN=...`.

**TURN.** STUN is enough for most home networks. Players behind symmetric NATs (some mobile and corporate
networks) cannot connect directly; they fall back to the server relay, which works but adds latency and
costs the server the bandwidth. A TURN server lets them connect over WebRTC with a relayed (but UDP-like)
path. With [coturn](https://github.com/coturn/coturn):

```
# /etc/turnserver.conf
realm=play.example.com
use-auth-secret
static-auth-secret=CHANGE-ME
fingerprint
no-multicast-peers
```

and start the signaling server with `--turn turn:turn.example.com:3478 --turn-secret CHANGE-ME` (add
`turns:turn.example.com:5349` for TLS). Credentials are made per player and expire after `--turn-ttl`.

Capacity: the server relays nothing in the normal case (a few hundred bytes of signaling per player). In relay
mode a room of *n* players sends about *n*·(n-1)·30 datagrams per second through it, around 100 kB/s for 4
players. The per-client limits (3000 datagrams/s, 3 MB/s) keep a misbehaving client from flooding a room.

## Limits

* **Players:** the game allows 8 per game; the room allows 16 (observers, people in the lobby). The mesh needs
  n·(n-1)/2 peer connections: fine for 8.
* **Latency:** a data channel between two browsers adds a few milliseconds to the path between them (localhost
  RTT in the test: 3 ms, 95% under 7 ms); the game's lockstep run-ahead absorbs the rest. Relay mode adds the
  round trip to the server.
* **NAT:** direct connections need STUN-reachable NATs; otherwise TURN or the relay (see above). Chrome only
  (RTCPeerConnection is used on the main thread; the engine thread talks to it through shared memory).
* **Reliability:** datagrams are unreliable and unordered, as on UDP; the game has its own acknowledgements
  and resends. A background tab keeps the connection (WebSockets and data channels are not throttled), but
  Chrome may slow the page's main thread, so keep the game tab in the foreground.
* **Not supported:** GameSpy online play, the "Direct Connect" dialog (the starter content has no layout for it;
  the retail layout works with the virtual addresses), joining a room while a game is running.

## Layout of the code

| | |
|---|---|
| `server/hub.mjs` | rooms, address assignment, signaling and relay routing (no sockets; unit tested) |
| `server/server.mjs` | HTTP + WebSocket server, CLI/environment configuration, static files |
| `client/zhnet.js` | the page's module: signaling, RTCPeerConnections, shared-memory rings (`window.zhNet`) |
| `Core/GameEngine/Include/GameNetwork/WebNet.h` | the seam between the engine's `UDP`/`IPEnumeration` and the browser network |
| `Core/GameEngineDevice/.../WebDevice/Network/WebNet.cpp` | the engine thread's end: socket table, rings (`WebNetShared.h` is the layout) |
| `Core/GameEngine/Source/GameNetwork/udp.cpp`, `IPEnumeration.cpp` | `#ifdef __EMSCRIPTEN__` branches calling `WebNet_*` |
| `test/` | `run_net_test.mjs` (transport test, 2 pages), `lan_flow.mjs` (the real game's LAN lobby, 2 pages), `launcher_net.mjs` (the launcher's room controls) |
| `Content/StarterPack/gen/wnd_lan.py` | the LAN lobby, game setup, map selection and disconnect screens of the free starter content |

## Tests

```sh
cd GeneralsMD/Code/Main/webnet/server && npm ci && npm test        # hub + server
# transport test: two pages, the engine's socket API, over WebRTC and over the relay
source emsdk_env.sh
emcmake cmake -S Core/GameEngineDevice/Source/WebDevice/Network -B build/em-nettest -G Ninja
ninja -C build/em-nettest web_net_test
NODE_PATH=/opt/node22/lib/node_modules node GeneralsMD/Code/Main/webnet/test/run_net_test.mjs build/em-nettest/Tests
# the launcher's room controls
NODE_PATH=/opt/node22/lib/node_modules node GeneralsMD/Code/Main/webnet/test/launcher_net.mjs --site build/emscripten/GeneralsMD
# the real game: lobby, join, start, lockstep for a minute (needs the web build with the starter content)
cmake --build build/emscripten --target z_generals starter_pack
NODE_PATH=/opt/node22/lib/node_modules node GeneralsMD/Code/Main/webnet/test/lan_flow.mjs --site build/emscripten/GeneralsMD --out out --seconds 60
```
