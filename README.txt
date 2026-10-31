SPELL LEVELING  (SKSE plugin for Skyrim Special Edition)
Version 1.1

The mod makes each spell now have its own level and gains experience every time you cast it.
Use a spell enough and it'll increase magnitude and decrease spell cost.

Main Features
	- Per-spell leveling: Every spell earns XP when you cast it, and each level-up makes that spell stronger.
	XP scales from 2x gain during combat to 4x against level 100 enemies.
	- Progression: Higher levels increases magnitude, reduces magicka cost, and increases spell time.
	Default setting is 2% per level as well as 0.25% cost reduction per level.
	- Level-up notifications: A short on-screen message tells you when a spell levels up.
	- Fully configurable: An ini file lets you set the XP rate, level cap, and scaling.
	- Works with any spell: Vanilla spells and spells added by other mods are *supported, with no patches needed.
	- Light on scripts: Built to avoid noticeable performance impact.
	
*tested with Apocolypse, Mysticism, Odin, Triumvirate

Requirements
	- Skyrim Special Edition / Anniversary Edition
	- SKSE64: Skyrim Script Extender
	- Address Library for SKSE Plugins
	- SkyUI is not needed

Installation
	Install the required mods above.
	Download the mod with your mod manager (Mod Organizer 2 or Vortex) and install it or download it and place the dll and ini in Data\SKSE\Plugins
	Launch the game.
	
Uninstalling
	Remove the dll from SKSE plugin folder, or disable it in Vortex


LICENSE
  Spell Leveling
  Copyright (C) 2026 TheHunterManX

  This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
  License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any
  later version.

  This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
  warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
  details.

  The full text of the license is in LICENSE.txt next to this file (also https://www.gnu.org/licenses/gpl-3.0.html).

SOURCE CODE
  The complete source code of this plugin, with the scripts to build it, is found on Github.
  https://github.com/TheHunterManX/Spell-Leveling

BUILT ON
  CommonLibSSE-NG, https://github.com/alandtse/CommonLibSSE-NG (GPL-3.0-or-later with the Modding Exception and the
  GPL-3.0 Linking Exception), and the libraries listed with their licenses in THIRD-PARTY-NOTICES.txt, next to this file (spdlog, fmt,
  DirectXTK, DirectXMath, rapidcsv, hde64). Thanks to their authors, and to the SKSE team.
