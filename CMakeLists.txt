cmake_minimum_required(VERSION 3.20)
project(sterrenvaarder VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Core data library: reads the original game data from the user's own install.
add_library(sterrenvaarder_data
  src/sterrenvaarder/hash.cpp
  src/sterrenvaarder/vfs.cpp
  src/sterrenvaarder/ini.cpp
  src/sterrenvaarder/utf.cpp
  src/sterrenvaarder/vmesh.cpp
  src/sterrenvaarder/texture.cpp
  src/sterrenvaarder/model.cpp
  src/sterrenvaarder/png.cpp
  src/sterrenvaarder/universe.cpp
  src/sterrenvaarder/sphere.cpp
  src/sterrenvaarder/ships.cpp
  src/sterrenvaarder/resources.cpp
  src/sterrenvaarder/economy.cpp
)
target_include_directories(sterrenvaarder_data PUBLIC src)

# Game logic without graphics (flight model); unit-tested headless.
add_library(sterrenvaarder_game src/game/flight.cpp src/game/combat.cpp src/game/navigation.cpp src/game/trade.cpp)
target_include_directories(sterrenvaarder_game PUBLIC src)
target_link_libraries(sterrenvaarder_game PUBLIC sterrenvaarder_data)

add_executable(sv-dump tools/sv_dump.cpp)
target_link_libraries(sv-dump PRIVATE sterrenvaarder_data)

add_executable(sv-export tools/sv_export.cpp)
target_link_libraries(sv-export PRIVATE sterrenvaarder_data)

enable_testing()
add_executable(sterrenvaarder_tests tests/unit_tests.cpp)
target_link_libraries(sterrenvaarder_tests PRIVATE sterrenvaarder_game)
add_test(NAME unit COMMAND sterrenvaarder_tests)

add_executable(sterrenvaarder_corpus tests/corpus_test.cpp)
target_link_libraries(sterrenvaarder_corpus PRIVATE sterrenvaarder_data)

# ---------------------------------------------------------------- client (renderer + viewer)
option(SV_BUILD_CLIENT "Build the renderer and viewer (fetches SDL3)" ON)
if(SV_BUILD_CLIENT)
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.20
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(SDL3)

  add_library(sterrenvaarder_render
    src/render/gpu.cpp
    src/render/renderer.cpp
    src/render/hud_batch.cpp
    src/game/hud.cpp
  )
  target_include_directories(sterrenvaarder_render PUBLIC src)
  target_link_libraries(sterrenvaarder_render PUBLIC sterrenvaarder_game SDL3::SDL3-static)

  add_executable(sterrenvaarder app/main.cpp)
  target_link_libraries(sterrenvaarder PRIVATE sterrenvaarder_render)
endif()
