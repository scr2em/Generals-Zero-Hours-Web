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

// FILE: ArmyPackages.cpp /////////////////////////////////////////////////////
// See ArmyPackages.h and docs/ARMY_PACKAGES.md.
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"

#include <algorithm>
#include <stdio.h>
#include <string.h>

#include "Common/ArmyPackages.h"

#include "Common/ArchiveFileSystem.h"
#include "Compression.h"
#include "Common/GameAudio.h"
#include "Common/GlobalData.h"
#include "Common/INIException.h"
#include "Common/LocalFileSystem.h"
#include "Common/MapReaderWriterInfo.h"
#include "Common/MiniJson.h"
#include "Common/NameKeyGenerator.h"
#include "Common/PlayerTemplate.h"
#include "Common/Science.h"
#include "Common/Sha256.h"
#include "Common/SpecialPower.h"
#include "Common/ThingFactory.h"
#include "Common/Upgrade.h"
#include "Common/ZipArchiveFile.h"
#include "Common/file.h"
#include "Common/DamageFX.h"
#include "GameClient/ControlBar.h"
#include "GameClient/FXList.h"
#include "GameClient/GameText.h"
#include "GameClient/Image.h"
#include "GameClient/ParticleSys.h"
#include "GameLogic/AI.h"
#include "GameLogic/Armor.h"
#include "GameLogic/CrateSystem.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/Weapon.h"
#include "Common/WellKnownKeys.h"
#include "Common/Dict.h"

ArmyPackages *TheArmyPackages = nullptr;

namespace
{

// ---------------------------------------------------------------------------------------------
// limits
// ---------------------------------------------------------------------------------------------
const Int ARMY_MAX_PACKAGES = 32;
const UnsignedInt ARMY_MAX_MANIFEST_SIZE = 1 << 20;     // 1 MB
const UnsignedInt ARMY_MAX_TEXT_SIZE = 32 << 20;        // 32 MB per INI / string / script file
const Int ARMY_MAX_FACTIONS_PER_PACKAGE = 8;

// ---------------------------------------------------------------------------------------------
// The definition files a package may carry, in the order the engine loads the same kinds of data.
// Each file holds blocks of exactly one type.
// ---------------------------------------------------------------------------------------------
struct IniFileRule
{
	const Char *m_file;        // lower case, in Army/INI/
	const Char *m_blockType;
};

const IniFileRule s_iniFiles[] =
{
	{ "science.ini",            "Science" },
	{ "soundeffects.ini",       "AudioEvent" },
	{ "voice.ini",              "AudioEvent" },
	{ "speech.ini",             "DialogEvent" },
	{ "music.ini",              "MusicTrack" },
	{ "playertemplate.ini",     "PlayerTemplate" },
	{ "particlesystem.ini",     "ParticleSystem" },
	{ "fxlist.ini",             "FXList" },
	{ "weapon.ini",             "Weapon" },
	{ "objectcreationlist.ini", "ObjectCreationList" },
	{ "locomotor.ini",          "Locomotor" },
	{ "specialpower.ini",       "SpecialPower" },
	{ "damagefx.ini",           "DamageFX" },
	{ "armor.ini",              "Armor" },
	{ "object.ini",             "Object" },
	{ "upgrade.ini",            "Upgrade" },
	{ "mappedimages.ini",       "MappedImage" },
	{ "commandbutton.ini",      "CommandButton" },
	{ "commandset.ini",         "CommandSet" },
	{ "crate.ini",              "CrateData" },
	{ "aidata.ini",             "AIData" },
};
const Int s_iniFileCount = sizeof(s_iniFiles) / sizeof(s_iniFiles[0]);

// Every top level block keyword of the INI reader (INI.cpp, theTypeTable). A line that starts with one of
// them, has a name after it and no '=' starts a block; it must be of the type the file allows.
const Char *const s_engineBlockKeywords[] =
{
	"AIData", "Animation", "Armor", "AudioEvent", "AudioSettings", "BenchProfile", "Bridge", "Campaign",
	"ChallengeGenerals", "CommandButton", "CommandMap", "CommandSet", "ControlBarResizer", "ControlBarScheme",
	"CrateData", "Credits", "DamageFX", "DialogEvent", "DrawGroupInfo", "DynamicGameLOD", "EvaEvent", "FXList",
	"GameData", "HeaderTemplate", "InGameUI", "LODPreset", "Language", "Locomotor", "MapCache", "MapData",
	"MappedImage", "MiscAudio", "Mouse", "MouseCursor", "MultiplayerColor", "MultiplayerSettings",
	"MultiplayerStartingMoneyChoice", "MusicTrack", "Object", "ObjectCreationList", "ObjectReskin",
	"OnlineChatColors", "ParticleSystem", "PlayerTemplate", "Rank", "ReallyLowMHz", "Road", "Science",
	"ScriptAction", "ScriptCondition", "ShellMenuScheme", "SpecialPower", "StaticGameLOD", "Terrain", "Upgrade",
	"Video", "WaterSet", "WaterTransparency", "Weapon", "Weather", "WebpageURL", "WindowTransition"
};
const Int s_engineBlockKeywordCount = sizeof(s_engineBlockKeywords) / sizeof(s_engineBlockKeywords[0]);

Bool isEngineBlockKeyword(const AsciiString &word)
{
	for (Int i = 0; i < s_engineBlockKeywordCount; ++i)
	{
		if (strcmp(s_engineBlockKeywords[i], word.str()) == 0)
			return TRUE;
	}
	return FALSE;
}

const IniFileRule *findIniFileRule(const AsciiString &lowerFile)
{
	for (Int i = 0; i < s_iniFileCount; ++i)
	{
		if (strcmp(s_iniFiles[i].m_file, lowerFile.str()) == 0)
			return &s_iniFiles[i];
	}
	return nullptr;
}

// ---------------------------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------------------------
AsciiString operator+(const AsciiString &a, const AsciiString &b)
{
	AsciiString out = a;
	out.concat(b);
	return out;
}
AsciiString operator+(const AsciiString &a, const Char *b)
{
	AsciiString out = a;
	out.concat(b);
	return out;
}
AsciiString operator+(const Char *a, const AsciiString &b)
{
	AsciiString out = a;
	out.concat(b);
	return out;
}

void armyLog(const Char *format, ...)
{
	Char buffer[1024];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	buffer[sizeof(buffer) - 1] = 0;

	DEBUG_LOG(("%s", buffer));
	// release builds have no log file; the line goes to the console (the browser console on the web)
	fprintf(stderr, "%s\n", buffer);
}

AsciiString fileBaseName(const AsciiString &path)
{
	const Char *s = path.str();
	const Char *slash = strrchr(s, '/');
	const Char *back = strrchr(s, '\\');
	if (back != nullptr && (slash == nullptr || back > slash))
		slash = back;
	return AsciiString(slash ? slash + 1 : s);
}

Bool startsWith(const AsciiString &s, const AsciiString &prefix)
{
	return strncmp(s.str(), prefix.str(), prefix.getLength()) == 0;
}

Bool isIdentifierChar(Char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

Bool isValidId(const AsciiString &id)
{
	const Int n = id.getLength();
	if (n < 3 || n > 64)
		return FALSE;
	for (Int i = 0; i < n; ++i)
	{
		Char c = id.getCharAt(i);
		Bool digitOrLower = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
		if (i == 0 ? !digitOrLower : !(digitOrLower || c == '.' || c == '-'))
			return FALSE;
	}
	return TRUE;
}

Bool isValidTag(const AsciiString &tag)
{
	const Int n = tag.getLength();
	if (n < 2 || n > 6)
		return FALSE;
	for (Int i = 0; i < n; ++i)
	{
		Char c = tag.getCharAt(i);
		Bool upper = (c >= 'A' && c <= 'Z');
		Bool digit = (c >= '0' && c <= '9');
		if (i == 0 ? !upper : !(upper || digit))
			return FALSE;
	}
	return TRUE;
}

Bool isValidDefinitionName(const AsciiString &name)
{
	if (name.isEmpty() || name.getLength() > 100)
		return FALSE;
	// Any character the INI reader keeps inside one token: mods use names like "TK-XLocomotor".
	for (Int i = 0; i < name.getLength(); ++i)
	{
		const Char c = name.getCharAt(i);
		if (c <= ' ' || c >= 127 || c == '=' || c == ',' || c == ';')
			return FALSE;
	}
	return TRUE;
}

AsciiString lowerCase(const AsciiString &s)
{
	AsciiString out = s;
	out.toLower();
	return out;
}

// UTF-8 to the engine's wide characters (characters outside the basic plane become '?')
UnicodeString utf8ToUnicode(const AsciiString &s)
{
	UnicodeString out;
	const unsigned char *p = (const unsigned char *)s.str();
	while (*p)
	{
		UnsignedInt cp;
		if (p[0] < 0x80) { cp = p[0]; p += 1; }
		else if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) { cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F); p += 2; }
		else if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) { cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; }
		else { cp = '?'; p += 1; while ((*p & 0xC0) == 0x80) ++p; }
		if (cp > 0xFFFF)
			cp = '?';
		out.concat((WideChar)cp);
	}
	return out;
}

