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

#include <cstdint>

// The engine hooks and the cast listener: where XP comes from and where the bonuses are applied.
namespace hooks
{
	// Patches the engine's spell cost / effect / menu text / caster-update functions and starts listening for casts.
	void install();

	// What install() managed to patch, for the self test and the log.
	struct Status
	{
		std::size_t costSites = 0;  // call sites of the game's cost function pointed at the wrapper
		bool        descriptionHooked = false;
		bool        menuDescriptionHooked = false;  // the visitor the magic menu's item card uses
		std::size_t startClasses = 0;               // effect classes whose Start is watched
		bool        adjustHooked = false;
		bool        updateHooked = false;
	};
	const Status& status();

	// Self test only.
	double        debug_combat_multiplier(std::uint16_t* a_enemyLevel);             // the XP multiplier for the fight the player is in (1 outside combat)
	std::uint64_t cost_changes();                                                  // times the wrapper changed a player's spell cost
	float         debug_cost(const RE::SpellItem* a_spell, RE::Actor* a_actor);    // the cost wrapper, called directly
	void          debug_scale(RE::ActiveEffect* a_effect, const RE::Actor* a_caster);  // the magnitude hook's logic, on a made-up effect
	void          debug_cast(RE::SpellItem* a_spell, bool a_dual);                 // a cast event for the player, as the game would send it
}
