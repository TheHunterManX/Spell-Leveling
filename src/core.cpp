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
#include "core.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <sstream>

// In the plugin build the game's headers come first and pull in windows.h, whose min/max macros break std::min / std::max.
#ifdef min
#	undef min
#endif
#ifdef max
#	undef max
#endif

namespace core
{
	const char* const kDefaultExcludedArchetypes =
		"Paralysis, Invisibility, Light, Darkness, NightEye, DetectLife, Guide, Disguise, "
		"SoulTrap, Etherealize, Dispel, CureDisease, CureParalysis, CureAddiction, CurePoison, Disarm, SlowTime, "
		"Stagger, WerewolfFeed, Werewolf, VampireLord";

	const char* const kDefaultDurationSchools = "Alteration, Conjuration, Destruction, Illusion, Restoration";
	const char* const kDefaultDurationExcludedArchetypes = "Paralysis, Calm, Frenzy, Demoralize, Rally";

	namespace
	{
		std::string lower(std::string a_text)
		{
			std::transform(a_text.begin(), a_text.end(), a_text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_text;
		}

		std::string trim(const std::string& a_text)
		{
			const auto first = a_text.find_first_not_of(" \t\r\n");
			if (first == std::string::npos) {
				return {};
			}
			const auto last = a_text.find_last_not_of(" \t\r\n");
			return a_text.substr(first, last - first + 1);
		}

		bool parse_double(const std::string& a_text, double& a_out)
		{
			try {
				std::size_t used = 0;
				const double value = std::stod(a_text, &used);
				if (used == 0 || !std::isfinite(value)) {
					return false;
				}
				a_out = value;
				return true;
			} catch (...) {
				return false;
			}
		}

		bool parse_bool(const std::string& a_text, bool& a_out)
		{
			const auto v = lower(a_text);
			if (v == "true" || v == "1" || v == "yes" || v == "on") {
				a_out = true;
				return true;
			}
			if (v == "false" || v == "0" || v == "no" || v == "off") {
				a_out = false;
				return true;
			}
			return false;
		}

		void replace_all(std::string& a_text, const std::string& a_from, const std::string& a_to)
		{
			for (std::size_t at = a_text.find(a_from); at != std::string::npos; at = a_text.find(a_from, at + a_to.size())) {
				a_text.replace(at, a_from.size(), a_to);
			}
		}

		void parse_archetype_set(const std::string& a_list, const ArchetypeTable& a_table, std::set<int>& a_out, std::vector<std::string>& a_messages)
		{
			a_out.clear();
			std::size_t start = 0;
			while (start <= a_list.size()) {
				auto end = a_list.find(',', start);
				if (end == std::string::npos) {
					end = a_list.size();
				}
				const auto name = lower(trim(a_list.substr(start, end - start)));
				if (!name.empty()) {
					const auto it = a_table.find(name);
					if (it != a_table.end()) {
						a_out.insert(it->second);
					} else {
						a_messages.push_back(std::format("unknown effect archetype '{}' in ExcludedArchetypes, ignored", name));
					}
				}
				start = end + 1;
			}
		}

		void parse_archetypes(const std::string& a_list, const ArchetypeTable& a_table, Settings& a_settings, std::vector<std::string>& a_messages)
		{
			parse_archetype_set(a_list, a_table, a_settings.excludedArchetypes, a_messages);
		}

		void parse_schools(const std::string& a_list, std::set<int>& a_out, std::vector<std::string>& a_messages)
		{
			static const std::map<std::string, int> names = {
				{ "alteration", kAlteration }, { "conjuration", kConjuration }, { "destruction", kDestruction },
				{ "illusion", kIllusion }, { "restoration", kRestoration }
			};
			a_out.clear();
			std::size_t start = 0;
			while (start <= a_list.size()) {
				auto end = a_list.find(',', start);
				if (end == std::string::npos) {
					end = a_list.size();
				}
				const auto name = lower(trim(a_list.substr(start, end - start)));
				if (!name.empty()) {
					const auto it = names.find(name);
					if (it != names.end()) {
						a_out.insert(it->second);
					} else {
						a_messages.push_back(std::format("unknown school '{}' in DurationSchools, ignored", name));
					}
				}
				start = end + 1;
			}
		}

		void set_double(const char* a_key, const std::string& a_value, double& a_target, double a_min, double a_max, std::vector<std::string>& a_messages)
		{
			double parsed = 0.0;
			if (!parse_double(a_value, parsed)) {
				a_messages.push_back(std::format("{} = '{}' is not a number, keeping {}", a_key, a_value, a_target));
				return;
			}
			a_target = std::clamp(parsed, a_min, a_max);
			if (a_target != parsed) {
				a_messages.push_back(std::format("{} = {} is outside {}..{}, using {}", a_key, parsed, a_min, a_max, a_target));
			}
		}

		void set_int(const char* a_key, const std::string& a_value, int& a_target, int a_min, int a_max, std::vector<std::string>& a_messages)
		{
			double parsed = 0.0;
			if (!parse_double(a_value, parsed)) {
				a_messages.push_back(std::format("{} = '{}' is not a number, keeping {}", a_key, a_value, a_target));
				return;
			}
			// Clamp before converting: a huge value must not overflow the int.
			a_target = static_cast<int>(std::clamp(parsed, static_cast<double>(a_min), static_cast<double>(a_max)));
		}

		void set_bool(const char* a_key, const std::string& a_value, bool& a_target, std::vector<std::string>& a_messages)
		{
			if (!parse_bool(a_value, a_target)) {
				a_messages.push_back(std::format("{} = '{}' is not true or false, keeping {}", a_key, a_value, a_target));
			}
		}

		// A piece of the XP bar: up to 4 plain characters, and none the game's text code reacts to.
		void set_bar_text(const char* a_key, const std::string& a_value, std::string& a_target, bool a_allowEmpty, std::vector<std::string>& a_messages)
		{
			bool ok = (a_allowEmpty || !a_value.empty()) && a_value.size() <= 4;
			for (const unsigned char c : a_value) {
				ok = ok && c >= 0x20 && c <= 0x7E && c != '<' && c != '>' && c != '%';
			}
			if (!ok) {
				a_messages.push_back(std::format("{} = '{}' must be 1 to 4 plain characters without < > or %, keeping '{}'", a_key, a_value, a_target));
				return;
			}
			a_target = a_value;
		}

		void apply(const std::string& a_key, const std::string& a_value, const ArchetypeTable& a_table, Settings& s, std::vector<std::string>& m)
		{
			if (a_key == "xpspeed") set_double("XpSpeed", a_value, s.xpSpeed, 0.0, 1000.0, m);
			else if (a_key == "xpbase") set_double("XpBase", a_value, s.xpBase, 0.01, 1.0e9, m);
			else if (a_key == "xpscale") set_double("XpScale", a_value, s.xpScale, 0.0, 1.0e9, m);
			else if (a_key == "xppower") set_double("XpPower", a_value, s.xpPower, 0.0, 6.0, m);
			else if (a_key == "dualcastmultiplier") set_double("DualCastMultiplier", a_value, s.dualCastMultiplier, 0.0, 100.0, m);
			else if (a_key == "referencecost") set_double("ReferenceCost", a_value, s.referenceCost, 1.0, 1.0e6, m);
			else if (a_key == "mincostfactor") set_double("MinCostFactor", a_value, s.minCostFactor, 0.0, 1000.0, m);
			else if (a_key == "maxcostfactor") set_double("MaxCostFactor", a_value, s.maxCostFactor, 0.0, 1000.0, m);
			else if (a_key == "concentrationxppersecond") set_double("ConcentrationXpPerSecond", a_value, s.concentrationXpPerSecond, 0.0, 1000.0, m);
			else if (a_key == "samespellcooldown") set_double("SameSpellCooldown", a_value, s.sameSpellCooldown, 0.0, 60.0, m);
			else if (a_key == "combatxpminmultiplier") set_double("CombatXpMinMultiplier", a_value, s.combatXpMinMultiplier, 0.0, 100.0, m);
			else if (a_key == "combatxpmaxmultiplier") set_double("CombatXpMaxMultiplier", a_value, s.combatXpMaxMultiplier, 0.0, 100.0, m);
			else if (a_key == "combatxpminlevel") set_int("CombatXpMinLevel", a_value, s.combatXpMinLevel, 0, 1000000, m);
			else if (a_key == "combatxpmaxlevel") set_int("CombatXpMaxLevel", a_value, s.combatXpMaxLevel, 0, 1000000, m);
			else if (a_key == "levelcap") set_int("LevelCap", a_value, s.levelCap, 0, 1000000, m);
			else if (a_key == "magnitudeperlevel") set_double("MagnitudePerLevel", a_value, s.magnitudePerLevel, -10.0, 10.0, m);
			else if (a_key == "durationperlevel") set_double("DurationPerLevel", a_value, s.durationPerLevel, 0.0, 10.0, m);
			else if (a_key == "maxdurationmultiplier") set_double("MaxDurationMultiplier", a_value, s.maxDurationMultiplier, 0.0, 1.0e6, m);
			else if (a_key == "durationminseconds") set_int("DurationMinSeconds", a_value, s.durationMinSeconds, 0, 1000000, m);
			else if (a_key == "durationschools") parse_schools(a_value, s.durationSchools, m);
			else if (a_key == "durationexcludedarchetypes") parse_archetype_set(a_value, a_table, s.durationExcludedArchetypes, m);
			else if (a_key == "costperlevel") set_double("CostPerLevel", a_value, s.costPerLevel, -10.0, 10.0, m);
			else if (a_key == "costfloor") set_double("CostFloor", a_value, s.costFloor, 0.0, 1.0, m);
			else if (a_key == "maxmagnitudemultiplier") set_double("MaxMagnitudeMultiplier", a_value, s.maxMagnitudeMultiplier, 0.0, 1.0e6, m);
			else if (a_key == "toastenabled") set_bool("ToastEnabled", a_value, s.toastEnabled, m);
			else if (a_key == "toasteverylevels") set_int("ToastEveryLevels", a_value, s.toastEveryLevels, 1, 1000000, m);
			else if (a_key == "showlevelzero") set_bool("ShowLevelZero", a_value, s.showLevelZero, m);
			else if (a_key == "showbonusindescription") set_bool("ShowBonusInDescription", a_value, s.showBonusInDescription, m);
			else if (a_key == "showxpbar") set_bool("ShowXpBar", a_value, s.showXpBar, m);
			else if (a_key == "xpbarwidth") set_int("XpBarWidth", a_value, s.xpBarWidth, 5, 60, m);
			else if (a_key == "xpbarfilled") set_bar_text("XpBarFilled", a_value, s.xpBarFilled, false, m);
			else if (a_key == "xpbarempty") set_bar_text("XpBarEmpty", a_value, s.xpBarEmpty, false, m);
			else if (a_key == "xpbaropen") set_bar_text("XpBarOpen", a_value, s.xpBarOpen, true, m);
			else if (a_key == "xpbarclose") set_bar_text("XpBarClose", a_value, s.xpBarClose, true, m);
			else if (a_key == "nameformat") {
				if (a_value.find("{name}") == std::string::npos) {
					m.push_back(std::format("NameFormat '{}' has no {{name}}, keeping '{}'", a_value, s.nameFormat));
				} else {
					s.nameFormat = a_value;
				}
			} else if (a_key == "excludedarchetypes") parse_archetypes(a_value, a_table, s, m);
			else if (a_key == "iniversion") set_int("IniVersion", a_value, s.iniVersion, 0, 1000000, m);
			else if (a_key == "verboselog") set_bool("VerboseLog", a_value, s.verbose, m);
			else m.push_back(std::format("unknown setting '{}', ignored", a_key));
		}
	}

