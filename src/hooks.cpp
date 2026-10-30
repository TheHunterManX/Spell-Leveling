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
#include "hooks.h"

#include "config.h"
#include "progress.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

// The game's headers pull in windows.h, whose min/max macros break std::min / std::max.
#undef min
#undef max

namespace hooks
{
	namespace
	{
		using CalcCost_t = float (*)(const RE::MagicItem*, RE::Actor*);
		using AdjustEffect_t = void (*)(RE::ActorMagicCaster*, RE::ActiveEffect*, float, bool);
		using Update_t = void (*)(RE::ActorMagicCaster*, float);
		using Visit_t = std::int32_t (*)(void*, RE::Effect*);
		using Start_t = void (*)(RE::ActiveEffect*);

		CalcCost_t     g_calcCost = nullptr;
		AdjustEffect_t g_adjustEffect = nullptr;
		Update_t       g_update = nullptr;
		Visit_t        g_visit = nullptr;
		Visit_t        g_visitMenu = nullptr;

		// While set, the cost hook leaves the cost alone.
		thread_local bool t_baseCost = false;

		struct BaseCost
		{
			BaseCost() { t_baseCost = true; }
			~BaseCost() { t_baseCost = false; }
		};

		std::atomic<bool>          g_costLogged{ false };
		std::atomic<std::uint64_t> g_costChanges{ 0 };
		Status                     g_status;

		RE::PlayerCharacter* player()
		{
			return RE::PlayerCharacter::GetSingleton();
		}

		// Points every direct call (and tail jump) to a_target at a_hook instead. The hook calls the real function
		// itself, by address, so it is not redirected again. Returns how many places were changed.
		std::size_t redirect_calls(std::uintptr_t a_target, const void* a_hook, const char* a_what)
		{
			logs::info("hooks: scanning the game's code for callers of {}", a_what);
			const auto  text = REL::Module::get().segment(REL::Segment::textx);
			const auto* code = reinterpret_cast<const std::uint8_t*>(text.address());
			const auto  size = text.size();

			std::vector<std::uintptr_t> calls;
			std::vector<std::uintptr_t> jumps;
			std::size_t                 skipped = 0;
			for (std::size_t i = 0; i + 5 <= size; ++i) {
				if (code[i] != 0xE8 && code[i] != 0xE9) {
					continue;
				}
				std::int32_t rel = 0;
				std::memcpy(&rel, code + i + 1, sizeof(rel));
				const auto at = text.address() + i;
				if (at + 5 + static_cast<std::intptr_t>(rel) != a_target) {
					continue;
				}
				// Only genuine code: the address must be inside a function the unwinder knows about.
				DWORD64 imageBase = 0;
				if (!RtlLookupFunctionEntry(at, &imageBase, nullptr)) {
					++skipped;
					continue;
				}
				(code[i] == 0xE8 ? calls : jumps).push_back(at);
			}

			logs::info("hooks: found {} call(s), {} jump(s); patching", calls.size(), jumps.size());
			auto& trampoline = SKSE::GetTrampoline();
			for (const auto at : calls) {
				trampoline.write_call<5>(at, reinterpret_cast<std::uintptr_t>(a_hook));
			}
			for (const auto at : jumps) {
				trampoline.write_branch<5>(at, reinterpret_cast<std::uintptr_t>(a_hook));
			}
			logs::info("hooks: {}: {} call(s) and {} jump(s) redirected{}", a_what, calls.size(), jumps.size(),
				skipped ? std::format(", {} skipped (not inside a function)", skipped) : std::string{});
			return calls.size() + jumps.size();
		}

		// --- cost -------------------------------------------------------------------------------------------------

