# Generated C++ for every .proto under contracts/. Consumers include e.g. <game/common.pb.h>.
set(SA_PROTO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(SA_PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${SA_PROTO_GEN_DIR}")

file(GLOB SA_GAME_PROTOS    CONFIGURE_DEPENDS "${SA_PROTO_ROOT}/game/*.proto")
file(GLOB SA_BACKEND_PROTOS CONFIGURE_DEPENDS "${SA_PROTO_ROOT}/backend/*.proto")
file(GLOB SA_EVENT_PROTOS   CONFIGURE_DEPENDS "${SA_PROTO_ROOT}/events/*.proto")
file(GLOB SA_INTERNAL_PROTOS CONFIGURE_DEPENDS "${SA_PROTO_ROOT}/internal/*.proto")

add_library(sa_contracts STATIC ${SA_GAME_PROTOS} ${SA_BACKEND_PROTOS} ${SA_EVENT_PROTOS} ${SA_INTERNAL_PROTOS})
add_library(sa::contracts ALIAS sa_contracts)

protobuf_generate(
  TARGET sa_contracts
  LANGUAGE cpp
  IMPORT_DIRS "${SA_PROTO_ROOT}"
  PROTOC_OUT_DIR "${SA_PROTO_GEN_DIR}")

# gRPC stubs only for the backend package: the game link is framed TCP, events go through Kafka.
protobuf_generate(
  TARGET sa_contracts
  LANGUAGE grpc
  GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
  PLUGIN "protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
  IMPORT_DIRS "${SA_PROTO_ROOT}"
  PROTOC_OUT_DIR "${SA_PROTO_GEN_DIR}"
  PROTOS ${SA_BACKEND_PROTOS})

target_include_directories(sa_contracts SYSTEM PUBLIC "${SA_PROTO_GEN_DIR}")
target_link_libraries(sa_contracts PUBLIC protobuf::libprotobuf gRPC::grpc++)
# Generated code is third-party output: it gets neither the warning policy nor clang-tidy.
target_compile_options(sa_contracts PRIVATE -w)
sa_exclude_from_static_analysis(sa_contracts)
