cmake_minimum_required(VERSION 3.20)
project(bytebraid VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(bytebraid_lib src/analyzer.cpp)
target_include_directories(bytebraid_lib PUBLIC include)
if(MSVC)
  target_compile_options(bytebraid_lib PRIVATE /W4 /permissive-)
else()
  target_compile_options(bytebraid_lib PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

add_executable(bytebraid src/main.cpp)
target_link_libraries(bytebraid PRIVATE bytebraid_lib)

enable_testing()
add_executable(bytebraid_tests tests/test_analyzer.cpp)
target_link_libraries(bytebraid_tests PRIVATE bytebraid_lib)
add_test(NAME bytebraid_tests COMMAND bytebraid_tests)
