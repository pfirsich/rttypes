cmake_minimum_required(VERSION 3.0.0)

project(rttypes)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS on)
set(ENABLE_ASAN FALSE)

if(ENABLE_ASAN)
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
  set(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
endif()

add_executable(rttypes main.cpp)
target_compile_options(rttypes PRIVATE -Wall -Wextra -pedantic -Werror)
