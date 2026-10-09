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

// FILE: web_net_test.cpp /////////////////////////////////////////////////////
//
// Transport level test of the browser network, run by two browser pages in one virtual LAN room (run_net_test.mjs).
// Both run this program on their engine thread and talk through the same C API the game's UDP class uses
// (GameNetwork/WebNet.h): a lobby socket on port 8086 and a game socket on 8088, as in the game.
//
// The player with the lower address (A) drives, the other (B) answers:
//   1 discovery   both broadcast HELLO to 255.255.255.255:8086 until they see each other
//   2 ping pong   A sends numbered datagrams of all sizes to B:8088, B echoes them; contents are verified
//   3 broadcast   B broadcasts to the room, A counts; B must not receive its own broadcast
//   4 ports       datagrams to the lobby port must not show up on the game socket
//   5 loopback    a datagram to one's own address is delivered locally
//   6 loss        A drops 30% of what it sends (zhNet.debug.dropTx): the rest must arrive intact, nothing hangs
//   7 burst       more datagrams than the ring holds, at once: dropped, not crashed, still working afterwards
//
// Output: lines "NETTEST ..." and a final "NETTEST_RESULT PASS|FAIL" from A, "NETTEST_B_DONE" from B.
//
///////////////////////////////////////////////////////////////////////////////

#include "GameNetwork/WebNet.h"
#include "WebDevice/Network/WebNetShared.h"

#include <emscripten.h>
#include <emscripten/threading.h>
#include <errno.h>