	Settings defaults(const ArchetypeTable& a_archetypes, std::vector<std::string>* a_messages)
	{
		Settings            settings;
		std::vector<std::string> local;
		auto& messages = a_messages ? *a_messages : local;
		parse_archetypes(kDefaultExcludedArchetypes, a_archetypes, settings, messages);
		parse_schools(kDefaultDurationSchools, settings.durationSchools, messages);
		parse_archetype_set(kDefaultDurationExcludedArchetypes, a_archetypes, settings.durationExcludedArchetypes, messages);
		return settings;
	}

	void parse_ini(const std::string& a_text, const ArchetypeTable& a_archetypes, Settings& a_settings, std::vector<std::string>& a_messages, int* a_read)
	{
		// Notepad and some editors put a UTF-8 byte order mark first; left in, it would glue itself to the first key.
		const bool         hasBom = a_text.size() >= 3 && a_text.compare(0, 3, "\xEF\xBB\xBF") == 0;
		std::istringstream in(hasBom ? a_text.substr(3) : a_text);
		std::string        line;
		int                read = 0;
		while (std::getline(in, line)) {
			line = trim(line);
			// Only whole-line comments: a trailing ';' could be part of NameFormat.
			if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[') {
				continue;
			}
			const auto eq = line.find('=');
			if (eq == std::string::npos) {
				continue;
			}
			apply(lower(trim(line.substr(0, eq))), trim(line.substr(eq + 1)), a_archetypes, a_settings, a_messages);
			++read;
		}
		if (a_read) {
			*a_read = read;
		}
	}

