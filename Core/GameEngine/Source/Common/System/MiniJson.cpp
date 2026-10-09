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

// FILE: MiniJson.cpp /////////////////////////////////////////////////////////
// See MiniJson.h.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include <stdlib.h>
#include <string.h>

#include "Common/MiniJson.h"

namespace
{

class Parser
{
public:
	Parser(const Char *text, Int length) : m_p(text), m_end(text + length), m_line(1) {}

	Bool parseDocument(MiniJson::Value &out, AsciiString &error)
	{
		if (m_end - m_p >= 3 && (unsigned char)m_p[0] == 0xEF && (unsigned char)m_p[1] == 0xBB && (unsigned char)m_p[2] == 0xBF)
			m_p += 3;
		skipSpace();
		if (!parseValue(out, 0))
		{
			error = m_error;
			return FALSE;
		}
		skipSpace();
		if (m_p != m_end)
		{
			fail("unexpected text after the document");
			error = m_error;
			return FALSE;
		}
		return TRUE;
	}

private:
	const Char *m_p;
	const Char *m_end;
	Int m_line;
	AsciiString m_error;

	Bool fail(const Char *what)
	{
		if (m_error.isEmpty())
			m_error.format("JSON error in line %d: %s", m_line, what);
		return FALSE;
	}

	void skipSpace()
	{
		while (m_p < m_end && (*m_p == ' ' || *m_p == '\t' || *m_p == '\r' || *m_p == '\n'))
		{
			if (*m_p == '\n')
				++m_line;
			++m_p;
		}
	}

	Bool literal(const Char *word)
	{
		const size_t n = strlen(word);
		if ((size_t)(m_end - m_p) >= n && strncmp(m_p, word, n) == 0)
		{
			m_p += n;
			return TRUE;
		}
		return FALSE;
	}

	static void appendUtf8(AsciiString &s, UnsignedInt cp)
	{
		Char buf[5];
		if (cp < 0x80)
		{
			buf[0] = (Char)cp; buf[1] = 0;
		}
		else if (cp < 0x800)
		{
			buf[0] = (Char)(0xC0 | (cp >> 6)); buf[1] = (Char)(0x80 | (cp & 0x3F)); buf[2] = 0;
		}
		else if (cp < 0x10000)
		{
			buf[0] = (Char)(0xE0 | (cp >> 12)); buf[1] = (Char)(0x80 | ((cp >> 6) & 0x3F)); buf[2] = (Char)(0x80 | (cp & 0x3F)); buf[3] = 0;
		}
		else
		{
			buf[0] = (Char)(0xF0 | (cp >> 18)); buf[1] = (Char)(0x80 | ((cp >> 12) & 0x3F)); buf[2] = (Char)(0x80 | ((cp >> 6) & 0x3F)); buf[3] = (Char)(0x80 | (cp & 0x3F)); buf[4] = 0;
		}
		s.concat(buf);
	}

