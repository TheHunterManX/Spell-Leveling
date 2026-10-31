// Spell Leveling, an SKSE plugin for Skyrim Special Edition.
// Copyright (C) 2026 TheHunterManX
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the GNU General
// Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
// option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
// for more details.
//
// You should have received a copy of the GNU General Public License along with this program (see LICENSE.txt).
// If not, see <https://www.gnu.org/licenses/>.
//
// It is built on CommonLibSSE-NG (GPL-3.0-or-later with the Modding Exception and the GPL-3.0 Linking Exception):
// https://github.com/alandtse/CommonLibSSE-NG
#pragma once

// The plugin's rules with no game code in them: the settings and the ini reader, the XP curve, level-ups, the
// bonuses, and the text shown for a spell. Kept apart so tests can run them without Skyrim.
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace core
{
	// The version of the ini layout this plugin expects. Raise it whenever a setting is added, removed or changes meaning: an ini
	// with another number (or none) is saved as .old and replaced by the defaults when the game starts.
	inline constexpr int kIniVersion = 1;

	struct Settings
	{
		// [XP]
		double xpSpeed = 1.0;
		double xpBase = 3.0;
		double xpScale = 0.25;
		double xpPower = 1.5;
		double dualCastMultiplier = 2.0;
		double referenceCost = 50.0;
		double minCostFactor = 0.25;
		double maxCostFactor = 4.0;
		double concentrationXpPerSecond = 1.0;
		double sameSpellCooldown = 1.0;
		// XP from casting while in combat is multiplied by a number that grows in a straight line with the level of the
		// highest-level enemy fighting the player: Min at MinLevel (and below), Max at MaxLevel (and above).
		double combatXpMinMultiplier = 2.0;
		double combatXpMaxMultiplier = 4.0;
		int    combatXpMinLevel = 1;
		int    combatXpMaxLevel = 100;

		// [Levels]
		int    levelCap = 0;  // 0 = none
		double magnitudePerLevel = 0.02;
		double costPerLevel = 0.0025;
		double costFloor = 0.0;
		double maxMagnitudeMultiplier = 0.0;  // 0 = none

		// Duration: spells of the listed schools also last longer with each level (Bound Sword, Conjure Familiar, Oakflesh...).
		double        durationPerLevel = 0.02;
		double        maxDurationMultiplier = 0.0;  // 0 = none
		std::set<int> durationSchools;              // School numbers below
		std::set<int> durationExcludedArchetypes;   // effects whose duration is left alone
		int           durationMinSeconds = 2;       // effects shorter than this (1-second ticks, instant-style) keep their duration

		// [Display]
		bool        toastEnabled = true;
		int         toastEveryLevels = 1;
		bool        showLevelZero = true;
		bool        showBonusInDescription = true;
		bool        showXpBar = true;
		int         xpBarWidth = 20;
		// The characters the bar is drawn with. No square brackets by default: the magic menu's card removes "[...]" from
		// a description, so a bar written between brackets disappears.
		std::string xpBarFilled = "I";
		std::string xpBarEmpty = ".";
		std::string xpBarOpen;
		std::string xpBarClose;
		std::string nameFormat = "[Lv. {level}] {name}";

		// [Spells]
		std::set<int> excludedArchetypes;  // the game's effect archetype numbers

		// [Debug]
		bool verbose = false;
		int  iniVersion = 0;  // the IniVersion line of the file that was read; 0 = it has none
	};

	// The five schools of magic, in the order the settings use them.
	enum School : int
	{
		kAlteration = 0,
		kConjuration = 1,
		kDestruction = 2,
		kIllusion = 3,
		kRestoration = 4
	};

	// Lower-case archetype name -> the game's archetype number. The real table comes from the game's enum.
	using ArchetypeTable = std::map<std::string, int>;

	// The archetypes that are not boosted unless the ini says otherwise (names, as written in the ini).
	extern const char* const kDefaultExcludedArchetypes;
	extern const char* const kDefaultDurationSchools;
	extern const char* const kDefaultDurationExcludedArchetypes;

	// Settings with every default in place, including the default archetype exclusions.
	Settings defaults(const ArchetypeTable& a_archetypes, std::vector<std::string>* a_messages = nullptr);

	// Applies the lines of an ini to a_settings. Bad values keep the old setting and add a message. a_read counts the
	// lines that had a "key = value" shape.
	void parse_ini(const std::string& a_text, const ArchetypeTable& a_archetypes, Settings& a_settings, std::vector<std::string>& a_messages, int* a_read = nullptr);

	struct Progress
	{
		std::uint32_t level = 0;
		double        xp = 0.0;
	};

	// The level that counts: the stored level, held at LevelCap.
	std::uint32_t effective_level(const Settings& a_settings, std::uint32_t a_level);

	// XP to go from a_level to a_level + 1.
	double xp_needed(const Settings& a_settings, std::uint32_t a_level);

	// Multipliers for a spell at an effective level (1.0 at level 0).
	double cost_factor(const Settings& a_settings, std::uint32_t a_effectiveLevel);
	double magnitude_factor(const Settings& a_settings, std::uint32_t a_effectiveLevel);
	double duration_factor(const Settings& a_settings, std::uint32_t a_effectiveLevel);

	// The combat multiplier for a fight whose highest-level enemy is a_enemyLevel.
	double combat_multiplier(const Settings& a_settings, std::uint32_t a_enemyLevel);

	// XP worth for a cast of a spell with a given magicka cost: scaled by the cost, doubled by a dual-cast, multiplied in combat.
	double cost_xp_factor(const Settings& a_settings, double a_cost);
	double cast_xp(const Settings& a_settings, double a_costFactor, bool a_dual, double a_combatMultiplier);
	double concentration_xp(const Settings& a_settings, double a_costFactor, double a_seconds, double a_combatMultiplier);

	// Adds a_rawXp (before XpSpeed) and levels up as far as the XP goes and the cap allows. Returns the levels gained.
	std::uint32_t add_xp(const Settings& a_settings, Progress& a_progress, double a_rawXp);

	// "[Lv. 12] Fireball", or the plain name at level 0 when ShowLevelZero is off.
	std::string display_name(const Settings& a_settings, const std::string& a_base, std::uint32_t a_effectiveLevel);

	// "XP to Lv. 2  [||||||||||..........]  12 / 32", or the cap notice.
	std::string xp_line(const Settings& a_settings, const Progress& a_progress);

	// Whether going from a_before to a_after earns a level-up message.
	bool should_toast(const Settings& a_settings, std::uint32_t a_before, std::uint32_t a_after);
}
