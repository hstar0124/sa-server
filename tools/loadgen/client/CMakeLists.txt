# Network bot client (ADR-0006). Pure parts (profile, world model, report) are a library with
# tests; the executable adds HTTP, sockets and timers.
add_library(sa_loadgen_core STATIC
  src/profile.cpp
  src/world_model.cpp
  src/report.cpp)
add_library(sa::loadgen_core ALIAS sa_loadgen_core)
sa_apply_policy(sa_loadgen_core)
target_include_directories(sa_loadgen_core PUBLIC src)
target_link_libraries(sa_loadgen_core PUBLIC sa::common sa::bot)

add_executable(loadgen
  src/main.cpp
  src/backend_api.cpp
  src/bot_session.cpp)
sa_apply_policy(loadgen)
target_link_libraries(loadgen PRIVATE sa::loadgen_core cpr::cpr nlohmann_json::nlohmann_json)

if(SA_BUILD_TESTS)
  sa_add_test(sa_loadgen_tests
    SOURCES tests/world_model_test.cpp tests/profile_test.cpp tests/report_test.cpp
    LIBS sa::loadgen_core)
endif()
