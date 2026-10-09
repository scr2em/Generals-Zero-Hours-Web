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

// FILE: ArmyPackages.h ///////////////////////////////////////////////////////
//
// Army packages (.zharmy): extra factions that are loaded on top of the game data.
// The format is described in docs/ARMY_PACKAGES.md; this file is the engine side.
//
//   -army <file>          (repeatable) names a package, see CommandLine.cpp
//   ArmyPackages::load    called once from GameEngine::init, after the base INI data
//                         and before the subsystems post process their data
//
// Every package is loaded completely or not at all: it is checked first (archive,
// manifest, file layout, definitions, strings, scripts) and skipped with a reason when
// anything is wrong. What can only be seen while the definitions are parsed (a bad
// value) stops the game with a message instead of leaving half a package behind.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Common/AsciiString.h"
#include "Common/INI.h"
#include "Common/STLTypedefs.h"

class ZipArchiveFile;
class SidesList;
class Xfer;
class PlayerTemplate;

struct ArmyFaction
{
	ArmyFaction() : m_ai(FALSE) {}

	AsciiString m_playerTemplate;   ///< name of the PlayerTemplate (starts with "<TAG>_")
	AsciiString m_side;             ///< side name of the faction's objects (starts with "<TAG>_")
	AsciiString m_displayName;
	Bool m_ai;                      ///< the computer may play it
};

struct ArmyPackage
{
	ArmyPackage() : m_loaded(FALSE), m_mounted(FALSE), m_zip(nullptr), m_hasScripts(FALSE) {}

	AsciiString m_path;             ///< as given on the command line
	AsciiString m_id;               ///< manifest id; the file name when the manifest could not be read
	AsciiString m_tag;
	AsciiString m_name;
	AsciiString m_version;
	AsciiString m_description;
	AsciiString m_contentHash;      ///< "sha256:<hex>" as stored in the manifest
	std::vector<AsciiString> m_requires;
	std::vector<ArmyFaction> m_factions;

	Bool m_loaded;
	AsciiString m_reason;           ///< why the package was skipped (empty when loaded)

	Bool m_mounted;                 ///< assets are in the archive file system
	ZipArchiveFile *m_zip;          ///< open while the package is loaded (owned by the archive file system once mounted)
	Bool m_hasScripts;              ///< Army/Scripts/Skirmish.scb is present
};

class ArmyPackages : public INIBlockGuard
{
public:
	/// Loads the packages named with -army, in the order of their ids. Never throws for a bad package.
	/// pXfer is the engine's INI checksum, which sees the package definitions like the base ones.
	static void load( Xfer *pXfer );
	static void shutdown();

	const AsciiString &getRuleset() const { return m_ruleset; }
	const std::vector<ArmyPackage> &getPackages() const { return m_packages; }

	/// The faction a PlayerTemplate belongs to, or nullptr for a template of the game data.
	const ArmyFaction *findFactionByTemplate( const AsciiString &playerTemplateName ) const;

	/// FALSE for a faction of a package that does not offer it to the computer; TRUE for everything else.
	Bool canBePlayedByAI( const PlayerTemplate *pt ) const;

	/// TRUE when a loaded package has a faction that the computer may not play (the lists for computer slots differ then).
	Bool hasHumanOnlyFactions() const;

	/// Adds the skirmish sides and scripts of the AI factions to a freshly prepared sides list
	/// (after SidesList::prepareForMP_or_Skirmish).
	void prepareSkirmishSides( SidesList *sides ) const;

	// INIBlockGuard: holds the definitions of the package that is being loaded to the rules
	virtual Bool checkBlock( const char *blockType, const AsciiString &name, AsciiString &error ) override;

private:
	ArmyPackages();
	~ArmyPackages();

	void loadAll( Xfer *pXfer );
	void readManifest( ArmyPackage &pkg );
	void loadPackage( ArmyPackage &pkg, Xfer *pXfer );
	void skip( ArmyPackage &pkg, const AsciiString &reason );
	void fail( ArmyPackage &pkg, const AsciiString &reason );   ///< stops the game: the package is half loaded
	void writeReport( const Char *failedId = nullptr, const AsciiString *failedReason = nullptr ) const;
	AsciiString detectRuleset() const;

	AsciiString m_ruleset;
	std::vector<ArmyPackage> m_packages;

	// state of the package that is being loaded (for the block guard)
	const ArmyPackage *m_current;
	AsciiString m_currentFile;
	AsciiString m_currentBlockType;     ///< the only block type allowed in the file being loaded

	friend class ArmyPackagesAccess;
};

extern ArmyPackages *TheArmyPackages;