// A buffer in memory as the input of the data chunk reader (skirmish scripts).
class MemoryChunkStream : public ChunkInputStream
{
public:
	MemoryChunkStream(Char *buffer, Int size) : m_buffer(buffer), m_size(size), m_pos(0)
	{
		// scripts may be stored compressed, like the files of the game data
		if (CompressionManager::isDataCompressed(m_buffer, m_size) != 0)
		{
			Int uncompressed = CompressionManager::getUncompressedSize(m_buffer, m_size);
			Char *out = NEW Char[uncompressed];
			if (CompressionManager::decompressData(m_buffer, m_size, out, uncompressed) == uncompressed)
			{
				delete[] m_buffer;
				m_buffer = out;
				m_size = uncompressed;
			}
			else
			{
				delete[] out;
			}
		}
	}
	~MemoryChunkStream() { delete[] m_buffer; }

	virtual Int read(void *pData, Int numBytes) override
	{
		if (numBytes + m_pos > m_size)
			numBytes = m_size - m_pos;
		if (numBytes > 0)
		{
			memcpy(pData, m_buffer + m_pos, numBytes);
			m_pos += numBytes;
		}
		return numBytes;
	}
	virtual UnsignedInt tell() override { return m_pos; }
	virtual Bool absoluteSeek(UnsignedInt pos) override { m_pos = pos > (UnsignedInt)m_size ? m_size : (Int)pos; return TRUE; }
	virtual Bool eof() override { return m_pos == m_size; }

private:
	Char *m_buffer;
	Int m_size;
	Int m_pos;
};

// ---------------------------------------------------------------------------------------------
// Does a definition of that block type and name exist already? (game data or an earlier package)
// ---------------------------------------------------------------------------------------------
Bool definitionExists(const AsciiString &type, const AsciiString &name)
{
	const Char *t = type.str();
	if (strcmp(t, "Object") == 0)
		return TheThingFactory != nullptr && TheThingFactory->findTemplate(name, FALSE) != nullptr;
	if (strcmp(t, "Weapon") == 0)
		return TheWeaponStore != nullptr && TheWeaponStore->findWeaponTemplateByNameKey(TheNameKeyGenerator->nameToKey(name)) != nullptr;
	if (strcmp(t, "Armor") == 0)
		return TheArmorStore != nullptr && TheArmorStore->findArmorTemplate(name) != nullptr;
	if (strcmp(t, "Locomotor") == 0)
		return TheLocomotorStore != nullptr && TheLocomotorStore->findLocomotorTemplate(TheNameKeyGenerator->nameToKey(name)) != nullptr;
	if (strcmp(t, "FXList") == 0)
		return TheFXListStore != nullptr && TheFXListStore->findFXList(name.str()) != nullptr;
	if (strcmp(t, "ObjectCreationList") == 0)
		return TheObjectCreationListStore != nullptr && TheObjectCreationListStore->findObjectCreationList(name.str()) != nullptr;
	if (strcmp(t, "ParticleSystem") == 0)
		return TheParticleSystemManager != nullptr && TheParticleSystemManager->findTemplate(name) != nullptr;
	if (strcmp(t, "Upgrade") == 0)
		return TheUpgradeCenter != nullptr && TheUpgradeCenter->findUpgrade(name) != nullptr;
	if (strcmp(t, "Science") == 0)
		return TheScienceStore != nullptr && TheScienceStore->isValidScience((ScienceType)TheNameKeyGenerator->nameToKey(name));
	if (strcmp(t, "SpecialPower") == 0)
		return TheSpecialPowerStore != nullptr && TheSpecialPowerStore->findSpecialPowerTemplate(name) != nullptr;
	if (strcmp(t, "CommandButton") == 0)
		return TheControlBar != nullptr && TheControlBar->findCommandButton(name) != nullptr;
	if (strcmp(t, "CommandSet") == 0)
		return TheControlBar != nullptr && TheControlBar->findCommandSet(name) != nullptr;
	if (strcmp(t, "PlayerTemplate") == 0)
		return ThePlayerTemplateStore != nullptr && ThePlayerTemplateStore->findPlayerTemplate(TheNameKeyGenerator->nameToKey(name)) != nullptr;
	if (strcmp(t, "MappedImage") == 0)
		return TheMappedImageCollection != nullptr && TheMappedImageCollection->findImageByName(name) != nullptr;
	if (strcmp(t, "AudioEvent") == 0 || strcmp(t, "DialogEvent") == 0 || strcmp(t, "MusicTrack") == 0)
		return TheAudio != nullptr && TheAudio->findAudioEventInfo(name) != nullptr;
	if (strcmp(t, "CrateData") == 0)
		return TheCrateSystem != nullptr && TheCrateSystem->findCrateTemplate(name) != nullptr;
	if (strcmp(t, "DamageFX") == 0)
		return TheDamageFXStore != nullptr && TheDamageFXStore->findDamageFX(name) != nullptr;
	return FALSE;
}

// ---------------------------------------------------------------------------------------------
// text scanning (before anything is loaded)
// ---------------------------------------------------------------------------------------------
// Splits one line the way the INI reader sees it: the comment after ';' is dropped, control characters count as
// spaces. With splitEquals the '=' separates words as well (the reader's separators).
void splitWords(const Char *line, Int length, Bool splitEquals, std::vector<AsciiString> &words, Bool &hasEquals)
{
	words.clear();
	hasEquals = FALSE;
	AsciiString current;
	for (Int i = 0; i <= length; ++i)
	{
		Char c = (i < length) ? line[i] : ' ';
		if (c == ';')
		{
			c = ' ';
			i = length;   // the rest is a comment; flush the word and stop
		}
		if (c == '=')
			hasEquals = TRUE;
		Bool separator = (c > 0 && c <= ' ') || (splitEquals && c == '=');
		if (separator)
		{
			if (current.isNotEmpty())
			{
				words.push_back(current);
				current.clear();
			}
		}
		else
		{
			Char one[2] = { c, 0 };
			current.concat(one);
		}
	}
}

// "ObjectReskin <new> <existing>" defines an Object too: it is allowed in Object.ini and its new name
// follows the rules of objects.
Bool blockAllowedInFile(const char *blockType, const AsciiString &fileBlockType)
{
	if (strcmp(blockType, fileBlockType.str()) == 0)
		return TRUE;
	return fileBlockType == "Object" && strcmp(blockType, "ObjectReskin") == 0;
}

const char *definitionTypeOf(const char *blockType)
{
	return strcmp(blockType, "ObjectReskin") == 0 ? "Object" : blockType;
}

struct TemplateScan
{
	AsciiString m_name;
	AsciiString m_side;
	AsciiString m_startingBuilding;
	std::vector<AsciiString> m_startingUnits;
	Bool m_playable;
	TemplateScan() : m_playable(FALSE) {}
};

struct DefinedName
{
	AsciiString m_type;
	AsciiString m_name;
};

// what the scan of all definition files found
struct PackageScan
{
	PackageScan() : m_upgradeCount(0) {}

	std::vector<DefinedName> m_defined;
	std::vector<TemplateScan> m_templates;
	std::vector<AsciiString> m_objectSides;      // values of "Side =" in Object.ini
	std::vector<AsciiString> m_buildListSides;   // AIData: SkirmishBuildList <side>
	std::vector<AsciiString> m_sideInfoSides;    // AIData: SideInfo <side>
	Int m_upgradeCount;
};

