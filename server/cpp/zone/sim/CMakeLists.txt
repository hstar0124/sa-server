# Pure simulation core (ADR-0008): no I/O, no threads, no protobuf. Clock and RNG are injected.
add_library(sa_zone_sim STATIC
  src/fixed_step_scheduler.cpp
  src/map_data.cpp
  src/content.cpp
  src/spatial_grid.cpp
  src/zone.cpp
  src/zone_combat.cpp
  src/zone_monsters.cpp
  src/zone_items.cpp)
add_library(sa::zone_sim ALIAS sa_zone_sim)
sa_apply_policy(sa_zone_sim)

target_include_directories(sa_zone_sim PUBLIC include)
# Depends on sa::common only for ids and geometry; sa::common's protobuf linkage is incidental.
target_link_libraries(sa_zone_sim PUBLIC sa::common)

if(SA_BUILD_TESTS)
  sa_add_test(sa_zone_sim_tests
    SOURCES
      tests/fixed_step_scheduler_test.cpp
      tests/rng_test.cpp
      tests/geometry_test.cpp
      tests/map_data_test.cpp
      tests/spatial_grid_test.cpp
      tests/zone_movement_test.cpp
      tests/zone_interest_test.cpp
      tests/zone_combat_test.cpp
      tests/zone_monster_test.cpp
      tests/zone_items_test.cpp
      tests/test_maps.hpp
    LIBS sa::zone_sim)
endif()
