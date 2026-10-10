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

// FILE: AIMatch.cpp //////////////////////////////////////////////////////////
//
// The AI-vs-AI test bench, see AIMatch.h for the command line.
//
// How a match runs (the same way the replay simulation runs, ReplaySimulation.cpp):
//   1. The command line is turned into a skirmish setup in TheSkirmishGameInfo: the map, one AI slot per
//      player, and the seed, which seeds the game logic random numbers with InitRandom() like the skirmish
//      menu does with the seed of its game.
//   2. A MSG_NEW_GAME goes straight into TheCommandList. There is no user interface, so no message stream
//      is pumped, no load screen shown and no frame paced: TheGameLogic->UPDATE() is simply called in a loop.
//   3. After every logic frame the match checks whether it is decided (TheVictoryConditions), and samples
//      the statistics. The mode ends on victory, at the frame limit, or on an error.
//   4. The statistics are printed on one line and optionally written to a file.
//
// The statistics come from three sources: the game's own bookkeeping (ScoreKeeper: what was built, lost and
// destroyed, by template; Money: income and spending), periodic scans of every player's objects (army value,
// factories that are idle, supply sources held) and the game logic CRC, taken at a fixed frame interval.
// Everything but the "perf" section is a function of the command line and the game data only.
//
///////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/AIMatch.h"
#include "Common/AIMatchShared.h"

#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/KindOf.h"
#include "Common/MessageStream.h"
#include "Common/MultiplayerSettings.h"
#include "Common/ObjectStatusTypes.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/RandomValue.h"
#include "Common/ScoreKeeper.h"
#include "Common/ThingTemplate.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/AI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/VictoryConditions.h"
#include "GameNetwork/GameInfo.h"

#include <stdarg.h>
#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>