	std::uint32_t effective_level(const Settings& a_settings, std::uint32_t a_level)
	{
		return a_settings.levelCap > 0 ? std::min(a_level, static_cast<std::uint32_t>(a_settings.levelCap)) : a_level;
	}

	double xp_needed(const Settings& a_settings, std::uint32_t a_level)
	{
		return a_settings.xpBase + a_settings.xpScale * std::pow(static_cast<double>(a_level) + 1.0, a_settings.xpPower);
	}

	double cost_factor(const Settings& a_settings, std::uint32_t a_effectiveLevel)
	{
		if (a_effectiveLevel == 0) {
			return 1.0;
		}
		return std::max({ 1.0 - static_cast<double>(a_effectiveLevel) * a_settings.costPerLevel, a_settings.costFloor, 0.0 });
	}

	double magnitude_factor(const Settings& a_settings, std::uint32_t a_effectiveLevel)
	{
		if (a_effectiveLevel == 0) {
			return 1.0;
		}
		double factor = 1.0 + static_cast<double>(a_effectiveLevel) * a_settings.magnitudePerLevel;
		if (a_settings.maxMagnitudeMultiplier > 0.0) {
			factor = std::min(factor, std::max(a_settings.maxMagnitudeMultiplier, 1.0));
		}
		return std::max(factor, 0.0);
	}

