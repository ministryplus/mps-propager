cmake_minimum_required(VERSION 3.21)

project(ProPager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt meta-object compiler runs automatically on sources that need it.
set(CMAKE_AUTOMOC ON)

# Network + WebSockets are declared now so later waves (ProPresenter REST,
# Slack Socket Mode) need no build-file changes. Test drives the unit tests.
find_package(Qt6 REQUIRED COMPONENTS Widgets Network WebSockets Test)

qt_standard_project_setup()

# App icon (Decision 11). The .icns lives in the bundle's Resources dir and is
# named in Info.plist via MACOSX_BUNDLE_ICON_FILE below.
set(PROPAGER_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/data/propager.icns")
set_source_files_properties(${PROPAGER_APP_ICON} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources"
)

qt_add_executable(ProPager
    MACOSX_BUNDLE
    ${PROPAGER_APP_ICON}
    src/main.cpp
    src/config.cpp
    src/propresenterclient.cpp
    src/pagercontroller.cpp
    src/slackclient.cpp
    src/logbroker.cpp
    src/ui/mainwindow.cpp
    src/ui/traymenu.cpp
)

# main.cpp and the UI sources include headers by "config.h" / "pagercontroller.h"
# etc.; put src on the include path so src/ui/*.cpp resolve them.
target_include_directories(ProPager PRIVATE src src/ui)

target_link_libraries(ProPager PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::WebSockets
)

# macOS bundle identity (Decision 11). Display name / window title is ProPager;
# the bundle identifier drives code-signing and QSettings later.
set_target_properties(ProPager PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "ProPager"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.isaacwiebe.propager"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
    MACOSX_BUNDLE_ICON_FILE "propager.icns"
)

# Bundle Qt frameworks/plugins and fix rpaths into the .app on install. This is
# the modern replacement for calling macdeployqt by hand; build.sh drives it via
# `cmake --install`. Guarded to Apple so the ctest configure on other hosts is
# unaffected.
if(APPLE)
    # Copy the built .app into the install prefix FIRST, then let the generated
    # deploy script fix it up in place. Without the install(TARGETS) rule the
    # prefix dir never gets created and macdeployqt fails with ENOENT.
    install(TARGETS ProPager BUNDLE DESTINATION .)
    qt_generate_deploy_app_script(
        TARGET ProPager
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${deploy_script})
endif()

# --- Tests -----------------------------------------------------------------
enable_testing()

qt_add_executable(tst_config
    tests/tst_config.cpp
    src/config.cpp
)

target_include_directories(tst_config PRIVATE src)

target_link_libraries(tst_config PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME tst_config COMMAND tst_config)

qt_add_executable(tst_propresenterclient
    tests/tst_propresenterclient.cpp
    src/propresenterclient.cpp
    src/config.cpp
)

target_include_directories(tst_propresenterclient PRIVATE src)

target_link_libraries(tst_propresenterclient PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
)

add_test(NAME tst_propresenterclient COMMAND tst_propresenterclient)

qt_add_executable(tst_slackclient
    tests/tst_slackclient.cpp
    src/slackclient.cpp
    src/pagercontroller.cpp
    src/propresenterclient.cpp
    src/config.cpp
)

target_include_directories(tst_slackclient PRIVATE src)

target_link_libraries(tst_slackclient PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::WebSockets
    Qt6::Test
)

add_test(NAME tst_slackclient COMMAND tst_slackclient)

qt_add_executable(tst_pagercontroller
    tests/tst_pagercontroller.cpp
    src/pagercontroller.cpp
    src/propresenterclient.cpp
    src/config.cpp
)

target_include_directories(tst_pagercontroller PRIVATE src)

target_link_libraries(tst_pagercontroller PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
)

add_test(NAME tst_pagercontroller COMMAND tst_pagercontroller)

qt_add_executable(tst_ui
    tests/tst_ui.cpp
    src/ui/mainwindow.cpp
    src/pagercontroller.cpp
    src/propresenterclient.cpp
    src/logbroker.cpp
    src/config.cpp
)

target_include_directories(tst_ui PRIVATE src src/ui)

target_link_libraries(tst_ui PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Test
)

add_test(NAME tst_ui COMMAND tst_ui)
