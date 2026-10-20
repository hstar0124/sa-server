add_library(sa_common STATIC
  src/framing.cpp
  src/config_source.cpp
  src/logging.cpp
  src/hmac.cpp
  src/opaque_id.cpp
  src/framed_connection.cpp
  src/lease_keeper.cpp
  src/bytes.cpp
  src/tcp_byte_stream.cpp
  src/websocket.cpp
  src/websocket_connection.cpp)
add_library(sa::common ALIAS sa_common)
sa_apply_policy(sa_common)

target_include_directories(sa_common PUBLIC include)
target_link_libraries(sa_common
  PUBLIC sa::contracts spdlog::spdlog tomlplusplus::tomlplusplus asio::asio
  PRIVATE OpenSSL::Crypto)

if(SA_BUILD_TESTS)
  # Loopback sockets and WebSocket client frames for transport tests, here and in the gateway.
  add_library(sa_common_testing INTERFACE)
  add_library(sa::common_testing ALIAS sa_common_testing)
  target_include_directories(sa_common_testing INTERFACE tests/support)
  target_link_libraries(sa_common_testing INTERFACE asio::asio)

  sa_add_test(sa_common_tests
    SOURCES tests/strong_id_test.cpp tests/framing_test.cpp tests/config_source_test.cpp
            tests/hmac_test.cpp tests/instance_id_test.cpp tests/lease_test.cpp tests/bytes_test.cpp
            tests/websocket_test.cpp tests/connection_test.cpp
    LIBS sa::common sa::common_testing)
endif()
