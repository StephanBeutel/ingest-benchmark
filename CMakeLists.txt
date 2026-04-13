cmake_minimum_required(VERSION 3.22)

project(ingest-benchmark-for-twitch
    VERSION 1.0.0
    DESCRIPTION "Benchmarks Twitch ingest servers and selects the best one"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt automation
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# On macOS out-of-tree builds we link against OBS's bundled Qt frameworks
# (version 6.8.3) rather than Homebrew Qt (6.11), to avoid dyld version
# mismatch at load time.  OBS Qt targets are created inside FindLibObs.cmake.
# We still need Homebrew Qt for MOC/UIC header generation — set the prefix
# so AUTOMOC finds qmake.
if(APPLE)
    if(NOT Qt6_DIR)
        # Try to find Homebrew Qt for AUTOMOC only
        find_package(Qt6 QUIET COMPONENTS Core Gui Widgets Network
            PATHS /opt/homebrew/opt/qt /opt/homebrew/opt/qt6)
    endif()
    # Teach CMake where to find moc/uic/rcc even if we override link targets
    if(Qt6_FOUND)
        set(_obs_qt_tools_from_homebrew TRUE)
    endif()
endif()

# ────────────────────────────────────────────────────────────────────────────────
# OBS detection
#
# Three modes:
#
# 1. Inside OBS source tree: OBS::libobs and OBS::obs-frontend-api already exist
#
# 2. Standard install with cmake config (Linux / Windows OBS SDK):
#      cmake .. -DCMAKE_PREFIX_PATH=/path/to/obs-install
#
# 3. macOS with OBS.app + cloned source headers (no install prefix):
#      cmake .. -DOBS_APP=/Applications/OBS.app \
#               -DOBS_SOURCE_DIR=/tmp/obs-headers
# ────────────────────────────────────────────────────────────────────────────────

# Check if targets already exist (in-tree build or prior find_package success)
if(NOT TARGET OBS::libobs)
    # Try standard find_package first (works with OBS cmake install).
    # Do NOT put cmake/ on CMAKE_MODULE_PATH before this call — our
    # FindLibObs.cmake is macOS-only and would be picked up by find_package on
    # every platform, including Windows where it immediately fatal-errors.
    find_package(libobs QUIET)
    find_package(obs-frontend-api QUIET)
endif()

if(NOT TARGET OBS::libobs)
    # macOS app-bundle path: use our custom finder via include() so it is
    # never accidentally triggered by find_package on other platforms.
    if(APPLE AND (DEFINED OBS_APP OR EXISTS "/Applications/OBS.app"))
        include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindLibObs.cmake")
    elseif(DEFINED OBS_ROOT)
        list(APPEND CMAKE_PREFIX_PATH "${OBS_ROOT}")
        find_package(libobs REQUIRED)
        find_package(obs-frontend-api REQUIRED)
    else()
        message(FATAL_ERROR
            "Could not find OBS libraries.\n"
            "macOS:   cmake .. -DOBS_APP=/Applications/OBS.app -DOBS_SOURCE_DIR=/tmp/obs-headers\n"
            "Other:   cmake .. -DOBS_ROOT=/path/to/obs-install\n")
    endif()
endif()

# ────────────────────────────────────────────────────────────────────────────────
# Qt6
# On non-macOS builds use standard find_package.
# On macOS the OBS-bundled Qt targets are created in FindLibObs.cmake above.
# ────────────────────────────────────────────────────────────────────────────────
if(NOT APPLE)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent)
endif()

find_package(CURL REQUIRED)

# ────────────────────────────────────────────────────────────────────────────────
# Source files
# ────────────────────────────────────────────────────────────────────────────────
set(PLUGIN_SOURCES
    src/plugin-main.cpp

    src/obs/obs-integration.cpp
    src/obs/obs-integration.hpp

    src/network/net-probe.cpp
    src/network/net-probe.hpp

    src/twitch/ingest-fetcher.cpp
    src/twitch/ingest-fetcher.hpp

    src/core/benchmark-engine.cpp
    src/core/benchmark-engine.hpp
    src/core/score-calculator.cpp
    src/core/score-calculator.hpp

    src/ui/benchmark-dock.cpp
    src/ui/benchmark-dock.hpp
    src/ui/results-table.cpp
    src/ui/results-table.hpp

    src/settings/plugin-settings.cpp
    src/settings/plugin-settings.hpp

    resources/plugin.qrc
)

