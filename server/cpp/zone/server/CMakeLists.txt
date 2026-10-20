# Host logic that does not touch sockets lives in a library so it can be unit-tested.
add_library(sa_zone_host STATIC
  src/codec.cpp
  src/zone_host.cpp
  src/economy_client.cpp
  src/gateway_link.cpp
  src/zone_server_config.cpp)
add_library(sa::zone_host ALIAS sa_zone_host)
sa_apply_policy(sa_zone_host)
target_include_directories(sa_zone_host PUBLIC src)
target_link_libraries(sa_zone_host PUBLIC sa::zone_sim sa::common sa::bot asio::asio)

add_executable(zone_server
  src/main.cpp
  src/tick_runner.cpp
  src/local_bot_host.cpp
  src/gateway_link_server.cpp
  src/lease_keeper.cpp)
sa_apply_policy(zone_server)
target_link_libraries(zone_server PRIVATE sa::zone_host sa::zone_data)

if(SA_BUILD_TESTS)
  sa_add_test(sa_zone_host_tests
    SOURCES tests/codec_test.cpp tests/zone_host_test.cpp tests/zone_host_economy_test.cpp
            tests/economy_client_test.cpp tests/zone_server_config_test.cpp
    LIBS sa::zone_host)
  # The config test reads the shipped file so a broken default fails CI, not a deployment.
  target_compile_definitions(sa_zone_host_tests
    PRIVATE SA_ZONE_CONFIG="${CMAKE_SOURCE_DIR}/deploy/compose/config/zone-a.toml")
endif()
