# Everything but main is a library, so tests reach the protocol, sessions, links and listener.
add_library(sa_gateway_core STATIC
  src/entry_ticket_verifier.cpp
  src/session_protocol.cpp
  src/gateway_config.cpp
  src/client_session.cpp
  src/listener.cpp
  src/zone_link.cpp
  src/directory_client.cpp
  src/session_registry.cpp)
add_library(sa::gateway_core ALIAS sa_gateway_core)
sa_apply_policy(sa_gateway_core)
target_include_directories(sa_gateway_core PUBLIC src)
target_link_libraries(sa_gateway_core PUBLIC sa::common)

add_executable(gateway src/main.cpp)
sa_apply_policy(gateway)
target_link_libraries(gateway PRIVATE sa::gateway_core)

if(SA_BUILD_TESTS)
  sa_add_test(sa_gateway_tests
    SOURCES tests/entry_ticket_verifier_test.cpp tests/session_protocol_test.cpp tests/test_tickets.hpp
            tests/zone_link_test.cpp tests/client_session_test.cpp tests/gateway_config_test.cpp
    LIBS sa::gateway_core sa::common_testing)
  # The config test reads the shipped file so a broken default fails CI, not a deployment.
  target_compile_definitions(sa_gateway_tests
    PRIVATE SA_GATEWAY_CONFIG="${CMAKE_SOURCE_DIR}/deploy/compose/config/gateway.toml")
endif()
