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

// FILE: Sha256.cpp ///////////////////////////////////////////////////////////
// See Sha256.h.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include <string.h>

#include "Common/Sha256.h"

namespace
{

const UnsignedInt K[64] =
{
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

inline UnsignedInt rotr(UnsignedInt x, int n) { return (x >> n) | (x << (32 - n)); }

} // namespace

void Sha256::reset()
{
	m_state[0] = 0x6a09e667; m_state[1] = 0xbb67ae85; m_state[2] = 0x3c6ef372; m_state[3] = 0xa54ff53a;
	m_state[4] = 0x510e527f; m_state[5] = 0x9b05688c; m_state[6] = 0x1f83d9ab; m_state[7] = 0x5be0cd19;
	m_bufferUsed = 0;
	m_lengthLow = 0;
	m_lengthHigh = 0;
}

void Sha256::block(const unsigned char *chunk)
{
	UnsignedInt w[64];
	int i;
	for (i = 0; i < 16; ++i)
	{
		w[i] = ((UnsignedInt)chunk[i * 4] << 24) | ((UnsignedInt)chunk[i * 4 + 1] << 16) | ((UnsignedInt)chunk[i * 4 + 2] << 8) | (UnsignedInt)chunk[i * 4 + 3];
	}
	for (i = 16; i < 64; ++i)
	{
		UnsignedInt s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
		UnsignedInt s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}

	UnsignedInt a = m_state[0], b = m_state[1], c = m_state[2], d = m_state[3];
	UnsignedInt e = m_state[4], f = m_state[5], g = m_state[6], h = m_state[7];
	for (i = 0; i < 64; ++i)
	{
		UnsignedInt S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
		UnsignedInt ch = (e & f) ^ (~e & g);
		UnsignedInt t1 = h + S1 + ch + K[i] + w[i];
		UnsignedInt S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
		UnsignedInt maj = (a & b) ^ (a & c) ^ (b & c);
		UnsignedInt t2 = S0 + maj;
		h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
	}
	m_state[0] += a; m_state[1] += b; m_state[2] += c; m_state[3] += d;
	m_state[4] += e; m_state[5] += f; m_state[6] += g; m_state[7] += h;
}

void Sha256::update(const void *data, UnsignedInt size)
{
	const unsigned char *p = (const unsigned char *)data;

	UnsignedInt oldLow = m_lengthLow;
	m_lengthLow += size;
	if (m_lengthLow < oldLow)
		++m_lengthHigh;

	if (m_bufferUsed > 0)
	{
		UnsignedInt take = 64 - m_bufferUsed;
		if (take > size)
			take = size;
		memcpy(m_buffer + m_bufferUsed, p, take);
		m_bufferUsed += take;
		p += take;
		size -= take;
		if (m_bufferUsed < 64)
			return;
		block(m_buffer);
		m_bufferUsed = 0;
	}
	while (size >= 64)
	{
		block(p);
		p += 64;
		size -= 64;
	}
	if (size > 0)
	{
		memcpy(m_buffer, p, size);
		m_bufferUsed = size;
	}
}

AsciiString Sha256::finish()
{
	// total length in bits, big endian
	const UnsignedInt bitsHigh = (m_lengthHigh << 3) | (m_lengthLow >> 29);
	const UnsignedInt bitsLow = m_lengthLow << 3;

	unsigned char pad[72];
	UnsignedInt padLen = (m_bufferUsed < 56) ? (56 - m_bufferUsed) : (120 - m_bufferUsed);
	memset(pad, 0, sizeof(pad));
	pad[0] = 0x80;
	update(pad, padLen);

	unsigned char lenBytes[8];
	lenBytes[0] = (unsigned char)(bitsHigh >> 24); lenBytes[1] = (unsigned char)(bitsHigh >> 16);
	lenBytes[2] = (unsigned char)(bitsHigh >> 8); lenBytes[3] = (unsigned char)bitsHigh;
	lenBytes[4] = (unsigned char)(bitsLow >> 24); lenBytes[5] = (unsigned char)(bitsLow >> 16);
	lenBytes[6] = (unsigned char)(bitsLow >> 8); lenBytes[7] = (unsigned char)bitsLow;
	update(lenBytes, 8);

	static const Char hex[] = "0123456789abcdef";
	Char out[65];
	for (int i = 0; i < 8; ++i)
	{
		for (int j = 0; j < 4; ++j)
		{
			unsigned char byte = (unsigned char)(m_state[i] >> (24 - 8 * j));
			out[i * 8 + j * 2] = hex[byte >> 4];
			out[i * 8 + j * 2 + 1] = hex[byte & 15];
		}
	}
	out[64] = 0;
	return AsciiString(out);
}
