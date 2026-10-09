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

// FILE: MiniJson.h ///////////////////////////////////////////////////////////
//
// A small strict JSON reader (RFC 8259) for the manifest of an army package.
// Strings are UTF-8 (\u escapes are converted). Nesting is limited to 32 levels.
// Object keys keep their file order; duplicate keys are an error.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/AsciiString.h"
#include "Common/STLTypedefs.h"

class MiniJson
{
public:
	enum Type
	{
		JSON_NULL,
		JSON_BOOL,
		JSON_NUMBER,
		JSON_STRING,
		JSON_ARRAY,
		JSON_OBJECT
	};

	struct Value
	{
		Value() : m_type(JSON_NULL), m_bool(FALSE), m_number(0.0) {}

		Bool isString() const { return m_type == JSON_STRING; }
		Bool isNumber() const { return m_type == JSON_NUMBER; }
		Bool isBool() const { return m_type == JSON_BOOL; }
		Bool isArray() const { return m_type == JSON_ARRAY; }
		Bool isObject() const { return m_type == JSON_OBJECT; }

		/// For objects: the member with that key, or nullptr.
		const Value *get(const Char *key) const
		{
			if (m_type != JSON_OBJECT)
				return nullptr;
			for (size_t i = 0; i < m_keys.size(); ++i)
			{
				if (strcmp(m_keys[i].str(), key) == 0)
					return &m_items[i];
			}
			return nullptr;
		}

		Type m_type;
		Bool m_bool;
		double m_number;
		AsciiString m_string;
		std::vector<Value> m_items;       ///< array elements, or object member values
		std::vector<AsciiString> m_keys;  ///< object member names (same index as m_items)
	};

	/// Parses text (length bytes, a UTF-8 byte order mark is skipped). On failure returns FALSE and fills error
	/// with a short sentence that names the line.
	static Bool parse(const Char *text, Int length, Value &out, AsciiString &error);
};
