cmake_minimum_required(VERSION 3.16)
project(tde-moonlight VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Experimental compositor blur support (see src/common/PanelBlur.h).
# Depends on Qt's private headers (Qt6::GuiPrivate) which some
# Debian/Ubuntu setups split into a separate qt6-base-private-dev
# package -- if that's not installed, or the blur feature otherwise
# fails to build, run cmake again with -DTDE_ENABLE_BLUR=OFF to get a
# clean build of everything else without needing to touch any other
# file. It also currently produces no visible effect at all under Sway
# specifically (Sway doesn't implement this protocol as of 1.11), so
# turning it off costs nothing when testing there.
option(TDE_ENABLE_BLUR "Enable experimental compositor background blur" ON)

# Real background-luminance sampling for the topbar (see
# src/common/BackgroundSampler.h) -- uses ext-image-copy-capture-v1 /
# ext-image-capture-source-v1, the modern replacement for wlroots' older
# wlr-screencopy-unstable-v1 (which upstream itself now points users
# away from). Sway has supported these since 1.11 (June 2025); on an
# older Sway or a compositor that doesn't implement them at all, this
# stays inactive and TopBar.qml's isDarkBackground keeps its safe
# hardcoded default instead of erroring.
option(TDE_ENABLE_BACKGROUND_SAMPLING "Enable real background sampling for topbar contrast" ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 QuickEffects DBus)

# LayerShellQt provides the Qt bindings for the wlr-layer-shell Wayland
# protocol -- this is what lets a normal Qt window behave like a real
# desktop panel (pinned to a screen edge, exclusive zone, no decorations,
# always-on-top) instead of a floating app window.
find_package(LayerShellQt REQUIRED)

qt_standard_project_setup()

# -----------------------------------------------------------------------
# Wayland protocol-extension dependencies -- shared setup for both blur
# (TDE_ENABLE_BLUR) and background sampling (TDE_ENABLE_BACKGROUND_SAMPLING),
# since both need the same Qt6::WaylandClient / GuiPrivate / wayland-client
# pkg-config plumbing and both generate into the same object library (see
# the comment below on why generation happens exactly once).
# -----------------------------------------------------------------------
set(TDE_BLUR_LIBS "")
set(TDE_BACKGROUND_SAMPLING_LIBS "")
if(TDE_ENABLE_BLUR OR TDE_ENABLE_BACKGROUND_SAMPLING)
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate WaylandClient)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WaylandClientPC REQUIRED IMPORTED_TARGET wayland-client)

    # Core wl_compositor/wl_region/wl_output bindings come from
    # libwayland's own protocol XML (ships as part of libwayland-dev) --
    # ask pkg-config where it actually installed it.
    pkg_get_variable(WAYLAND_CORE_PROTOCOL_DIR wayland-client pkgdatadir)
    set(WAYLAND_CORE_XML "${WAYLAND_CORE_PROTOCOL_DIR}/wayland.xml")

    # Generated ONCE here rather than separately per-binary -- calling
    # qt6_generate_wayland_protocol_client_sources on two different
    # targets against the same XML files makes them both try to write
    # the same generated file paths into the shared build directory,
    # which CMake rejects as a duplicate custom rule. Each feature's XML
    # is only added to the FILES list when its own option is actually
    # ON, so turning one feature off doesn't pull in the other's
    # protocol.
    add_library(tde_wayland_protocols OBJECT)
    target_link_libraries(tde_wayland_protocols PUBLIC Qt6::WaylandClient)

    set(TDE_WAYLAND_PROTOCOL_FILES "${WAYLAND_CORE_XML}")
    if(TDE_ENABLE_BLUR)
        list(APPEND TDE_WAYLAND_PROTOCOL_FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/protocols/ext-background-effect-v1.xml")
    endif()
    if(TDE_ENABLE_BACKGROUND_SAMPLING)
        list(APPEND TDE_WAYLAND_PROTOCOL_FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/protocols/ext-image-capture-source-v1.xml"
            "${CMAKE_CURRENT_SOURCE_DIR}/protocols/ext-image-copy-capture-v1.xml")
    endif()

    qt6_generate_wayland_protocol_client_sources(tde_wayland_protocols FILES
        ${TDE_WAYLAND_PROTOCOL_FILES}
    )
    # Generated headers land directly in the top-level build directory
    # regardless of which target triggered generation.
    target_include_directories(tde_wayland_protocols PUBLIC ${CMAKE_BINARY_DIR})

    if(TDE_ENABLE_BLUR)
        set(TDE_BLUR_LIBS Qt6::GuiPrivate Qt6::WaylandClient tde_wayland_protocols)
    endif()
    if(TDE_ENABLE_BACKGROUND_SAMPLING)
        set(TDE_BACKGROUND_SAMPLING_LIBS Qt6::GuiPrivate Qt6::WaylandClient tde_wayland_protocols)
        target_compile_definitions(tde_wayland_protocols PUBLIC TDE_BACKGROUND_SAMPLING_ENABLED)
    endif()
endif()

# -----------------------------------------------------------------------
# Shared design tokens (Theme.qml) -- a QML-only module with no C++, so
# every process (topbar, dock, tsettings) that imports
# TDEMoonlight.Theme gets the same corner radius and any other token
# defined there, without duplicating the value in each target's own
# QML_FILES list. Built once, ahead of the executables that use it.
# -----------------------------------------------------------------------
qt_add_qml_module(tde_theme
    URI TDEMoonlight.Theme
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/TDEMoonlight/Theme
    QML_FILES
        qml/theme/Theme.qml
    STATIC
)

# -----------------------------------------------------------------------
# Shared client-side window decoration (TitleBar.qml) -- see that
# file's own header comment for the important scope note: this is a
# titlebar an app draws as part of its own window content (client-side
# decoration), not a compositor-drawn server-side decoration, and it
# cannot appear on windows belonging to other applications. Depends on
# tde_theme for its corner-radius icon swatch.
# -----------------------------------------------------------------------
qt_add_qml_module(tde_decoration
    URI TDEMoonlight.Decoration
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/TDEMoonlight/Decoration
    QML_FILES
        qml/decoration/TitleBar.qml
    STATIC
)
target_link_libraries(tde_decoration PRIVATE tde_themeplugin)

# -----------------------------------------------------------------------
# Force-link helper for tde_themeplugin / tde_decorationplugin into any
# executable that imports TDEMoonlight.Theme / TDEMoonlight.Decoration.
#
# Why this is needed at all: qt_add_qml_module(... STATIC) builds each
# module's type-registration code into a plain static library archive.
# A static archive only pulls in the object files a linker can already
# see are referenced from elsewhere -- but nothing in tde-topbar's (or
# tde-dock's, tsettings', about-tde's) own C++ actually calls into
# tde_themeplugin/tde_decorationplugin directly, so the linker has no
# reason to keep their static plugin-registration constructors, and
# they get silently dropped. The QML engine then has no idea these
# types exist at runtime ("Theme is not defined"), even though the
# build succeeds and the plugin target is correctly listed in
# target_link_libraries.
#
# The Qt-documented fix for this is Q_IMPORT_QML_PLUGIN(ClassName) in
# a .cpp file, but the generated plugin class name's exact naming rule
# is inconsistently documented across Qt versions (URI-with-dots-as-
# underscores, sometimes with a "Plugin" suffix appended, sometimes
# not) and depends on internal qt_add_qml_module details this project
# doesn't control -- guessing it risks a second, harder-to-diagnose
# "undefined reference" at link time instead of this one.
#
# --whole-archive sidesteps needing that name at all: it tells the GNU
# linker (this project already assumes gcc/GNU ld, per its Linux-only
# LayerShellQt/Wayland dependencies) to keep every object file from the
# named static library, not just the ones something else already
# references -- which is exactly what's needed to keep a plugin's
# static registration constructor alive with zero guessing involved.
#
# Takes an explicit list of which plugin(s) a target actually needs
# (ARGN) rather than always linking both tde_themeplugin and
# tde_decorationplugin into every caller: a target that never imports
# TDEMoonlight.Decoration (tde-dock, which only uses
# TDEMoonlight.Theme) has no reason to force-keep Decoration's static
# registration code, and doing so anyway risks exactly the kind of
# crash this was tracking down -- force-linking a plugin's global
# static initializer for a module qt_add_qml_module never told this
# specific target's own QML_IMPORT metadata to expect can misregister
# or double-register types against the engine in ways that show up as
# a hard-to-diagnose crash rather than a clean error.
function(tde_link_static_qml_plugins target)
    target_link_libraries(${target} PRIVATE
        -Wl,--whole-archive
        ${ARGN}
        -Wl,--no-whole-archive
    )
endfunction()

# -----------------------------------------------------------------------
# Shared backend code, used by more than one binary (AppModel by both
# dock and app menu; DesktopDaemon is its own binary but its header is
# also referenced by AppModel for the D-Bus interface name/path
# constants living alongside it). Built once as an object library and
# linked into whichever binaries need it, rather than recompiling per
# target or duplicating the source.
# -----------------------------------------------------------------------
add_library(tde_common OBJECT
    src/common/AppModel.cpp
    src/common/AppModel.h
    src/common/DesktopDaemon.cpp
    src/common/DesktopDaemon.h
)

target_include_directories(tde_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

target_link_libraries(tde_common PUBLIC
    Qt6::Core
    Qt6::DBus
)

# -----------------------------------------------------------------------
# Topbar
# -----------------------------------------------------------------------
qt_add_executable(tde-topbar
    src/topbar/main.cpp
)

set(TOPBAR_QML_SOURCES
    src/common/BatteryMonitor.h
    src/common/IconThemeProvider.h
    src/common/LayerShellHelper.h
    src/common/NetworkStatusModel.h
    src/common/BluetoothStatusModel.h
    src/common/MediaPlayerModel.h
    src/common/VolumeModel.h
    src/common/PowerManager.h
    src/common/DockRemote.h
    src/common/SettingsLauncher.h
)
if(TDE_ENABLE_BLUR)
    list(APPEND TOPBAR_QML_SOURCES src/common/PanelBlur.h)
endif()
if(TDE_ENABLE_BACKGROUND_SAMPLING)
    list(APPEND TOPBAR_QML_SOURCES src/common/BackgroundSampler.h)
endif()

qt_add_qml_module(tde-topbar
    URI TDEMoonlight.TopBar
    VERSION 1.0
    IMPORT_PATH ${CMAKE_BINARY_DIR}/qml
    QML_FILES
        qml/topbar/TopBar.qml
        qml/topbar/StatusIcon.qml
        qml/topbar/NotifPill.qml
        qml/topbar/ControlPanel.qml
        qml/topbar/QuickSettingTile.qml
        qml/topbar/PowerMenu.qml
        qml/topbar/PowerMenuButton.qml
    SOURCES
        ${TOPBAR_QML_SOURCES}
)

target_link_libraries(tde-topbar PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickEffects
    Qt6::DBus
    LayerShellQt::Interface
    ${TDE_BLUR_LIBS}
    ${TDE_BACKGROUND_SAMPLING_LIBS}
)
tde_link_static_qml_plugins(tde-topbar tde_themeplugin)

target_include_directories(tde-topbar PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

if(TDE_ENABLE_BLUR)
    target_compile_definitions(tde-topbar PRIVATE TDE_BLUR_ENABLED)
endif()
if(TDE_ENABLE_BACKGROUND_SAMPLING)
    target_compile_definitions(tde-topbar PRIVATE TDE_BACKGROUND_SAMPLING_ENABLED)
endif()

# -----------------------------------------------------------------------
# Dock
# -----------------------------------------------------------------------
qt_add_executable(tde-dock
    src/dock/main.cpp
)

set(DOCK_QML_SOURCES
    src/common/IconThemeProvider.h
    src/common/LayerShellHelper.h
    src/dock/DockController.h
)
if(TDE_ENABLE_BLUR)
    list(APPEND DOCK_QML_SOURCES src/common/PanelBlur.h)
endif()

qt_add_qml_module(tde-dock
    URI TDEMoonlight.Dock
    VERSION 1.0
    IMPORT_PATH ${CMAKE_BINARY_DIR}/qml
    QML_FILES
        qml/dock/Dock.qml
        qml/dock/DockItem.qml
        qml/dock/AppMenu.qml
    SOURCES
        ${DOCK_QML_SOURCES}
)

target_link_libraries(tde-dock PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::DBus
    LayerShellQt::Interface
    tde_common
    ${TDE_BLUR_LIBS}
)
tde_link_static_qml_plugins(tde-dock tde_themeplugin)

target_include_directories(tde-dock PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

if(TDE_ENABLE_BLUR)
    target_compile_definitions(tde-dock PRIVATE TDE_BLUR_ENABLED)
endif()

# -----------------------------------------------------------------------
# Wallpaper
# -----------------------------------------------------------------------
qt_add_executable(tde-wallpaper
    src/wallpaper/main.cpp
)

qt_add_qml_module(tde-wallpaper
    URI TDEMoonlight.Wallpaper
    VERSION 1.0
    QML_FILES
        qml/wallpaper/WallpaperSurface.qml
)

target_link_libraries(tde-wallpaper PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    LayerShellQt::Interface
)

# -----------------------------------------------------------------------
# tSettings (placeholder system settings app -- see
# src/common/SettingsLauncher.h and src/tsettings/main.cpp for what this
# currently is/isn't. Real enough to build and run, not yet a real
# settings app.)
# -----------------------------------------------------------------------
qt_add_executable(tsettings
    src/tsettings/main.cpp
)

qt_add_qml_module(tsettings
    URI TDEMoonlight.Settings
    VERSION 1.0
    IMPORT_PATH ${CMAKE_BINARY_DIR}/qml
    QML_FILES
        qml/settings/PlaceholderWindow.qml
)

target_link_libraries(tsettings PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
)
tde_link_static_qml_plugins(tsettings tde_themeplugin tde_decorationplugin)

# -----------------------------------------------------------------------
# About tDE -- real, buildable app matching
# tDE-Moonlight-Interface-AppBar.png. Version/build strings come from
# real metadata captured at configure time (git describe when the
# source is a git checkout, PROJECT_VERSION otherwise), not hardcoded
# placeholder text.
# -----------------------------------------------------------------------
find_package(Git QUIET)
set(TDE_BUILD_INFO "unknown (not a git checkout)")
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE TDE_GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(TDE_GIT_HASH)
        set(TDE_BUILD_INFO "${TDE_GIT_HASH}")
    endif()
endif()

qt_add_executable(about-tde
    src/about-tde/main.cpp
)

qt_add_qml_module(about-tde
    URI TDEMoonlight.About
    VERSION 1.0
    IMPORT_PATH ${CMAKE_BINARY_DIR}/qml
    QML_FILES
        qml/about/AboutWindow.qml
)

target_compile_definitions(about-tde PRIVATE
    TDE_VERSION_STRING="${PROJECT_VERSION}"
    TDE_BUILD_INFO="${TDE_BUILD_INFO}"
)

target_link_libraries(about-tde PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
)
tde_link_static_qml_plugins(about-tde tde_themeplugin tde_decorationplugin)

# -----------------------------------------------------------------------
# Daemon (background D-Bus service, no GUI/QML)
# -----------------------------------------------------------------------
add_executable(tde-daemon
    src/daemon/main.cpp
)

target_link_libraries(tde-daemon PRIVATE
    Qt6::Core
    Qt6::DBus
    tde_common
)

target_include_directories(tde-daemon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

install(TARGETS tde-topbar tde-dock tde-wallpaper tde-daemon tsettings about-tde RUNTIME DESTINATION bin)