#include <algorithm>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace
{

const unsigned BROADCAST = 0xffffffffu;
const unsigned short LOBBY_PORT = 8086;
const unsigned short GAME_PORT = 8088;

int g_lobby = 0;
int g_game = 0;
unsigned g_myIP = 0;
unsigned g_peerIP = 0;
int g_failures = 0;
int g_checks = 0;

double nowMs() { return emscripten_get_now(); }
void sleepMs(double ms) { emscripten_thread_sleep(ms); }

#define CHECK(cond, ...) \
	do { \
		++g_checks; \
		if (cond) { printf("NETTEST   ok   %s\n", #cond); } \
		else { ++g_failures; printf("NETTEST   FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } \
	} while (0)

std::string ipString(unsigned ip)
{
	char s[32];
	snprintf(s, sizeof(s), "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
	return s;
}

void setPageValue(const char *expr, double value)
{
	MAIN_THREAD_EM_ASM({ var net = globalThis.zhNet; if (net) { net.debug[UTF8ToString($0)] = $1; } }, expr, value);
}

struct Datagram
{
	unsigned ip = 0;
	unsigned short port = 0;
	std::vector<uint8_t> data;
};

bool receive(int socket, Datagram &out)
{
	uint8_t buffer[WEBNET_MAX_DATAGRAM];
	const int n = WebNet_RecvFrom(socket, buffer, sizeof(buffer), &out.ip, &out.port);
	if (n <= 0) return false;
	out.data.assign(buffer, buffer + n);
	return true;
}

void sendText(int socket, unsigned ip, unsigned short port, const std::string &text)
{
	WebNet_SendTo(socket, text.data(), (unsigned)text.size(), ip, port);
}

bool startsWith(const Datagram &d, const char *prefix)
{
	const size_t n = strlen(prefix);
	return d.data.size() >= n && memcmp(d.data.data(), prefix, n) == 0;
}

std::string textOf(const Datagram &d) { return std::string((const char *)d.data.data(), d.data.size()); }

// "PING" datagram: "PING" | u32 seq | u16 length | pattern bytes
std::vector<uint8_t> makePing(const char *tag, uint32_t seq, unsigned length)
{
	if (length < 10) length = 10;
	std::vector<uint8_t> d(length);
	memcpy(d.data(), tag, 4);
	memcpy(d.data() + 4, &seq, 4);
	const uint16_t len16 = (uint16_t)length;
	memcpy(d.data() + 8, &len16, 2);
	for (unsigned i = 10; i < length; ++i) d[i] = (uint8_t)(seq * 31 + i);
	return d;
}

// 0 = not a ping/pong, 1 = intact, 2 = corrupted
int checkPing(const Datagram &d, const char *tag, uint32_t *seq)
{
	if (d.data.size() < 10 || memcmp(d.data.data(), tag, 4) != 0) return 0;
	uint16_t len16;
	memcpy(seq, d.data.data() + 4, 4);
	memcpy(&len16, d.data.data() + 8, 2);
	if (len16 != d.data.size()) return 2;
	for (unsigned i = 10; i < d.data.size(); ++i)
		if (d.data[i] != (uint8_t)(*seq * 31 + i)) return 2;
	return 1;
}

unsigned queryStat(const char *name)
{
	return (unsigned)MAIN_THREAD_EM_ASM_INT({ var net = globalThis.zhNet; return net ? (net.stats()[UTF8ToString($0)] | 0) : 0; }, name);
}

// ---- B: answers --------------------------------------------------------------------------------

struct BState
{
	unsigned gamePackets = 0;
	unsigned gameBad = 0;
	unsigned lobbyJunk = 0;
	unsigned ownBroadcasts = 0;
	unsigned aIP = 0;
};

int runB()
{
	BState b;
	double lastHello = 0;
	const double deadline = nowMs() + 120000;
	bool done = false;
	while (!done && nowMs() < deadline)
	{
		Datagram d;
		bool idle = true;
		while (receive(g_game, d))
		{
			idle = false;
			uint32_t seq;
			const int kind = checkPing(d, "PING", &seq);
			if (kind == 0) continue;
			++b.gamePackets;
			if (kind == 2) ++b.gameBad;
			// the echo: a PONG with the same sequence number and contents
			std::vector<uint8_t> pong = d.data;
			memcpy(pong.data(), "PONG", 4);
			WebNet_SendTo(g_game, pong.data(), (unsigned)pong.size(), d.ip, d.port);
		}
		while (receive(g_lobby, d))
		{
			idle = false;
			if (d.ip == g_myIP) ++b.ownBroadcasts;
			if (startsWith(d, "HELLO"))
			{
				b.aIP = d.ip;
				sendText(g_lobby, d.ip, LOBBY_PORT, "HELLO:B");    // A may have missed the broadcasts from before it started
			}
			else if (startsWith(d, "GO:"))
			{
				sendText(g_lobby, d.ip, LOBBY_PORT, "ACK:" + textOf(d).substr(3));
			}
			else if (startsWith(d, "BCASTREQ:"))
			{
				sendText(g_lobby, d.ip, LOBBY_PORT, "ACK:" + textOf(d).substr(9));
				const int count = atoi(textOf(d).c_str() + 9);
				for (int i = 0; i < count; ++i)
				{
					char text[32];
					snprintf(text, sizeof(text), "BC:%d", i);
					sendText(g_lobby, BROADCAST, LOBBY_PORT, text);
					sleepMs(5);
				}
			}
			else if (startsWith(d, "STATS?"))
			{
				char text[128];
				snprintf(text, sizeof(text), "STATS:%u:%u:%u:%u", b.gamePackets, b.gameBad, b.lobbyJunk, b.ownBroadcasts);
				sendText(g_lobby, d.ip, LOBBY_PORT, text);
			}
			else if (startsWith(d, "RESET"))
			{
				b.gamePackets = b.gameBad = b.lobbyJunk = b.ownBroadcasts = 0;
				sendText(g_lobby, d.ip, LOBBY_PORT, "ACK:RESET");
			}
			else if (startsWith(d, "BYE"))
			{
				sendText(g_lobby, d.ip, LOBBY_PORT, "ACK:BYE");
				done = true;
			}
			else
			{
				uint32_t seq;
				if (checkPing(d, "PING", &seq)) ++b.lobbyJunk;
			}
		}
		if (b.aIP == 0 && nowMs() - lastHello > 100)
		{
			lastHello = nowMs();
			sendText(g_lobby, BROADCAST, LOBBY_PORT, "HELLO:B");
		}
		if (idle) sleepMs(1);
	}
	printf("NETTEST_B_DONE gamePackets=%u bad=%u\n", b.gamePackets, b.gameBad);
	return done ? 0 : 1;
}

// ---- A: drives -----------------------------------------------------------------------------------

// Sends text to B's lobby port until B answers with ACK:<tag>; false after the timeout.
bool handshake(const std::string &request, const std::string &tag, double timeoutMs = 10000, std::vector<Datagram> *others = nullptr)
{
	const double start = nowMs();
	double last = -1000;
	while (nowMs() - start < timeoutMs)
	{
		if (nowMs() - last > 100)
		{
			last = nowMs();
			sendText(g_lobby, g_peerIP, LOBBY_PORT, request);
		}
		Datagram d;
		while (receive(g_lobby, d))
		{
			if (textOf(d) == "ACK:" + tag) return true;
			if (others) others->push_back(d);
		}
		sleepMs(1);
	}
	return false;
}

bool stats(unsigned *packets, unsigned *bad, unsigned *junk, unsigned *own)
{
	const double start = nowMs();
	double last = -1000;
	while (nowMs() - start < 10000)
	{
		if (nowMs() - last > 200)
		{
			last = nowMs();
			sendText(g_lobby, g_peerIP, LOBBY_PORT, "STATS?");
		}
		Datagram d;
		while (receive(g_lobby, d))
		{
			if (startsWith(d, "STATS:") && sscanf(textOf(d).c_str(), "STATS:%u:%u:%u:%u", packets, bad, junk, own) == 4) return true;
		}
		sleepMs(1);
	}
	return false;
}

struct PongCount
{
	unsigned pongs = 0;
	unsigned bad = 0;
	std::vector<bool> seen;
	std::vector<double> rtt;
};

// Reads what has arrived on the game socket right now; true if anything did.
bool pumpPongs(PongCount &c, const std::vector<double> &sentAt)
{
	bool got = false;
	Datagram d;
	while (receive(g_game, d))
	{
		uint32_t seq;
		const int kind = checkPing(d, "PONG", &seq);
		if (kind == 0) continue;
		got = true;
		if (kind == 2) { ++c.bad; continue; }
		if (seq < c.seen.size() && !c.seen[seq])
		{
			c.seen[seq] = true;
			++c.pongs;
			if (seq < sentAt.size()) c.rtt.push_back(nowMs() - sentAt[seq]);
		}
	}
	return got;
}

// Keeps reading until nothing has arrived for quietMs.
void collectPongs(PongCount &c, const std::vector<double> &sentAt, double quietMs)
{
	double lastGot = nowMs();
	while (nowMs() - lastGot < quietMs)
	{
		if (pumpPongs(c, sentAt)) lastGot = nowMs();
		sleepMs(1);
	}
}

int runA()
{
	// 1 discovery
	printf("NETTEST A: discovery\n");
	{
		const double start = nowMs();
		double last = -1000;
		while (g_peerIP == 0 && nowMs() - start < 60000)
		{
			if (nowMs() - last > 100)
			{
				last = nowMs();
				sendText(g_lobby, BROADCAST, LOBBY_PORT, "HELLO:A");
			}
			Datagram d;
			while (receive(g_lobby, d))
				if (startsWith(d, "HELLO") && d.ip != g_myIP) g_peerIP = d.ip;
			sleepMs(1);
		}
		CHECK(g_peerIP != 0, "no other player found within 60 s");
		if (!g_peerIP) return 1;
		printf("NETTEST A: found %s\n", ipString(g_peerIP).c_str());
	}
	CHECK(handshake("GO:1", "1"), "no answer from B");

	// 2 ping pong
	printf("NETTEST A: ping pong\n");
	{
		const unsigned sizes[] = { 10, 17, 64, 400, 1000, 1100, 1524 };
		const unsigned count = 300;
		PongCount c;
		c.seen.assign(count, false);
		std::vector<double> sentAt(count, 0);
		for (unsigned i = 0; i < count; ++i)
		{
			const std::vector<uint8_t> p = makePing("PING", i, sizes[i % (sizeof(sizes) / sizeof(sizes[0]))]);
			sentAt[i] = nowMs();
			WebNet_SendTo(g_game, p.data(), (unsigned)p.size(), g_peerIP, GAME_PORT);
			sleepMs(3);
			pumpPongs(c, sentAt);    // like the game, read every frame
		}
		collectPongs(c, sentAt, 1000);
		std::sort(c.rtt.begin(), c.rtt.end());
		const double p50 = c.rtt.empty() ? -1 : c.rtt[c.rtt.size() / 2];
		const double p95 = c.rtt.empty() ? -1 : c.rtt[c.rtt.size() * 95 / 100];
		printf("NETTEST A: ping pong %u/%u echoed, %u corrupted, rtt p50 %.1f ms p95 %.1f ms\n", c.pongs, count, c.bad, p50, p95);
		CHECK(c.bad == 0, "%u corrupted datagrams", c.bad);
		CHECK(c.pongs >= count * 98 / 100, "only %u of %u echoed", c.pongs, count);
	}

	// 3 broadcast
	printf("NETTEST A: broadcast\n");
	{
		unsigned got = 0;
		std::vector<bool> seen(20, false);
		std::vector<Datagram> others;
		sendText(g_lobby, g_peerIP, LOBBY_PORT, "BCASTREQ:20");
		const double start = nowMs();
		while (nowMs() - start < 3000)
		{
			Datagram d;
			while (receive(g_lobby, d))
			{
				int i;
				if (startsWith(d, "BC:") && sscanf(textOf(d).c_str(), "BC:%d", &i) == 1 && i >= 0 && i < 20 && !seen[i] && d.ip == g_peerIP)
				{
					seen[i] = true;
					++got;
				}
			}
			sleepMs(1);
		}
		printf("NETTEST A: %u of 20 broadcasts received\n", got);
		CHECK(got >= 19, "only %u of 20 broadcasts", got);
		unsigned packets, bad, junk, own;
		CHECK(stats(&packets, &bad, &junk, &own), "no statistics from B");
		CHECK(own == 0, "B received its own broadcasts (%u)", own);
	}

	// 4 ports
	printf("NETTEST A: ports\n");
	{
		CHECK(handshake("RESET", "RESET"), "no answer from B");
		for (unsigned i = 0; i < 30; ++i)
		{
			const std::vector<uint8_t> p = makePing("PING", 1000 + i, 100);
			WebNet_SendTo(g_game, p.data(), (unsigned)p.size(), g_peerIP, LOBBY_PORT);   // wrong port: the lobby must see it
			sleepMs(3);
		}
		sleepMs(300);
		unsigned packets, bad, junk, own;
		CHECK(stats(&packets, &bad, &junk, &own), "no statistics from B");
		printf("NETTEST A: lobby port got %u, game port got %u\n", junk, packets);
		CHECK(packets == 0, "datagrams for port 8086 reached socket 8088 (%u)", packets);
		CHECK(junk >= 28, "only %u of 30 reached port 8086", junk);
		Datagram d;
		unsigned stray = 0;
		while (receive(g_game, d)) ++stray;
		CHECK(stray == 0, "%u stray datagrams on the game socket", stray);
	}

	// 5 loopback
	printf("NETTEST A: loopback\n");
	{
		Datagram d;
		while (receive(g_lobby, d)) {}
		sendText(g_game, g_myIP, LOBBY_PORT, "SELF");
		sleepMs(50);
		bool got = false;
		while (receive(g_lobby, d))
			if (textOf(d) == "SELF") { got = true; CHECK(d.ip == g_myIP && d.port == GAME_PORT, "from %s:%u", ipString(d.ip).c_str(), d.port); }
		CHECK(got, "a datagram to the own address was not delivered");
	}

	// 6 loss
	printf("NETTEST A: loss\n");
	{
		CHECK(handshake("RESET", "RESET"), "no answer from B");
		const unsigned count = 500;
		PongCount c;
		c.seen.assign(count, false);
		std::vector<double> sentAt(count, 0);
		setPageValue("dropTx", 0.3);
		for (unsigned i = 0; i < count; ++i)
		{
			const std::vector<uint8_t> p = makePing("PING", i, 200 + (i % 7) * 100);
			sentAt[i] = nowMs();
			WebNet_SendTo(g_game, p.data(), (unsigned)p.size(), g_peerIP, GAME_PORT);
			sleepMs(2);
			pumpPongs(c, sentAt);
		}
		sleepMs(100);
		setPageValue("dropTx", 0);
		collectPongs(c, sentAt, 800);
		unsigned packets, bad, junk, own;
		CHECK(stats(&packets, &bad, &junk, &own), "no statistics from B");
		printf("NETTEST A: with 30%% loss on sending: B received %u of %u, %u echoes came back, %u corrupted\n", packets, count, c.pongs, c.bad);
		CHECK(bad == 0 && c.bad == 0, "corrupted datagrams (B %u, A %u)", bad, c.bad);
		CHECK(packets >= count * 55 / 100 && packets <= count * 85 / 100, "%u of %u arrived (expected about 70%%)", packets, count);
		CHECK(c.pongs >= packets * 95 / 100, "only %u echoes of %u received datagrams", c.pongs, packets);
	}

	// 7 burst
	printf("NETTEST A: burst\n");
	{
		CHECK(handshake("RESET", "RESET"), "no answer from B");
		const unsigned count = 1000;
		const unsigned droppedBefore = queryStat("engineTxDropped");
		for (unsigned i = 0; i < count; ++i)
		{
			const std::vector<uint8_t> p = makePing("PING", i, 100);
			WebNet_SendTo(g_game, p.data(), (unsigned)p.size(), g_peerIP, GAME_PORT);
		}
		sleepMs(1500);
		unsigned packets, bad, junk, own;
		CHECK(stats(&packets, &bad, &junk, &own), "no statistics from B");
		const unsigned dropped = queryStat("engineTxDropped") - droppedBefore;
		printf("NETTEST A: burst of %u datagrams: B received %u, ring dropped %u, corrupted %u\n", count, packets, dropped, bad);
		CHECK(bad == 0, "corrupted datagrams");
		CHECK(packets > 0 && packets <= count, "B received %u", packets);
		// still working afterwards
		PongCount c;
		c.seen.assign(10, false);
		std::vector<double> sentAt(10, 0);
		Datagram d;
		while (receive(g_game, d)) {}
		for (unsigned i = 0; i < 10; ++i)
		{
			const std::vector<uint8_t> p = makePing("PING", i, 100);
			WebNet_SendTo(g_game, p.data(), (unsigned)p.size(), g_peerIP, GAME_PORT);
			sleepMs(5);
		}
		collectPongs(c, sentAt, 500);
		CHECK(c.pongs >= 9, "only %u of 10 echoes after the burst", c.pongs);
	}

	handshake("BYE", "BYE", 5000);
	printf("NETTEST A: %d checks, %d failures\n", g_checks, g_failures);
	printf("NETTEST_RESULT %s\n", g_failures ? "FAIL" : "PASS");
	return g_failures ? 1 : 0;
}

} // namespace

int main()
{
	// The page joined the room before the program started (shell.html).
	g_myIP = WebNet_GetLocalIP();
	printf("NETTEST: address %s name %s\n", ipString(g_myIP).c_str(), WebNet_GetPlayerName());
	unsigned short port = 0;
	g_lobby = WebNet_Open(g_myIP, LOBBY_PORT, &port);
	g_game = WebNet_Open(g_myIP, GAME_PORT, &port);
	CHECK(g_lobby > 0 && g_game > 0, "sockets");
	CHECK(WebNet_Open(g_myIP, LOBBY_PORT, &port) == -EADDRINUSE, "a port can be bound once");
	CHECK(WebNet_Open(0x08080808, 0, &port) < 0, "a foreign address cannot be bound");
	if (g_myIP == 0x7f000001) { printf("NETTEST: no room joined\n"); printf("NETTEST_RESULT FAIL\n"); return 1; }

	// Who drives? The lower address.
	printf("NETTEST: waiting for the other player\n");
	// The other player's address is learned from its HELLO broadcasts, so both start as B first and A takes over
	// as soon as the page reports it is the lower one of the room.
	const bool isA = MAIN_THREAD_EM_ASM_INT({
		var net = globalThis.zhNet;
		if (!net || !net.state.self) return 0;
		return net.state.self.id === 2 ? 1 : 0;
	}) != 0;
	return isA ? runA() : runB();
}
