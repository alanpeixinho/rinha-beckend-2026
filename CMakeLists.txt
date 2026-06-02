cmake_minimum_required(VERSION 3.20)
project(rinha-2026 C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  simdjson
  GIT_REPOSITORY https://github.com/simdjson/simdjson.git
  GIT_TAG v4.6.1
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(simdjson)

include_directories(include/)
file(GLOB SRC_FILES src/*.cpp)

add_executable(rinha-2026 ${SRC_FILES})
target_compile_options(rinha-2026 PRIVATE -O3 -DNDEBUG -funroll-loops -flto -mf16c -mavx)
target_link_options(rinha-2026 PRIVATE -flto)
target_link_libraries(rinha-2026 PRIVATE simdjson::simdjson)

add_executable(proxy src/proxy.c)
target_compile_options(proxy PRIVATE -O3 -flto -DNDEBUG)
