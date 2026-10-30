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
#include "progress.h"

#include "config.h"
#include "core.h"

#include <algorithm>
#include <shared_mutex>
#include <string>
#include <unordered_map>

// The game's headers pull in windows.h, whose min/max macros break std::min / std::max.
#undef min
#undef max

namespace progress
{
	namespace
	{
		constexpr std::uint32_t kUniqueId = 'SPLV';
		constexpr std::uint32_t kRecord = 'LVLS';
		constexpr std::uint32_t kRecordVersion = 1;

		struct Info
		{
			RE::SpellItem* spell = nullptr;
			std::string    base;    // the name before this mod touched it
			std::string    plugin;  // file that defines the spell, for the cosave
			std::uint32_t  local = 0;
			core::Progress progress;
		};

		// Progress for a spell whose plugin is not loaded right now. Kept and written back, so disabling a
		// spell mod for a while does not wipe its levels.
		struct Orphan
		{
			std::string   plugin;
			std::uint32_t local = 0;
			std::uint32_t level = 0;
			double        xp = 0.0;
		};

		constexpr std::size_t kMaxRetired = 20000;

		std::shared_mutex                    g_lock;
		std::unordered_map<RE::FormID, Info> g_spells;
		std::vector<Orphan>                  g_orphans;
		std::vector<RE::BSFixedString>       g_retired;  // names no longer in use, kept alive on purpose

		// Which tracked spell each effect belongs to: the text hook is handed an effect, not the spell around it.
		std::unordered_map<const RE::Effect*, const RE::SpellItem*> g_effectOwner;

		std::uint32_t effective_level(const Info& a_info)
		{
			return core::effective_level(config::get(), a_info.progress.level);
		}

		// The original name is read the first time it is needed, which is after every other plugin has finished its
		// own kDataLoaded work: a mod that renames or retranslates spells at start-up is then respected, not
		// overwritten with an older name.
		void ensure_base(Info& a_info)
		{
			if (a_info.base.empty()) {
				const char* name = a_info.spell->GetName();
				a_info.base = name ? name : "";
			}
		}

		void apply_name(Info& a_info)
		{
			ensure_base(a_info);
			const auto  name = core::display_name(config::get(), a_info.base, effective_level(a_info));
			const char* current = a_info.spell->GetName();
			if (!current || name != current) {
				// Something may still hold the old text's pointer (a HUD that shows the equipped spell, another
				// plugin's cache). Keeping a reference to every string we replace means it is never freed under them.
				if (g_retired.size() < kMaxRetired) {
					g_retired.push_back(a_info.spell->fullName);
				}
				a_info.spell->fullName = RE::BSFixedString(name.c_str());
			}
		}

		void on_save(SKSE::SerializationInterface* a_intfc)
		{
			std::shared_lock lock(g_lock);

			std::vector<Orphan> out = g_orphans;
			for (const auto& [id, info] : g_spells) {
				if (info.progress.level > 0 || info.progress.xp > 0.0) {
					out.push_back({ info.plugin, info.local, info.progress.level, info.progress.xp });
				}
			}

			if (!a_intfc->OpenRecord(kRecord, kRecordVersion)) {
				logs::error("progress: could not open the cosave record");
				return;
			}
			const auto count = static_cast<std::uint32_t>(out.size());
			bool       ok = a_intfc->WriteRecordData(&count, sizeof(count));
			for (const auto& e : out) {
				const auto len = static_cast<std::uint16_t>(std::min<std::size_t>(e.plugin.size(), 0xFFFF));
				ok = ok && a_intfc->WriteRecordData(&len, sizeof(len));
				ok = ok && a_intfc->WriteRecordData(e.plugin.data(), len);
				ok = ok && a_intfc->WriteRecordData(&e.local, sizeof(e.local));
				ok = ok && a_intfc->WriteRecordData(&e.level, sizeof(e.level));
				ok = ok && a_intfc->WriteRecordData(&e.xp, sizeof(e.xp));
			}
			if (ok) {
				logs::info("progress: saved {} spell(s)", count);
			} else {
				logs::error("progress: writing the cosave record failed");
			}
		}

