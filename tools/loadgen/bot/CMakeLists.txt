# Bot brains (ADR-0006): deterministic decision makers that turn what a client observes into the
# next intent (movement, skill, pickup, zone change, trade). Shared by the in-process dev bots and,
# from stage 3, the network loadgen.
add_library(sa_bot STATIC src/wanderer.cpp src/fighter.cpp src/looter.cpp src/traveler.cpp
                          src/trader.cpp)
add_library(sa::bot ALIAS sa_bot)
sa_apply_policy(sa_bot)
target_include_directories(sa_bot PUBLIC include)
target_link_libraries(sa_bot PUBLIC sa::common)

if(SA_BUILD_TESTS)
  sa_add_test(sa_bot_tests
    SOURCES tests/wanderer_test.cpp tests/fighter_test.cpp tests/looter_test.cpp
            tests/traveler_test.cpp tests/trader_test.cpp
    LIBS sa::bot)
endif()