		// MagicItem::CalculateCost is the game's one place that works out what a spell costs, for the magic menu and for
		// the magicka actually spent. It only calls SpellItem::AdjustCost for spells with a manual cost, so that is
		// not a hook for most spells; this is applied to the result instead, whichever way it was computed.
		float calc_cost(const RE::MagicItem* a_self, RE::Actor* a_actor)
		{
			const float cost = g_calcCost(a_self, a_actor);
			if (t_baseCost || !a_self || !a_actor || !a_actor->IsPlayerRef() || a_self->GetFormType() != RE::FormType::Spell) {
				return cost;
			}
			const auto* spell = static_cast<const RE::SpellItem*>(a_self);
			const double factor = progress::cost_factor(spell);  // 1.0 unless the spell is levelled
			if (factor == 1.0) {
				return cost;
			}
			++g_costChanges;
			if (config::get().verbose && !g_costLogged.exchange(true)) {
				logs::info("hooks: cost hook is active: {} costs {:.1f} instead of {:.1f}", spell->GetName(), cost * factor, cost);
			}
			return static_cast<float>(cost * factor);
		}

		// --- magnitude --------------------------------------------------------------------------------------------

		// Whether levels make this kind of effect stronger. Used for the real effect and for the description,
		// so the two always agree.
		bool is_boosted(const RE::EffectSetting* base)
		{
			if (!base) {
				return false;
			}
			if (config::get().excludedArchetypes.contains(static_cast<int>(base->GetArchetype()))) {
				return false;
			}
			// Speed and the school cost modifiers are percentages with hard meaning in the engine: five times as
			// much Slow would run the target backwards, and five times Fortify Destruction would pay you magicka.
			switch (base->data.primaryAV) {
			case RE::ActorValue::kSpeedMult:
			case RE::ActorValue::kWeaponSpeedMult:
			case RE::ActorValue::kLeftWeaponSpeedMultiply:
			case RE::ActorValue::kAlterationModifier:
			case RE::ActorValue::kConjurationModifier:
			case RE::ActorValue::kDestructionModifier:
			case RE::ActorValue::kIllusionModifier:
			case RE::ActorValue::kRestorationModifier:
				return false;
			default:
				return true;
			}
		}

		// Whether this effect object is an AccumulatingValueModifierEffect (what Accumulate Magnitude effects are made as).
		bool is_accumulating(const RE::ActiveEffect* a_effect)
		{
			static const auto vtable = REL::Relocation<std::uintptr_t>{ RE::VTABLE_AccumulatingValueModifierEffect[0] }.address();
			return a_effect && *reinterpret_cast<const std::uintptr_t*>(a_effect) == vtable;
		}

		// Whether the duration of this kind of effect grows with levels (when its spell is of a school in DurationSchools):
		// every kind except those the ini excludes (scripted effects, by default, whose scripts may depend on the duration).
		bool duration_boosted(const RE::EffectSetting* base)
		{
			return base && !config::get().durationExcludedArchetypes.contains(static_cast<int>(base->GetArchetype()));
		}

		// An effect must be scaled exactly once, and before it starts: a buff takes off at the end whatever number is
		// stored on it, so a number changed after the start would leave the stat permanently off. Two places can
		// scale it (the caster's "adjust" step and the effect's own Start) and whichever sees it first does the
		// work; the other finds it in this list and leaves it alone. The match is by identity (effect object,
		// spell, effect entry) and not by magnitude, because perks change the magnitude in between.
		bool already_scaled(const RE::ActiveEffect* a_effect)
		{
			struct Seen
			{
				const void*  ae = nullptr;
				const void*  spell = nullptr;
				const void*  effect = nullptr;
				std::int64_t at = 0;
			};
			static std::mutex            lock;
			static std::array<Seen, 256> ring{};
			static std::size_t           next = 0;
			constexpr std::int64_t       kWindowMicroseconds = 250000;  // a queued start can be a few frames late

			const auto now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
			std::lock_guard guard(lock);
			for (const auto& s : ring) {
				if (s.ae == a_effect && s.spell == a_effect->spell && s.effect == a_effect->effect && now - s.at < kWindowMicroseconds) {
					return true;
				}
			}
			ring[next++ % ring.size()] = { a_effect, a_effect->spell, a_effect->effect, now };
			return false;
		}