Bool containsString(const std::vector<AsciiString> &list, const AsciiString &s)
{
	for (size_t i = 0; i < list.size(); ++i)
	{
		if (list[i] == s)
			return TRUE;
	}
	return FALSE;
}

// Walks over the lines of a text buffer. Calls handler(line, length, lineNumber) for each.
template <typename Handler>
void forEachLine(const Char *text, Int size, Handler &handler)
{
	Int line = 1;
	Int start = 0;
	for (Int i = 0; i <= size; ++i)
	{
		if (i == size || text[i] == '\n')
		{
			Int end = i;
			if (end > start && text[end - 1] == '\r')
				--end;
			if (!handler.line(text + start, end - start, line))
				return;
			start = i + 1;
			++line;
		}
	}
}

struct IniScanner
{
	IniScanner(const AsciiString &tag, const AsciiString &file, const AsciiString &blockType, const std::vector<AsciiString> &sides, PackageScan &scan, AsciiString &error)
		: m_tag(tag), m_file(file), m_blockType(blockType), m_sides(sides), m_scan(scan), m_error(error), m_current(-1)
	{
		m_prefix.format("%s_", tag.str());
	}

	Bool fail(Int lineNo, const Char *format, ...)
	{
		Char text[512];
		va_list args;
		va_start(args, format);
		vsnprintf(text, sizeof(text), format, args);
		va_end(args);
		text[sizeof(text) - 1] = 0;
		m_error.format("Army/INI/%s line %d: %s", m_file.str(), lineNo, text);
		return FALSE;
	}

	Bool line(const Char *text, Int length, Int lineNo)
	{
		std::vector<AsciiString> words;
		Bool hasEquals;
		splitWords(text, length, FALSE, words, hasEquals);
		if (words.empty())
			return TRUE;

		// a block of the INI reader: keyword and name, no '='
		if (!hasEquals && words.size() >= 2 && isEngineBlockKeyword(words[0]))
		{
			if (!blockAllowedInFile(words[0].str(), m_blockType))
				return fail(lineNo, "'%s' blocks are not allowed in this file (only %s)", words[0].str(), m_blockType.str());
			const AsciiString defType(definitionTypeOf(words[0].str()));

			const AsciiString &name = words[1];
			if (!isValidDefinitionName(name))
				return fail(lineNo, "bad %s name '%s'", m_blockType.str(), name.str());
			if (!startsWith(name, m_prefix))
				return fail(lineNo, "%s '%s' does not start with %s (a package may only add definitions with its own prefix)", m_blockType.str(), name.str(), m_prefix.str());
			if (name.getLength() == m_prefix.getLength())
				return fail(lineNo, "%s name '%s' has nothing after the prefix", m_blockType.str(), name.str());
			for (size_t i = 0; i < m_scan.m_defined.size(); ++i)
			{
				if (m_scan.m_defined[i].m_type == defType && m_scan.m_defined[i].m_name == name)
					return fail(lineNo, "%s '%s' is defined twice in the package", m_blockType.str(), name.str());
			}
			if (definitionExists(defType, name))
				return fail(lineNo, "%s '%s' already exists (a package may not redefine anything)", m_blockType.str(), name.str());

			if (strcmp(words[0].str(), "ObjectReskin") == 0)
			{
				// the engine copies the existing object when it reads the line: it must be defined already,
				// in the game data or earlier in this file
				if (words.size() < 3)
					return fail(lineNo, "ObjectReskin '%s' names no object to copy", name.str());
				const AsciiString &parent = words[2];
				Bool found = definitionExists(AsciiString("Object"), parent);
				for (size_t i = 0; !found && i < m_scan.m_defined.size(); ++i)
					found = m_scan.m_defined[i].m_type == "Object" && m_scan.m_defined[i].m_name == parent;
				if (!found)
					return fail(lineNo, "ObjectReskin '%s' copies '%s', which is not defined before it (neither in the game data nor earlier in the package)", name.str(), parent.str());
			}

			DefinedName d;
			d.m_type = defType;
			d.m_name = name;
			m_scan.m_defined.push_back(d);

			if (m_blockType == "Upgrade")
				++m_scan.m_upgradeCount;
			if (m_blockType == "PlayerTemplate")
			{
				TemplateScan t;
				t.m_name = name;
				m_scan.m_templates.push_back(t);
				m_current = (Int)m_scan.m_templates.size() - 1;
			}
			return TRUE;
		}

		// fields we look at: "Side = x" in objects, three fields in player templates
		std::vector<AsciiString> parts;
		Bool dummy;
		splitWords(text, length, TRUE, parts, dummy);
		if (parts.size() >= 2)
		{
			if (m_blockType == "Object" && strcmp(parts[0].str(), "Side") == 0)
				m_scan.m_objectSides.push_back(parts[1]);
			if (m_blockType == "PlayerTemplate" && m_current >= 0)
			{
				TemplateScan &t = m_scan.m_templates[m_current];
				if (strcmp(parts[0].str(), "Side") == 0)
					t.m_side = parts[1];
				else if (strcmp(parts[0].str(), "StartingBuilding") == 0)
					t.m_startingBuilding = parts[1];
				else if (strncmp(parts[0].str(), "StartingUnit", 12) == 0)
					t.m_startingUnits.push_back(parts[1]);
				else if (strcmp(parts[0].str(), "PlayableSide") == 0)
					t.m_playable = (stricmp(parts[1].str(), "Yes") == 0 || stricmp(parts[1].str(), "True") == 0);
			}
		}
		return TRUE;
	}

	AsciiString m_tag;
	AsciiString m_prefix;
	AsciiString m_file;
	AsciiString m_blockType;
	const std::vector<AsciiString> &m_sides;
	PackageScan &m_scan;
	AsciiString &m_error;
	Int m_current;
};

// AIData.ini: one AIData block (more are fine) with only SideInfo and SkirmishBuildList entries inside
struct AIDataScanner
{
	enum State { OUTSIDE, IN_AIDATA, IN_SIDEINFO, IN_SKILLSET, IN_BUILDLIST, IN_STRUCTURE };

	AIDataScanner(const std::vector<AsciiString> &sides, PackageScan &scan, AsciiString &error)
		: m_sides(sides), m_scan(scan), m_error(error), m_state(OUTSIDE) {}

	Bool fail(Int lineNo, const Char *format, ...)
	{
		Char text[512];
		va_list args;
		va_start(args, format);
		vsnprintf(text, sizeof(text), format, args);
		va_end(args);
		text[sizeof(text) - 1] = 0;
		m_error.format("Army/INI/aidata.ini line %d: %s", lineNo, text);
		return FALSE;
	}

	Bool checkSide(Int lineNo, const Char *what, const std::vector<AsciiString> &words, std::vector<AsciiString> &store)
	{
		if (words.size() < 2)
			return fail(lineNo, "%s needs a side name", what);
		if (!containsString(m_sides, words[1]))
			return fail(lineNo, "%s '%s' is not a side of this package", what, words[1].str());
		if (containsString(store, words[1]))
			return fail(lineNo, "%s '%s' is defined twice", what, words[1].str());
		store.push_back(words[1]);
		return TRUE;
	}

