cmake_minimum_required(VERSION 3.21)

project(ProPager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt meta-object compiler runs automatically on sources that need it.
set(CMAKE_AUTOMOC ON)

# Network + WebSockets are declared now so later waves (ProPresenter REST,
# Slack Socket Mode) need no build-file changes.
find_package(Qt6 REQUIRED COMPONENTS Widgets Network WebSockets)

qt_standard_project_setup()

qt_add_executable(ProPager
    MACOSX_BUNDLE
    src/main.cpp
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