		// Scales a new effect if it is the player's and from a levelled spell: its magnitude (for the kinds of effect that
		// scale) and its duration (for the schools in DurationSchools). a_where is only for the log.
		void scale_effect(RE::ActiveEffect* a_effect, const RE::Actor* a_caster, const char* a_where)
		{
			if (!a_effect || !a_caster || !a_caster->IsPlayerRef()) {
				return;
			}
			const auto* spell = a_effect->spell ? a_effect->spell->As<RE::SpellItem>() : nullptr;
			if (!spell || !progress::tracked(spell)) {
				return;
			}
			const auto* base = a_effect->effect ? a_effect->effect->baseEffect : nullptr;
			const double magnitudeFactor = is_boosted(base) ? progress::magnitude_factor(spell) : 1.0;
			const double durationFactor = (a_effect->duration > 0.0f && duration_boosted(base)) ? progress::duration_factor(spell) : 1.0;
			if (magnitudeFactor == 1.0 && durationFactor == 1.0) {
				return;
			}
			if (already_scaled(a_effect)) {
				if (config::get().verbose) {
					logs::info("hooks: {} effect already scaled, {} leaves it", spell->GetName(), a_where);
				}
				return;
			}
			const float magnitudeBefore = a_effect->magnitude;
			const float durationBefore = a_effect->duration;
			a_effect->magnitude = magnitudeBefore * static_cast<float>(magnitudeFactor);
			a_effect->duration = durationBefore * static_cast<float>(durationFactor);
			// A ward's capacity effect (Accumulate Magnitude on the Ward Power stat) fills the stat at its magnitude per second
			// but only up to a cap of its own, copied from the spell when the effect was made; the game takes the highest cap among
			// the actor's ward effects as the ward's maximum when the effect starts. So the cap is scaled here too, before that.
			float capBefore = 0.0f;
			if (magnitudeFactor != 1.0 && base && base->GetArchetype() == RE::EffectArchetypes::ArchetypeID::kAccumulateMagnitude && is_accumulating(a_effect)) {
				auto* accumulating = static_cast<RE::AccumulatingValueModifierEffect*>(a_effect);
				capBefore = accumulating->maximumMagnitude;
				accumulating->maximumMagnitude = capBefore * static_cast<float>(magnitudeFactor);
			}
			if (config::get().verbose) {
				logs::info("hooks: {} effect magnitude {:.2f} -> {:.2f} (x{:.2f}), duration {:.1f} -> {:.1f} s (x{:.2f}) at {}{}", spell->GetName(), magnitudeBefore,
					a_effect->magnitude, magnitudeFactor, durationBefore, a_effect->duration, durationFactor, a_where,
					capBefore > 0.0f ? std::format(", ward capacity {:.1f} -> {:.1f}", capBefore, static_cast<RE::AccumulatingValueModifierEffect*>(a_effect)->maximumMagnitude) : std::string{});
			}
		}

		// ActorMagicCaster::AdjustActiveEffect is where the engine finishes a freshly made effect on its way to the
		// target, which should be before the effect starts.
		void adjust_effect(RE::ActorMagicCaster* a_self, RE::ActiveEffect* a_effect, float a_power, bool a_flag)
		{
			g_adjustEffect(a_self, a_effect, a_power, a_flag);
			if (a_self && a_effect) {
				scale_effect(a_effect, a_self->GetCasterAsActor(), "adjust");
			}
		}

		// The safety net for the above: if the engine ever starts an effect before it is adjusted, the number is
		// scaled right here, still before the original Start applies it. Only effects that apply their stored
		// magnitude and later undo it (every kind of value modifier) are hooked; for the rest, order does not matter.
		// An effect restored from a save has already run for a while (elapsedSeconds > 0) and already carries its
		// scaled number, so it is left alone.
		std::vector<std::pair<std::uintptr_t, Start_t>> g_start;