	Bool line(const Char *text, Int length, Int lineNo)
	{
		std::vector<AsciiString> words;
		Bool hasEquals;
		splitWords(text, length, TRUE, words, hasEquals);
		if (words.empty())
			return TRUE;
		const Char *first = words[0].str();
		const Bool isEnd = (stricmp(first, "End") == 0);

		switch (m_state)
		{
			case OUTSIDE:
				if (strcmp(first, "AIData") != 0)
					return fail(lineNo, "only an AIData block is allowed here, found '%s'", first);
				m_state = IN_AIDATA;
				return TRUE;

			case IN_AIDATA:
				if (isEnd) { m_state = OUTSIDE; return TRUE; }
				if (strcmp(first, "SideInfo") == 0)
				{
					m_state = IN_SIDEINFO;
					return checkSide(lineNo, "SideInfo", words, m_scan.m_sideInfoSides);
				}
				if (strcmp(first, "SkirmishBuildList") == 0)
				{
					m_state = IN_BUILDLIST;
					return checkSide(lineNo, "SkirmishBuildList", words, m_scan.m_buildListSides);
				}
				return fail(lineNo, "'%s' is not allowed in AIData of a package (only SideInfo and SkirmishBuildList entries for its own sides)", first);

			case IN_SIDEINFO:
				if (isEnd) { m_state = IN_AIDATA; return TRUE; }
				if (strncmp(first, "SkillSet", 8) == 0 && words.size() == 1)
					m_state = IN_SKILLSET;
				return TRUE;

			case IN_SKILLSET:
				if (isEnd) m_state = IN_SIDEINFO;
				return TRUE;

			case IN_BUILDLIST:
				if (isEnd) { m_state = IN_AIDATA; return TRUE; }
				if (strcmp(first, "Structure") == 0 && !hasEquals)
					m_state = IN_STRUCTURE;
				return TRUE;

			case IN_STRUCTURE:
				if (isEnd) m_state = IN_BUILDLIST;
				return TRUE;
		}
		return TRUE;
	}

	const std::vector<AsciiString> &m_sides;
	PackageScan &m_scan;
	AsciiString &m_error;
	State m_state;
};

// ---------------------------------------------------------------------------------------------
// Classification of the entries of a package
// ---------------------------------------------------------------------------------------------
enum EntryKind
{
	ENTRY_MANIFEST,
	ENTRY_INI,
	ENTRY_STRINGS,
	ENTRY_SCRIPTS,
	ENTRY_INFO,
	ENTRY_ASSET,
	ENTRY_BAD
};

EntryKind classifyEntry(const AsciiString &lower, const AsciiString &tagLower, AsciiString &problem)
{
	const Char *s = lower.str();
	if (strcmp(s, "manifest.json") == 0)
		return ENTRY_MANIFEST;
	if (strncmp(s, "army/", 5) == 0)
	{
		if (strncmp(s, "army/ini/", 9) == 0)
		{
			const Char *file = s + 9;
			if (strchr(file, '/') == nullptr && findIniFileRule(AsciiString(file)) != nullptr)
				return ENTRY_INI;
			if (strcmp(file, "eva.ini") == 0 || strcmp(file, "rank.ini") == 0)
				problem = "this kind of definition cannot be added by a package";
			else
				problem = "unknown definition file";
			return ENTRY_BAD;
		}
		if (strcmp(s, "army/strings.str") == 0)
			return ENTRY_STRINGS;
		if (strcmp(s, "army/scripts/skirmish.scb") == 0)
			return ENTRY_SCRIPTS;
		problem = "unknown entry in Army/";
		return ENTRY_BAD;
	}
	if (strchr(s, '/') == nullptr && (strncmp(s, "license", 7) == 0 || strncmp(s, "readme", 6) == 0))
		return ENTRY_INFO;
	if (strncmp(s, "art/", 4) == 0 || strncmp(s, "data/audio/", 11) == 0)
	{
		const Char *slash = strrchr(s, '/');
		const Char *base = slash + 1;
		// house-colour textures start with "ZHC" (the asset manager takes the team colour from it), then the tag
		if (strncmp(base, tagLower.str(), tagLower.getLength()) != 0
			&& !(strncmp(base, "zhc", 3) == 0 && strncmp(base + 3, tagLower.str(), tagLower.getLength()) == 0))
		{
			problem = "file name does not start with the tag (or ZHC and the tag)";
			return ENTRY_BAD;
		}
		return ENTRY_ASSET;
	}
	problem = "not allowed in a package (only Art/, Data/Audio/ and Army/ files)";
	return ENTRY_BAD;
}

Bool isAssetPath(const AsciiString &lowerPath)
{
	return strncmp(lowerPath.str(), "art/", 4) == 0 || strncmp(lowerPath.str(), "data/audio/", 11) == 0;
}

// reads a text entry; returns a buffer (delete[]) with a terminating zero
Char *readEntryChecked(const ZipArchiveFile *zip, const AsciiString &lowerName, UnsignedInt maxSize, Int &size, AsciiString &error)
{
	const ZipArchiveFile::Entry *e = zip->findEntry(lowerName);
	if (e == nullptr)
	{
		error.format("%s is missing", lowerName.str());
		return nullptr;
	}
	if (e->m_size > maxSize)
	{
		error.format("%s is too large", e->m_originalName.str());
		return nullptr;
	}
	size = (Int)e->m_size;
	return zip->readEntry(*e, error);
}

AsciiString jsonEscape(const AsciiString &s)
{
	AsciiString out;
	for (Int i = 0; i < s.getLength(); ++i)
	{
		unsigned char c = (unsigned char)s.getCharAt(i);
		if (c == '"') out.concat("\\\"");
		else if (c == '\\') out.concat("\\\\");
		else if (c == '\n') out.concat("\\n");
		else if (c == '\r') out.concat("\\r");
		else if (c == '\t') out.concat("\\t");
		else if (c < 0x20)
		{
			Char buf[8];
			sprintf(buf, "\\u%04x", c);
			out.concat(buf);
		}
		else
		{
			Char one[2] = { (Char)c, 0 };
			out.concat(one);
		}
	}
	return out;
}

} // namespace


// =============================================================================================
// ArmyPackages
// =============================================================================================
ArmyPackages::ArmyPackages() : m_current(nullptr)
{
}

ArmyPackages::~ArmyPackages()
{
}

void ArmyPackages::load(Xfer *pXfer)
{
	if (TheArmyPackages != nullptr)
		return;
	TheArmyPackages = NEW ArmyPackages;
	TheArmyPackages->loadAll(pXfer);
}

void ArmyPackages::shutdown()
{
	delete TheArmyPackages;
	TheArmyPackages = nullptr;
}

AsciiString ArmyPackages::detectRuleset() const
{
	// The data decides: the starter content defines the faction "FactionIronwood" and has no retail factions.
	if (ThePlayerTemplateStore != nullptr)
	{
		const Bool retail = ThePlayerTemplateStore->findPlayerTemplate(TheNameKeyGenerator->nameToKey("FactionAmerica")) != nullptr;
		const Bool starter = ThePlayerTemplateStore->findPlayerTemplate(TheNameKeyGenerator->nameToKey("FactionIronwood")) != nullptr;
		if (starter && !retail)
			return "starter";
	}
	return "zerohour";
}

const ArmyFaction *ArmyPackages::findFactionByTemplate(const AsciiString &playerTemplateName) const
{
	for (size_t p = 0; p < m_packages.size(); ++p)
	{
		if (!m_packages[p].m_loaded)
			continue;
		for (size_t f = 0; f < m_packages[p].m_factions.size(); ++f)
		{
			if (m_packages[p].m_factions[f].m_playerTemplate.compareNoCase(playerTemplateName) == 0)
				return &m_packages[p].m_factions[f];
		}
	}
	return nullptr;
}

Bool ArmyPackages::canBePlayedByAI(const PlayerTemplate *pt) const
{
	if (pt == nullptr)
		return TRUE;
	const ArmyFaction *f = findFactionByTemplate(pt->getName());
	return f == nullptr || f->m_ai;
}

Bool ArmyPackages::hasHumanOnlyFactions() const
{
	for (size_t p = 0; p < m_packages.size(); ++p)
	{
		if (!m_packages[p].m_loaded)
			continue;
		for (size_t f = 0; f < m_packages[p].m_factions.size(); ++f)
		{
			if (!m_packages[p].m_factions[f].m_ai)
				return TRUE;
		}
	}
	return FALSE;
}

void ArmyPackages::skip(ArmyPackage &pkg, const AsciiString &reason)
{
	pkg.m_loaded = FALSE;
	pkg.m_reason = reason;
	if (pkg.m_zip != nullptr)
	{
		if (pkg.m_mounted)
			TheArchiveFileSystem->unmountZipArchive(pkg.m_zip);
		else
			delete pkg.m_zip;
		pkg.m_zip = nullptr;
		pkg.m_mounted = FALSE;
	}
	armyLog("ZHARMY: %s skipped: %s", pkg.m_id.str(), reason.str());
}

