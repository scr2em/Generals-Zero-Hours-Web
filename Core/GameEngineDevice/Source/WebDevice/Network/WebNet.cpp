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

// FILE: WebNet.cpp ///////////////////////////////////////////////////////////
//
// The engine thread's end of the browser network, see GameNetwork/WebNet.h. The datagram sockets of the game
// (the LAN lobby on port 8086, the game's transport on 8088) are entries of a small table; what they send goes
// into a ring in shared memory that the page's main thread drains onto WebRTC data channels, what arrives is
// put into the other ring by the page and sorted into the queues of the sockets here by destination port.
// Datagrams between two sockets of this very engine (the game sends to its own address) never leave it.
//
// Nothing here blocks. A mutex serialises the engine threads that use sockets (in practice one).
//
///////////////////////////////////////////////////////////////////////////////

#include "GameNetwork/WebNet.h"
#include "WebDevice/Network/WebNetShared.h"

#include <emscripten.h>
#include <emscripten/threading.h>
#include <emscripten/threading_primitives.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <memory>
#include <mutex>

namespace
{

enum
{
	MAX_SOCKETS = 16,
	SOCKET_QUEUE_SIZE = 256,			// datagrams a socket buffers (the game reads them every frame)
	FIRST_EPHEMERAL_PORT = 49152,
	OFFLINE_IP = 0x7f000001				// 127.0.0.1
};

struct Packet
{
	uint32_t ip;
	uint16_t port;
	uint16_t length;
	uint8_t data[WEBNET_MAX_DATAGRAM];
};

struct Socket
{
	bool used = false;
	uint32_t ip = 0;
	uint16_t port = 0;
	std::unique_ptr<Packet[]> queue;
	uint32_t head = 0;	// next to write
	uint32_t tail = 0;	// next to read
};

alignas(64) WebNetShared g_shared;
std::mutex g_lock;
bool g_initialized = false;
bool g_attached = false;
int g_attachTries = 0;
Socket g_sockets[MAX_SOCKETS];
uint16_t g_nextEphemeral = FIRST_EPHEMERAL_PORT;

inline uint32_t load(const uint32_t &v) { return __atomic_load_n(&v, __ATOMIC_ACQUIRE); }
inline void store(uint32_t &v, uint32_t x) { __atomic_store_n(&v, x, __ATOMIC_RELEASE); }

inline uint32_t getU32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
inline uint16_t getU16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
inline void putU32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
inline void putU16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }

// Asks the page's network module (webnet/client/zhnet.js) to take over the rings. Without the module (a test
// page, or the launcher without it) the sockets still work among themselves. Called with the lock held.
void ensureInitialized()
{
	if (!g_initialized)
	{
		g_initialized = true;
		g_shared.magic = WEBNET_MAGIC;
		g_shared.version = WEBNET_VERSION;
		g_shared.slots = WEBNET_SLOTS;
		g_shared.slotBytes = WEBNET_SLOT_BYTES;
	}
	// The module can only be missing when the page does not have it at all; a few tries cover a page that
	// loads it late.
	if (!g_attached && g_attachTries < 8)
	{
		++g_attachTries;
		g_attached = MAIN_THREAD_EM_ASM_INT({
			var net = globalThis.zhNet;
			return (net && net.attachEngine && net.attachEngine($0, wasmMemory.buffer)) ? 1 : 0;
		}, &g_shared) != 0;
	}
}

uint32_t localIPLocked()
{
	const uint32_t ip = load(g_shared.localIP);
	return ip ? ip : OFFLINE_IP;
}

bool isLoopback(uint32_t ip) { return (ip >> 24) == 127; }

bool isBroadcast(uint32_t ip) { return ip == 0xffffffffu || (ip & 0xff) == 0xff; }

Socket *find(int handle)
{
	if (handle <= 0 || handle > MAX_SOCKETS) return nullptr;
	Socket &s = g_sockets[handle - 1];
	return s.used ? &s : nullptr;
}

Socket *findByPort(uint16_t port)
{
	for (Socket &s : g_sockets)
		if (s.used && s.port == port) return &s;
	return nullptr;
}

void enqueue(Socket &s, uint32_t ip, uint16_t port, const uint8_t *data, uint32_t length)
{
	if (s.head - s.tail >= SOCKET_QUEUE_SIZE)
	{
		__atomic_fetch_add(&g_shared.rxQueueDropped, 1, __ATOMIC_RELAXED);
		return;
	}
	Packet &p = s.queue[s.head % SOCKET_QUEUE_SIZE];
	p.ip = ip;
	p.port = port;
	p.length = (uint16_t)length;
	memcpy(p.data, data, length);
	++s.head;
}

