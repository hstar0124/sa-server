# Interface target carrying the repository-wide warning policy. Every C++ target links it.
add_library(sa_warnings INTERFACE)
add_library(sa::warnings ALIAS sa_warnings)

target_compile_options(sa_warnings INTERFACE
  -Wall
  -Wextra
  -Wpedantic
  -Werror
  -Wshadow
  -Wnon-virtual-dtor
  -Wold-style-cast
  -Wcast-align
  -Wunused
  -Woverloaded-virtual
  -Wconversion
  -Wsign-conversion
  # GCC's null-dereference analysis depends on optimisation: at -O2 it reports paths inside
  # inlined library code (asio's io_context) that it cannot prove safe, even from a system header.
  # The Debug presets (dev, asan, tidy) keep it; issue 015.
  $<$<CONFIG:Debug>:-Wnull-dereference>
  -Wdouble-promotion
  -Wformat=2
  -Wimplicit-fallthrough
  # Designated initialisers leave the remaining members value-initialised on purpose; -Wextra's
  # missing-field-initializers would flag every such aggregate.
  -Wno-missing-field-initializers)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(sa_warnings INTERFACE
    -Wduplicated-cond
    -Wduplicated-branches
    -Wlogical-op
    -Wuseless-cast)
endif()
