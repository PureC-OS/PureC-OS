cmake_minimum_required(VERSION 3.21)
project(purec-qt-apps LANGUAGES C CXX)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

get_filename_component(PUREC_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(PUREC_BIN "${PUREC_ROOT}/bin")
if(DEFINED ENV{BIN_DIR})
  set(PUREC_BIN "$ENV{BIN_DIR}")
endif()

add_subdirectory(common)
add_subdirectory(terminal)
add_subdirectory(login)
