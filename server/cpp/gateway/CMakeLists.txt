# Protocol logic is a library so it can be unit-tested without sockets or gRPC.
add_library(sa_gateway_core STATIC
  src/entry_ticket_verifier.cpp
  src/session_protocol.cpp
  src/gateway_config.cpp)
add_library(sa::gateway_core ALIAS sa_gateway_core)
sa_apply_policy(sa_gateway_core)
target_include_directories(sa_gateway_core PUBLIC src)
target_link_libraries(sa_gateway_core PUBLIC sa::common)

add_executable(gateway
  src/main.cpp
  src/client_session.cpp
  src/listener.cpp
  src/zone_link.cpp
  src/directory_client.cpp
  src/session_registry.cpp)
sa_apply_policy(gateway)
target_link_libraries(gateway PRIVATE sa::gateway_core)

if(SA_BUILD_TESTS)
  sa_add_test(sa_gateway_tests
    SOURCES tests/entry_ticket_verifier_test.cpp tests/session_protocol_test.cpp tests/test_tickets.hpp
    LIBS sa::gateway_core)
endif()
