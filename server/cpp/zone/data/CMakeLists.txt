# Loads game-data/*.toml into the simulation's plain structs. This is the only place the zone
# touches content files; the simulation itself stays I/O-free (ADR-0008).
add_library(sa_zone_data STATIC src/game_data_loader.cpp src/toml_fields.cpp)
add_library(sa::zone_data ALIAS sa_zone_data)
sa_apply_policy(sa_zone_data)
target_include_directories(sa_zone_data PUBLIC include)
target_link_libraries(sa_zone_data PUBLIC sa::zone_sim sa::common tomlplusplus::tomlplusplus)

if(SA_BUILD_TESTS)
  sa_add_test(sa_zone_data_tests SOURCES tests/game_data_loader_test.cpp LIBS sa::zone_data)
  # Tests read the real content files so a broken map fails CI, not the first play session.
  target_compile_definitions(sa_zone_data_tests PRIVATE SA_GAME_DATA_DIR="${CMAKE_SOURCE_DIR}/game-data")
endif()
