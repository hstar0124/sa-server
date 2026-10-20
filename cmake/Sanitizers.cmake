# SA_SANITIZER selects one sanitizer family for the whole build (mixing ASan and TSan is unsupported).
add_library(sa_sanitizers INTERFACE)
add_library(sa::sanitizers ALIAS sa_sanitizers)

# -fno-sanitize-recover=all: a finding aborts the test instead of only printing, so the asan job
# fails on undefined behaviour as it does on memory errors.
if(SA_SANITIZER STREQUAL "address")
  target_compile_options(sa_sanitizers INTERFACE -fsanitize=address,undefined -fno-sanitize-recover=all
                                                 -fno-omit-frame-pointer)
  target_link_options(sa_sanitizers INTERFACE -fsanitize=address,undefined -fno-sanitize-recover=all)
elseif(SA_SANITIZER STREQUAL "thread")
  target_compile_options(sa_sanitizers INTERFACE -fsanitize=thread -fno-omit-frame-pointer)
  target_link_options(sa_sanitizers INTERFACE -fsanitize=thread)
elseif(NOT SA_SANITIZER STREQUAL "")
  message(FATAL_ERROR "Unknown SA_SANITIZER='${SA_SANITIZER}' (expected '', 'address' or 'thread')")
endif()