	double cost_xp_factor(const Settings& a_settings, double a_cost)
	{
		return std::clamp(a_cost / a_settings.referenceCost, a_settings.minCostFactor, std::max(a_settings.minCostFactor, a_settings.maxCostFactor));
	}

	double combat_multiplier(const Settings& a_settings, std::uint32_t a_enemyLevel)
	{
		const double low = a_settings.combatXpMinLevel;
		const double high = a_settings.combatXpMaxLevel;
		double       t = 0.0;
		if (high > low) {
			t = std::clamp((static_cast<double>(a_enemyLevel) - low) / (high - low), 0.0, 1.0);
		} else {
			t = static_cast<double>(a_enemyLevel) >= high ? 1.0 : 0.0;  // a single step when the two levels are equal or crossed
		}
		return a_settings.combatXpMinMultiplier + (a_settings.combatXpMaxMultiplier - a_settings.combatXpMinMultiplier) * t;
	}

	double cast_xp(const Settings& a_settings, double a_costFactor, bool a_dual, double a_combatMultiplier)
	{
		return a_costFactor * (a_dual ? a_settings.dualCastMultiplier : 1.0) * a_combatMultiplier;
	}

	double concentration_xp(const Settings& a_settings, double a_costFactor, double a_seconds, double a_combatMultiplier)
	{
		return a_settings.concentrationXpPerSecond * a_costFactor * a_seconds * a_combatMultiplier;
	}

