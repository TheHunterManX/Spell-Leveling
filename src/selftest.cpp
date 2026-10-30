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
#include "selftest.h"

#include "config.h"
#include "hooks.h"
#include "progress.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#undef min
#undef max

namespace selftest
{
	namespace
	{
		constexpr auto kSwitchFile = "Data/SKSE/Plugins/SpellLeveling.selftest";
		constexpr RE::FormID kFlames = 0x00012FCD;

		bool g_enabled = false;
		int  g_pass = 0;
		int  g_fail = 0;
		int  g_note = 0;

		void check(bool a_ok, const std::string& a_what)
		{
			if (a_ok) {
				++g_pass;
				logs::info("selftest: PASS {}", a_what);
			} else {
				++g_fail;
				logs::error("selftest: FAIL {}", a_what);
			}
		}

		void note(const std::string& a_what)
		{
			++g_note;
			logs::info("selftest: note {}", a_what);
		}

		// The text the game builds for a spell's effects, through the game's own function and so through the hook.
		std::string describe(RE::SpellItem* a_spell)
		{
			RE::BSString out;
			RE::MagicSystem::GetMagicItemDescription(out, a_spell, "", "");
			const char* text = out.c_str();
			return text ? std::string(text) : std::string{};
		}

		// The first whole number in the text, or -1.
		long long first_number(const std::string& a_text)
		{
			for (std::size_t i = 0; i < a_text.size(); ++i) {
				if (a_text[i] >= '0' && a_text[i] <= '9') {
					std::size_t end = i;
					while (end < a_text.size() && a_text[end] >= '0' && a_text[end] <= '9' && end - i < 12) {
						++end;
					}
					return std::stoll(a_text.substr(i, end - i));
				}
			}
			return -1;
		}

		constexpr const char* kXpMarker = "\nXP to Lv. ";

		// The text without the XP line the plugin adds.
		std::string body(const std::string& a_text)
		{
			const auto at = a_text.find(kXpMarker);
			return at == std::string::npos ? a_text : a_text.substr(0, at);
		}

		std::size_t count_of(const std::string& a_text, const char* a_needle)
		{
			std::size_t n = 0;
			for (auto at = a_text.find(a_needle); at != std::string::npos; at = a_text.find(a_needle, at + 1)) {
				++n;
			}
			return n;
		}

		// What the description hook is allowed to touch for a moment, recorded so it can be compared afterwards.
		struct EffectState
		{
			RE::Effect* effect = nullptr;
			float       magnitude = 0.0f;
			std::string text;
		};

		std::vector<EffectState> snapshot(const RE::SpellItem* a_spell)
		{
			std::vector<EffectState> out;
			for (auto* effect : a_spell->effects) {
				if (effect) {
					EffectState s;
					s.effect = effect;
					s.magnitude = effect->effectItem.magnitude;
					if (effect->baseEffect && effect->baseEffect->magicItemDescription.c_str()) {
						s.text = effect->baseEffect->magicItemDescription.c_str();
					}
					out.push_back(std::move(s));
				}
			}
			return out;
		}

		bool unchanged(const std::vector<EffectState>& a_before, const RE::SpellItem* a_spell)
		{
			const auto after = snapshot(a_spell);
			if (after.size() != a_before.size()) {
				return false;
			}
			for (std::size_t i = 0; i < after.size(); ++i) {
				if (std::memcmp(&after[i].magnitude, &a_before[i].magnitude, sizeof(float)) != 0 || after[i].text != a_before[i].text) {
					return false;
				}
			}
			return true;
		}

		bool has_effect_text(const RE::SpellItem* a_spell)
		{
			for (const auto* effect : a_spell->effects) {
				if (effect && effect->baseEffect && effect->baseEffect->magicItemDescription.c_str() && *effect->baseEffect->magicItemDescription.c_str()) {
					return true;
				}
			}
			return false;
		}

		void finish(const char* a_stage)
		{
			logs::info("selftest: {} finished: {} passed, {} failed, {} notes", a_stage, g_pass, g_fail, g_note);
		}
	}

	bool enabled()
	{
		g_enabled = std::filesystem::exists(kSwitchFile);
		if (g_enabled) {
			logs::info("selftest: switched on (SpellLeveling.selftest found)");
		}
		return g_enabled;
	}

	bool is_on()
	{
		return g_enabled;
	}

