cmake_minimum_required(VERSION 3.10)

set(PROJECT_NAME "flutter_usb_event")
project(${PROJECT_NAME} LANGUAGES CXX)

set(PLUGIN_NAME "flutter_usb_event_plugin")

list(APPEND PLUGIN_SOURCES
  "flutter_usb_event_plugin.cc"
)

add_library(${PLUGIN_NAME} SHARED
  ${PLUGIN_SOURCES}
)

apply_standard_settings(${PLUGIN_NAME})

set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden)
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)

target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")

# Build requires libudev-dev (Debian/Ubuntu) or systemd-devel (Fedora)
find_package(PkgConfig REQUIRED)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)

target_link_libraries(${PLUGIN_NAME} PRIVATE flutter)
target_link_libraries(${PLUGIN_NAME} PRIVATE PkgConfig::GTK)
target_link_libraries(${PLUGIN_NAME} PRIVATE PkgConfig::UDEV)

set(flutter_usb_event_bundled_libraries
  ""
  PARENT_SCOPE
)
