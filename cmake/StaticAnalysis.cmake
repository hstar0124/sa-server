if(SA_ENABLE_CLANG_TIDY)
  find_program(SA_CLANG_TIDY clang-tidy REQUIRED)
  # The build compiles with GCC; clang-tidy re-parses with clang, which does not know the
  # GCC-only warning flags from CompilerWarnings.cmake.
  set(CMAKE_CXX_CLANG_TIDY "${SA_CLANG_TIDY};--extra-arg=-Wno-unknown-warning-option")
endif()

# Generated protobuf/gRPC code is third-party output and is never analysed.
function(sa_exclude_from_static_analysis target)
  set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "")
endfunction()
