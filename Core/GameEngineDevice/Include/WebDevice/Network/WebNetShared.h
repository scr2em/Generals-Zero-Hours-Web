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

// FILE: WebNetShared.h ///////////////////////////////////////////////////////
//
// The block of shared memory through which the engine thread and the page's main thread (webnet/client/zhnet.js,
// which owns the WebRTC connections) exchange datagrams. Two single producer / single consumer rings of fixed
// size slots; the counters are free running 32 bit values (slot = counter % slots) read and written with
// atomics on both sides, so nobody ever waits for anybody:
//
//   tx  engine -> page   the engine advances txHead, the page advances txTail. The engine also bumps txSeq and
//                        wakes it (memory.atomic.notify), which the page waits on with Atomics.waitAsync.
//   rx  page -> engine   the page advances rxHead, the engine polls and advances rxTail.
//
// A full ring drops the datagram, like a network would. zhnet.js mirrors this layout (it reads the sizes from the
// header words, so only the header layout and the slot layout below are duplicated there).
//
// Slot (little endian):  u32 ip | u16 srcPort | u16 dstPort | u16 length | u16 reserved | payload
//   tx: ip = destination (host order IPv4, 255.255.255.255 or <subnet>.255 = broadcast), srcPort = local port
//   rx: ip = sender, srcPort = sender's port, dstPort = local port
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdint.h>

enum
{
	WEBNET_MAGIC = 0x544e485a,	// "ZHNT"
	WEBNET_VERSION = 1,
	WEBNET_SLOTS = 512,					// power of two
	WEBNET_SLOT_BYTES = 1536,
	WEBNET_SLOT_HEADER = 12,
	WEBNET_MAX_DATAGRAM = WEBNET_SLOT_BYTES - WEBNET_SLOT_HEADER,
	WEBNET_HEADER_BYTES = 256,
	WEBNET_NAME_BYTES = 64,

	// WebNetShared::state
	WEBNET_OFFLINE = 0,
	WEBNET_CONNECTING = 1,
	WEBNET_ONLINE = 2,
	WEBNET_FAILED = 3
};

struct WebNetShared
{
	// 0..3, set by the engine before the page attaches
	uint32_t magic;
	uint32_t version;
	uint32_t slots;
	uint32_t slotBytes;
	// 4..7, set by the page
	uint32_t state;			// WEBNET_*
	uint32_t localIP;		// host order, 0 while no room is joined
	uint32_t peerCount;	// other players in the room
	uint32_t epoch;			// incremented on every join
	// 8..15 ring counters
	uint32_t txHead;		// engine
	uint32_t txTail;		// page
	uint32_t txSeq;			// engine, the page waits on it
	uint32_t rxHead;		// page
	uint32_t rxTail;		// engine
	uint32_t txDropped;	// engine: ring full
	uint32_t rxDropped;	// page: ring full
	uint32_t txPackets;	// engine
	// 16..
	uint32_t rxPackets;	// engine: handed to a socket
	uint32_t rxNoSocket;	// engine: nobody listens on that port
	uint32_t rxQueueDropped;	// engine: the socket's queue was full
	uint32_t loopPackets;	// engine: delivered to a socket of this very engine
	uint32_t reserved[12];

	char name[WEBNET_NAME_BYTES];	// page: the player name, NUL terminated (byte offset 128)
	uint8_t pad[WEBNET_HEADER_BYTES - 128 - WEBNET_NAME_BYTES];

	uint8_t tx[WEBNET_SLOTS * WEBNET_SLOT_BYTES];
	uint8_t rx[WEBNET_SLOTS * WEBNET_SLOT_BYTES];
};

static_assert(sizeof(uint32_t) == 4, "");
static_assert(__builtin_offsetof(WebNetShared, name) == 128, "zhnet.js reads the name at byte 128");
static_assert(__builtin_offsetof(WebNetShared, tx) == WEBNET_HEADER_BYTES, "zhnet.js expects the tx ring after the header");
