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

// A check of the plugin's hooks against the real game, switched on by a file next to the plugin:
//   Data\SKSE\Plugins\SpellLeveling.selftest
// It writes "selftest:" lines to SpellLeveling.log and changes nothing that is saved. Without the file it does nothing.
namespace selftest
{
	// Looks for the switch file; call once, at plugin load.
	bool enabled();

	// What enabled() found.
	bool is_on();

	// At the main menu: the hooks are installed, and the description hook gives the right text for every spell.
	void run_menu();

	// After a save is loaded: the cost hook with the real player, and the game's own cost path.
	void run_loaded();
}