void ArmyPackages::fail(ArmyPackage &pkg, const AsciiString &reason)
{
	armyLog("ZHARMY: %s failed: %s", pkg.m_id.str(), reason.str());
	pkg.m_reason = reason;
	writeReport(pkg.m_id.str(), &reason);

	AsciiString message;
	message.format("The army package '%s' could not be loaded completely and the game cannot start with it. %s. "
		"Remove the package from the list of armies and start again.", pkg.m_id.str(), reason.str());
	RELEASE_CRASH((message.str()));
}

// ---------------------------------------------------------------------------------------------
// the manifest
// ---------------------------------------------------------------------------------------------
void ArmyPackages::readManifest(ArmyPackage &pkg)
{
	AsciiString error;
	Int size = 0;
	Char *text = readEntryChecked(pkg.m_zip, "manifest.json", ARMY_MAX_MANIFEST_SIZE, size, error);
	if (text == nullptr)
	{
		skip(pkg, AsciiString("manifest.json: ") + error);
		return;
	}

	MiniJson::Value root;
	Bool parsed = MiniJson::parse(text, size, root, error);
	delete[] text;
	if (!parsed)
	{
		skip(pkg, AsciiString("manifest.json: ") + error);
		return;
	}
	if (!root.isObject())
	{
		skip(pkg, "manifest.json: the document must be an object");
		return;
	}

	// format
	const MiniJson::Value *format = root.get("format");
	if (format == nullptr || !format->isNumber())
	{
		skip(pkg, "manifest.json: 'format' is missing");
		return;
	}
	if (format->m_number != 1.0)
	{
		AsciiString r;
		if (format->m_number > 1.0)
			r.format("package format %g is newer than this game understands (1)", format->m_number);
		else
			r.format("package format %g is not supported", format->m_number);
		skip(pkg, r);
		return;
	}

	// id
	const MiniJson::Value *id = root.get("id");
	if (id == nullptr || !id->isString() || !isValidId(id->m_string))
	{
		skip(pkg, "manifest.json: 'id' is missing or not valid (a-z, 0-9, '.', '-', 3 to 64 characters)");
		return;
	}
	pkg.m_id = id->m_string;

	// tag
	const MiniJson::Value *tag = root.get("tag");
	if (tag == nullptr || !tag->isString() || !isValidTag(tag->m_string))
	{
		skip(pkg, "manifest.json: 'tag' is missing or not valid (2 to 6 characters, A-Z and 0-9, starting with a letter)");
		return;
	}
	pkg.m_tag = tag->m_string;

	// optional text fields
	const MiniJson::Value *v;
	if ((v = root.get("name")) != nullptr && v->isString()) pkg.m_name = v->m_string;
	if ((v = root.get("version")) != nullptr && v->isString()) pkg.m_version = v->m_string;
	if ((v = root.get("description")) != nullptr && v->isString()) pkg.m_description = v->m_string;
	if (pkg.m_name.isEmpty())
		pkg.m_name = pkg.m_id;

	// requires
	const MiniJson::Value *req = root.get("requires");
	if (req == nullptr || !req->isArray())
	{
		skip(pkg, "manifest.json: 'requires' is missing (use [] for a package that works with any game data)");
		return;
	}
	for (size_t i = 0; i < req->m_items.size(); ++i)
	{
		if (!req->m_items[i].isString())
		{
			skip(pkg, "manifest.json: 'requires' must list names of rulesets");
			return;
		}
		pkg.m_requires.push_back(req->m_items[i].m_string);
	}

	// content hash
	const MiniJson::Value *hash = root.get("contentHash");
	if (hash == nullptr || !hash->isString() || strncmp(hash->m_string.str(), "sha256:", 7) != 0 || hash->m_string.getLength() != 7 + 64)
	{
		skip(pkg, "manifest.json: 'contentHash' is missing or not 'sha256:' and 64 hex digits");
		return;
	}
	pkg.m_contentHash = hash->m_string;
	pkg.m_contentHash.toLower();
	for (Int i = 7; i < pkg.m_contentHash.getLength(); ++i)
	{
		Char c = pkg.m_contentHash.getCharAt(i);
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
		{
			skip(pkg, "manifest.json: 'contentHash' is not 64 hex digits");
			return;
		}
	}

	// factions
	const MiniJson::Value *factions = root.get("factions");
	if (factions == nullptr || !factions->isArray() || factions->m_items.empty())
	{
		skip(pkg, "manifest.json: 'factions' is missing or empty");
		return;
	}
	if ((Int)factions->m_items.size() > ARMY_MAX_FACTIONS_PER_PACKAGE)
	{
		AsciiString r;
		r.format("manifest.json: more than %d factions", ARMY_MAX_FACTIONS_PER_PACKAGE);
		skip(pkg, r);
		return;
	}
	AsciiString prefix;
	prefix.format("%s_", pkg.m_tag.str());
	for (size_t i = 0; i < factions->m_items.size(); ++i)
	{
		const MiniJson::Value &f = factions->m_items[i];
		AsciiString where;
		where.format("manifest.json: faction %d", (Int)i + 1);
		if (!f.isObject())
		{
			skip(pkg, where + " is not an object");
			return;
		}
		ArmyFaction faction;
		const MiniJson::Value *pt = f.get("playerTemplate");
		const MiniJson::Value *side = f.get("side");
		const MiniJson::Value *dn = f.get("displayName");
		const MiniJson::Value *ai = f.get("ai");
		if (pt == nullptr || !pt->isString() || !isValidDefinitionName(pt->m_string))
		{
			skip(pkg, where + ": 'playerTemplate' is missing or not a name");
			return;
		}
		if (side == nullptr || !side->isString() || !isValidDefinitionName(side->m_string))
		{
			skip(pkg, where + ": 'side' is missing or not a name");
			return;
		}
		if (!startsWith(pt->m_string, prefix) || pt->m_string.getLength() == prefix.getLength())
		{
			skip(pkg, where + ": playerTemplate '" + pt->m_string + "' does not start with " + prefix);
			return;
		}
		if (!startsWith(side->m_string, prefix) || side->m_string.getLength() == prefix.getLength())
		{
			skip(pkg, where + ": side '" + side->m_string + "' does not start with " + prefix);
			return;
		}
		if (dn == nullptr || !dn->isString() || dn->m_string.isEmpty() || dn->m_string.getLength() > 64)
		{
			skip(pkg, where + ": 'displayName' is missing (1 to 64 characters)");
			return;
		}
		if (ai != nullptr && !ai->isBool())
		{
			skip(pkg, where + ": 'ai' must be true or false");
			return;
		}
		faction.m_playerTemplate = pt->m_string;
		faction.m_side = side->m_string;
		faction.m_displayName = dn->m_string;
		faction.m_ai = (ai != nullptr && ai->m_bool);
		for (size_t j = 0; j < pkg.m_factions.size(); ++j)
		{
			if (pkg.m_factions[j].m_playerTemplate == faction.m_playerTemplate || pkg.m_factions[j].m_side == faction.m_side)
			{
				skip(pkg, where + ": playerTemplate and side must be different for every faction");
				return;
			}
		}
		pkg.m_factions.push_back(faction);
	}
}

// ---------------------------------------------------------------------------------------------
// loading all packages
// ---------------------------------------------------------------------------------------------
namespace
{
bool packageLess(const ArmyPackage &a, const ArmyPackage &b)
{
	int c = strcmp(a.m_id.str(), b.m_id.str());
	if (c != 0)
		return c < 0;
	return strcmp(a.m_path.str(), b.m_path.str()) < 0;
}
}