		void start_effect(RE::ActiveEffect* a_self)
		{
			if (a_self && a_self->elapsedSeconds <= 0.0f) {
				const auto caster = a_self->caster.get();
				scale_effect(a_self, caster.get(), "start");
			}
			// Each class has its own table, so the original is found by the table this object uses.
			const auto table = a_self ? *reinterpret_cast<std::uintptr_t*>(a_self) : 0;
			for (const auto& [address, original] : g_start) {
				if (address == table) {
					original(a_self);
					return;
				}
			}
			logs::critical("hooks: effect start from an unknown class, cannot continue");
		}

		void hook_start(const REL::VariantID& a_table)
		{
			REL::Relocation<std::uintptr_t> vtable{ a_table };
			g_start.emplace_back(vtable.address(), reinterpret_cast<Start_t>(vtable.write_vfunc(0x14, start_effect)));
		}

		// --- the text in the magic menu -----------------------------------------------------------------------------

		// The game builds a spell's description by walking its effects with a visitor, one call per effect. The text
		// of an effect ("Deals <mag> points of fire damage") is looked up on the effect type and its <mag> is read
		// from the number stored on the spell's effect at that moment. For the length of one call this shows the
		// boosted number, and (on the last effect that has text) adds the XP line to the effect's text; both are put
		// back straight after. The menu runs on the main thread, as does everything else that reads these.
		std::int32_t visit_common(void* a_self, RE::Effect* a_effect, Visit_t a_original)
		{
			const auto&         c = config::get();
			const RE::SpellItem* spell = (a_effect && a_effect->baseEffect) ? progress::spell_of_effect(a_effect) : nullptr;

			bool               magnitudeChanged = false;
			float              savedMagnitude = 0.0f;
			bool               durationChanged = false;
			std::uint32_t       savedDuration = 0;
			RE::EffectSetting* textBase = nullptr;
			RE::BSFixedString  savedText;
			RE::EffectSetting* capacityBase = nullptr;
			RE::BSFixedString  savedCapacityText;

			if (spell) {
				if (c.showBonusInDescription) {
					if (is_boosted(a_effect->baseEffect)) {
						const double factor = progress::magnitude_factor(spell);
						if (factor != 1.0) {
							savedMagnitude = a_effect->effectItem.magnitude;
							a_effect->effectItem.magnitude = savedMagnitude * static_cast<float>(factor);
							magnitudeChanged = true;
						}
					}
					// The same for the duration ("<dur>" reads it): Alteration and Conjuration spells last longer as they level.
					if (a_effect->effectItem.duration > 0 && duration_boosted(a_effect->baseEffect)) {
						const double durationFactor = progress::duration_factor(spell);
						if (durationFactor != 1.0) {
							savedDuration = a_effect->effectItem.duration;
							a_effect->effectItem.duration = static_cast<std::uint32_t>(static_cast<double>(savedDuration) * durationFactor);
							durationChanged = true;
						}
					}
					// A ward's capacity is written into its text as a plain number ("Negates up to <50> points ..."), not read from the
					// effect, so it is replaced by the scaled number for this one call.
					if (magnitudeChanged && a_effect->baseEffect->GetArchetype() == RE::EffectArchetypes::ArchetypeID::kAccumulateMagnitude) {
						const std::string literal = std::format("<{}>", static_cast<long long>(savedMagnitude));
						std::string       text = a_effect->baseEffect->magicItemDescription.c_str();
						if (const auto at = text.find(literal); at != std::string::npos) {
							capacityBase = a_effect->baseEffect;
							savedCapacityText = capacityBase->magicItemDescription;
							text.replace(at, literal.size(), std::format("<{}>", static_cast<long long>(a_effect->effectItem.magnitude)));
							capacityBase->magicItemDescription = RE::BSFixedString(text.c_str());
						}
					}
				}
				if (c.showXpBar) {
					// The last effect that has any text of its own is the one that carries the line.
					const RE::Effect* last = nullptr;
					for (const auto* effect : spell->effects) {
						if (effect && effect->baseEffect) {
							const char* text = effect->baseEffect->magicItemDescription.c_str();
							if (text && *text) {
								last = effect;
							}
						}
					}
					if (last == a_effect) {
						const auto line = progress::xp_line(spell);
						if (!line.empty()) {
							textBase = a_effect->baseEffect;
							savedText = textBase->magicItemDescription;
							const std::string combined = std::string(savedText.c_str()) + "\n" + line;
							textBase->magicItemDescription = RE::BSFixedString(combined.c_str());
						}
					}
				}
			}

			const auto result = a_original(a_self, a_effect);

			if (textBase) {
				textBase->magicItemDescription = savedText;
			}
			if (capacityBase) {
				capacityBase->magicItemDescription = savedCapacityText;
			}
			if (magnitudeChanged) {
				a_effect->effectItem.magnitude = savedMagnitude;
			}
			if (durationChanged) {
				a_effect->effectItem.duration = savedDuration;
			}
			return result;
		}