	void run_menu()
	{
		if (!g_enabled) {
			return;
		}
		g_pass = g_fail = g_note = 0;
		logs::info("selftest: menu stage starting");

		// The self test must not show messages or depend on the user's ini.
		auto       saved = config::get();
		auto&      cfg = config::debug_settings();
		cfg.toastEnabled = false;
		cfg.showXpBar = true;
		cfg.showBonusInDescription = true;
		cfg.showLevelZero = true;
		cfg.nameFormat = "[Lv. {level}] {name}";
		cfg.levelCap = 0;
		cfg.magnitudePerLevel = 0.02;
		cfg.costPerLevel = 0.0025;
		cfg.maxMagnitudeMultiplier = 0.0;
		cfg.durationPerLevel = 0.02;
		cfg.maxDurationMultiplier = 0.0;
		cfg.durationSchools = { core::kAlteration, core::kConjuration };
		cfg.costFloor = 0.0;
		cfg.xpSpeed = 1.0;
		cfg.xpBase = 3.0;
		cfg.xpScale = 0.25;
		cfg.xpPower = 1.5;
		cfg.xpBarWidth = 20;
		cfg.combatXpMinMultiplier = 2.0;
		cfg.combatXpMaxMultiplier = 4.0;
		cfg.combatXpMinLevel = 1;
		cfg.combatXpMaxLevel = 100;

		// --- the hooks went in -------------------------------------------------------------------------------------
		const auto& st = hooks::status();
		check(st.costSites > 0, std::format("cost: {} call site(s) of the game's cost function redirected (30 on the plain 1.7.104 exe)", st.costSites));
		if (st.costSites != 30) {
			note(std::format("cost: {} site(s) redirected, not 30: another plugin may have patched some of them already", st.costSites));
		}
		check(st.descriptionHooked, "description visitor hooked");
		check(st.menuDescriptionHooked, "the magic menu's description visitor hooked");
		check(st.adjustHooked && st.updateHooked, "caster adjust-effect and update hooked");
		check(st.startClasses == 7, std::format("effect Start watched on {} classes", st.startClasses));

		// --- Flames: the numbers ------------------------------------------------------------------------------------
		auto* flames = RE::TESForm::LookupByID<RE::SpellItem>(kFlames);
		check(flames != nullptr, "Flames found (form 00012FCD)");
		if (flames && progress::tracked(flames)) {
			progress::debug_set(flames, 0, 0.0);
			const auto text0 = describe(flames);
			logs::info("selftest: Flames at Lv.0 reads: {}", text0);
			const long long base = first_number(text0);
			check(base > 0, std::format("Flames description has a number ({})", base));
			check(count_of(text0, "XP to Lv. 1  ") == 1, "Flames at Lv.0 ends with the XP line for Lv.1");
			check(text0.find("XP to Lv. 1  ....................  0 / 4") != std::string::npos, "Flames XP line at 0 XP is an empty bar, 0 / 4");
			check(text0.find('[') == std::string::npos, "the XP line has no square brackets (the menu card removes them)");

			const auto before = snapshot(flames);
			if (base > 0) {
				for (std::uint32_t level : { 1u, 13u, 50u, 100u, 400u }) {
					progress::debug_set(flames, level, 0.0);
					const auto  text = describe(flames);
					const float expectedF = static_cast<float>(base) * static_cast<float>(1.0 + level * 0.02);
					const auto  expected = static_cast<long long>(expectedF);
					const auto  got = first_number(text);
					// The game shows the magnitude as a whole number, cut, from the float it stores (8.0 for Flames).
					check(got == expected, std::format("Flames at Lv.{}: shows {} (expected {})", level, got, expected));
					check(unchanged(before, flames), std::format("Flames at Lv.{}: effect numbers and texts are back to the originals afterwards", level));
				}
			}

			// the line is on its own, last, and only once; it has moved on at Lv.100
			progress::debug_set(flames, 100, 126.5);
			const auto text100 = describe(flames);
			check(text100.find("XP to Lv. 101  ") != std::string::npos, "Flames at Lv.100 says XP to Lv. 101");
			check(count_of(text100, "XP to Lv.") == 1, "the XP line appears once");
			const auto lineAt = text100.find(kXpMarker);
			check(lineAt != std::string::npos && text100.find('\n', lineAt + 1) == std::string::npos, "the XP line is the last line");
			check(text100.find('<') == std::string::npos && text100.find('>') == std::string::npos && text100.find('%') == std::string::npos,
				"the text has no tag or format characters left");

			// settings turn it off
			cfg.showXpBar = false;
			check(describe(flames).find("XP to Lv.") == std::string::npos, "ShowXpBar = false removes the line");
			cfg.showXpBar = true;
			cfg.showBonusInDescription = false;
			check(first_number(describe(flames)) == base, "ShowBonusInDescription = false shows the base number");
			cfg.showBonusInDescription = true;

			// at the cap the line says so
			cfg.levelCap = 100;
			check(describe(flames).find("Level cap reached (Lv. 100)") != std::string::npos, "at the level cap the line says so");
			cfg.levelCap = 0;

			// names
			progress::debug_set(flames, 12, 0.0);
			check(std::string(flames->GetName()) == "[Lv. 12] Flames", std::format("name at Lv.12 is '{}'", flames->GetName()));
			progress::debug_set(flames, 0, 0.0);
			check(std::string(flames->GetName()) == "[Lv. 0] Flames", std::format("name at Lv.0 is '{}'", flames->GetName()));

			// XP through the real add_xp: 1 cast-worth levels it, and renames it
			progress::add_xp(flames, 3.5);
			check(progress::level_of(flames) == 1 && std::string(flames->GetName()) == "[Lv. 1] Flames", std::format("3.5 XP makes Lv.1 and renames it ('{}')", flames->GetName()));
			progress::debug_set(flames, 0, 0.0);
		} else {
			note("Flames is not a tracked spell here; the Flames checks were skipped");
		}

		// --- every spell: the hook never leaves anything changed --------------------------------------------------
		const auto spells = progress::sample_spells(1000000);
		note(std::format("sweep over {} spells", spells.size()));
		{
			// which plugins the levelable spells come from (so it is clear that modded spells are in the sweep)
			std::map<std::string, int> perPlugin;
			for (auto* spell : spells) {
				const auto* file = spell->GetFile(0);
				++perPlugin[file ? std::string(file->GetFilename()) : std::string("(none)")];
			}
			std::string list;
			for (const auto& [name, count] : perPlugin) {
				list += std::format("{} {}; ", name, count);
			}
			note(std::format("spells per plugin ({} plugins): {}", perPlugin.size(), list));
			// The save keeps a spell as (plugin name, id inside that plugin). Every spell, from full and light plugins alike,
			// must be found again from that pair, or its levels would be lost or given to another spell after a load.
			int identityBad = 0;
			for (auto* spell : spells) {
				const auto* file = spell->GetFile(0);
				auto*       found = file ? RE::TESDataHandler::GetSingleton()->LookupForm<RE::SpellItem>(spell->GetLocalFormID(), file->GetFilename()) : nullptr;
				if (found != spell) {
					++identityBad;
					logs::error("selftest: {} ({} / {:#x}) is not found again from its saved identity", spell->GetName(), file ? file->GetFilename() : "?", spell->GetLocalFormID());
				}
			}
			check(identityBad == 0, std::format("saved identity: all {} spells (full and light plugins) are found again from plugin name + local id ({} not)", spells.size(), identityBad));
		}
		const auto start = std::chrono::steady_clock::now();
		int        withText = 0;
		int        lineMissing = 0;
		int        lineTwice = 0;
		int        notRestored = 0;
		int        numberChanged = 0;
		int        tagLeft = 0;
		int        index = 0;
		for (auto* spell : spells) {
			if (index % 100 == 0) {
				logs::info("selftest: sweep at spell {} of {} ({})", index, spells.size(), spell->GetName());
			}
			++index;
			const auto before = snapshot(spell);
			progress::debug_set(spell, 0, 0.0);
			const auto text0 = describe(spell);
			progress::debug_set(spell, 100, 40.0);
			const auto text100 = describe(spell);
			if (!unchanged(before, spell)) {
				++notRestored;
				logs::error("selftest: {} was left changed by the description hook", spell->GetName());
			}
			if (has_effect_text(spell)) {
				++withText;
				const auto lines = count_of(text100, "XP to Lv.");
				if (lines == 0) {
					++lineMissing;
					logs::warn("selftest: no XP line on {}: '{}'", spell->GetName(), text100);
				}
				if (lines > 1) {
					++lineTwice;
				}
				if (body(text0) != body(text100)) {
					++numberChanged;
				}
			}
			if (body(text100).find('<') != std::string::npos) {
				++tagLeft;
				logs::warn("selftest: a tag is left in the text of {}: '{}'", spell->GetName(), body(text100));
			}
			progress::debug_set(spell, 0, 0.0);
		}
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
		note(std::format("sweep took {} ms for {} spells, two descriptions each", ms, spells.size()));
		check(notRestored == 0, std::format("sweep: no spell left changed ({} did)", notRestored));
		check(lineTwice == 0, std::format("sweep: the XP line is never added twice ({} spells)", lineTwice));
		check(lineMissing == 0, std::format("sweep: every spell with effect text got its XP line ({} of {} missed)", lineMissing, withText));
		check(numberChanged > 100, std::format("sweep: levelling changed the shown text of {} of {} spells with text", numberChanged, withText));
		note(std::format("sweep: {} spells show effect text, {} still have a '<' in their text", withText, tagLeft));

		// --- the magnitude hook's rules, on made-up effects built from every real effect of every spell ---------------
		if (auto* pc = RE::PlayerCharacter::GetSingleton(); pc && pc->GetFormID() == 0x14) {
			std::vector<std::unique_ptr<std::byte[]>> keep;  // each made-up effect stays alive and at its own address
			auto make = [&](RE::MagicItem* a_item, RE::Effect* a_effect) {
				keep.push_back(std::make_unique<std::byte[]>(sizeof(RE::ActiveEffect)));
				auto* ae = reinterpret_cast<RE::ActiveEffect*>(keep.back().get());
				ae->spell = a_item;
				ae->effect = a_effect;
				ae->magnitude = 10.0f;
				ae->duration = 10.0f;
				return ae;
			};
			const auto av_is_protected = [](RE::ActorValue a_av) {
				switch (a_av) {
				case RE::ActorValue::kSpeedMult:
				case RE::ActorValue::kWeaponSpeedMult:
				case RE::ActorValue::kLeftWeaponSpeedMultiply:
				case RE::ActorValue::kAlterationModifier:
				case RE::ActorValue::kConjurationModifier:
				case RE::ActorValue::kDestructionModifier:
				case RE::ActorValue::kIllusionModifier:
				case RE::ActorValue::kRestorationModifier:
					return true;
				default:
					return false;
				}
			};

			int durationScaled = 0;
			int durationWrong = 0;
			int boosted = 0;
			int byArchetype = 0;
			int byAV = 0;
			int wrong = 0;
			for (auto* spell : spells) {
				progress::debug_set(spell, 100, 0.0);
				for (auto* effect : spell->effects) {
					if (!effect || !effect->baseEffect) {
						continue;
					}
					auto* ae = make(spell, effect);
					hooks::debug_scale(ae, pc);
					const auto* base = effect->baseEffect;
					const bool  excludedArchetype = cfg.excludedArchetypes.contains(static_cast<int>(base->GetArchetype()));
					const bool  excludedAV = av_is_protected(base->data.primaryAV);
					const bool  shouldChange = !excludedArchetype && !excludedAV;
					const bool  changed = ae->magnitude != 10.0f;
					// duration: scaled (x3 at Lv.100) for the schools in the settings, except the excluded archetypes
					int school = -1;
					switch (spell->GetAssociatedSkill()) {
					case RE::ActorValue::kAlteration: school = core::kAlteration; break;
					case RE::ActorValue::kConjuration: school = core::kConjuration; break;
					case RE::ActorValue::kDestruction: school = core::kDestruction; break;
					case RE::ActorValue::kIllusion: school = core::kIllusion; break;
					case RE::ActorValue::kRestoration: school = core::kRestoration; break;
					default: break;
					}
					const bool durationShould = cfg.durationSchools.contains(school) && !cfg.durationExcludedArchetypes.contains(static_cast<int>(base->GetArchetype()));
					const bool durationChanged = ae->duration != 10.0f;
					if (durationChanged != durationShould || (durationChanged && std::fabs(ae->duration - 30.0f) > 0.001f)) {
						++durationWrong;
						logs::error("selftest: {} / effect {:#x}: duration 10 became {} (should scale: {})", spell->GetName(), base->GetFormID(), ae->duration, durationShould);
					} else if (durationChanged) {
						++durationScaled;
					}
					if (changed != shouldChange || (changed && std::fabs(ae->magnitude - 30.0f) > 0.001f)) {
						++wrong;
						logs::error("selftest: {} / effect {:#x}: magnitude 10 became {} (archetype excluded: {}, stat protected: {})", spell->GetName(),
							base->GetFormID(), ae->magnitude, excludedArchetype, excludedAV);
					} else if (changed) {
						++boosted;
					} else if (excludedArchetype) {
						++byArchetype;
					} else {
						++byAV;
					}
				}
				progress::debug_set(spell, 0, 0.0);
			}
			check(wrong == 0, std::format("effect scaling: every real effect is scaled or left alone as the settings say ({} wrong)", wrong));
			check(boosted > 200, std::format("effect scaling: {} effects tripled at Lv.100 (+2% a level)", boosted));
			note(std::format("effect scaling: {} effects left alone by archetype, {} by the protected stats", byArchetype, byAV));
			check(byArchetype > 0, "effect scaling: some real effects (paralysis, calm, ...) are excluded by archetype");
			check(durationWrong == 0, std::format("duration scaling: every real effect lasts longer or not as the settings say ({} wrong)", durationWrong));
			check(durationScaled > 100, std::format("duration scaling: {} effects of Alteration and Conjuration spells last 3 times as long at Lv.100", durationScaled));

				// A ward's capacity effect carries a cap of its own besides its magnitude. Each real Accumulate Magnitude effect is
				// made up here as the engine's own kind of object (its real table, so the hook recognises it) with cap and magnitude 10.
				{
					const auto accumulatingTable = REL::Relocation<std::uintptr_t>{ RE::VTABLE_AccumulatingValueModifierEffect[0] }.address();
					int        wards = 0;
					int        wardsWrong = 0;
					for (auto* spell : spells) {
						progress::debug_set(spell, 100, 0.0);
						for (auto* effect : spell->effects) {
							if (!effect || !effect->baseEffect || effect->baseEffect->GetArchetype() != RE::EffectArchetypes::ArchetypeID::kAccumulateMagnitude) {
								continue;
							}
							keep.push_back(std::make_unique<std::byte[]>(sizeof(RE::AccumulatingValueModifierEffect)));
							auto* ae = reinterpret_cast<RE::AccumulatingValueModifierEffect*>(keep.back().get());
							*reinterpret_cast<std::uintptr_t*>(ae) = accumulatingTable;
							ae->spell = spell;
							ae->effect = effect;
							ae->magnitude = 10.0f;
							ae->duration = 0.0f;
							ae->maximumMagnitude = 10.0f;
							hooks::debug_scale(ae, pc);
							const bool shouldScale = !cfg.excludedArchetypes.contains(static_cast<int>(RE::EffectArchetypes::ArchetypeID::kAccumulateMagnitude)) &&
							                         !av_is_protected(effect->baseEffect->data.primaryAV);
							const float expected = shouldScale ? 30.0f : 10.0f;
							++wards;
							if (std::fabs(ae->magnitude - expected) > 0.001f || std::fabs(ae->maximumMagnitude - expected) > 0.001f) {
								++wardsWrong;
								logs::error("selftest: {} / effect {:#x}: capacity effect with magnitude 10 and cap 10 became {} and {} (expected {})", spell->GetName(),
									effect->baseEffect->GetFormID(), ae->magnitude, ae->maximumMagnitude, expected);
							}
						}
						progress::debug_set(spell, 0, 0.0);
					}
					check(wards > 0 && wardsWrong == 0, std::format("ward capacity: all {} real Accumulate Magnitude effects have magnitude and cap tripled together at Lv.100 ({} wrong)", wards, wardsWrong));
				}

			// Which well-known spells get the bonus (for the written report: this is what the exclusions mean in play).
			const auto scales = [&](RE::SpellItem* a_spell) {
				int total = 0;
				int scaled = 0;
				progress::debug_set(a_spell, 100, 0.0);
				for (auto* effect : a_spell->effects) {
					if (!effect || !effect->baseEffect) {
						continue;
					}
					++total;
					auto* ae = make(a_spell, effect);
					hooks::debug_scale(ae, pc);
					scaled += ae->magnitude != 10.0f ? 1 : 0;
				}
				progress::debug_set(a_spell, 0, 0.0);
				return std::pair{ scaled, total };
			};
			std::string report;
			for (const char* wanted : { "Flames", "Frostbite", "Sparks", "Firebolt", "Ice Spike", "Lightning Bolt", "Fireball", "Ice Storm", "Chain Lightning",
					 "Incinerate", "Healing", "Fast Healing", "Close Wounds", "Healing Hands", "Grand Healing", "Oakflesh", "Stoneflesh", "Ironflesh",
					 "Ebonyflesh", "Lesser Ward", "Greater Ward", "Paralyze", "Mass Paralysis", "Candlelight", "Magelight", "Detect Life", "Telekinesis",
					 "Muffle", "Waterbreathing", "Transmute Mineral Ore", "Conjure Familiar", "Conjure Flame Atronach", "Bound Sword", "Raise Zombie",
					 "Reanimate Corpse", "Soul Trap", "Banish Daedra", "Turn Lesser Undead", "Fury", "Calm", "Fear", "Courage", "Rout", "Invisibility",
					 "Clairvoyance", "Slow Time", "Vampiric Drain", "Equilibrium", "Wall of Flames", "Flame Cloak", "Thunderbolt", "Mage Light" }) {
				for (auto* spell : spells) {
					std::string name = spell->GetName();
					if (const auto at = name.find("] "); name.rfind("[Lv.", 0) == 0 && at != std::string::npos) {
						name = name.substr(at + 2);
					}
					if (name == wanted && (spell->GetFormID() >> 24) == 0) {  // the game's own file
						const auto [scaled, total] = scales(spell);
						report += std::format("{} {}/{}; ", wanted, scaled, total);
						break;
					}
				}
			}
			note("spells of the base game, effects that scale / effects: " + report);

			// How the game defines ward spells and a few others for comparison (read-only dump, for the written analysis).
			for (auto* spell : spells) {
				std::string name = spell->GetName();
				if (const auto at = name.find("] "); name.rfind("[Lv.", 0) == 0 && at != std::string::npos) {
					name = name.substr(at + 2);
				}
				if ((spell->GetFormID() >> 24) != 0 || (name.find("Ward") == std::string::npos && name != "Healing" && name != "Oakflesh")) {
					continue;
				}
				logs::info("selftest: DUMP spell '{}' form {:08X} school {} casting type {} delivery {} cost {:.1f}", name, spell->GetFormID(), static_cast<int>(spell->GetAssociatedSkill()),
					static_cast<int>(spell->GetCastingType()), static_cast<int>(spell->GetDelivery()), spell->CalculateMagickaCost(nullptr));
				for (const auto* effect : spell->effects) {
					if (!effect || !effect->baseEffect) {
						continue;
					}
					const auto* b = effect->baseEffect;
					logs::info("selftest: DUMP   effect {:08X} archetype {} primaryAV {} secondaryAV {} flags {:#x} (recover {}, detrimental {}) casting type {} | magnitude {} area {} duration {} | text '{}'",
						b->GetFormID(), static_cast<int>(b->GetArchetype()), static_cast<int>(b->data.primaryAV), static_cast<int>(b->data.secondaryAV),
						static_cast<std::uint32_t>(b->data.flags.get()), b->data.flags.all(RE::EffectSetting::EffectSettingData::Flag::kRecover),
						b->data.flags.all(RE::EffectSetting::EffectSettingData::Flag::kDetrimental), static_cast<int>(b->data.castingType),
						effect->effectItem.magnitude, effect->effectItem.area, effect->effectItem.duration, b->magicItemDescription.c_str());
				}
			}

			// --- the demo table: one row per spell, three spells from each school ----------------------------------------
			const auto school_name = [](RE::ActorValue a_av) -> const char* {
				switch (a_av) {
				case RE::ActorValue::kAlteration: return "Alteration";
				case RE::ActorValue::kConjuration: return "Conjuration";
				case RE::ActorValue::kDestruction: return "Destruction";
				case RE::ActorValue::kIllusion: return "Illusion";
				case RE::ActorValue::kRestoration: return "Restoration";
				default: return "?";
				}
			};
			const auto plain_name = [](const RE::SpellItem* a_spell) {
				std::string name = a_spell->GetName();
				if (const auto at = name.find("] "); name.rfind("[Lv.", 0) == 0 && at != std::string::npos) {
					name = name.substr(at + 2);
				}
				return name;
			};
			for (const char* wanted : { "Oakflesh", "Candlelight", "Paralyze", "Conjure Familiar", "Bound Sword", "Soul Trap", "Flames", "Firebolt", "Fireball",
					 "Courage", "Fury", "Invisibility", "Healing", "Fast Healing", "Lesser Ward" }) {
				RE::SpellItem* spell = nullptr;
				for (auto* s : spells) {
					if (plain_name(s) == wanted && (s->GetFormID() >> 24) == 0) {
						spell = s;
						break;
					}
				}
				if (!spell) {
					logs::info("selftest: DEMO | {} | not found", wanted);
					continue;
				}
				const char*  school = school_name(spell->GetAssociatedSkill());
				const double baseCost = spell->CalculateMagickaCost(nullptr);
				const double factor = std::clamp(baseCost / cfg.referenceCost, cfg.minCostFactor, std::max(cfg.minCostFactor, cfg.maxCostFactor));

				core::Progress toFirst;
				int            casts = 0;
				while (toFirst.level < 1 && casts < 100000) {
					core::add_xp(cfg, toFirst, factor);
					++casts;
				}

				std::string shown[3];
				int         i = 0;
				for (std::uint32_t level : { 0u, 50u, 100u }) {
					progress::debug_set(spell, level, 0.0);
					const auto n = first_number(body(describe(spell)));
					shown[i++] = n < 0 ? "-" : std::to_string(n);
				}
				// Bound Sword, Conjure Familiar and Candlelight (their duration) and Flames (its damage) are three times as big at Lv.100.
				// The base game has several spells with some of these names (different base numbers), so the check is the ratio.
				{
					const std::string name = wanted;
					if (name == "Bound Sword" || name == "Conjure Familiar" || name == "Candlelight" || name == "Flames" || name == "Lesser Ward") {
						const long long base = shown[0] == "-" ? -1 : std::stoll(shown[0]);
						const long long at100 = shown[2] == "-" ? -1 : std::stoll(shown[2]);
						check(base > 0 && at100 == base * 3, std::format("{}: the description shows {} at Lv.0 and {} at Lv.100 (3 times as much expected)", wanted, shown[0], shown[2]));
					}
				}
				progress::debug_set(spell, 12, 0.0);
				const std::string name12 = spell->GetName();
				const auto [scaled, total] = scales(spell);

				core::Progress after;
				for (int k = 0; k < 100; ++k) {
					core::add_xp(cfg, after, factor);
				}
				progress::debug_set(spell, after.level, after.xp);
				const auto text = describe(spell);
				const auto at = text.find(kXpMarker);
				const std::string line = at == std::string::npos ? "" : text.substr(at + 1);
				progress::debug_set(spell, 0, 0.0);

				logs::info("selftest: DEMO | {} | {} | {:.2f} | {} | {} | {} / {} / {} | {}/{} | {:.1f} -> {:.1f} | Lv.{}: {}", school, wanted, factor, casts, name12,
					shown[0], shown[1], shown[2], scaled, total, baseCost, baseCost * core::cost_factor(cfg, 100), after.level, line);
			}
			if (flames) {
				progress::debug_set(flames, 100, 0.0);
				auto* effect = flames->effects.empty() ? nullptr : flames->effects.front();
				if (effect) {
					auto* once = make(flames, effect);
					hooks::debug_scale(once, pc);
					hooks::debug_scale(once, pc);  // the engine offering the same effect twice
					check(std::fabs(once->magnitude - 30.0f) < 0.001f, std::format("an effect offered twice is scaled once (10 -> {}, expected 30)", once->magnitude));
					auto* another = make(flames, effect);
					hooks::debug_scale(another, pc);
					check(std::fabs(another->magnitude - 30.0f) < 0.001f, "a different effect object of the same spell is scaled too");
					auto* stranger = make(flames, effect);
					hooks::debug_scale(stranger, nullptr);
					check(stranger->magnitude == 10.0f, "an effect cast by someone else (no caster here) is left alone");
					auto* enemy = make(flames, effect);
					const auto* spellList = RE::TESDataHandler::GetSingleton()->GetFormArray<RE::EnchantmentItem>().data();
					if (spellList && spellList[0] && !spellList[0]->effects.empty()) {
						auto* fromEnchantment = make(spellList[0], spellList[0]->effects.front());
						hooks::debug_scale(fromEnchantment, pc);
						check(fromEnchantment->magnitude == 10.0f, "an effect from an enchantment is left alone");
					}
					(void)enemy;
					progress::debug_set(flames, 0, 0.0);
					auto* atZero = make(flames, effect);
					hooks::debug_scale(atZero, pc);
					check(atZero->magnitude == 10.0f, "a spell at Lv.0 is left alone");
				}
			}
		} else {
			note("the player object does not exist at the main menu; the effect scaling checks were skipped");
		}

		// leave everything as it was
		config::debug_settings() = saved;
		progress::refresh_names();
		finish("menu stage");
	}