	double duration_factor(const Settings& a_settings, std::uint32_t a_effectiveLevel)
	{
		if (a_effectiveLevel == 0) {
			return 1.0;
		}
		double factor = 1.0 + static_cast<double>(a_effectiveLevel) * a_settings.durationPerLevel;
		if (a_settings.maxDurationMultiplier > 0.0) {
			factor = std::min(factor, std::max(a_settings.maxDurationMultiplier, 1.0));
		}
		return factor;
	}

	std::uint32_t add_xp(const Settings& a_settings, Progress& a_progress, double a_rawXp)
	{
		if (!(a_rawXp > 0.0)) {
			return 0;
		}
		const std::uint32_t cap = a_settings.levelCap > 0 ? static_cast<std::uint32_t>(a_settings.levelCap) : 0xFFFFFFFFu;
		if (a_progress.level >= cap) {
			return 0;  // at the cap: no XP piles up for later
		}
		const auto before = a_progress.level;
		a_progress.xp += a_rawXp * a_settings.xpSpeed;
		for (int guard = 0; guard < 10000 && a_progress.level < cap; ++guard) {
			const double need = xp_needed(a_settings, a_progress.level);
			if (a_progress.xp < need) {
				break;
			}
			a_progress.xp -= need;
			++a_progress.level;
		}
		return a_progress.level - before;
	}

	std::string display_name(const Settings& a_settings, const std::string& a_base, std::uint32_t a_effectiveLevel)
	{
		if (a_effectiveLevel == 0 && !a_settings.showLevelZero) {
			return a_base;
		}
		std::string out = a_settings.nameFormat;
		replace_all(out, "{level}", std::to_string(a_effectiveLevel));
		replace_all(out, "{name}", a_base);
		return out;
	}

	std::string xp_line(const Settings& a_settings, const Progress& a_progress)
	{
		if (!a_settings.showXpBar) {
			return {};
		}
		const auto level = effective_level(a_settings, a_progress.level);
		if (a_settings.levelCap > 0 && level >= static_cast<std::uint32_t>(a_settings.levelCap)) {
			return std::format("Level cap reached (Lv. {})", level);
		}
		const double need = xp_needed(a_settings, level);
		const double have = std::clamp(a_progress.xp, 0.0, need);
		const int    width = std::clamp(a_settings.xpBarWidth, 5, 60);
		const int    filled = std::clamp(static_cast<int>(std::floor(have / need * width)), 0, width);
		std::string  bar = a_settings.xpBarOpen;
		for (int i = 0; i < width; ++i) {
			bar += i < filled ? a_settings.xpBarFilled : a_settings.xpBarEmpty;
		}
		bar += a_settings.xpBarClose;
		// Only plain characters, and no '<' '>' or '%': the game runs this text through its own tag and format code.
		return std::format("XP to Lv. {}  {}  {} / {}", level + 1, bar, static_cast<long long>(std::floor(have)), static_cast<long long>(std::ceil(need)));
	}

	bool should_toast(const Settings& a_settings, std::uint32_t a_before, std::uint32_t a_after)
	{
		if (!a_settings.toastEnabled || a_after == a_before) {
			return false;
		}
		const auto every = static_cast<std::uint32_t>(std::max(a_settings.toastEveryLevels, 1));
		return a_after / every > a_before / every;
	}
}
