# Spell Leveling, an SKSE plugin for Skyrim Special Edition.
# Copyright (C) 2026 TheHunterManX
# SPDX-License-Identifier: GPL-3.0-or-later
# Licensed under the GNU General Public License, version 3 or later; see LICENSE.txt.
cmake_minimum_required(VERSION 3.21)
project(spellleveling LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Runtime set must stay at the CommonLibSSE-NG defaults (SE, AE and VR all on).
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

# The CommonLibSSE-NG source tree (https://github.com/alandtse/CommonLibSSE-NG, version in BUILDING.txt). build.ps1 passes the
# real location with -DCOMMONLIB_DIR=...; this default is where BUILDING.txt says to put it.
set(COMMONLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibSSE-NG"
	CACHE PATH "CommonLibSSE-NG source tree")
add_subdirectory("${COMMONLIB_DIR}" commonlib)

# A source build via add_subdirectory does not define the plugin helper.
if(NOT COMMAND add_commonlibsse_plugin)
	include("${COMMONLIB_DIR}/cmake/CommonLibSSE.cmake")
endif()

# The code is in the src folder next to this file in the author's project folder, and in this same folder in the
# release archive (where this file is in src itself).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
	set(SPL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src")
else()
	set(SPL_SRC "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

add_commonlibsse_plugin(spellleveling
	AUTHOR "TheHunterManX"
	NAME "SpellLeveling"
	VERSION ${PROJECT_VERSION}
	SOURCES ${SPL_SRC}/main.cpp ${SPL_SRC}/core.cpp ${SPL_SRC}/config.cpp ${SPL_SRC}/progress.cpp ${SPL_SRC}/hooks.cpp ${SPL_SRC}/selftest.cpp)

set_target_properties(spellleveling PROPERTIES OUTPUT_NAME "SpellLeveling")

target_include_directories(spellleveling PRIVATE ${SPL_SRC})
target_precompile_headers(spellleveling PRIVATE ${SPL_SRC}/pch.h)

# Dev aid: a linker map turns a crash address from the log into a function name.
target_link_options(spellleveling PRIVATE /MAP)