		// The game has two such visitors. The one the magic menu's item card uses is built in the interface code
		// (found when the first card showed no XP line); the other is the game's general "describe this magic item".
		std::int32_t visit_effect(void* a_self, RE::Effect* a_effect)
		{
			return visit_common(a_self, a_effect, g_visit);
		}

		std::int32_t visit_effect_menu(void* a_self, RE::Effect* a_effect)
		{
			return visit_common(a_self, a_effect, g_visitMenu);
		}

		// --- XP from holding a concentration spell ----------------------------------------------------------------

		// Whether each spell was last being dual-cast. The cast event arrives when the hands have already let go, and
		// by then the game may have cleared its dual-casting flag, so the flag is noted while the spell is charging.
		struct DualNote
		{
			bool                                  dual = false;
			std::chrono::steady_clock::time_point at{};
		};
		std::mutex                               g_dualLock;
		std::unordered_map<RE::FormID, DualNote> g_dualNotes;

		void note_dual(RE::FormID a_spell, bool a_dual)
		{
			std::lock_guard guard(g_dualLock);
			g_dualNotes[a_spell] = { a_dual, std::chrono::steady_clock::now() };
		}

		bool dual_casting(RE::PlayerCharacter* a_player, RE::FormID a_spell)
		{
			{
				std::lock_guard guard(g_dualLock);
				const auto      it = g_dualNotes.find(a_spell);
				if (it != g_dualNotes.end() && std::chrono::steady_clock::now() - it->second.at < std::chrono::seconds(3)) {
					return it->second.dual;
				}
			}
			for (auto source : { RE::MagicSystem::CastingSource::kLeftHand, RE::MagicSystem::CastingSource::kRightHand }) {
				if (const auto* caster = a_player->GetMagicCaster(source); caster && caster->GetIsDualCasting()) {
					return true;
				}
			}
			return false;
		}

		// The XP multiplier for the fight the player is in right now: 1 outside combat, otherwise it follows the level of the
		// highest-level living enemy that is fighting the player (targeting the player, or hostile and in combat). Cached for a
		// quarter of a second because a held concentration spell asks every frame.
		struct CombatCache
		{
			std::chrono::steady_clock::time_point at{};
			double                                multiplier = 1.0;
			std::uint16_t                         enemyLevel = 0;
			bool                                  valid = false;
		};
		CombatCache g_combat;

