/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// FILE: WebNet.h /////////////////////////////////////////////////////////////
//
// The seam between the game's UDP class (udp.cpp) / IPEnumeration and the browser's network, WebAssembly
// build only. Browsers have no UDP; the datagrams of a virtual LAN room travel over WebRTC data channels,
// which live on the browser's main thread (WebDevice/Network/WebNet.cpp is the engine thread's end of it,
// webnet/client/zhnet.js the page's end). Addresses are host order IPv4 values (a<<24|b<<16|c<<8|d) and
// ports are host order, as everywhere in the game's network code.
//
// All functions may be called from any engine thread and never block.
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

#ifdef __EMSCRIPTEN__

#ifdef __cplusplus
extern "C" {
#endif

/// The address of the local player in the virtual LAN (10.77.0.n), or 127.0.0.1 while no room is joined.
unsigned WebNet_GetLocalIP(void);

/// The player name chosen on the launcher page (or "Player<n>"); a static string.
const char *WebNet_GetPlayerName(void);

/// Opens a datagram socket. ip is 0 (any), the local address or a loopback address; port 0 picks a free one.
/// Returns a handle > 0 and the bound port, or minus an errno value (EADDRINUSE, EADDRNOTAVAIL, ...).
int WebNet_Open(unsigned ip, unsigned short port, unsigned short *boundPort);

void WebNet_Close(int handle);

/// Sends a datagram (ip 255.255.255.255 or <subnet>.255: to every other player). Like UDP it is not
/// acknowledged; the result is len, or minus an errno value (EMSGSIZE, EBADF).
int WebNet_SendTo(int handle, const void *data, unsigned len, unsigned ip, unsigned short port);

/// The next received datagram for this socket: its length (at most cap, the rest is dropped), 0 when there
/// is none, or minus an errno value (EBADF). The sender's address and port are stored in *ip and *port.
int WebNet_RecvFrom(int handle, void *data, unsigned cap, unsigned *ip, unsigned short *port);

#ifdef __cplusplus
}
#endif

#endif // __EMSCRIPTEN__
