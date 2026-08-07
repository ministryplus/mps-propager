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

qt_add_executable(ProPager
    MACOSX_BUNDLE
    src/main.cpp
    src/config.cpp
    src/propresenterclient.cpp
    src/pagercontroller.cpp
    src/slackclient.cpp
)

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
)

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