		double combat_multiplier(std::uint16_t* a_enemyLevel = nullptr)
		{
			const auto now = std::chrono::steady_clock::now();
			if (!g_combat.valid || now - g_combat.at > std::chrono::milliseconds(250)) {
				g_combat = {};
				g_combat.at = now;
				g_combat.valid = true;
				auto* pc = player();
				if (pc && pc->IsInCombat()) {
					std::uint16_t highest = 0;
					if (auto* lists = RE::ProcessLists::GetSingleton()) {
						lists->ForEachHighActor([&](RE::Actor* a_actor) {
							if (a_actor && a_actor != pc && !a_actor->IsDead() && a_actor->IsInCombat()) {
								const auto target = a_actor->GetActorRuntimeData().currentCombatTarget.get();
								if ((target && target.get() == pc) || a_actor->IsHostileToActor(pc)) {
									highest = std::max(highest, a_actor->GetLevel());
								}
							}
							return RE::BSContainer::ForEachResult::kContinue;
						});
					}
					g_combat.enemyLevel = highest;
					// In a fight with nobody found (the enemy out of range, say): the weakest bonus.
					g_combat.multiplier = core::combat_multiplier(config::get(), std::max<std::uint16_t>(highest, 1));
				}
			}
			if (a_enemyLevel) {
				*a_enemyLevel = g_combat.enemyLevel;
			}
			return g_combat.multiplier;
		}
		void update(RE::ActorMagicCaster* a_self, float a_delta)
		{
			g_update(a_self, a_delta);
			if (!a_self || a_self->state.get() == RE::MagicCaster::State::kNone) {
				return;
			}
			const auto* actor = a_self->GetCasterAsActor();
			if (!actor || !actor->IsPlayerRef()) {
				return;
			}
			auto* spell = a_self->currentSpell ? a_self->currentSpell->As<RE::SpellItem>() : nullptr;
			if (!spell) {
				return;
			}
			note_dual(spell->GetFormID(), a_self->GetIsDualCasting());
			if (a_self->state.get() != RE::MagicCaster::State::kCasting || spell->GetCastingType() != RE::MagicSystem::CastingType::kConcentration) {
				return;
			}
			// Worth the same as casting: a spell that drains more magicka per second earns more per second, clamped
			// like a normal cast. Without this, holding a cheap concentration spell at a wall would out-earn
			// everything else per magicka spent.
			const auto& c = config::get();
			float       cost = 0.0f;
			{
				BaseCost base;
				cost = spell->CalculateMagickaCost(player());
			}
			const double costFactor = core::cost_xp_factor(c, cost);
			// A long frame (loading, alt-tab) must not pay as if the button had been held through it.
			progress::add_xp(spell, core::concentration_xp(c, costFactor, std::min(a_delta, 0.25f), combat_multiplier()));
		}

		// --- XP from casting ---------------------------------------------------------------------------------------

		class CastSink : public RE::BSTEventSink<RE::TESSpellCastEvent>
		{
		public:
			static CastSink* get()
			{
				static CastSink instance;
				return &instance;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESSpellCastEvent* a_event, RE::BSTEventSource<RE::TESSpellCastEvent>*) override
			{
				if (!a_event || !a_event->object || !a_event->object->IsPlayerRef()) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(a_event->spell);
				auto* pc = player();
				if (!spell || !pc || !progress::tracked(spell)) {
					return RE::BSEventNotifyControl::kContinue;
				}
				// Any cast by the player counts, whatever it came from (hands, a staff, another mod's script). Spam is
				// held back by the cooldown below and by XP following the spell's cost.
				const auto& c = config::get();
				const auto  now = std::chrono::steady_clock::now();
				auto&       last = m_lastXp[spell->GetFormID()];
				// A dual-cast may raise the event once per hand; the short floor keeps that to one XP award even if
				// the ini turns the cooldown off.
				if (std::chrono::duration<double>(now - last).count() < std::max(c.sameSpellCooldown, 0.15)) {
					return RE::BSEventNotifyControl::kContinue;
				}
				last = now;

				float cost = 0.0f;
				{
					BaseCost base;
					cost = spell->CalculateMagickaCost(pc);
				}
				const double costFactor = core::cost_xp_factor(c, cost);
				const bool   dual = dual_casting(pc, spell->GetFormID());
				std::uint16_t enemyLevel = 0;
				const double  combat = combat_multiplier(&enemyLevel);
				const double xp = core::cast_xp(c, costFactor, dual, combat);
				if (c.verbose) {
					logs::info("hooks: cast {} (cost {:.1f}, cost factor {:.2f}, dual {}, combat x{:.2f} (highest enemy level {}), xp {:.3f})", spell->GetName(), cost, costFactor, dual, combat, enemyLevel, xp);
				}
				progress::add_xp(spell, xp);
				return RE::BSEventNotifyControl::kContinue;
			}

