# Helpers that apply the repo policy to a target in one line.
function(sa_apply_policy target)
  target_link_libraries(${target} PRIVATE sa::warnings sa::sanitizers)
  target_compile_features(${target} PUBLIC cxx_std_23)
endfunction()

# sa_add_test(<name> SOURCES ... LIBS ...): a GoogleTest executable registered with CTest.
function(sa_add_test name)
  cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
  add_executable(${name} ${ARG_SOURCES})
  sa_apply_policy(${name})
  target_link_libraries(${name} PRIVATE ${ARG_LIBS} GTest::gtest GTest::gtest_main)
  gtest_discover_tests(${name})
endfunction()