	void run_loaded()
	{
		if (!g_enabled) {
			return;
		}
		g_pass = g_fail = g_note = 0;
		logs::info("selftest: loaded-game stage starting");

		auto* pc = RE::PlayerCharacter::GetSingleton();
		auto* flames = RE::TESForm::LookupByID<RE::SpellItem>(kFlames);
		check(pc != nullptr && flames != nullptr, "player and Flames exist");
		if (!pc || !flames || !progress::tracked(flames)) {
			finish("loaded-game stage");
			return;
		}

		const auto  saved = config::get();
		auto&       cfg = config::debug_settings();
		cfg.costPerLevel = 0.0025;
		cfg.costFloor = 0.0;
		cfg.levelCap = 0;

		// Flames is borrowed for the checks: its real level and XP are put back afterwards.
		std::uint32_t savedLevel = 0;
		double        savedXp = 0.0;
		progress::debug_get(flames, savedLevel, savedXp);
		progress::debug_set(flames, 0, 0.0);

		// the cost the game computes for the player, as the engine does without this plugin
		const float base = flames->CalculateMagickaCost(pc);
		note(std::format("Flames costs {:.2f} per second for the player at Lv.0", base));
		check(base > 0.0f, "Flames has a cost");
		check(std::abs(hooks::debug_cost(flames, pc) - base) < 0.001f, "Lv.0: the wrapper leaves the cost alone");

		progress::debug_set(flames, 100, 0.0);
		const float discounted = hooks::debug_cost(flames, pc);
		check(std::abs(discounted - base * 0.75f) < 0.01f, std::format("Lv.100: the wrapper gives {:.3f} (75% of {:.3f})", discounted, base));
		check(std::abs(hooks::debug_cost(flames, nullptr) - flames->CalculateMagickaCost(nullptr)) < 0.001f, "no actor (gold value, tooltips): cost untouched");
		progress::debug_set(flames, 400, 0.0);
		check(hooks::debug_cost(flames, pc) == 0.0f, "Lv.400: free");

		// the game's own routes to the cost (not through this plugin's function) must pass the patched call sites
		progress::debug_set(flames, 100, 0.0);
		for (auto source : { RE::MagicSystem::CastingSource::kLeftHand, RE::MagicSystem::CastingSource::kRightHand }) {
			auto* caster = pc->GetMagicCaster(source);
			auto* actorCaster = caster ? skyrim_cast<RE::ActorMagicCaster*>(caster) : nullptr;
			if (!actorCaster) {
				note("no caster for a hand");
				continue;
			}
			const auto before = hooks::cost_changes();
			float      strength = 1.0f;
			auto       reason = RE::MagicSystem::CannotCastReason::kOK;
			const bool canCast = actorCaster->CheckCast(flames, false, &strength, &reason, false);
			const auto after = hooks::cost_changes();
			check(after > before, std::format("the game's CheckCast ({} hand) computed Flames' cost through the patched call site (can cast: {})",
								  source == RE::MagicSystem::CastingSource::kLeftHand ? "left" : "right", canCast));
		}

		// a cast event as the game sends it: XP follows the spell's cost, a dual-cast gives double, and a second
		// event right after the first (a dual-cast raises one per hand) adds nothing
		cfg.sameSpellCooldown = 0.0;
		cfg.dualCastMultiplier = 2.0;
		cfg.referenceCost = 50.0;
		cfg.minCostFactor = 0.25;
		cfg.maxCostFactor = 4.0;
		cfg.xpSpeed = 1.0;
		cfg.toastEnabled = false;
		progress::debug_set(flames, 0, 0.0);
		const double costFactor = core::cost_xp_factor(cfg, base);
		std::uint16_t enemyLevel = 0;
		const double  inCombat = hooks::debug_combat_multiplier(&enemyLevel);  // 1 unless the player is in a fight
		note(std::format("combat multiplier now x{:.2f} (highest enemy level {})", inCombat, enemyLevel));
		std::uint32_t level = 0;
		double        xp1 = 0.0;
		double        xp2 = 0.0;
		double        xp3 = 0.0;
		hooks::debug_cast(flames, false);
		progress::debug_get(flames, level, xp1);
		check(std::abs(xp1 - costFactor * inCombat) < 0.001, std::format("a single cast gives {:.3f} XP (cost factor {:.3f} for a cost of {:.1f}, combat bonus x{:.1f})", xp1, costFactor, base, inCombat));
		hooks::debug_cast(flames, false);
		progress::debug_get(flames, level, xp2);
		check(xp2 == xp1, "a second event within 0.15 s adds nothing");
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
		hooks::debug_cast(flames, true);
		progress::debug_get(flames, level, xp3);
		check(std::abs((xp3 - xp2) - 2.0 * costFactor * inCombat) < 0.001, std::format("a dual-cast gives double ({:.3f} XP)", xp3 - xp2));

		progress::debug_set(flames, savedLevel, savedXp);
		config::debug_settings() = saved;
		progress::refresh_names();
		finish("loaded-game stage");
	}
}
