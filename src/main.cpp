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
#include "hooks.h"
#include "progress.h"
#include "selftest.h"

#include <chrono>
#include <thread>

namespace
{
	// Relative to the game folder, which is the process working directory.
	constexpr auto kIniPath = "Data/SKSE/Plugins/SpellLeveling.ini";

	void on_message(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			logs::info("data loaded: scanning spells");
			progress::on_data_loaded();
			logs::info("data loaded: installing hooks");
			hooks::install();
			// This message arrives on a loading thread. The self test (only when its switch file exists) waits for the
			// game's main thread instead.
			if (selftest::is_on()) {
				SKSE::GetTaskInterface()->AddTask([] { selftest::run_menu(); });
			}
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			// The cosave has been read by now: write every spell's name from its level.
			progress::refresh_names();
			if (a_msg->type == SKSE::MessagingInterface::kPostLoadGame) {
				if (selftest::is_on()) {
					// The load has only just finished: give the player a few seconds to be standing in the world before
					// the checks call the game's own cast code.
					std::thread([] {
						std::this_thread::sleep_for(std::chrono::seconds(5));
						SKSE::GetTaskInterface()->AddTask([] { selftest::run_loaded(); });
					}).detach();
				}
			}
			break;
		default:
			break;
		}
	}
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	// The trampoline is the space the redirected calls jump through.
	SKSE::InitInfo info{};
	info.trampoline = true;
	info.trampolineSize = 1024;
	SKSE::Init(a_skse, info);
	spdlog::default_logger()->flush_on(spdlog::level::info);

	logs::info("SpellLeveling loaded");
	config::load(kIniPath);
	if (selftest::enabled()) {
		// With the self test on, a crash writes where it happened into the log (the game's own crash logger may not be installed).
		AddVectoredExceptionHandler(1, [](EXCEPTION_POINTERS* info) -> LONG {
			const auto code = info->ExceptionRecord->ExceptionCode;
			if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_STACK_OVERFLOW || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
				code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == 0xC0000374 /* heap corruption */ || code == 0xC0000409 /* fast fail */) {
				void*       frames[24]{};
				const auto  count = CaptureStackBackTrace(0, 24, frames, nullptr);
				std::string text;
				for (USHORT i = 0; i < count; ++i) {
					HMODULE module = nullptr;
					char    name[MAX_PATH]{};
					GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCSTR>(frames[i]), &module);
					if (module) {
						GetModuleFileNameA(module, name, MAX_PATH);
					}
					const char* base = std::strrchr(name, '\\');
					text += std::format("\n    {}+{:X}", base ? base + 1 : "?", reinterpret_cast<std::uintptr_t>(frames[i]) - reinterpret_cast<std::uintptr_t>(module));
				}
				logs::critical("CRASH exception {:08X} at {:p}; access type {} address {:p}; stack:{}", static_cast<std::uint32_t>(code),
					info->ExceptionRecord->ExceptionAddress, info->ExceptionRecord->NumberParameters > 0 ? info->ExceptionRecord->ExceptionInformation[0] : 0,
					reinterpret_cast<void*>(info->ExceptionRecord->NumberParameters > 1 ? info->ExceptionRecord->ExceptionInformation[1] : 0), text);
				spdlog::default_logger()->flush();
			}
			return EXCEPTION_CONTINUE_SEARCH;
		});
	}
	progress::install_serialization();
	SKSE::GetMessagingInterface()->RegisterListener(on_message);
	return true;
}