namespace AIMatchShared
{

// ------------------------------------------------------------------------------------------------
// Small string helpers
// ------------------------------------------------------------------------------------------------
std::string lowered(const std::string &s)
{
	std::string r = s;
	for (size_t i = 0; i < r.size(); ++i)
	{
		char c = r[i];
		if (c >= 'A' && c <= 'Z')
			r[i] = (char)(c - 'A' + 'a');
	}
	return r;
}

// Lower case letters and digits only: "Ironwood Crossing", "ironwood_crossing" and "IronwoodCrossing" are one name.
std::string squashed(const std::string &s)
{
	std::string r;
	for (size_t i = 0; i < s.size(); ++i)
	{
		char c = s[i];
		if (c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
			r += c;
	}
	return r;
}

std::string trimmed(const std::string &s)
{
	size_t b = 0, e = s.size();
	while (b < e && (s[b] == ' ' || s[b] == '\t'))
		++b;
	while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t'))
		--e;
	return s.substr(b, e - b);
}

std::vector<std::string> split(const std::string &s, char sep)
{
	std::vector<std::string> parts;
	size_t start = 0;
	for (;;)
	{
		size_t at = s.find(sep, start);
		if (at == std::string::npos)
		{
			parts.push_back(s.substr(start));
			break;
		}
		parts.push_back(s.substr(start, at - start));
		start = at + 1;
	}
	return parts;
}

std::string format(const char *fmt, ...)
{
	char buffer[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	buffer[sizeof(buffer) - 1] = 0;
	return std::string(buffer);
}

// "maps\\foo bar\\foo bar.map" -> "foo bar"; also accepts "foo bar", "Maps/foo bar/foo bar.map" and "foo bar.map".
std::string mapLeafName(const std::string &path)
{
	std::string leaf = path;
	size_t slash = leaf.find_last_of("\\/");
	if (slash != std::string::npos)
		leaf = leaf.substr(slash + 1);
	if (leaf.size() > 4 && lowered(leaf.substr(leaf.size() - 4)) == ".map")
		leaf = leaf.substr(0, leaf.size() - 4);
	return leaf;
}

std::string normalizedMapPath(const std::string &path)
{
	std::string r = lowered(path);
	for (size_t i = 0; i < r.size(); ++i)
		if (r[i] == '/')
			r[i] = '\\';
	return r;
}

Bool parseUnsigned(const std::string &text, UnsignedInt &out)
{
	if (text.empty())
		return FALSE;
	UnsignedInt v = 0;
	for (size_t i = 0; i < text.size(); ++i)
	{
		if (text[i] < '0' || text[i] > '9')
			return FALSE;
		v = v * 10 + (UnsignedInt)(text[i] - '0');
	}
	out = v;
	return TRUE;
}

Bool parseSigned(const std::string &text, Int &out)
{
	if (text.empty())
		return FALSE;
	Bool negative = text[0] == '-';
	UnsignedInt v = 0;
	if (!parseUnsigned(negative ? text.substr(1) : text, v))
		return FALSE;
	out = negative ? -(Int)v : (Int)v;
	return TRUE;
}

} // namespace AIMatchShared


namespace
{

using namespace AIMatchShared;

const char *const kSchema = "zh-aibench-1";

// Defaults of the intervals, in logic frames (30 per second).
const UnsignedInt kDefaultCrcInterval = 300;			// 10 s
const UnsignedInt kDefaultSampleInterval = 300;		// 10 s
const UnsignedInt kDefaultIdleInterval = 15;			// 0.5 s
const UnsignedInt kDefaultProgressInterval = 1800;	// 1 min
const UnsignedInt kDefenceCheckInterval = 5;			// how often defeats are noticed
const UnsignedInt kDefaultMaxMinutes = 30;				// game time

// Safety nets against a match that never advances or never starts.
const UnsignedInt kMaxUpdatesToStart = 100;
const UnsignedInt kMaxUpdatesWithoutProgress = 20000;

Bool s_active = FALSE;
Bool s_record = FALSE;

// The variant tag of every slot, for AIMatch::getPlayerVariant().
std::string s_variantBySlot[MAX_SLOTS];

// ------------------------------------------------------------------------------------------------
// Configuration, from the command line
// ------------------------------------------------------------------------------------------------
struct PlayerSpec
{
	PlayerSpec() : state(SLOT_MED_AI), team(-1), startPos(-1), templateIndex(PLAYERTEMPLATE_RANDOM), idle(FALSE) {}

	SlotState state;
	std::string difficulty;		// as given, normalized: easy, normal, hard, expert or idle
	std::string side;				// as given
	Int team;							// -1: on its own
	Int startPos;					// 0 based, -1: random
	std::string variant;
	Int templateIndex;			// into ThePlayerTemplateStore, PLAYERTEMPLATE_RANDOM for "random"
	Bool idle;						// a computer player whose AI is removed when the match starts (a stand-in for a human who does nothing)
};

struct Config
{
	Config()
		: seed(0), maxFrames(0), crcInterval(kDefaultCrcInterval), sampleInterval(kDefaultSampleInterval),
		  idleInterval(kDefaultIdleInterval), progressInterval(kDefaultProgressInterval), startingCash(-1),
		  record(FALSE), engineLoop(FALSE)
	{}

	std::string map;				// as given
	std::string mapPath;			// as found in the map cache
	std::vector<PlayerSpec> players;
	Int seed;
	UnsignedInt maxFrames;
	UnsignedInt crcInterval;
	UnsignedInt sampleInterval;
	UnsignedInt idleInterval;
	UnsignedInt progressInterval;
	Int startingCash;				// -1: the game's default
	std::string statsPath;
	std::string label;
	std::string aiIniPath;			// aiini=: an AIData override file loaded before the match (empty: none)
	std::string aiIniHash;			// FNV-1a of its content, 8 hex digits
	Bool record;
	std::vector<std::pair<Int, UnsignedInt> > eliminations;	// (slot, frame): test hook, see the eliminate= option
	Bool engineLoop;				// run the whole engine update (client included) instead of the game logic alone
};

// players=hard:Ironwood,normal:random:2:1:experiment
Bool parsePlayers(const std::string &text, std::vector<PlayerSpec> &out, std::string &error)
{
	std::vector<std::string> entries = split(text, ',');
	for (size_t e = 0; e < entries.size(); ++e)
	{
		std::vector<std::string> f = split(trimmed(entries[e]), ':');
		if (f.size() < 2 || f.size() > 5)
		{
			error = "player '" + entries[e] + "' is not difficulty:side[:team[:start[:variant]]]";
			return FALSE;
		}
		PlayerSpec spec;
		std::string d = lowered(trimmed(f[0]));
		if (d == "easy")
			spec.state = SLOT_EASY_AI, spec.difficulty = "easy";
		else if (d == "normal" || d == "medium" || d == "med")
			spec.state = SLOT_MED_AI, spec.difficulty = "normal";
		else if (d == "hard" || d == "brutal")
			spec.state = SLOT_BRUTAL_AI, spec.difficulty = "hard";
		else if (d == "expert")
			spec.state = SLOT_EXPERT_AI, spec.difficulty = "expert";	// the level above Hard, see GameInfo.h
		else if (d == "idle")
			spec.state = SLOT_EASY_AI, spec.difficulty = "idle", spec.idle = TRUE;	// its AI is removed at the start (as AssistMatch's idle kind)
		else
		{
			error = "unknown difficulty '" + f[0] + "' (easy, normal, hard, expert or idle)";
			return FALSE;
		}
		spec.side = trimmed(f[1]);
		if (spec.side.empty())
		{
			error = "player '" + entries[e] + "' has no side";
			return FALSE;
		}
		if (f.size() > 2 && !trimmed(f[2]).empty())
		{
			Int team;
			if (!parseSigned(trimmed(f[2]), team) || team < -1)
			{
				error = "bad team '" + f[2] + "' (a number, or -1 for none)";
				return FALSE;
			}
			spec.team = team;
		}
		if (f.size() > 3 && !trimmed(f[3]).empty())
		{
			Int start;
			if (!parseSigned(trimmed(f[3]), start) || start < 0)
			{
				error = "bad start position '" + f[3] + "' (1 based, or 0 for random)";
				return FALSE;
			}
			spec.startPos = start - 1;	// the command line counts from 1, the game from 0; 0 -> -1: random
		}
		if (f.size() > 4 && !trimmed(f[4]).empty())
			spec.variant = trimmed(f[4]);
		out.push_back(spec);
	}
	return TRUE;
}

// Reads the options of the match from the command line.
Bool parseConfig(Config &cfg, std::string &error)
{
	Bool timeoutGiven = FALSE;
	Bool playersGiven = FALSE;
	Bool seedGiven = FALSE;
	UnsignedInt minutes = kDefaultMaxMinutes;

	for (int i = 1; i < __argc; ++i)
	{
		const char *arg = __argv[i];
		const char *eq = strchr(arg, '=');
		if (arg[0] == '-' || eq == nullptr)
			continue;
		std::string key = lowered(std::string(arg, eq - arg));
		std::string value = eq + 1;
		UnsignedInt u = 0;

		if (key == "map")
			cfg.map = value;
		else if (key == "players")
		{
			if (!parsePlayers(value, cfg.players, error))
				return FALSE;
			playersGiven = TRUE;
		}
		else if (key == "seed")
		{
			if (!parseSigned(value, cfg.seed))
			{
				error = "bad seed '" + value + "'";
				return FALSE;
			}
			seedGiven = TRUE;
		}
		else if (key == "timeout")
		{
			if (!parseUnsigned(value, minutes) || minutes == 0)
			{
				error = "bad timeout '" + value + "' (minutes of game time)";
				return FALSE;
			}
			timeoutGiven = TRUE;
		}
		else if (key == "maxframes")
		{
			if (!parseUnsigned(value, cfg.maxFrames) || cfg.maxFrames == 0)
			{
				error = "bad maxframes '" + value + "'";
				return FALSE;
			}
		}
		else if (key == "stats")
			cfg.statsPath = value;
		else if (key == "label")
			cfg.label = value;
		else if (key == "aiini")
		{
			if (value.empty())
			{
				error = "aiini=<file> needs a file";
				return FALSE;
			}
			cfg.aiIniPath = value;
		}
		else if (key == "eliminate")
		{
			// Test hook: eliminate=<slot>@<frame> kills everything the player has at that frame, so that the end of a
			// match (defeat, victory, ranks) can be tested even where the AI does not fight.
			size_t at = value.find('@');
			Int slot = 0;
			UnsignedInt frame = 0;
			if (at == std::string::npos || !parseSigned(value.substr(0, at), slot) || slot < 0 || !parseUnsigned(value.substr(at + 1), frame))
			{
				error = "bad eliminate '" + value + "' (<slot>@<frame>, slots count from 0)";
				return FALSE;
			}
			cfg.eliminations.push_back(std::make_pair(slot, frame));
		}
		else if (key == "loop")
		{
			if (lowered(value) != "logic" && lowered(value) != "engine")
			{
				error = "bad loop '" + value + "' (logic or engine)";
				return FALSE;
			}
			cfg.engineLoop = lowered(value) == "engine";
		}
		else if (key == "record")
			cfg.record = (value != "0" && lowered(value) != "no" && lowered(value) != "false");
		else if (key == "cash")
		{
			if (!parseSigned(value, cfg.startingCash) || cfg.startingCash < 0)
			{
				error = "bad cash '" + value + "'";
				return FALSE;
			}
		}
		else if (key == "crcinterval" || key == "sample" || key == "idleinterval" || key == "progress")
		{
			if (!parseUnsigned(value, u) || (u == 0 && key != "progress"))
			{
				error = "bad " + key + " '" + value + "' (logic frames)";
				return FALSE;
			}
			if (key == "crcinterval")
				cfg.crcInterval = u;
			else if (key == "sample")
				cfg.sampleInterval = u;
			else if (key == "idleinterval")
				cfg.idleInterval = u;
			else
				cfg.progressInterval = u;
		}
		else
		{
			// Be strict: a misspelled option silently ignored would give a different match than the one asked for.
			error = "unknown option '" + key + "'";
			return FALSE;
		}
	}

	if (cfg.map.empty())
	{
		error = "map=<map> is required";
		return FALSE;
	}
	if (!playersGiven || cfg.players.size() < 2)
	{
		error = "players=<difficulty>:<side>,... needs at least two players";
		return FALSE;
	}
	if ((Int)cfg.players.size() > MAX_SLOTS)
	{
		error = format("at most %d players", (int)MAX_SLOTS);
		return FALSE;
	}
	if (!seedGiven)
	{
		error = "seed=<n> is required: a match without a seed cannot be reproduced";
		return FALSE;
	}
	if (cfg.maxFrames == 0)
		cfg.maxFrames = minutes * 60 * LOGICFRAMES_PER_SECOND;
	(void)timeoutGiven;
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
// aiini=<file>: settings of the AI (an "AIData" block, normally with an "ExpertSkill" block in it) loaded over
// the game's own AI data before the match, with the game's INI reader in overwrite mode: only the fields named in
// the file change. It is how the Expert's settings are searched (scripts/aibench/tune.mjs) without a rebuild.
// The file is read with the C library, so it is a path of the host (absolute or relative to the current folder).
// Strict: another block than AIData, an unknown field or a bad value stops the match with an error.
// ------------------------------------------------------------------------------------------------
class AiIniBlockGuard : public INIBlockGuard
{
public:
	virtual Bool checkBlock(const char *blockType, const AsciiString & /*name*/, AsciiString &error) override
	{
		if (stricmp(blockType, "AIData") == 0)
			return TRUE;
		error.format("only AIData blocks belong in an aiini file, not '%s'", blockType);
		return FALSE;
	}
};

// The value of a plain field as text ("" for a field that is a block of its own or a list).
std::string fieldText(const FieldParse &f, const void *base)
{
	const char *at = static_cast<const char *>(base) + f.offset;
	if (f.parse == INI::parseReal || f.parse == INI::parsePercentToReal)
		return format("%g", (double)*reinterpret_cast<const Real *>(at));
	if (f.parse == INI::parseInt)
		return format("%d", (int)*reinterpret_cast<const Int *>(at));
	if (f.parse == INI::parseDurationUnsignedInt)
		return format("%u", (unsigned)*reinterpret_cast<const UnsignedInt *>(at));
	if (f.parse == INI::parseBool)
		return *reinterpret_cast<const Bool *>(at) ? "Yes" : "No";
	return std::string();
}

// The plain fields of the AI data and of its ExpertSkill block, as "Block.Field" -> value.
std::vector<std::pair<std::string, std::string> > aiSettings()
{
	std::vector<std::pair<std::string, std::string> > out;
	const TAiData *data = TheAI->getAiData();
	for (const FieldParse *f = AI::getAiDataFieldParse(); f->token != nullptr; ++f)
	{
		std::string v = fieldText(*f, data);
		if (!v.empty())
			out.push_back(std::make_pair(std::string("AIData.") + f->token, v));
	}
	for (const FieldParse *f = AI::getSkillSettingsFieldParse(); f->token != nullptr; ++f)
	{
		std::string v = fieldText(*f, &data->m_expertSkill);
		if (!v.empty())
			out.push_back(std::make_pair(std::string("ExpertSkill.") + f->token, v));
	}
	return out;
}

Bool loadAiIni(Config &cfg, std::string &error)
{
	if (cfg.aiIniPath.empty())
		return TRUE;
	if (TheAI == nullptr || TheAI->getAiData() == nullptr)
	{
		error = "aiini: the AI data is not loaded";
		return FALSE;
	}
	FILE *f = fopen(cfg.aiIniPath.c_str(), "rb");
	if (f == nullptr)
	{
		error = "aiini: cannot open '" + cfg.aiIniPath + "'";
		return FALSE;
	}
	std::string text;
	char chunk[4096];
	size_t got;
	while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0)
		text.append(chunk, got);
	fclose(f);

	UnsignedInt hash = 2166136261u;	// FNV-1a
	for (size_t i = 0; i < text.size(); ++i)
		hash = (hash ^ (UnsignedByte)text[i]) * 16777619u;
	cfg.aiIniHash = format("%08x", hash);

	const std::vector<std::pair<std::string, std::string> > before = aiSettings();
	if (!text.empty())
	{
		Char *buffer = new Char[text.size()];		// the INI reader owns it from here on
		memcpy(buffer, text.data(), text.size());
		AiIniBlockGuard guard;
		INI::setBlockGuard(&guard);
		INI::setStrictFields(TRUE);
		std::string failure;
		try
		{
			INI ini;
			ini.loadFromBuffer(AsciiString(cfg.aiIniPath.c_str()), buffer, (Int)text.size(), INI_LOAD_OVERWRITE, nullptr);
		}
		catch (INIException &e)
		{
			failure = e.mFailureMessage != nullptr ? std::string(e.mFailureMessage) : std::string("unreadable");
			while (!failure.empty() && (failure.back() == '\n' || failure.back() == '\r' || failure.back() == ' '))
				failure.pop_back();
		}
		catch (...)
		{
			failure = "'" + cfg.aiIniPath + "': the INI reader rejected it (an unknown block, a bad value or a missing End)";
		}
		INI::setStrictFields(FALSE);
		INI::setBlockGuard(nullptr);
		if (!failure.empty())
		{
			error = "aiini: " + failure;
			return FALSE;
		}
	}
	const std::vector<std::pair<std::string, std::string> > after = aiSettings();
	Int changed = 0;
	for (size_t i = 0; i < after.size() && i < before.size(); ++i)
	{
		if (after[i].second == before[i].second)
			continue;
		printf("AIMATCH aiini %s: %s -> %s\n", after[i].first.c_str(), before[i].second.c_str(), after[i].second.c_str());
		++changed;
	}
	printf("AIMATCH aiini %s: loaded (hash %s, %d setting%s changed)\n", cfg.aiIniPath.c_str(), cfg.aiIniHash.c_str(),
		(int)changed, changed == 1 ? "" : "s");
	fflush(stdout);
	return TRUE;
}

// The map cache key for a name given on the command line: a full or relative path, or the name of the map's
// folder/file, or its display name.
Bool findMap(Config &cfg, std::string &error)
{
	if (TheMapCache == nullptr)
	{
		error = "the map cache is not available";
		return FALSE;
	}
	const std::string wantedPath = normalizedMapPath(cfg.map);
	const std::string wantedLeaf = squashed(mapLeafName(cfg.map));

	std::string found;
	std::string available;
	Int numFound = 0;
	for (MapCache::const_iterator it = TheMapCache->begin(); it != TheMapCache->end(); ++it)
	{
		std::string key = normalizedMapPath(it->first.str());
		AsciiString displayName;
		displayName.translate(it->second.m_displayName);
		if (!available.empty())
			available += ", ";
		available += mapLeafName(key) + format(" (%d players)", it->second.m_numPlayers);

		if (key == wantedPath || squashed(mapLeafName(key)) == wantedLeaf
			|| (displayName.isNotEmpty() && squashed(displayName.str()) == wantedLeaf))
		{
			if (found != key)
				++numFound;
			found = key;
		}
	}
	if (numFound == 0)
	{
		error = "map '" + cfg.map + "' not found. Maps: " + available;
		return FALSE;
	}
	if (numFound > 1)
	{
		error = "map '" + cfg.map + "' is ambiguous; give the path. Maps: " + available;
		return FALSE;
	}
	cfg.mapPath = found;
	return TRUE;
}

// "random", a template name ("FactionAmerica"), or its side ("America"), case insensitive.
Bool findTemplate(PlayerSpec &spec, std::string &error)
{
	std::string wanted = lowered(spec.side);
	if (wanted == "random")
	{
		spec.templateIndex = PLAYERTEMPLATE_RANDOM;
		return TRUE;
	}
	// The template's own name ("FactionAmerica", or "America" without the prefix) first; then the side, which several
	// templates share (the generals of Zero Hour): the first of them.
	std::string available;
	Int bySide = -2;
	for (Int i = 0; i < ThePlayerTemplateStore->getPlayerTemplateCount(); ++i)
	{
		const PlayerTemplate *pt = ThePlayerTemplateStore->getNthPlayerTemplate(i);
		if (pt == nullptr || pt->isObserver() || !pt->isPlayableSide())
			continue;
		std::string name = pt->getName().str();
		std::string side = pt->getSide().str();
		if (!available.empty())
			available += ", ";
		available += name + " (side " + side + ")";

		std::string shortName = lowered(name);
		if (shortName.compare(0, 7, "faction") == 0)
			shortName = shortName.substr(7);
		if (lowered(name) == wanted || shortName == wanted)
		{
			spec.templateIndex = i;
			return TRUE;
		}
		if (bySide == -2 && lowered(side) == wanted)
			bySide = i;
	}
	if (bySide >= 0)
	{
		spec.templateIndex = bySide;
		return TRUE;
	}
	error = "side '" + spec.side + "' not found. Sides: random, " + available;
	return FALSE;
}

// ------------------------------------------------------------------------------------------------
// Statistics
// ------------------------------------------------------------------------------------------------

// What is on the field for one player at one moment.
struct Sample
{
	Sample() { memset(this, 0, sizeof(*this)); }

	Int money;
	Int armyValue;				// units that can attack, by what they cost
	Int armyUnits;
	Int otherUnits;			// workers and other units that do not attack
	Int otherUnitValue;
	Int structureValue;		// finished structures, by cost
	Int structures;
	Int supplyCenters;
	Int supplySourcesHeld;
	Int factories;				// finished structures that produce
	Int idleFactories;			// ... of which have nothing in their queue
};

struct FactoryTally
{
	FactoryTally() : frames(0), idleFrames(0) {}
	UnsignedInt frames;			// factory-frames available (one per factory per frame)
	UnsignedInt idleFrames;
};

struct PlayerStats
{
	PlayerStats()
		: slot(-1), playerIndex(-1), startPos(-1), color(-1), defeatedFrame(0), defeated(FALSE),
		  idleSamples(0), factoryFramesTotal(0), idleFramesTotal(0), peakArmyValue(0), peakArmyUnits(0),
		  baseIncome(0), baseWithdrawn(0), baseOtherDeposits(0), supplySourceSamples(0), supplySourceSum(0)
	{}

	Int slot;
	Int playerIndex;
	std::string name;
	std::string templateName;
	std::string side;
	Int startPos;
	Int color;

	UnsignedInt defeatedFrame;	// 0: not defeated
	Bool defeated;

	// idle production, sampled every idleInterval frames
	UnsignedInt idleSamples;
	UnsignedInt factoryFramesTotal;
	UnsignedInt idleFramesTotal;
	std::map<std::string, FactoryTally> factoryByTemplate;

	Int peakArmyValue;
	Int peakArmyUnits;

	// Money counters at the first frame, to leave out starting cash and anything before the match.
	UnsignedInt baseIncome;
	UnsignedInt baseWithdrawn;
	UnsignedInt baseOtherDeposits;

	UnsignedInt supplySourceSamples;
	UnsignedInt supplySourceSum;

	std::vector<Sample> timeline;
	Sample last;
};

// The state of the scan of one player's objects. The callback only reads.
struct ScanContext
{
	ScanContext() : player(nullptr), wantFactories(FALSE), tally(nullptr) {}

	const Player *player;
	Bool wantFactories;
	Sample snap;
	std::map<std::string, FactoryTally> *tally;
	std::vector<Coord3D> supplyCenters;
};

void scanObject(Object *obj, void *userData)
{
	ScanContext *ctx = static_cast<ScanContext *>(userData);
	if (obj == nullptr || obj->isDestroyed() || obj->isEffectivelyDead())
		return;
	const ThingTemplate *tmpl = obj->getTemplate();
	if (tmpl == nullptr)
		return;
	if (tmpl->isKindOf(KINDOF_PROJECTILE) || tmpl->isKindOf(KINDOF_MINE) || tmpl->isKindOf(KINDOF_INERT))
		return;

	const Int cost = tmpl->calcCostToBuild(ctx->player);

	if (tmpl->isKindOf(KINDOF_STRUCTURE))
	{
		if (obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION) || obj->testStatus(OBJECT_STATUS_SOLD))
			return;
		ctx->snap.structureValue += cost;
		ctx->snap.structures += 1;
		if (tmpl->isKindOf(KINDOF_FS_SUPPLY_CENTER))
		{
			ctx->snap.supplyCenters += 1;
			ctx->supplyCenters.push_back(*obj->getPosition());
		}
		if (ctx->wantFactories)
		{
			ProductionUpdateInterface *pu = obj->getProductionUpdateInterface();
			if (pu != nullptr)
			{
				const Bool idle = pu->getProductionCount() == 0;
				ctx->snap.factories += 1;
				if (idle)
					ctx->snap.idleFactories += 1;
				if (ctx->tally != nullptr)
				{
					FactoryTally &t = (*ctx->tally)[tmpl->getName().str()];
					t.frames += 1;		// counted in samples here; the caller scales by the interval
					if (idle)
						t.idleFrames += 1;
				}
			}
		}
		return;
	}

	if (tmpl->isKindOf(KINDOF_CAN_ATTACK))
	{
		ctx->snap.armyValue += cost;
		ctx->snap.armyUnits += 1;
	}
	else if (tmpl->isKindOf(KINDOF_INFANTRY) || tmpl->isKindOf(KINDOF_VEHICLE) || tmpl->isKindOf(KINDOF_AIRCRAFT)
		|| tmpl->isKindOf(KINDOF_DOZER) || tmpl->isKindOf(KINDOF_HARVESTER))
	{
		ctx->snap.otherUnitValue += cost;
		ctx->snap.otherUnits += 1;
	}
}

Real distanceSquared(const Coord3D &a, const Coord3D &b)
{
	const Real dx = a.x - b.x;
	const Real dy = a.y - b.y;
	return dx * dx + dy * dy;
}

class Match
{
public:
	Match(const Config &cfg)
		: m_cfg(cfg), m_frame(0), m_endFrame(0), m_decided(FALSE), m_timedOut(FALSE), m_updates(0), m_finalCrc(0),
		  m_lastSampleFrame(0xFFFFFFFFu), m_lastProductionFrame(0xFFFFFFFFu), m_loadMs(0), m_simMs(0)
	{}

	Bool setup(std::string &error);
	Bool play(std::string &error);
	std::string report(UnsignedInt setupMs);

private:
	void captureBaseline();
	void scanAll(Bool wantFactories, std::vector<Sample> &out, Bool trackFactories);
	void sampleTimeline();
	void sampleProduction();
	void checkDefeats();
	void addCrc();
	void finishStats();
	void stepGame();

	void writePlayer(JsonWriter &w, const PlayerStats &ps, Int rank, const char *outcome);
	static void writeCounts(JsonWriter &w, const std::map<std::string, Int> &counts);

	Config m_cfg;
	std::vector<PlayerStats> m_players;	// by slot order of the players in the command line
	UnsignedInt m_frame;
	UnsignedInt m_endFrame;				// the frame the victory conditions were met, 0 if not decided
	Bool m_decided;
	Bool m_timedOut;
	UnsignedInt m_updates;

	std::vector<UnsignedInt> m_sampleFrames;
	std::vector<UnsignedInt> m_crcFrames;
	std::vector<UnsignedInt> m_crcValues;
	UnsignedInt m_finalCrc;
	UnsignedInt m_lastSampleFrame;
	UnsignedInt m_lastProductionFrame;
	UnsignedInt m_loadMs;
	UnsignedInt m_simMs;
	std::vector<Int> m_winners;
};

// ------------------------------------------------------------------------------------------------
Bool Match::setup(std::string &error)
{
	// The map and the sides need the INI data, which is loaded by now.
	if (!findMap(m_cfg, error))
		return FALSE;
	const MapMetaData *md = TheMapCache->findMap(AsciiString(m_cfg.mapPath.c_str()));
	if (md == nullptr)
	{
		error = "map '" + m_cfg.mapPath + "' is not in the map cache";
		return FALSE;
	}
	if (!md->m_isMultiplayer)
	{
		error = "map '" + m_cfg.mapPath + "' is not a skirmish map";
		return FALSE;
	}
	if ((Int)m_cfg.players.size() > md->m_numPlayers)
	{
		error = format("map '%s' has room for %d players, %d requested", m_cfg.mapPath.c_str(), md->m_numPlayers,
			(int)m_cfg.players.size());
		return FALSE;
	}

	for (size_t i = 0; i < m_cfg.players.size(); ++i)
	{
		if (!findTemplate(m_cfg.players[i], error))
			return FALSE;
		if (m_cfg.players[i].startPos >= md->m_numPlayers)
		{
			error = format("start position %d is beyond the %d of the map", m_cfg.players[i].startPos + 1, md->m_numPlayers);
			return FALSE;
		}
	}
	// Two players cannot take one start position.
	for (size_t i = 0; i < m_cfg.players.size(); ++i)
		for (size_t j = i + 1; j < m_cfg.players.size(); ++j)
			if (m_cfg.players[i].startPos >= 0 && m_cfg.players[i].startPos == m_cfg.players[j].startPos)
			{
				error = "two players share a start position";
				return FALSE;
			}
	// A match needs at least two sides that fight: one team for everyone is a sandbox that never ends.
	{
		Bool opponents = FALSE;
		for (size_t i = 0; i < m_cfg.players.size() && !opponents; ++i)
			for (size_t j = i + 1; j < m_cfg.players.size(); ++j)
				if (m_cfg.players[i].team < 0 || m_cfg.players[i].team != m_cfg.players[j].team)
				{
					opponents = TRUE;
					break;
				}
		if (!opponents)
		{
			error = "all players are on one team: nobody can win";
			return FALSE;
		}
	}

	// The skirmish setup the menu would have made: the AI players in the first slots, the rest closed.
	if (TheSkirmishGameInfo == nullptr)
		TheSkirmishGameInfo = NEW SkirmishGameInfo;
	else if (TheSkirmishGameInfo->isInGame())
		TheSkirmishGameInfo->endGame();
	TheSkirmishGameInfo->init();
	TheSkirmishGameInfo->clearSlotList();
	TheSkirmishGameInfo->reset();
	TheSkirmishGameInfo->enterGame();

	const Int numColors = TheMultiplayerSettings != nullptr ? TheMultiplayerSettings->getNumColors() : 0;
	for (Int slot = 0; slot < MAX_SLOTS; ++slot)
	{
		GameSlot gs;
		if ((size_t)slot < m_cfg.players.size())
		{
			const PlayerSpec &spec = m_cfg.players[slot];
			gs.setState(spec.state);
			gs.setPlayerTemplate(spec.templateIndex);
			gs.setColor(numColors > 0 ? slot % numColors : -1);
			gs.setStartPos(spec.startPos);
			gs.setTeamNumber(spec.team);
		}
		else
		{
			gs.setState(SLOT_CLOSED);
		}
		TheSkirmishGameInfo->setSlot(slot, gs);
		s_variantBySlot[slot] = (size_t)slot < m_cfg.players.size() ? m_cfg.players[slot].variant : std::string();
	}

	TheSkirmishGameInfo->setMap(AsciiString(m_cfg.mapPath.c_str()));
	TheSkirmishGameInfo->setSeed(m_cfg.seed);
	if (m_cfg.startingCash >= 0)
	{
		Money cash;
		cash.setStartingCash((UnsignedInt)m_cfg.startingCash);
		TheSkirmishGameInfo->setStartingCash(cash);
	}
	TheSkirmishGameInfo->setSuperweaponRestriction(0);
	TheSkirmishGameInfo->startGame(0);

	TheWritableGlobalData->m_mapName = AsciiString(m_cfg.mapPath.c_str());
	s_record = m_cfg.record;
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
// The first frame: where the money counters stand, who is who.
void Match::captureBaseline()
{
	m_players.clear();
	for (size_t i = 0; i < m_cfg.players.size(); ++i)
	{
		PlayerStats ps;
		ps.slot = (Int)i;
		Player *p = ThePlayerList->getPlayerFromSlotIndex((Int)i);
		if (p != nullptr)
		{
			ps.playerIndex = p->getPlayerIndex();
			AsciiString name;
			name.translate(p->getPlayerDisplayName());
			ps.name = name.str();
			ps.side = p->getSide().str();
			ps.templateName = p->getPlayerTemplate() ? p->getPlayerTemplate()->getName().str() : "";
			ps.startPos = p->getMpStartIndex();
			const GameSlot *slot = TheSkirmishGameInfo->getConstSlot((Int)i);
			ps.color = slot ? slot->getColor() : -1;
			ps.baseIncome = p->getMoney()->getTotalIncome();
			ps.baseWithdrawn = p->getMoney()->getTotalWithdrawn();
			ps.baseOtherDeposits = p->getMoney()->getTotalOtherDeposits();
		}
		m_players.push_back(ps);
	}
}

// Scans every player; supply sources are shared by all, so everything is done together.
void Match::scanAll(Bool wantFactories, std::vector<Sample> &out, Bool trackFactories)
{
	std::vector<ScanContext> contexts(m_players.size());
	for (size_t i = 0; i < m_players.size(); ++i)
	{
		Player *p = m_players[i].playerIndex >= 0 ? ThePlayerList->getNthPlayer(m_players[i].playerIndex) : nullptr;
		contexts[i].player = p;
		contexts[i].wantFactories = wantFactories;
		contexts[i].tally = trackFactories ? &m_players[i].factoryByTemplate : nullptr;
		if (p != nullptr)
		{
			p->iterateObjects(scanObject, &contexts[i]);
			contexts[i].snap.money = (Int)p->getMoney()->countMoney();
		}
	}

	if (!wantFactories)
	{
		// Supply sources held: each source counts for the player whose supply center is nearest to it.
		// Sources are neutral objects; a source with no supply center anywhere belongs to nobody.
		for (Object *obj = TheGameLogic->getFirstObject(); obj != nullptr; obj = obj->getNextObject())
		{
			const ThingTemplate *tmpl = obj->getTemplate();
			if (tmpl == nullptr || !tmpl->isKindOf(KINDOF_SUPPLY_SOURCE) || obj->isDestroyed() || obj->isEffectivelyDead())
				continue;
			Real best = 0;
			Int owner = -1;
			Bool tie = FALSE;
			for (size_t i = 0; i < contexts.size(); ++i)
			{
				for (size_t c = 0; c < contexts[i].supplyCenters.size(); ++c)
				{
					const Real d = distanceSquared(*obj->getPosition(), contexts[i].supplyCenters[c]);
					if (owner < 0 || d < best)
					{
						best = d;
						owner = (Int)i;
						tie = FALSE;
					}
					else if (d == best && owner != (Int)i)
					{
						tie = TRUE;
					}
				}
			}
			if (owner >= 0 && !tie)
				contexts[owner].snap.supplySourcesHeld += 1;
		}
	}

	out.resize(contexts.size());
	for (size_t i = 0; i < contexts.size(); ++i)
		out[i] = contexts[i].snap;
}

void Match::sampleTimeline()
{
	m_lastSampleFrame = m_frame;
	std::vector<Sample> snaps;
	scanAll(FALSE, snaps, FALSE);
	m_sampleFrames.push_back(m_frame);
	for (size_t i = 0; i < m_players.size(); ++i)
	{
		PlayerStats &ps = m_players[i];
		// The factory numbers of the timeline come from the last production sample.
		snaps[i].factories = ps.last.factories;
		snaps[i].idleFactories = ps.last.idleFactories;
		ps.timeline.push_back(snaps[i]);
		ps.last.money = snaps[i].money;
		ps.last.armyValue = snaps[i].armyValue;
		ps.last.armyUnits = snaps[i].armyUnits;
		ps.last.otherUnits = snaps[i].otherUnits;
		ps.last.otherUnitValue = snaps[i].otherUnitValue;
		ps.last.structureValue = snaps[i].structureValue;
		ps.last.structures = snaps[i].structures;
		ps.last.supplyCenters = snaps[i].supplyCenters;
		ps.last.supplySourcesHeld = snaps[i].supplySourcesHeld;
		ps.peakArmyValue = std::max(ps.peakArmyValue, snaps[i].armyValue);
		ps.peakArmyUnits = std::max(ps.peakArmyUnits, snaps[i].armyUnits);
		ps.supplySourceSamples += 1;
		ps.supplySourceSum += (UnsignedInt)snaps[i].supplySourcesHeld;
	}
}

void Match::sampleProduction()
{
	m_lastProductionFrame = m_frame;
	std::vector<Sample> snaps;
	scanAll(TRUE, snaps, TRUE);
	for (size_t i = 0; i < m_players.size(); ++i)
	{
		PlayerStats &ps = m_players[i];
		// A defeated player's leftovers do not count as idle production.
		if (ps.defeated)
		{
			ps.last.factories = 0;
			ps.last.idleFactories = 0;
			continue;
		}
		ps.last.factories = snaps[i].factories;
		ps.last.idleFactories = snaps[i].idleFactories;
		ps.factoryFramesTotal += (UnsignedInt)snaps[i].factories * m_cfg.idleInterval;
		ps.idleFramesTotal += (UnsignedInt)snaps[i].idleFactories * m_cfg.idleInterval;
		ps.idleSamples += 1;
	}
}

void Match::checkDefeats()
{
	for (size_t i = 0; i < m_players.size(); ++i)
	{
		PlayerStats &ps = m_players[i];
		if (ps.defeated || ps.playerIndex < 0)
			continue;
		Player *p = ThePlayerList->getNthPlayer(ps.playerIndex);
		if (p != nullptr && TheVictoryConditions->hasSinglePlayerBeenDefeated(p))
		{
			ps.defeated = TRUE;
			ps.defeatedFrame = m_frame;
		}
	}
}

void Match::addCrc()
{
	m_crcFrames.push_back(m_frame);
	m_crcValues.push_back(TheGameLogic->getCRC(CRC_RECALC));
}

// One step of the game: the logic alone (the way replays are simulated), or the whole engine update, which also
// runs the client and pumps the message stream. The results must not depend on it; loop=engine proves that.
void Match::stepGame()
{
	if (m_cfg.engineLoop)
		TheGameEngine->update();
	else
		TheGameLogic->UPDATE();
}

// ------------------------------------------------------------------------------------------------
Bool Match::play(std::string &error)
{
	// New game: the same message the skirmish menu sends, but into the command list, because no message stream
	// is pumped.
	const UnsignedInt loadStart = GetTickCount();
	InitRandom((UnsignedInt)m_cfg.seed);
	GameMessage *msg = newInstance(GameMessage)(GameMessage::MSG_NEW_GAME);
	msg->appendIntegerArgument(GAME_SKIRMISH);
	msg->appendIntegerArgument(DIFFICULTY_NORMAL);
	msg->appendIntegerArgument(0);
	TheCommandList->appendMessage(msg);

	// Until the map is loaded.
	while (!(TheGameLogic->getGameMode() == GAME_SKIRMISH && TheGameLogic->getFrame() >= 1))
	{
		if (++m_updates > kMaxUpdatesToStart || TheGameEngine->getQuitting())
		{
			error = "the match did not start (see the engine log; a skirmish needs the multiplayer scripts or the starter content)";
			return FALSE;
		}
		stepGame();
	}

	// Idle players: computer players without their AI, a stand-in for a human player who gives no orders (team games: does an
	// Expert ally come to help when the human's base is attacked?).
	for (size_t i = 0; i < m_cfg.players.size(); ++i)
	{
		Player *p = m_cfg.players[i].idle ? ThePlayerList->getPlayerFromSlotIndex((Int)i) : nullptr;
		if (p != nullptr)
			p->deletePlayerAI();
	}

	captureBaseline();
	m_loadMs = GetTickCount() - loadStart;
	UnsignedInt lastProgressFrame = 0;
	UnsignedInt stuck = 0;
	UnsignedInt lastFrame = TheGameLogic->getFrame();
	UnsignedInt wallStart = GetTickCount();

	for (;;)
	{
		m_frame = TheGameLogic->getFrame();	// frames simulated so far

		// Statistics, at fixed frames only so that they do not depend on timing.
		if (m_frame % m_cfg.idleInterval == 0)
			sampleProduction();
		if (m_frame % kDefenceCheckInterval == 0)
			checkDefeats();
		if (m_frame % m_cfg.sampleInterval == 0)
			sampleTimeline();
		if (m_frame % m_cfg.crcInterval == 0)
			addCrc();

		if (m_cfg.progressInterval > 0 && m_frame - lastProgressFrame >= m_cfg.progressInterval)
		{
			lastProgressFrame = m_frame;
			const UnsignedInt ms = GetTickCount() - wallStart;
			printf("AIMATCH_PROGRESS frame=%u/%u logic_fps=%.0f\n", m_frame, m_cfg.maxFrames,
				ms > 0 ? (double)m_frame * 1000.0 / (double)ms : 0.0);
			fflush(stdout);
		}

		// Is it decided?
		const UnsignedInt endFrame = TheVictoryConditions->getEndFrame();
		if (endFrame != 0)
		{
			m_decided = TRUE;
			m_endFrame = endFrame;
			break;
		}
		if (m_frame >= m_cfg.maxFrames)
		{
			m_timedOut = TRUE;
			break;
		}
		if (!TheGameLogic->isInGame() || TheGameEngine->getQuitting())
		{
			error = format("the game ended by itself at frame %u (no victory)", m_frame);
			return FALSE;
		}

		for (size_t e = 0; e < m_cfg.eliminations.size(); ++e)
		{
			const Int slot = m_cfg.eliminations[e].first;
			if (m_cfg.eliminations[e].second == m_frame && (size_t)slot < m_players.size() && m_players[slot].playerIndex >= 0)
			{
				Player *p = ThePlayerList->getNthPlayer(m_players[slot].playerIndex);
				if (p != nullptr)
					p->killPlayer();
			}
		}

		stepGame();

		if (TheGameLogic->getFrame() == lastFrame)
		{
			if (++stuck > kMaxUpdatesWithoutProgress)
			{
				error = format("the game logic stopped advancing at frame %u (time frozen or paused)", lastFrame);
				return FALSE;
			}
		}
		else
		{
			stuck = 0;
			lastFrame = TheGameLogic->getFrame();
		}
	}

	m_simMs = GetTickCount() - wallStart;

	// The end: one more look at everything, and the final CRC.
	m_frame = TheGameLogic->getFrame();
	checkDefeats();
	if (m_lastProductionFrame != m_frame)
		sampleProduction();
	if (m_lastSampleFrame != m_frame)
		sampleTimeline();
	if (m_crcFrames.empty() || m_crcFrames.back() != m_frame)
		addCrc();
	m_finalCrc = m_crcValues.back();
	finishStats();
	return TRUE;
}

// Who won, in order.
void Match::finishStats()
{
	m_winners.clear();
	if (m_decided)
	{
		for (size_t i = 0; i < m_players.size(); ++i)
		{
			Player *p = m_players[i].playerIndex >= 0 ? ThePlayerList->getNthPlayer(m_players[i].playerIndex) : nullptr;
			if (p != nullptr && TheVictoryConditions->hasAchievedVictory(p))
				m_winners.push_back((Int)i);
		}
	}
}

// ------------------------------------------------------------------------------------------------
void Match::writeCounts(JsonWriter &w, const std::map<std::string, Int> &counts)
{
	w.beginObject();
	for (std::map<std::string, Int>::const_iterator it = counts.begin(); it != counts.end(); ++it)
		w.field(it->first.c_str(), it->second);
	w.endObject();
}

namespace
{
// ScoreKeeper's tallies are keyed by template pointer; the report is keyed by name, in name order.
void collectCounts(const ScoreKeeper::ObjectCountMap &in, std::map<std::string, Int> &out)
{
	for (ScoreKeeper::ObjectCountMap::const_iterator it = in.begin(); it != in.end(); ++it)
	{
		if (it->first == nullptr || it->second == 0)
			continue;
		out[it->first->getName().str()] += it->second;
	}
}

Int sumCounts(const std::map<std::string, Int> &counts)
{
	Int total = 0;
	for (std::map<std::string, Int>::const_iterator it = counts.begin(); it != counts.end(); ++it)
		total += it->second;
	return total;
}
} // namespace

void Match::writePlayer(JsonWriter &w, const PlayerStats &ps, Int rank, const char *outcome)
{
	const PlayerSpec &spec = m_cfg.players[ps.slot];
	Player *p = ps.playerIndex >= 0 ? ThePlayerList->getNthPlayer(ps.playerIndex) : nullptr;

	w.beginObject();
	w.field("slot", ps.slot);
	w.field("playerIndex", ps.playerIndex);		// the p<N> of the AI trace lines
	w.field("name", ps.name);
	w.field("difficulty", spec.difficulty);
	w.field("side", ps.side);
	w.field("template", ps.templateName);
	w.field("variant", spec.variant);
	w.field("team", spec.team);
	w.field("startPos", ps.startPos + 1);		// 1 based like the command line
	w.field("color", ps.color);
	w.field("outcome", outcome);
	w.field("rank", rank);
	if (ps.defeated)
		w.field("defeatedFrame", ps.defeatedFrame);
	else
		w.key("defeatedFrame"), w.null();

	if (p != nullptr)
	{
		const Money *money = p->getMoney();
		const Int gathered = (Int)(money->getTotalIncome() - ps.baseIncome);
		const Int withdrawn = (Int)(money->getTotalWithdrawn() - ps.baseWithdrawn);
		const Int refunded = (Int)(money->getTotalOtherDeposits() - ps.baseOtherDeposits);
		w.key("money");
		w.beginObject();
		w.field("final", (Int)money->countMoney());
		w.field("gathered", gathered);
		w.field("withdrawn", withdrawn);
		w.field("refunded", refunded);			// cancelled production and sales
		w.field("spent", withdrawn - refunded);
		w.endObject();

		ScoreKeeper *sk = p->getScoreKeeper();
		std::map<std::string, Int> built, lost, killed;
		collectCounts(sk->getObjectsBuilt(), built);
		collectCounts(sk->getObjectsLost(), lost);
		for (Int victim = 0; victim < MAX_PLAYER_COUNT; ++victim)
			collectCounts(sk->getObjectsDestroyed(victim), killed);
		w.key("built");
		writeCounts(w, built);
		w.key("lost");
		writeCounts(w, lost);
		w.key("killed");
		writeCounts(w, killed);
		w.key("totals");
		w.beginObject();
		w.field("unitsBuilt", sk->getTotalUnitsBuilt());
		w.field("structuresBuilt", sk->getTotalBuildingsBuilt());
		w.field("unitsLost", sk->getTotalUnitsLost());
		w.field("structuresLost", sk->getTotalBuildingsLost());
		w.field("objectsKilled", sumCounts(killed));
		w.field("score", sk->calculateScore());
		w.endObject();
	}

	w.key("production");
	w.beginObject();
	w.field("factoryFrames", ps.factoryFramesTotal);
	w.field("idleFrames", ps.idleFramesTotal);
	w.field("idleFraction", ps.factoryFramesTotal > 0 ? (double)ps.idleFramesTotal / (double)ps.factoryFramesTotal : 0.0);
	w.key("byTemplate");
	w.beginObject();
	for (std::map<std::string, FactoryTally>::const_iterator it = ps.factoryByTemplate.begin(); it != ps.factoryByTemplate.end(); ++it)
	{
		w.key(it->first);
		w.beginObject();
		w.field("factoryFrames", it->second.frames * m_cfg.idleInterval);
		w.field("idleFrames", it->second.idleFrames * m_cfg.idleInterval);
		w.endObject();
	}
	w.endObject();
	w.endObject();

	w.key("supply");
	w.beginObject();
	w.field("centers", ps.last.supplyCenters);
	w.field("sourcesHeld", ps.last.supplySourcesHeld);
	w.field("sourcesHeldAverage", ps.supplySourceSamples > 0 ? (double)ps.supplySourceSum / (double)ps.supplySourceSamples : 0.0);
	w.endObject();

	w.key("final");
	w.beginObject();
	w.field("armyValue", ps.last.armyValue);
	w.field("armyUnits", ps.last.armyUnits);
	w.field("otherUnits", ps.last.otherUnits);
	w.field("structureValue", ps.last.structureValue);
	w.field("structures", ps.last.structures);
	w.field("value", ps.last.money + ps.last.armyValue + ps.last.otherUnitValue + ps.last.structureValue);
	w.endObject();

	w.key("peak");
	w.beginObject();
	w.field("armyValue", ps.peakArmyValue);
	w.field("armyUnits", ps.peakArmyUnits);
	w.endObject();

	w.endObject();
}

std::string Match::report(UnsignedInt setupMs)
{
	JsonWriter w;
	w.beginObject();
	w.field("schema", kSchema);
	w.field("label", m_cfg.label);
	w.field("aiini", m_cfg.aiIniPath);
	w.field("aiiniHash", m_cfg.aiIniHash);

	// What was asked.
	w.key("config");
	w.beginObject();
	w.field("map", m_cfg.mapPath);
	w.field("seed", m_cfg.seed);
	w.field("maxFrames", m_cfg.maxFrames);
	w.field("startingCash", m_cfg.startingCash);
	w.field("crcInterval", m_cfg.crcInterval);
	w.field("sampleInterval", m_cfg.sampleInterval);
	w.field("idleInterval", m_cfg.idleInterval);
	w.key("players");
	w.beginArray();
	for (size_t i = 0; i < m_cfg.players.size(); ++i)
	{
		const PlayerSpec &s = m_cfg.players[i];
		w.beginObject();
		w.field("difficulty", s.difficulty);
		w.field("side", s.side);
		w.field("team", s.team);
		w.field("startPos", s.startPos + 1);
		w.field("variant", s.variant);
		w.endObject();
	}
	w.endArray();
	w.endObject();

	// Ranks: winners first, then by when they fell (later is better; standing at the end is best), then by value.
	std::vector<Int> order;
	for (size_t i = 0; i < m_players.size(); ++i)
		order.push_back((Int)i);
	for (size_t a = 0; a < order.size(); ++a)
		for (size_t b = a + 1; b < order.size(); ++b)
		{
			const PlayerStats &pa = m_players[order[a]];
			const PlayerStats &pb = m_players[order[b]];
			const Bool wa = std::find(m_winners.begin(), m_winners.end(), pa.slot) != m_winners.end();
			const Bool wb = std::find(m_winners.begin(), m_winners.end(), pb.slot) != m_winners.end();
			Bool swap = FALSE;
			if (wa != wb)
				swap = wb;
			else
			{
				const UnsignedInt da = pa.defeated ? pa.defeatedFrame : 0xFFFFFFFFu;
				const UnsignedInt db = pb.defeated ? pb.defeatedFrame : 0xFFFFFFFFu;
				if (da != db)
					swap = db > da;
				else
				{
					const Int va = pa.last.money + pa.last.armyValue + pa.last.otherUnitValue + pa.last.structureValue;
					const Int vb = pb.last.money + pb.last.armyValue + pb.last.otherUnitValue + pb.last.structureValue;
					swap = vb > va;
				}
			}
			if (swap)
				std::swap(order[a], order[b]);
		}
	std::vector<Int> rankOf(m_players.size(), 0);
	for (size_t i = 0; i < order.size(); ++i)
	{
		const Bool winner = std::find(m_winners.begin(), m_winners.end(), order[i]) != m_winners.end();
		rankOf[order[i]] = winner ? 1 : (Int)i + 1;
	}

	const char *outcome = m_decided ? (m_winners.empty() ? "draw" : "victory") : "timeout";
	w.key("result");
	w.beginObject();
	w.field("outcome", outcome);
	w.field("endReason", m_decided ? (m_winners.empty() ? "all players eliminated" : "victory conditions met") : "frame limit reached");
	w.field("frames", m_frame);
	w.field("gameSeconds", m_frame / LOGICFRAMES_PER_SECOND);
	w.field("endFrame", m_endFrame);
	w.key("winners");
	w.beginArray();
	for (size_t i = 0; i < m_winners.size(); ++i)
		w.num(m_winners[i]);
	w.endArray();
	w.field("finalCRC", format("%08X", m_finalCrc));
	w.endObject();

	w.key("players");
	w.beginArray();
	for (size_t i = 0; i < m_players.size(); ++i)
	{
		const PlayerStats &ps = m_players[i];
		const Bool winner = std::find(m_winners.begin(), m_winners.end(), ps.slot) != m_winners.end();
		const char *o = winner ? "won" : (!m_decided ? "timeout" : (m_winners.empty() ? "draw" : "lost"));
		writePlayer(w, ps, rankOf[i], o);
	}
	w.endArray();

	// Sampled series, one array per quantity and player.
	w.key("timeline");
	w.beginObject();
	w.field("interval", m_cfg.sampleInterval);
	w.key("frames");
	w.beginArray();
	for (size_t i = 0; i < m_sampleFrames.size(); ++i)
		w.num(m_sampleFrames[i]);
	w.endArray();
	w.key("players");
	w.beginArray();
	for (size_t i = 0; i < m_players.size(); ++i)
	{
		const PlayerStats &ps = m_players[i];
		w.beginObject();
		w.field("slot", ps.slot);
#define SERIES(NAME, MEMBER) \
		w.key(NAME); \
		w.beginArray(); \
		for (size_t s = 0; s < ps.timeline.size(); ++s) \
			w.num(ps.timeline[s].MEMBER); \
		w.endArray();
		SERIES("money", money)
		SERIES("armyValue", armyValue)
		SERIES("armyUnits", armyUnits)
		SERIES("otherUnits", otherUnits)
		SERIES("structureValue", structureValue)
		SERIES("structures", structures)
		SERIES("supplyCenters", supplyCenters)
		SERIES("supplySourcesHeld", supplySourcesHeld)
		SERIES("factories", factories)
		SERIES("idleFactories", idleFactories)
#undef SERIES
		w.endObject();
	}
	w.endArray();
	w.endObject();

	// The determinism evidence.
	w.key("crcTimeline");
	w.beginObject();
	w.field("interval", m_cfg.crcInterval);
	w.key("frames");
	w.beginArray();
	for (size_t i = 0; i < m_crcFrames.size(); ++i)
		w.num(m_crcFrames[i]);
	w.endArray();
	w.key("crc");
	w.beginArray();
	for (size_t i = 0; i < m_crcValues.size(); ++i)
		w.str(format("%08X", m_crcValues[i]));
	w.endArray();
	w.endObject();

	// Not deterministic, and not to be compared between runs.
	w.key("perf");
	w.beginObject();
	w.field("setupMs", setupMs);
	w.field("loadMs", m_loadMs);			// from the start of the match to frame 0: map, objects, scripts
	w.field("simMs", m_simMs);
	w.field("logicFps", m_simMs > 0 ? (double)m_frame * 1000.0 / (double)m_simMs : 0.0);
	w.endObject();

	w.endObject();
	return w.text();
}

// One line "AIMATCH_ERROR <message>" and, with stats=, a file with the same in JSON.
void reportError(const Config &cfg, const std::string &message)
{
	printf("AIMATCH_ERROR %s\n", message.c_str());
	fflush(stdout);
	JsonWriter w;
	w.beginObject();
	w.field("schema", kSchema);
	w.field("label", cfg.label);
	w.field("aiini", cfg.aiIniPath);
	w.field("aiiniHash", cfg.aiIniHash);
	w.key("result");
	w.beginObject();
	w.field("outcome", "error");
	w.field("endReason", message);
	w.endObject();
	w.endObject();
	if (!cfg.statsPath.empty())
	{
		FILE *f = fopen(cfg.statsPath.c_str(), "wb");
		if (f != nullptr)
		{
			fputs(w.text().c_str(), f);
			fputc('\n', f);
			fclose(f);
		}
	}
	printf("AIMATCH_RESULT %s\n", w.text().c_str());
	fflush(stdout);
}

} // namespace

// ================================================================================================
namespace AIMatchShared
{

Bool findMapPath(const std::string &name, std::string &path, std::string &error)
{
	Config cfg;
	cfg.map = name;
	if (!findMap(cfg, error))
		return FALSE;
	path = cfg.mapPath;
	return TRUE;
}

Bool findSideTemplate(const std::string &side, Int &templateIndex, std::string &error)
{
	PlayerSpec spec;
	spec.side = side;
	if (!findTemplate(spec, error))
		return FALSE;
	templateIndex = spec.templateIndex;
	return TRUE;
}

void setBenchActive(Bool active)
{
	s_active = active;
	s_record = FALSE;
	for (Int i = 0; i < MAX_SLOTS; ++i)
		s_variantBySlot[i].clear();
}

} // namespace AIMatchShared

// ================================================================================================
Bool AIMatch::isRequested()
{
	for (int i = 1; i < __argc; ++i)
	{
		if (stricmp(__argv[i], "-aiMatch") == 0)
			return TRUE;
	}
	return FALSE;
}

Bool AIMatch::isActive()
{
	return s_active;
}

Bool AIMatch::shouldRecordReplay()
{
	return !s_active || s_record;
}

AsciiString AIMatch::getPlayerVariant(const Player *player)
{
	if (!s_active || player == nullptr || ThePlayerList == nullptr)
		return AsciiString::TheEmptyString;
	const Int slot = ThePlayerList->getSlotIndex(player->getPlayerIndex());
	if (slot < 0 || slot >= MAX_SLOTS)
		return AsciiString::TheEmptyString;
	return AsciiString(s_variantBySlot[slot].c_str());
}

Int AIMatch::run()
{
	const UnsignedInt t0 = GetTickCount();
	s_active = TRUE;

	Config cfg;
	std::string error;
	if (!parseConfig(cfg, error))
	{
		reportError(cfg, error);
		s_active = FALSE;
		return 1;
	}

	// The AI settings of aiini=, over the game's own (loaded by now with the rest of the INI data).
	if (!loadAiIni(cfg, error))
	{
		reportError(cfg, error);
		s_active = FALSE;
		return 1;
	}

	Match match(cfg);
	UnsignedInt setupMs = 0;
	try
	{
		if (!match.setup(error))
		{
			reportError(cfg, error);
			s_active = FALSE;
			return 1;
		}
		setupMs = GetTickCount() - t0;
		if (!match.play(error))
		{
			reportError(cfg, error);
			s_active = FALSE;
			return 1;
		}
	}
	catch (...)
	{
		reportError(cfg, "an exception ended the match");
		s_active = FALSE;
		return 1;
	}

	const std::string json = match.report(setupMs);

	if (!cfg.statsPath.empty())
	{
		FILE *f = fopen(cfg.statsPath.c_str(), "wb");
		if (f == nullptr)
			printf("AIMATCH_WARNING cannot write %s\n", cfg.statsPath.c_str());
		else
		{
			fputs(json.c_str(), f);
			fputc('\n', f);
			fclose(f);
		}
	}
	printf("AIMATCH_RESULT %s\n", json.c_str());
	fflush(stdout);

	// Leave the game the way a player does, so that the shutdown has a world to take down.
	TheGameLogic->clearGameData(FALSE);
	s_active = FALSE;
	return 0;
}