void ArmyPackages::loadAll(Xfer *pXfer)
{
	m_ruleset = detectRuleset();
	const std::vector<AsciiString> &paths = TheGlobalData->m_armyPackages;

	if (!paths.empty())
		armyLog("ZHARMY: ruleset %s, %d package file(s) named", m_ruleset.str(), (Int)paths.size());

	// 1. open every package and read its manifest
	for (size_t i = 0; i < paths.size(); ++i)
	{
		bool duplicate = false;
		for (size_t j = 0; j < m_packages.size(); ++j)
		{
			if (m_packages[j].m_path == paths[i])
				duplicate = true;
		}
		if (duplicate)
			continue;

		ArmyPackage pkg;
		pkg.m_path = paths[i];
		pkg.m_id = lowerCase(fileBaseName(paths[i]));

		if ((Int)m_packages.size() >= ARMY_MAX_PACKAGES)
		{
			m_packages.push_back(pkg);
			skip(m_packages.back(), "too many packages");
			continue;
		}

		AsciiString error;
		pkg.m_zip = TheArchiveFileSystem->openZipArchive(paths[i].str(), error);
		m_packages.push_back(pkg);
		ArmyPackage &stored = m_packages.back();
		if (stored.m_zip == nullptr)
		{
			skip(stored, error);
			continue;
		}
		readManifest(stored);
	}

	// 2. the load order is the order of the ids, so the numbering of the new factions does not depend on the command line
	std::stable_sort(m_packages.begin(), m_packages.end(), packageLess);

	// 3. check and load them one by one
	for (size_t i = 0; i < m_packages.size(); ++i)
	{
		ArmyPackage &pkg = m_packages[i];
		if (pkg.m_zip == nullptr)
			continue;   // skipped already

		// id and tag must be unique among the packages that are loaded
		bool clash = false;
		for (size_t j = 0; j < i && !clash; ++j)
		{
			const ArmyPackage &other = m_packages[j];
			if (!other.m_loaded)
				continue;
			if (other.m_id == pkg.m_id)
			{
				skip(pkg, AsciiString("another package with the id '") + pkg.m_id + "' is already loaded");
				clash = true;
			}
			else if (other.m_tag == pkg.m_tag)
			{
				skip(pkg, AsciiString("the tag ") + pkg.m_tag + " is already used by the package '" + other.m_id + "'");
				clash = true;
			}
		}
		if (clash)
			continue;

		loadPackage(pkg, pXfer);
	}

	// 4. things the base data post-processes after its INI files
	Bool any = FALSE;
	for (size_t i = 0; i < m_packages.size(); ++i)
		any = any || m_packages[i].m_loaded;
	if (any && TheControlBar != nullptr)
		TheControlBar->postProcessCommands();

	writeReport();
}