// Moves what the page delivered into the sockets' queues.
void pump()
{
	uint32_t tail = g_shared.rxTail;	// only this side writes it
	const uint32_t head = load(g_shared.rxHead);
	if (tail == head) return;
	while (tail != head)
	{
		const uint8_t *slot = g_shared.rx + (tail % WEBNET_SLOTS) * WEBNET_SLOT_BYTES;
		const uint32_t ip = getU32(slot);
		const uint16_t srcPort = getU16(slot + 4);
		const uint16_t dstPort = getU16(slot + 6);
		const uint32_t length = getU16(slot + 8);
		if (length > 0 && length <= WEBNET_MAX_DATAGRAM)
		{
			Socket *s = findByPort(dstPort);
			if (s)
			{
				enqueue(*s, ip, srcPort, slot + WEBNET_SLOT_HEADER, length);
				__atomic_fetch_add(&g_shared.rxPackets, 1, __ATOMIC_RELAXED);
			}
			else
			{
				__atomic_fetch_add(&g_shared.rxNoSocket, 1, __ATOMIC_RELAXED);
			}
		}
		++tail;
	}
	store(g_shared.rxTail, tail);
}

} // namespace

extern "C" unsigned WebNet_GetLocalIP(void)
{
	std::lock_guard<std::mutex> guard(g_lock);
	ensureInitialized();
	return localIPLocked();
}

extern "C" const char *WebNet_GetPlayerName(void)
{
	std::lock_guard<std::mutex> guard(g_lock);
	ensureInitialized();
	static char name[WEBNET_NAME_BYTES];
	memcpy(name, g_shared.name, sizeof(name));
	name[sizeof(name) - 1] = 0;
	if (!name[0])
		snprintf(name, sizeof(name), "Player%u", (unsigned)(localIPLocked() & 0xff));
	return name;
}

extern "C" int WebNet_Open(unsigned ip, unsigned short port, unsigned short *boundPort)
{
	std::lock_guard<std::mutex> guard(g_lock);
	ensureInitialized();

	if (ip != 0 && ip != localIPLocked() && !isLoopback(ip))
		return -EADDRNOTAVAIL;

	if (port == 0)
	{
		for (int tries = 0; tries < 20000 && port == 0; ++tries)
		{
			const uint16_t candidate = g_nextEphemeral;
			g_nextEphemeral = (uint16_t)(g_nextEphemeral == 65535 ? FIRST_EPHEMERAL_PORT : g_nextEphemeral + 1);
			if (!findByPort(candidate)) port = candidate;
		}
		if (port == 0) return -EADDRINUSE;
	}
	else if (findByPort(port))
	{
		return -EADDRINUSE;
	}

	for (int i = 0; i < MAX_SOCKETS; ++i)
	{
		Socket &s = g_sockets[i];
		if (s.used) continue;
		s.queue.reset(new Packet[SOCKET_QUEUE_SIZE]);
		s.head = s.tail = 0;
		s.ip = ip;
		s.port = port;
		s.used = true;
		if (boundPort) *boundPort = port;
		return i + 1;
	}
	return -EMFILE;
}

extern "C" void WebNet_Close(int handle)
{
	std::lock_guard<std::mutex> guard(g_lock);
	if (Socket *s = find(handle))
	{
		s->used = false;
		s->queue.reset();
	}
}

extern "C" int WebNet_SendTo(int handle, const void *data, unsigned len, unsigned ip, unsigned short port)
{
	std::lock_guard<std::mutex> guard(g_lock);
	Socket *s = find(handle);
	if (!s) return -EBADF;
	if (len > WEBNET_MAX_DATAGRAM) return -EMSGSIZE;

	const uint32_t local = localIPLocked();

	if (!isBroadcast(ip) && (ip == local || isLoopback(ip)))
	{
		// To ourselves.
		if (Socket *dst = findByPort(port))
		{
			enqueue(*dst, local, s->port, (const uint8_t *)data, len);
			__atomic_fetch_add(&g_shared.loopPackets, 1, __ATOMIC_RELAXED);
		}
		return (int)len;
	}

	ensureInitialized();	// a page that loads its network module late
	const uint32_t head = g_shared.txHead;	// only this side writes it
	if (head - load(g_shared.txTail) >= WEBNET_SLOTS)
	{
		__atomic_fetch_add(&g_shared.txDropped, 1, __ATOMIC_RELAXED);
		return (int)len;	// like a network that drops it
	}
	uint8_t *slot = g_shared.tx + (head % WEBNET_SLOTS) * WEBNET_SLOT_BYTES;
	putU32(slot, ip);
	putU16(slot + 4, s->port);
	putU16(slot + 6, port);
	putU16(slot + 8, (uint16_t)len);
	putU16(slot + 10, 0);
	memcpy(slot + WEBNET_SLOT_HEADER, data, len);
	store(g_shared.txHead, head + 1);
	__atomic_fetch_add(&g_shared.txPackets, 1, __ATOMIC_RELAXED);
	__atomic_fetch_add(&g_shared.txSeq, 1, __ATOMIC_SEQ_CST);
	emscripten_futex_wake(&g_shared.txSeq, 1);
	return (int)len;
}

extern "C" int WebNet_RecvFrom(int handle, void *data, unsigned cap, unsigned *ip, unsigned short *port)
{
	std::lock_guard<std::mutex> guard(g_lock);
	Socket *s = find(handle);
	if (!s) return -EBADF;
	pump();
	if (s->tail == s->head) return 0;
	const Packet &p = s->queue[s->tail % SOCKET_QUEUE_SIZE];
	const unsigned n = p.length < cap ? p.length : cap;
	memcpy(data, p.data, n);
	if (ip) *ip = p.ip;
	if (port) *port = p.port;
	++s->tail;
	return (int)n;
}
