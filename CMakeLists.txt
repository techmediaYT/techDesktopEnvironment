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

# QTP0004 (NEW, Qt 6.8+): makes every QML file in a module see every
# other QML file in that same module regardless of which subdirectory
# each one lives in. Without this, a type in one subdirectory
# (qml/decoration/TitleBar.qml, shared by tsettings and about-tde) is
# invisible to a file in a different subdirectory of the same module
# (qml/about/AboutWindow.qml, qml/settings/PlaceholderWindow.qml)
# unless that file explicitly imports the module by its own URI --
# which doesn't even make sense for a file importing its own module.
# This was the actual cause of "TitleBar is not a type" at runtime
# despite TitleBar.qml being correctly listed in the same
# qt_add_qml_module(...) QML_FILES list. If the installed Qt predates
# 6.8 (no QTP0004 policy exists yet), this call is skipped rather than
# erroring, and TitleBar.qml would need to move into each consuming
# app's own qml/ subdirectory instead as a fallback fix.
if(POLICY QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

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
    src/common/Theme.h
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
    src/common/Theme.h
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
    QML_FILES
        qml/settings/PlaceholderWindow.qml
        qml/decoration/TitleBar.qml
    SOURCES
        src/common/Theme.h
)

target_link_libraries(tsettings PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
)

target_include_directories(tsettings PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

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
    QML_FILES
        qml/about/AboutWindow.qml
        qml/decoration/TitleBar.qml
    SOURCES
        src/common/Theme.h
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

target_include_directories(about-tde PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

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