add_library(${PROJECT_NAME} MODULE ${PLUGIN_SOURCES})

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        OBS::libobs
        OBS::obs-frontend-api
        $<IF:$<BOOL:${APPLE}>,OBS::QtCore,Qt6::Core>
        $<IF:$<BOOL:${APPLE}>,OBS::QtGui,Qt6::Gui>
        $<IF:$<BOOL:${APPLE}>,OBS::QtWidgets,Qt6::Widgets>
        $<$<NOT:$<BOOL:${APPLE}>>:Qt6::Network>
        $<$<NOT:$<BOOL:${APPLE}>>:Qt6::Concurrent>
        # libcurl: system library on macOS (TLS via SecureTransport),
        # used for the Twitch ingest list HTTPS fetch instead of Qt network
        # (OBS does not ship a Qt TLS backend plugin).
        CURL::libcurl
)

# ────────────────────────────────────────────────────────────────────────────────
# Platform-specific
# ────────────────────────────────────────────────────────────────────────────────
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32 iphlpapi)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
elseif(APPLE)
    # ── macOS .plugin bundle ──────────────────────────────────────────────────
    # OBS on macOS loads plugins as CFBundle (.plugin) with the layout:
    #   <name>.plugin/Contents/MacOS/<name>
    #
    # CMake MODULE libraries default to lib<name>.so on macOS.
    # We rename the output and structure it as a bundle manually.

    set_target_properties(${PROJECT_NAME} PROPERTIES
        # No "lib" prefix, no ".so" suffix — OBS expects the bare binary name
        PREFIX   ""
        SUFFIX   ""
        # Place the binary inside the bundle structure at build time
        LIBRARY_OUTPUT_DIRECTORY
            "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.plugin/Contents/MacOS"
    )

    # Generate Info.plist into the bundle
    set(_plist_in  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/MacOSXBundleInfo.plist.in")
    set(_plist_out "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.plugin/Contents/Info.plist")
    configure_file("${_plist_in}" "${_plist_out}" @ONLY)

    # Rewrite absolute Homebrew Qt framework paths to @rpath so OBS's bundled
    # Qt frameworks are used at runtime.  We use otool to read the exact
    # embedded paths, then rewrite each Qt one to @rpath — this is resilient
    # to Homebrew symlink vs Cellar path differences.
    find_program(INSTALL_NAME_TOOL install_name_tool REQUIRED)
    find_program(OTOOL otool REQUIRED)

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/cmake/fix-qt-rpaths.sh"
                "$<TARGET_FILE:${PROJECT_NAME}>"
        COMMENT "Rewriting Qt framework paths to @rpath"
        VERBATIM
    )
endif()

# ────────────────────────────────────────────────────────────────────────────────
# Compile definitions
# ────────────────────────────────────────────────────────────────────────────────
target_compile_definitions(${PROJECT_NAME} PRIVATE
    PLUGIN_VERSION="${PROJECT_VERSION}"
    # Suppress Qt's load-time version tag (qt_version_tag_6_N).
    # We compile with Homebrew Qt 6.11 headers but run against OBS's bundled
    # Qt 6.8.3 frameworks.  We have verified no Qt 6.9+ symbols are used, so
    # it is safe to disable the version guard that would otherwise prevent load.
    QT_NO_VERSION_TAGGING
)

# ────────────────────────────────────────────────────────────────────────────────
# Installation
# When building inside OBS source tree, use obs_install_plugin() macro.
# When building out-of-tree, install to the user plugin directory.
# ────────────────────────────────────────────────────────────────────────────────
set(_obs_plugin_dir "$ENV{HOME}/Library/Application Support/obs-studio/plugins")

if(COMMAND obs_install_plugin)
    obs_install_plugin(${PROJECT_NAME})
elseif(WIN32)
    install(TARGETS ${PROJECT_NAME}
        LIBRARY DESTINATION "obs-plugins/64bit"
        RUNTIME DESTINATION "obs-plugins/64bit")
elseif(APPLE)
    install(
        DIRECTORY "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.plugin"
        DESTINATION "${_obs_plugin_dir}"
        USE_SOURCE_PERMISSIONS
    )
else()
    install(TARGETS ${PROJECT_NAME}
        LIBRARY DESTINATION "lib/obs-plugins")
    install(DIRECTORY resources/
        DESTINATION "share/obs/obs-plugins/${PROJECT_NAME}")
endif()