	Bool hex4(UnsignedInt &out)
	{
		if (m_end - m_p < 4)
			return fail("bad \\u escape");
		out = 0;
		for (Int i = 0; i < 4; ++i)
		{
			Char c = m_p[i];
			out <<= 4;
			if (c >= '0' && c <= '9') out |= (UnsignedInt)(c - '0');
			else if (c >= 'a' && c <= 'f') out |= (UnsignedInt)(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F') out |= (UnsignedInt)(c - 'A' + 10);
			else return fail("bad \\u escape");
		}
		m_p += 4;
		return TRUE;
	}

	Bool parseString(AsciiString &out)
	{
		// m_p is at the opening quote
		++m_p;
		out.clear();
		while (TRUE)
		{
			if (m_p >= m_end)
				return fail("unterminated string");
			unsigned char c = (unsigned char)*m_p++;
			if (c == '"')
				return TRUE;
			if (c < 0x20)
				return fail("control character in string");
			if (c != '\\')
			{
				Char one[2] = { (Char)c, 0 };
				out.concat(one);
				continue;
			}
			if (m_p >= m_end)
				return fail("unterminated string");
			Char e = *m_p++;
			switch (e)
			{
				case '"': out.concat('"'); break;
				case '\\': out.concat('\\'); break;
				case '/': out.concat('/'); break;
				case 'b': out.concat('\b'); break;
				case 'f': out.concat('\f'); break;
				case 'n': out.concat('\n'); break;
				case 'r': out.concat('\r'); break;
				case 't': out.concat('\t'); break;
				case 'u':
				{
					UnsignedInt cp;
					if (!hex4(cp))
						return FALSE;
					if (cp >= 0xD800 && cp < 0xDC00)
					{
						UnsignedInt lo;
						if (m_end - m_p < 2 || m_p[0] != '\\' || m_p[1] != 'u')
							return fail("lone surrogate in string");
						m_p += 2;
						if (!hex4(lo))
							return FALSE;
						if (lo < 0xDC00 || lo > 0xDFFF)
							return fail("bad surrogate pair in string");
						cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
					}
					else if (cp >= 0xDC00 && cp <= 0xDFFF)
					{
						return fail("lone surrogate in string");
					}
					if (cp == 0)
						return fail("NUL in string");
					appendUtf8(out, cp);
					break;
				}
				default:
					return fail("bad escape in string");
			}
		}
	}

	Bool parseNumber(MiniJson::Value &out)
	{
		const Char *start = m_p;
		if (m_p < m_end && *m_p == '-') ++m_p;
		if (m_p >= m_end) return fail("bad number");
		if (*m_p == '0') ++m_p;
		else if (*m_p >= '1' && *m_p <= '9') { while (m_p < m_end && *m_p >= '0' && *m_p <= '9') ++m_p; }
		else return fail("bad number");
		if (m_p < m_end && *m_p == '.')
		{
			++m_p;
			if (m_p >= m_end || *m_p < '0' || *m_p > '9') return fail("bad number");
			while (m_p < m_end && *m_p >= '0' && *m_p <= '9') ++m_p;
		}
		if (m_p < m_end && (*m_p == 'e' || *m_p == 'E'))
		{
			++m_p;
			if (m_p < m_end && (*m_p == '+' || *m_p == '-')) ++m_p;
			if (m_p >= m_end || *m_p < '0' || *m_p > '9') return fail("bad number");
			while (m_p < m_end && *m_p >= '0' && *m_p <= '9') ++m_p;
		}
		AsciiString text;
		{
			Char *tmp = NEW Char[(m_p - start) + 1];
			memcpy(tmp, start, m_p - start);
			tmp[m_p - start] = 0;
			text = tmp;
			delete[] tmp;
		}
		out.m_type = MiniJson::JSON_NUMBER;
		out.m_number = strtod(text.str(), nullptr);
		return TRUE;
	}

	Bool parseValue(MiniJson::Value &out, Int depth)
	{
		if (depth > 32)
			return fail("nested too deeply");
		if (m_p >= m_end)
			return fail("unexpected end of the document");

		const Char c = *m_p;
		if (c == '{')
		{
			++m_p;
			out.m_type = MiniJson::JSON_OBJECT;
			skipSpace();
			if (m_p < m_end && *m_p == '}')
			{
				++m_p;
				return TRUE;
			}
			while (TRUE)
			{
				skipSpace();
				if (m_p >= m_end || *m_p != '"')
					return fail("object key expected");
				AsciiString key;
				if (!parseString(key))
					return FALSE;
				for (size_t i = 0; i < out.m_keys.size(); ++i)
				{
					if (out.m_keys[i] == key)
					{
						AsciiString msg;
						msg.format("duplicate key '%s'", key.str());
						return fail(msg.str());
					}
				}
				skipSpace();
				if (m_p >= m_end || *m_p != ':')
					return fail("':' expected");
				++m_p;
				skipSpace();
				out.m_keys.push_back(key);
				out.m_items.push_back(MiniJson::Value());
				if (!parseValue(out.m_items.back(), depth + 1))
					return FALSE;
				skipSpace();
				if (m_p < m_end && *m_p == ',')
				{
					++m_p;
					continue;
				}
				if (m_p < m_end && *m_p == '}')
				{
					++m_p;
					return TRUE;
				}
				return fail("',' or '}' expected");
			}
		}
		if (c == '[')
		{
			++m_p;
			out.m_type = MiniJson::JSON_ARRAY;
			skipSpace();
			if (m_p < m_end && *m_p == ']')
			{
				++m_p;
				return TRUE;
			}
			while (TRUE)
			{
				skipSpace();
				out.m_items.push_back(MiniJson::Value());
				if (!parseValue(out.m_items.back(), depth + 1))
					return FALSE;
				skipSpace();
				if (m_p < m_end && *m_p == ',')
				{
					++m_p;
					continue;
				}
				if (m_p < m_end && *m_p == ']')
				{
					++m_p;
					return TRUE;
				}
				return fail("',' or ']' expected");
			}
		}
		if (c == '"')
		{
			out.m_type = MiniJson::JSON_STRING;
			return parseString(out.m_string);
		}
		if (c == '-' || (c >= '0' && c <= '9'))
			return parseNumber(out);
		if (literal("true")) { out.m_type = MiniJson::JSON_BOOL; out.m_bool = TRUE; return TRUE; }
		if (literal("false")) { out.m_type = MiniJson::JSON_BOOL; out.m_bool = FALSE; return TRUE; }
		if (literal("null")) { out.m_type = MiniJson::JSON_NULL; return TRUE; }
		return fail("value expected");
	}
};

} // namespace

Bool MiniJson::parse(const Char *text, Int length, Value &out, AsciiString &error)
{
	error.clear();
	out = Value();
	Parser p(text, length);
	return p.parseDocument(out, error);
}
