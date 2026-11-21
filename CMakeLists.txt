cmake_minimum_required(VERSION 3.14)
project(ev_charger LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Use the extraction time (not the archive's) for downloaded files; silences a CMake warning.
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

# SQLite, built from the official source amalgamation (no system install needed)
include(FetchContent)
FetchContent_Declare(
  sqlite_src
  URL https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip
  URL_HASH SHA256=0a9c8e34352dad46006aa82836d5383bff9dc6bc9ce5a60553c05aa30e6ad1f9
)
FetchContent_MakeAvailable(sqlite_src)

find_package(Threads REQUIRED)
add_library(sqlite3_lib STATIC ${sqlite_src_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3_lib PUBLIC ${sqlite_src_SOURCE_DIR})
target_link_libraries(sqlite3_lib PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
target_compile_options(sqlite3_lib PRIVATE -w)  # third-party code: no warnings

# Library holding the Charger class
add_library(charger_lib
  src/charger.cpp
  src/abb_charger.cpp
  src/btc_charger.cpp
  src/battery.cpp
  src/payment_terminal.cpp
  src/payment_session.cpp
  src/charging_app.cpp
  src/charger_spec.cpp
  src/charger_repository.cpp
  src/charger_database.cpp
  src/charger_factory.cpp
)
target_include_directories(charger_lib PUBLIC include)
target_link_libraries(charger_lib PUBLIC sqlite3_lib)

# Optional code coverage (GCC/Clang). Only our own library is instrumented, not
# SQLite or GoogleTest. Run scripts/coverage.sh to build, test and make a report.
option(ENABLE_COVERAGE "Instrument charger_lib with gcov coverage" OFF)
if(ENABLE_COVERAGE)
  target_compile_options(charger_lib PRIVATE --coverage -O0 -g)
  target_link_libraries(charger_lib PUBLIC --coverage)
endif()

# Command-line demo
add_executable(ev_charger src/main.cpp)
target_link_libraries(ev_charger PRIVATE charger_lib)

# REST interface: cpp-httplib (HTTP) and nlohmann/json, both header-only and hash-pinned.
FetchContent_Declare(
  httplib_src
  URL https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.18.3/httplib.h
  URL_HASH SHA256=a0a0c13dc086663863dbe6730a19716f7d3744904b6205496e8301750814dbd5
  DOWNLOAD_NO_EXTRACT TRUE  # a single header; the repo zip has filenames some locales cannot extract
)
FetchContent_Declare(
  json_src
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/include.zip
  URL_HASH SHA256=a22461d13119ac5c78f205d3df1db13403e58ce1bb1794edc9313677313f4a9d
)
FetchContent_MakeAvailable(httplib_src json_src)

add_library(rest_lib src/rest_api.cpp)
target_include_directories(rest_lib PUBLIC include)
target_include_directories(rest_lib SYSTEM PUBLIC ${httplib_src_SOURCE_DIR} ${json_src_SOURCE_DIR}/include)
target_link_libraries(rest_lib PUBLIC charger_lib Threads::Threads)

add_executable(ev_charger_server src/server_main.cpp)
target_link_libraries(ev_charger_server PRIVATE rest_lib)

# Unit tests (GoogleTest, downloaded at configure time)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

# Each group below is its own test program, so the Testing panel (and ctest)
# shows them as separate groups. Test names are prefixed "<group>." so the
# panel can split them into a tree (see .vscode/settings.json).
function(add_ev_tests group)
  set(target ${group}_tests)
  add_executable(${target} ${ARGN})
  target_link_libraries(${target} PRIVATE charger_lib GTest::gtest_main GTest::gmock)
  gtest_discover_tests(${target} TEST_PREFIX "${group}.")
endfunction()

add_ev_tests(battery
  tests/battery_test.cpp
)
add_ev_tests(charger
  tests/charger_test.cpp
  tests/charger_types_test.cpp
)
add_ev_tests(payment
  tests/payment_terminal_test.cpp
  tests/payment_session_test.cpp
)
add_ev_tests(app
  tests/charging_app_test.cpp
)
add_ev_tests(database
  tests/charger_database_test.cpp
  tests/charger_factory_test.cpp
  tests/charger_repository_test.cpp
)
add_ev_tests(rest
  tests/rest_api_test.cpp
)
target_link_libraries(rest_tests PRIVATE rest_lib)
# End-to-end "master" suites
add_ev_tests(payter_master
  tests/payter_master_test.cpp
)
add_ev_tests(app_master
  tests/app_master_test.cpp
)
