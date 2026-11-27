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
)
target_include_directories(sterrenvaarder_data PUBLIC src)

add_executable(sv-dump tools/sv_dump.cpp)
target_link_libraries(sv-dump PRIVATE sterrenvaarder_data)

add_executable(sv-export tools/sv_export.cpp)
target_link_libraries(sv-export PRIVATE sterrenvaarder_data)

enable_testing()
add_executable(sterrenvaarder_tests tests/unit_tests.cpp)
target_link_libraries(sterrenvaarder_tests PRIVATE sterrenvaarder_data)
add_test(NAME unit COMMAND sterrenvaarder_tests)

add_executable(sterrenvaarder_corpus tests/corpus_test.cpp)
target_link_libraries(sterrenvaarder_corpus PRIVATE sterrenvaarder_data)