// ---------------------------------------------------------------------------------------------
// one package
// ---------------------------------------------------------------------------------------------
void ArmyPackages::loadPackage(ArmyPackage &pkg, Xfer *pXfer)
{
	ZipArchiveFile *zip = pkg.m_zip;
	AsciiString error;

	// ---- requires
	if (!pkg.m_requires.empty())
	{
		if (!containsString(pkg.m_requires, m_ruleset))
		{
			AsciiString list;
			for (size_t i = 0; i < pkg.m_requires.size(); ++i)
			{
				if (i > 0) list.concat(", ");
				list.concat(pkg.m_requires[i]);
			}
			skip(pkg, AsciiString("needs the game data '") + list + "', this game runs on '" + m_ruleset + "'");
			return;
		}
	}

	// ---- layout of the archive
	const std::vector<ZipArchiveFile::Entry> &entries = zip->getEntries();
	const AsciiString tagLower = lowerCase(pkg.m_tag);
	std::vector<AsciiString> iniEntries;     // lower case names
	Bool hasStrings = FALSE;
	for (size_t i = 0; i < entries.size(); ++i)
	{
		AsciiString problem;
		EntryKind kind = classifyEntry(entries[i].m_name, tagLower, problem);
		switch (kind)
		{
			case ENTRY_INI: iniEntries.push_back(entries[i].m_name); break;
			case ENTRY_STRINGS: hasStrings = TRUE; break;
			case ENTRY_SCRIPTS: pkg.m_hasScripts = TRUE; break;
			case ENTRY_BAD:
				skip(pkg, AsciiString("entry '") + entries[i].m_originalName + "': " + problem);
				return;
			default: break;
		}
	}

	// ---- content hash (detects a package that was edited after the converter wrote it)
	{
		Sha256 hashLower, hashStored;
		Bool needStored = FALSE;
		for (size_t i = 0; i < entries.size(); ++i)
		{
			if (entries[i].m_name != entries[i].m_originalName)
				needStored = TRUE;
		}
		for (size_t i = 0; i < entries.size(); ++i)
		{
			const ZipArchiveFile::Entry &e = entries[i];
			if (strcmp(e.m_name.str(), "manifest.json") == 0)
				continue;
			Char *data = zip->readEntry(e, error);
			if (data == nullptr)
			{
				skip(pkg, AsciiString("corrupt package: ") + error);
				return;
			}
			unsigned char zero = 0;
			unsigned char size8[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
			size8[0] = (unsigned char)(e.m_size); size8[1] = (unsigned char)(e.m_size >> 8);
			size8[2] = (unsigned char)(e.m_size >> 16); size8[3] = (unsigned char)(e.m_size >> 24);
			hashLower.update(e.m_name.str(), e.m_name.getLength());
			hashLower.update(&zero, 1);
			hashLower.update(size8, 8);
			hashLower.update(data, e.m_size);
			if (needStored)
			{
				hashStored.update(e.m_originalName.str(), e.m_originalName.getLength());
				hashStored.update(&zero, 1);
				hashStored.update(size8, 8);
				hashStored.update(data, e.m_size);
			}
			delete[] data;
		}
		AsciiString a = AsciiString("sha256:") + hashLower.finish();
		AsciiString b = needStored ? (AsciiString("sha256:") + hashStored.finish()) : a;
		if (a != pkg.m_contentHash && b != pkg.m_contentHash)
		{
			skip(pkg, "the content hash does not match (the package was changed or is damaged)");
			return;
		}
	}

	// ---- the definition files: read, scan, check against the game data
	PackageScan scan;
	std::vector<AsciiString> sides;
	for (size_t i = 0; i < pkg.m_factions.size(); ++i)
		sides.push_back(pkg.m_factions[i].m_side);

	std::vector<Char *> iniBuffers(s_iniFileCount, (Char *)nullptr);
	std::vector<Int> iniSizes(s_iniFileCount, 0);
	for (Int r = 0; r < s_iniFileCount; ++r)
	{
		AsciiString lowerName;
		lowerName.format("army/ini/%s", s_iniFiles[r].m_file);
		if (!containsString(iniEntries, lowerName))
			continue;
		Char *text = readEntryChecked(zip, lowerName, ARMY_MAX_TEXT_SIZE, iniSizes[r], error);
		if (text == nullptr)
		{
			for (Int k = 0; k < s_iniFileCount; ++k) delete[] iniBuffers[k];
			skip(pkg, error);
			return;
		}
		iniBuffers[r] = text;

		Bool ok;
		AsciiString fileName = s_iniFiles[r].m_file;
		if (strcmp(s_iniFiles[r].m_blockType, "AIData") == 0)
		{
			AIDataScanner scanner(sides, scan, error);
			forEachLine(text, iniSizes[r], scanner);
			ok = error.isEmpty() && scanner.m_state == AIDataScanner::OUTSIDE;
			if (error.isEmpty() && !ok)
				error = "Army/INI/aidata.ini: the AIData block is not closed with End";
		}
		else
		{
			IniScanner scanner(pkg.m_tag, fileName, s_iniFiles[r].m_blockType, sides, scan, error);
			forEachLine(text, iniSizes[r], scanner);
			ok = error.isEmpty();
		}
		if (!ok)
		{
			for (Int k = 0; k < s_iniFileCount; ++k) delete[] iniBuffers[k];
			skip(pkg, error);
			return;
		}
	}

	struct Cleanup
	{
		std::vector<Char *> &m_buffers;
		explicit Cleanup(std::vector<Char *> &b) : m_buffers(b) {}
		void release() { for (size_t k = 0; k < m_buffers.size(); ++k) { delete[] m_buffers[k]; m_buffers[k] = nullptr; } }
	} cleanup(iniBuffers);

	// objects may only belong to the package's own sides
	for (size_t i = 0; i < scan.m_objectSides.size(); ++i)
	{
		if (!containsString(sides, scan.m_objectSides[i]))
		{
			cleanup.release();
			skip(pkg, AsciiString("an object uses the side '") + scan.m_objectSides[i] + "', which is not a side of this package");
			return;
		}
	}

	// every faction of the manifest needs its player template, and no other template may be defined
	for (size_t i = 0; i < pkg.m_factions.size(); ++i)
	{
		const ArmyFaction &f = pkg.m_factions[i];
		const TemplateScan *t = nullptr;
		for (size_t j = 0; j < scan.m_templates.size(); ++j)
		{
			if (scan.m_templates[j].m_name == f.m_playerTemplate)
				t = &scan.m_templates[j];
		}
		if (t == nullptr)
		{
			cleanup.release();
			skip(pkg, AsciiString("faction '") + f.m_playerTemplate + "' has no PlayerTemplate in Army/INI/PlayerTemplate.ini");
			return;
		}
		if (t->m_side != f.m_side)
		{
			cleanup.release();
			skip(pkg, AsciiString("PlayerTemplate '") + f.m_playerTemplate + "' has Side '" + t->m_side + "' but the manifest says '" + f.m_side + "'");
			return;
		}
		if (t->m_startingBuilding.isEmpty() || !t->m_playable)
		{
			cleanup.release();
			skip(pkg, AsciiString("PlayerTemplate '") + f.m_playerTemplate + "' needs PlayableSide = Yes and a StartingBuilding");
			return;
		}
		{
			// The starting objects must exist (in the package or the game data): a faction without them
			// cannot start a match, and the engine does not survive the attempt.
			std::vector<AsciiString> starting = t->m_startingUnits;
			starting.insert(starting.begin(), t->m_startingBuilding);
			for (size_t k = 0; k < starting.size(); ++k)
			{
				const AsciiString &objectName = starting[k];
				if (objectName.isEmpty() || stricmp(objectName.str(), "None") == 0)
					continue;
				Bool found = definitionExists(AsciiString("Object"), objectName);
				for (size_t d = 0; !found && d < scan.m_defined.size(); ++d)
					found = scan.m_defined[d].m_type == "Object" && scan.m_defined[d].m_name == objectName;
				if (!found)
				{
					cleanup.release();
					skip(pkg, AsciiString("PlayerTemplate '") + f.m_playerTemplate + "' starts with object '" + objectName + "', which exists neither in the package nor in the game data");
					return;
				}
			}
		}
		if (f.m_ai && !containsString(scan.m_buildListSides, f.m_side))
		{
			cleanup.release();
			skip(pkg, AsciiString("faction '") + f.m_playerTemplate + "' is marked ai but AIData.ini has no SkirmishBuildList for '" + f.m_side + "'");
			return;
		}
	}
	for (size_t j = 0; j < scan.m_templates.size(); ++j)
	{
		bool listed = false;
		for (size_t i = 0; i < pkg.m_factions.size(); ++i)
			listed = listed || pkg.m_factions[i].m_playerTemplate == scan.m_templates[j].m_name;
		if (!listed)
		{
			cleanup.release();
			skip(pkg, AsciiString("PlayerTemplate '") + scan.m_templates[j].m_name + "' is not listed in the manifest");
			return;
		}
	}

	// AIData: the sides must not exist yet
	if (TheAI != nullptr && TheAI->getAiData() != nullptr)
	{
		for (const AISideInfo *info = TheAI->getAiData()->m_sideInfo; info != nullptr; info = info->m_next)
		{
			if (containsString(scan.m_sideInfoSides, info->m_side))
			{
				cleanup.release();
				skip(pkg, AsciiString("AIData already has a SideInfo for '") + info->m_side + "'");
				return;
			}
		}
		for (const AISideBuildList *list = TheAI->getAiData()->m_sideBuildLists; list != nullptr; list = list->m_next)
		{
			if (containsString(scan.m_buildListSides, list->m_side))
			{
				cleanup.release();
				skip(pkg, AsciiString("AIData already has a SkirmishBuildList for '") + list->m_side + "'");
				return;
			}
		}
	}

	// engine limits
	if (TheUpgradeCenter != nullptr && TheUpgradeCenter->getUpgradeCount() + scan.m_upgradeCount > UPGRADE_MAX_COUNT)
	{
		cleanup.release();
		AsciiString r;
		r.format("too many upgrades: %d defined, the package adds %d, the limit is %d",
			TheUpgradeCenter->getUpgradeCount(), scan.m_upgradeCount, (Int)UPGRADE_MAX_COUNT);
		skip(pkg, r);
		return;
	}
	{
		Int aiFactions = 0;
		for (size_t p = 0; p < m_packages.size(); ++p)
		{
			if (!m_packages[p].m_loaded)
				continue;
			for (size_t f = 0; f < m_packages[p].m_factions.size(); ++f)
				aiFactions += m_packages[p].m_factions[f].m_ai ? 1 : 0;
		}
		for (size_t f = 0; f < pkg.m_factions.size(); ++f)
			aiFactions += pkg.m_factions[f].m_ai ? 1 : 0;
		if (aiFactions > SidesList::MAX_ARMY_SKIRMISH_SIDES)
		{
			cleanup.release();
			AsciiString r;
			r.format("too many computer-playable army factions (limit %d)", (Int)SidesList::MAX_ARMY_SKIRMISH_SIDES);
			skip(pkg, r);
			return;
		}
	}

	// ---- skirmish scripts: readable, and only for the package's own AI sides
	if (pkg.m_hasScripts)
	{
		std::vector<AsciiString> allowed;
		for (size_t i = 0; i < pkg.m_factions.size(); ++i)
			allowed.push_back(AsciiString("Skirmish") + pkg.m_factions[i].m_side);

		Int size = 0;
		Char *scb = readEntryChecked(zip, "army/scripts/skirmish.scb", ARMY_MAX_TEXT_SIZE, size, error);
		if (scb == nullptr)
		{
			cleanup.release();
			skip(pkg, error);
			return;
		}
		MemoryChunkStream stream(scb, size);
		if (TheSidesList == nullptr || !TheSidesList->mergeArmySkirmishScripts(stream, allowed, TRUE, error))
		{
			cleanup.release();
			skip(pkg, AsciiString("Army/Scripts/Skirmish.scb: ") + error);
			return;
		}
	}

	// ---- mount the assets (refused when one of the files already exists)
	if (!TheArchiveFileSystem->mountZipArchive(zip, isAssetPath, error))
	{
		cleanup.release();
		skip(pkg, error);
		return;
	}
	pkg.m_mounted = TRUE;

	// ---- strings (all or nothing, nothing has been added to the game yet)
	if (hasStrings)
	{
		File *strFile = zip->openFile("army/strings.str", File::READ | File::TEXT);
		if (strFile == nullptr || TheGameText == nullptr || !TheGameText->addExtraStrings(strFile, pkg.m_tag.str(), error))
		{
			if (error.isEmpty())
				error = "cannot read Army/Strings.str";
			cleanup.release();
			skip(pkg, AsciiString("Army/Strings.str: ") + error);
			return;
		}
	}
	// the skirmish list asks for "SIDE:<side>"; the manifest's display name stands in when the package has no such string
	for (size_t i = 0; i < pkg.m_factions.size() && TheGameText != nullptr; ++i)
	{
		AsciiString label = AsciiString("SIDE:") + pkg.m_factions[i].m_side;
		if (!TheGameText->doesStringExist(label.str()))
		{
			UnicodeString text = utf8ToUnicode(pkg.m_factions[i].m_displayName);
			TheGameText->addExtraString(label.str(), text.str());
		}
	}

	// ---- from here on the definitions go into the game. A failure now cannot be undone.
	m_current = &pkg;
	INI::setBlockGuard(this);
	AsciiString failure;
	try
	{
		for (Int r = 0; r < s_iniFileCount; ++r)
		{
			if (iniBuffers[r] == nullptr)
				continue;
			m_currentFile = s_iniFiles[r].m_file;
			m_currentBlockType = s_iniFiles[r].m_blockType;

			AsciiString displayName;
			displayName.format("%s!Army/INI/%s", fileBaseName(pkg.m_path).str(), s_iniFiles[r].m_file);
			Char *buffer = iniBuffers[r];
			iniBuffers[r] = nullptr;    // the INI reader owns it now
			INI ini;
			ini.loadFromBuffer(displayName, buffer, iniSizes[r], INI_LOAD_OVERWRITE, pXfer);
		}
	}
	catch (const INIException &e)
	{
		failure = e.mFailureMessage ? e.mFailureMessage : "error in a definition file";
	}
	catch (...)
	{
		failure = "error in a definition file";
	}
	INI::setBlockGuard(nullptr);
	m_current = nullptr;
	m_currentFile.clear();
	m_currentBlockType.clear();
	cleanup.release();
	if (!failure.isEmpty())
	{
		// strip the line break the reader's messages end with
		while (failure.getLength() > 0 && (failure.back() == '\n' || failure.back() == '\r'))
			failure.removeLastChar();
		fail(pkg, failure);
		return;
	}

	// ---- the player templates must be there with the right side
	for (size_t i = 0; i < pkg.m_factions.size(); ++i)
	{
		const PlayerTemplate *pt = ThePlayerTemplateStore->findPlayerTemplate(TheNameKeyGenerator->nameToKey(pkg.m_factions[i].m_playerTemplate));
		if (pt == nullptr || pt->getSide() != pkg.m_factions[i].m_side || pt->getStartingBuilding().isEmpty())
		{
			fail(pkg, AsciiString("PlayerTemplate '") + pkg.m_factions[i].m_playerTemplate + "' is not what the manifest says");
			return;
		}
	}

	pkg.m_loaded = TRUE;
	pkg.m_reason.clear();
	AsciiString names;
	for (size_t i = 0; i < pkg.m_factions.size(); ++i)
	{
		if (i > 0) names.concat(", ");
		names.concat(pkg.m_factions[i].m_displayName);
	}
	armyLog("ZHARMY: %s loaded: %s (%d definitions, factions: %s)", pkg.m_id.str(), pkg.m_name.str(), (Int)scan.m_defined.size(), names.str());
}

// ---------------------------------------------------------------------------------------------
// the guard: called by the INI reader for every top level block of a package file
// ---------------------------------------------------------------------------------------------
Bool ArmyPackages::checkBlock(const char *blockType, const AsciiString &name, AsciiString &error)
{
	if (m_current == nullptr)
		return TRUE;

	if (!blockAllowedInFile(blockType, m_currentBlockType))
	{
		error.format("%s blocks are not allowed in %s (only %s)", blockType, m_currentFile.str(), m_currentBlockType.str());
		return FALSE;
	}
	if (strcmp(blockType, "AIData") == 0)
		return TRUE;   // its content was checked line by line before loading

	AsciiString prefix;
	prefix.format("%s_", m_current->m_tag.str());
	if (!startsWith(name, prefix) || name.getLength() == prefix.getLength())
	{
		error.format("%s '%s' does not start with %s", blockType, name.str(), prefix.str());
		return FALSE;
	}
	if (definitionExists(AsciiString(definitionTypeOf(blockType)), name))
	{
		error.format("%s '%s' already exists", blockType, name.str());
		return FALSE;
	}
	return TRUE;
}

// ---------------------------------------------------------------------------------------------
// skirmish: sides and scripts of the AI factions, added for every match
// ---------------------------------------------------------------------------------------------
void ArmyPackages::prepareSkirmishSides(SidesList *sides) const
{
	if (sides == nullptr)
		return;

	for (size_t p = 0; p < m_packages.size(); ++p)
	{
		const ArmyPackage &pkg = m_packages[p];
		if (!pkg.m_loaded)
			continue;

		std::vector<AsciiString> names;
		for (size_t f = 0; f < pkg.m_factions.size(); ++f)
		{
			const ArmyFaction &faction = pkg.m_factions[f];
			if (!faction.m_ai)
				continue;

			AsciiString playerName = AsciiString("Skirmish") + faction.m_side;
			Dict d;
			d.setAsciiString(TheKey_playerName, playerName);
			d.setAsciiString(TheKey_playerFaction, faction.m_playerTemplate);
			if (!sides->addArmySkirmishSide(&d))
			{
				armyLog("ZHARMY: %s: no room for the skirmish side of %s", pkg.m_id.str(), faction.m_side.str());
				continue;
			}
			names.push_back(playerName);
		}
		if (names.empty())
			continue;

		if (pkg.m_hasScripts && pkg.m_zip != nullptr)
		{
			AsciiString error;
			Int size = 0;
			Char *scb = readEntryChecked(pkg.m_zip, "army/scripts/skirmish.scb", ARMY_MAX_TEXT_SIZE, size, error);
			if (scb != nullptr)
			{
				MemoryChunkStream stream(scb, size);
				if (!sides->mergeArmySkirmishScripts(stream, names, FALSE, error))
					armyLog("ZHARMY: %s: skirmish scripts not used: %s", pkg.m_id.str(), error.str());
				else
					armyLog("ZHARMY: %s: skirmish scripts added for %d side(s)", pkg.m_id.str(), (Int)names.size());
			}
			else
			{
				armyLog("ZHARMY: %s: skirmish scripts not used: %s", pkg.m_id.str(), error.str());
			}
		}

		// every skirmish player needs its default team, as the map's own skirmish players have
		for (size_t n = 0; n < names.size(); ++n)
		{
			AsciiString teamName = AsciiString("team") + names[n];
			if (sides->findSkirmishTeamInfo(teamName) == nullptr)
			{
				Dict team;
				team.setAsciiString(TheKey_teamName, teamName);
				team.setAsciiString(TheKey_teamOwner, names[n]);
				team.setBool(TheKey_teamIsSingleton, true);
				sides->addSkirmishTeam(&team);
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------
// ArmyReport.json
// ---------------------------------------------------------------------------------------------
void ArmyPackages::writeReport(const Char *failedId, const AsciiString *failedReason) const
{
	AsciiString json;
	json.concat("{\n");
	json.concat(AsciiString("  \"ruleset\": \"") + jsonEscape(m_ruleset) + "\",\n");
	json.concat("  \"packages\": [");
	for (size_t i = 0; i < m_packages.size(); ++i)
	{
		const ArmyPackage &pkg = m_packages[i];
		const Bool failed = (failedId != nullptr && pkg.m_id == failedId);
		json.concat(i == 0 ? "\n" : ",\n");
		json.concat("    {\n");
		json.concat(AsciiString("      \"id\": \"") + jsonEscape(pkg.m_id) + "\",\n");
		json.concat(AsciiString("      \"path\": \"") + jsonEscape(pkg.m_path) + "\",\n");
		json.concat(AsciiString("      \"status\": \"") + (failed ? "failed" : (pkg.m_loaded ? "loaded" : "skipped")) + "\",\n");
		json.concat(AsciiString("      \"reason\": \"") + jsonEscape(failed && failedReason ? *failedReason : pkg.m_reason) + "\",\n");
		json.concat(AsciiString("      \"tag\": \"") + jsonEscape(pkg.m_tag) + "\",\n");
		json.concat(AsciiString("      \"name\": \"") + jsonEscape(pkg.m_name) + "\",\n");
		json.concat(AsciiString("      \"version\": \"") + jsonEscape(pkg.m_version) + "\",\n");
		json.concat("      \"factions\": [");
		for (size_t f = 0; f < pkg.m_factions.size(); ++f)
		{
			const ArmyFaction &faction = pkg.m_factions[f];
			json.concat(f == 0 ? "\n" : ",\n");
			json.concat(AsciiString("        { \"playerTemplate\": \"") + jsonEscape(faction.m_playerTemplate)
				+ "\", \"side\": \"" + jsonEscape(faction.m_side)
				+ "\", \"displayName\": \"" + jsonEscape(faction.m_displayName)
				+ "\", \"ai\": " + (faction.m_ai ? "true" : "false") + " }");
		}
		json.concat(pkg.m_factions.empty() ? "]\n" : "\n      ]\n");
		json.concat("    }");
	}
	json.concat(m_packages.empty() ? "]\n" : "\n  ]\n");
	json.concat("}\n");

#ifdef __EMSCRIPTEN__
	// the launcher reads it from the root of the browser's persistent user data, whatever the game's own user data folder is called
	AsciiString path = "/userdata/ArmyReport.json";
#else
	AsciiString path = TheGlobalData->getPath_UserData();
	path.concat("ArmyReport.json");
#endif
	File *file = TheLocalFileSystem->openFile(path.str(), File::WRITE | File::CREATE | File::TRUNCATE | File::BINARY);
	if (file == nullptr)
	{
		armyLog("ZHARMY: cannot write %s", path.str());
		return;
	}
	file->write(json.str(), json.getLength());
	file->close();
	armyLog("ZHARMY: report written to %s", path.str());
}