		void on_load(SKSE::SerializationInterface* a_intfc)
		{
			std::uint32_t type = 0;
			std::uint32_t version = 0;
			std::uint32_t length = 0;

			std::unique_lock lock(g_lock);
			for (auto& [id, info] : g_spells) {
				info.progress = {};
			}
			g_orphans.clear();

			while (a_intfc->GetNextRecordInfo(type, version, length)) {
				if (type != kRecord) {
					continue;
				}
				if (version != kRecordVersion) {
					logs::warn("progress: cosave record version {} is not supported; starting fresh", version);
					continue;
				}
				std::uint32_t count = 0;
				if (!a_intfc->ReadRecordData(&count, sizeof(count))) {
					continue;
				}
				std::size_t resolved = 0;
				for (std::uint32_t i = 0; i < count; ++i) {
					Orphan        e;
					std::uint16_t len = 0;
					if (!a_intfc->ReadRecordData(&len, sizeof(len))) {
						break;
					}
					e.plugin.resize(len);
					if (!a_intfc->ReadRecordData(e.plugin.data(), len) ||
						!a_intfc->ReadRecordData(&e.local, sizeof(e.local)) ||
						!a_intfc->ReadRecordData(&e.level, sizeof(e.level)) ||
						!a_intfc->ReadRecordData(&e.xp, sizeof(e.xp))) {
						break;
					}
					auto* spell = RE::TESDataHandler::GetSingleton()->LookupForm<RE::SpellItem>(e.local, e.plugin);
					const auto it = spell ? g_spells.find(spell->GetFormID()) : g_spells.end();
					if (it != g_spells.end()) {
						it->second.progress = { e.level, e.xp };
						++resolved;
					} else {
						g_orphans.push_back(std::move(e));
					}
				}
				logs::info("progress: loaded {} spell(s), {} kept aside (plugin missing)", resolved, g_orphans.size());
			}
		}

		void on_revert(SKSE::SerializationInterface*)
		{
			{
				std::unique_lock lock(g_lock);
				for (auto& [id, info] : g_spells) {
					info.progress = {};
				}
				g_orphans.clear();
			}
			refresh_names();
		}
	}

	void on_data_loaded()
	{
		logs::info("progress: reading the spell list");
		std::unique_lock lock(g_lock);
		g_spells.clear();
		g_effectOwner.clear();
		for (auto* spell : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::SpellItem>()) {
			if (!spell || spell->GetSpellType() != RE::MagicSystem::SpellType::kSpell) {
				continue;
			}
			const char* name = spell->GetName();
			if (!name || !*name || spell->effects.empty()) {
				continue;
			}
			const auto* file = spell->GetFile(0);
			if (!file) {
				continue;  // made while playing: no stable identity to save
			}
			Info info;
			info.spell = spell;  // base name is read later, see ensure_base
			info.plugin = std::string(file->GetFilename());
			info.local = spell->GetLocalFormID();
			g_spells.emplace(spell->GetFormID(), std::move(info));
			for (const auto* effect : spell->effects) {
				if (effect) {
					g_effectOwner[effect] = spell;
				}
			}
		}
		logs::info("progress: {} spell(s) can level", g_spells.size());
	}

	void install_serialization()
	{
		const auto* serialization = SKSE::GetSerializationInterface();
		serialization->SetUniqueID(kUniqueId);
		serialization->SetSaveCallback(on_save);
		serialization->SetLoadCallback(on_load);
		serialization->SetRevertCallback(on_revert);
	}

	void refresh_names()
	{
		std::unique_lock lock(g_lock);
		for (auto& [id, info] : g_spells) {
			apply_name(info);
		}
	}

	bool tracked(const RE::SpellItem* a_spell)
	{
		if (!a_spell) {
			return false;
		}
		std::shared_lock lock(g_lock);
		return g_spells.contains(a_spell->GetFormID());
	}

	std::uint32_t level_of(const RE::SpellItem* a_spell)
	{
		if (!a_spell) {
			return 0;
		}
		std::shared_lock lock(g_lock);
		const auto       it = g_spells.find(a_spell->GetFormID());
		return it == g_spells.end() ? 0 : effective_level(it->second);
	}

