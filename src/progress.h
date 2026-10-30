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

#include <vector>

// Every spell's level and XP, the level maths, the "[Lv. x]" names and the SKSE cosave.
namespace progress
{
	// Finds every levelable spell (vanilla and modded) and remembers its original name.
	void on_data_loaded();

	// Registers the cosave callbacks. Call once from the plugin load.
	void install_serialization();

	// Rewrites every spell's name from its current level (after a load, a new game, a revert).
	void refresh_names();

	// True for spells that take part: castable spells (not powers, shouts, scrolls, enchantments).
	bool tracked(const RE::SpellItem* a_spell);

	// Level after the LevelCap, 0 for a spell that is not tracked.
	std::uint32_t level_of(const RE::SpellItem* a_spell);

	// Multiplier on the magicka cost / on effect magnitudes for this spell (1.0 at level 0).
	double cost_factor(const RE::SpellItem* a_spell);
	double magnitude_factor(const RE::SpellItem* a_spell);
	double duration_factor(const RE::SpellItem* a_spell);  // 1.0 unless the spell is of a school in DurationSchools

	// Adds XP (before XpSpeed), levels the spell up, renames it and shows the message.
	void add_xp(RE::SpellItem* a_spell, double a_xp);

	// The tracked spell whose effect list holds this effect, or null (an effect of a potion, a staff, an untracked spell...).
	const RE::SpellItem* spell_of_effect(const RE::Effect* a_effect);

	// One line for the spell's description, drawn from plain characters so the game's text code leaves it alone:
	//   XP to Lv. 2  [||||||||||..........]  12 / 32
	// Empty when the spell is not tracked or the bar is turned off.
	std::string xp_line(const RE::SpellItem* a_spell);

	// For the self test only: sets a spell's level and XP directly (and its name). False if the spell is not tracked.
	bool debug_set(const RE::SpellItem* a_spell, std::uint32_t a_level, double a_xp);
	bool debug_get(const RE::SpellItem* a_spell, std::uint32_t& a_level, double& a_xp);

	// Some tracked spells, for the self test.
	std::vector<RE::SpellItem*> sample_spells(std::size_t a_max);
}