		private:
			std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point> m_lastXp;
		};
	}

	void install()
	{
		if (REL::Module::IsVR()) {
			logs::warn("hooks: the cost and the menu text are not patched on VR (different addresses); casting and magnitude still work");
		} else {
			// MagicItem::CalculateCost, and the visitor that writes each effect's line of a spell's description.
			g_calcCost = reinterpret_cast<CalcCost_t>(REL::RelocationID(11213, 11321).address());
			g_status.costSites = redirect_calls(reinterpret_cast<std::uintptr_t>(g_calcCost), reinterpret_cast<const void*>(&calc_cost), "spell cost");

			REL::Relocation<std::uintptr_t> visitor{ RE::VTABLE_GetMagicItemDescriptionFunctor[0] };
			g_visit = reinterpret_cast<Visit_t>(visitor.write_vfunc(1, visit_effect));
			g_status.descriptionHooked = true;

			REL::Relocation<std::uintptr_t> menuVisitor{ RE::VTABLE___GetMagicItemDescriptionFunctor[0] };
			g_visitMenu = reinterpret_cast<Visit_t>(menuVisitor.write_vfunc(1, visit_effect_menu));
			g_status.menuDescriptionHooked = true;
		}
		logs::info("hooks: hooking the caster");
		{
			REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_ActorMagicCaster[0] };
			g_adjustEffect = reinterpret_cast<AdjustEffect_t>(vtable.write_vfunc(0x1C, adjust_effect));
			// VR has two extra virtuals in front of Update.
			g_update = reinterpret_cast<Update_t>(vtable.write_vfunc(REL::Module::IsVR() ? 0x1F : 0x1D, update));
			g_status.adjustHooked = true;
			g_status.updateHooked = true;
		}
		logs::info("hooks: hooking effect starts");
		// Every kind of effect that applies a stored magnitude on start and takes the same number off at the end.
		for (const auto& table : { RE::VTABLE_ValueModifierEffect[0], RE::VTABLE_PeakValueModifierEffect[0],
				 RE::VTABLE_DualValueModifierEffect[0], RE::VTABLE_AccumulatingValueModifierEffect[0],
				 RE::VTABLE_TargetValueModifierEffect[0], RE::VTABLE_AbsorbEffect[0], RE::VTABLE_ValueAndConditionsEffect[0] }) {
			hook_start(table);
			++g_status.startClasses;
		}
		RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESSpellCastEvent>(CastSink::get());
		logs::info("hooks: installed (spell cost, effect magnitude, menu text, concentration time, cast events)");
	}

	const Status& status()
	{
		return g_status;
	}

	double debug_combat_multiplier(std::uint16_t* a_enemyLevel)
	{
		return combat_multiplier(a_enemyLevel);
	}

	std::uint64_t cost_changes()
	{
		return g_costChanges.load();
	}

	float debug_cost(const RE::SpellItem* a_spell, RE::Actor* a_actor)
	{
		return calc_cost(a_spell, a_actor);
	}

	void debug_scale(RE::ActiveEffect* a_effect, const RE::Actor* a_caster)
	{
		scale_effect(a_effect, a_caster, "self test");
	}

	void debug_cast(RE::SpellItem* a_spell, bool a_dual)
	{
		auto* pc = player();
		if (!pc || !a_spell) {
			return;
		}
		note_dual(a_spell->GetFormID(), a_dual);
		RE::TESSpellCastEvent event{};
		event.object = RE::NiPointer<RE::TESObjectREFR>(pc);
		event.spell = a_spell->GetFormID();
		CastSink::get()->ProcessEvent(&event, nullptr);
	}
}