	double cost_factor(const RE::SpellItem* a_spell)
	{
		return core::cost_factor(config::get(), level_of(a_spell));
	}

	double magnitude_factor(const RE::SpellItem* a_spell)
	{
		return core::magnitude_factor(config::get(), level_of(a_spell));
	}

	double duration_factor(const RE::SpellItem* a_spell)
	{
		const auto& c = config::get();
		if (!a_spell || c.durationSchools.empty()) {
			return 1.0;
		}
		int school = -1;
		switch (a_spell->GetAssociatedSkill()) {
		case RE::ActorValue::kAlteration: school = core::kAlteration; break;
		case RE::ActorValue::kConjuration: school = core::kConjuration; break;
		case RE::ActorValue::kDestruction: school = core::kDestruction; break;
		case RE::ActorValue::kIllusion: school = core::kIllusion; break;
		case RE::ActorValue::kRestoration: school = core::kRestoration; break;
		default: break;
		}
		if (!c.durationSchools.contains(school)) {
			return 1.0;
		}
		return core::duration_factor(c, level_of(a_spell));
	}

	void add_xp(RE::SpellItem* a_spell, double a_xp)
	{
		if (!a_spell || a_xp <= 0.0) {
			return;
		}
		const auto&   c = config::get();
		std::uint32_t before = 0;
		std::uint32_t after = 0;
		std::string   base;
		{
			std::unique_lock lock(g_lock);
			const auto       it = g_spells.find(a_spell->GetFormID());
			if (it == g_spells.end()) {
				return;
			}
			auto& info = it->second;
			ensure_base(info);
			before = info.progress.level;
			core::add_xp(c, info.progress, a_xp);
			after = info.progress.level;
			// Concentration spells report every frame; only log the lines that matter.
			if (c.verbose && (after != before || a_xp >= 0.2)) {
				logs::info("progress: {} +{:.3f} xp -> Lv.{} ({:.1f}/{:.1f})", info.base, a_xp * c.xpSpeed, after, info.progress.xp, core::xp_needed(c, after));
			}
			if (after != before) {
				apply_name(info);
				base = info.base;
			}
		}

		if (core::should_toast(c, before, after)) {
			const auto text = std::format("{} reached Lv. {}", base, after);
			RE::SendHUDMessage::ShowHUDMessage(text.c_str());
		}
	}

	const RE::SpellItem* spell_of_effect(const RE::Effect* a_effect)
	{
		std::shared_lock lock(g_lock);
		const auto       it = g_effectOwner.find(a_effect);
		return it == g_effectOwner.end() ? nullptr : it->second;
	}

	std::string xp_line(const RE::SpellItem* a_spell)
	{
		if (!a_spell) {
			return {};
		}
		std::shared_lock lock(g_lock);
		const auto       it = g_spells.find(a_spell->GetFormID());
		if (it == g_spells.end()) {
			return {};
		}
		return core::xp_line(config::get(), it->second.progress);
	}

	bool debug_set(const RE::SpellItem* a_spell, std::uint32_t a_level, double a_xp)
	{
		std::unique_lock lock(g_lock);
		const auto       it = a_spell ? g_spells.find(a_spell->GetFormID()) : g_spells.end();
		if (it == g_spells.end()) {
			return false;
		}
		it->second.progress = { a_level, a_xp };
		apply_name(it->second);
		return true;
	}

	bool debug_get(const RE::SpellItem* a_spell, std::uint32_t& a_level, double& a_xp)
	{
		std::shared_lock lock(g_lock);
		const auto       it = a_spell ? g_spells.find(a_spell->GetFormID()) : g_spells.end();
		if (it == g_spells.end()) {
			return false;
		}
		a_level = it->second.progress.level;
		a_xp = it->second.progress.xp;
		return true;
	}

	std::vector<RE::SpellItem*> sample_spells(std::size_t a_max)
	{
		std::shared_lock            lock(g_lock);
		std::vector<RE::SpellItem*> out;
		for (const auto& [id, info] : g_spells) {
			if (out.size() >= a_max) {
				break;
			}
			out.push_back(info.spell);
		}
		return out;
	}
}
