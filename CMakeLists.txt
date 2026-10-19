cmake_minimum_required(VERSION 3.28)

project(sa_server
  VERSION 0.2.0
  DESCRIPTION "MMORPG server: C++ gateway and zone"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# No C++20 modules here. Without this, CMake 3.28+ adds GCC's module-scanning flags
# (-fmodules-ts, -fdeps-format, -fmodule-mapper) to every compile command, and clang-tidy, which
# re-parses those commands with clang, rejects them (issues/004).
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(SA_BUILD_TESTS "Build unit tests" ON)
option(SA_ENABLE_CLANG_TIDY "Run clang-tidy as part of the build" OFF)
set(SA_SANITIZER "" CACHE STRING "Sanitizer preset: '', 'address', or 'thread'")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)   # defines sa::warnings
include(Sanitizers)         # defines sa::sanitizers
include(StaticAnalysis)     # wires clang-tidy when SA_ENABLE_CLANG_TIDY
include(SaTarget)           # sa_apply_policy, sa_add_test

find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(asio CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(cpr CONFIG REQUIRED)            # loadgen's HTTP client for the Backend API
find_package(nlohmann_json CONFIG REQUIRED)

if(SA_BUILD_TESTS)
  find_package(GTest CONFIG REQUIRED)
  include(GoogleTest)
  enable_testing()
endif()

# Each component adds itself here when it lands; the tree always builds as a whole.
add_subdirectory(contracts)
add_subdirectory(server/cpp/common)
add_subdirectory(server/cpp/zone)
add_subdirectory(server/cpp/gateway)
add_subdirectory(tools/loadgen)
