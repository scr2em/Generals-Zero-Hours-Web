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

// FILE: AIMatchShared.h //////////////////////////////////////////////////////
//
// The parts of the AI test bench (AIMatch.cpp) that the other command line test benches reuse (the player assist
// test bench, AssistMatch.cpp): string helpers for the key=value options, the lookup of a map and a side by the
// names given on the command line, the compact JSON writer of the result line, and the switch that tells the engine
// a test bench drives the game (AIMatch::isActive(): no load screen, no replay, CRC messages into the command list).
//
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <vector>

namespace AIMatchShared
{

std::string lowered(const std::string &s);
std::string squashed(const std::string &s);		///< lower case letters and digits only
std::string trimmed(const std::string &s);
std::vector<std::string> split(const std::string &s, char sep);
std::string format(const char *fmt, ...);
Bool parseUnsigned(const std::string &text, UnsignedInt &out);
Bool parseSigned(const std::string &text, Int &out);

/// The map cache key of a map given by path, folder/file name or display name (as -aiMatch map= takes it).
Bool findMapPath(const std::string &name, std::string &path, std::string &error);
/// The index in ThePlayerTemplateStore of a side given as "random", a template name or a side name (as -aiMatch players= takes it).
Bool findSideTemplate(const std::string &side, Int &templateIndex, std::string &error);

/// While a test bench plays, AIMatch::isActive() is true: the engine skips the load screen and the replay, and puts its
/// CRC messages into the command list (no message stream is pumped).
void setBenchActive(Bool active);

// ------------------------------------------------------------------------------------------------
// JSON output. Compact, keys in the order written, numbers as integers where they are integers.
// ------------------------------------------------------------------------------------------------
class JsonWriter
{
public:
	JsonWriter() : m_pendingKey(false) { m_needComma.push_back(false); }

	void beginObject() { value(); m_out += '{'; m_needComma.push_back(false); }
	void endObject() { m_needComma.pop_back(); m_out += '}'; }
	void beginArray() { value(); m_out += '['; m_needComma.push_back(false); }
	void endArray() { m_needComma.pop_back(); m_out += ']'; }

	void key(const std::string &k)
	{
		comma();
		appendString(k);
		m_out += ':';
		m_pendingKey = true;
	}

	void str(const std::string &v) { value(); appendString(v); }
	void num(Int v) { value(); m_out += format("%d", v); }
	void num(UnsignedInt v) { value(); m_out += format("%u", v); }
	void real(double v) { value(); m_out += format("%.4f", v); }
	void boolean(Bool v) { value(); m_out += v ? "true" : "false"; }
	void null() { value(); m_out += "null"; }

	template <class T> void field(const char *k, T v) { key(k); put(v); }
	void field(const char *k, const char *v) { key(k); str(v); }
	void field(const char *k, const std::string &v) { key(k); str(v); }

	const std::string &text() const { return m_out; }

private:
	void put(Int v) { num(v); }
	void put(UnsignedInt v) { num(v); }
	void put(double v) { real(v); }
	void put(bool v) { boolean(v); }

	void comma()
	{
		if (m_needComma.back())
			m_out += ',';
		m_needComma.back() = true;
	}
	// A value either follows a key (no comma) or is an element (comma).
	void value()
	{
		if (m_pendingKey)
			m_pendingKey = false;
		else
			comma();
	}
	void appendString(const std::string &s)
	{
		m_out += '"';
		for (size_t i = 0; i < s.size(); ++i)
		{
			unsigned char c = (unsigned char)s[i];
			if (c == '"' || c == '\\')
			{
				m_out += '\\';
				m_out += (char)c;
			}
			else if (c < 0x20)
				m_out += format("\\u%04x", (unsigned)c);
			else
				m_out += (char)c;
		}
		m_out += '"';
	}

	std::string m_out;
	std::vector<bool> m_needComma;
	bool m_pendingKey;
};

} // namespace AIMatchShared
