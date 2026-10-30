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
#include "config.h"

#include <fstream>
#include <iterator>

namespace config
{
	namespace
	{
		Settings g_settings;

		using Id = RE::EffectArchetypes::ArchetypeID;

		// The game's effect archetypes by the name used in the ini (lower case).
		const core::ArchetypeTable& archetype_names()
		{
			static const core::ArchetypeTable names = {
				{ "valuemodifier", static_cast<int>(Id::kValueModifier) }, { "script", static_cast<int>(Id::kScript) },
				{ "dispel", static_cast<int>(Id::kDispel) }, { "curedisease", static_cast<int>(Id::kCureDisease) },
				{ "absorb", static_cast<int>(Id::kAbsorb) }, { "dualvaluemodifier", static_cast<int>(Id::kDualValueModifier) },
				{ "calm", static_cast<int>(Id::kCalm) }, { "demoralize", static_cast<int>(Id::kDemoralize) },
				{ "frenzy", static_cast<int>(Id::kFrenzy) }, { "disarm", static_cast<int>(Id::kDisarm) },
				{ "commandsummoned", static_cast<int>(Id::kCommandSummoned) }, { "invisibility", static_cast<int>(Id::kInvisibility) },
				{ "light", static_cast<int>(Id::kLight) }, { "darkness", static_cast<int>(Id::kDarkness) },
				{ "nighteye", static_cast<int>(Id::kNightEye) }, { "lock", static_cast<int>(Id::kLock) },
				{ "open", static_cast<int>(Id::kOpen) }, { "boundweapon", static_cast<int>(Id::kBoundWeapon) },
				{ "summoncreature", static_cast<int>(Id::kSummonCreature) }, { "detectlife", static_cast<int>(Id::kDetectLife) },
				{ "telekinesis", static_cast<int>(Id::kTelekinesis) }, { "paralysis", static_cast<int>(Id::kParalysis) },
				{ "reanimate", static_cast<int>(Id::kReanimate) }, { "soultrap", static_cast<int>(Id::kSoulTrap) },
				{ "turnundead", static_cast<int>(Id::kTurnUndead) }, { "guide", static_cast<int>(Id::kGuide) },
				{ "werewolffeed", static_cast<int>(Id::kWerewolfFeed) }, { "cureparalysis", static_cast<int>(Id::kCureParalysis) },
				{ "cureaddiction", static_cast<int>(Id::kCureAddiction) }, { "curepoison", static_cast<int>(Id::kCurePoison) },
				{ "concussion", static_cast<int>(Id::kConcussion) }, { "valueandparts", static_cast<int>(Id::kValueAndParts) },
				{ "accumulatemagnitude", static_cast<int>(Id::kAccumulateMagnitude) }, { "stagger", static_cast<int>(Id::kStagger) },
				{ "peakvaluemodifier", static_cast<int>(Id::kPeakValueModifier) }, { "cloak", static_cast<int>(Id::kCloak) },
				{ "werewolf", static_cast<int>(Id::kWerewolf) }, { "slowtime", static_cast<int>(Id::kSlowTime) },
				{ "rally", static_cast<int>(Id::kRally) }, { "enhanceweapon", static_cast<int>(Id::kEnhanceWeapon) },
				{ "spawnhazard", static_cast<int>(Id::kSpawnHazard) }, { "etherealize", static_cast<int>(Id::kEtherealize) },
				{ "banish", static_cast<int>(Id::kBanish) }, { "spawnscriptedref", static_cast<int>(Id::kSpawnScriptedRef) },
				{ "disguise", static_cast<int>(Id::kDisguise) }, { "grabactor", static_cast<int>(Id::kGrabActor) },
				{ "vampirelord", static_cast<int>(Id::kVampireLord) }
			};
			return names;
		}

		void log_messages(const std::vector<std::string>& a_messages)
		{
			for (const auto& message : a_messages) {
				logs::warn("ini: {}", message);
			}
		}
	}

	const Settings& get()
	{
		return g_settings;
	}

	Settings& debug_settings()
	{
		return g_settings;
	}

	void load(const char* a_path)
	{
		std::vector<std::string> messages;
		g_settings = core::defaults(archetype_names(), &messages);

		std::ifstream file(a_path, std::ios::binary);
		if (!file) {
			log_messages(messages);
			logs::info("ini: {} not found, using the defaults", a_path);
			return;
		}
		const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		int               read = 0;
		core::parse_ini(text, archetype_names(), g_settings, messages, &read);
		log_messages(messages);
		logs::info("ini: {} setting(s) read from {}", read, a_path);
	}
}
