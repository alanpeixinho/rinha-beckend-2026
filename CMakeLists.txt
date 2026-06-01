cmake_minimum_required(VERSION 3.20)
project(rinha-2026 C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include/)
file(GLOB SRC_FILES src/*.cpp)

add_executable(rinha-2026 ${SRC_FILES})
target_compile_options(rinha-2026 PRIVATE -O3 -flto -DNDEBUG -funroll-loops)
target_link_options(rinha-2026 PRIVATE -flto)

add_executable(proxy src/proxy.c)
target_compile_options(proxy PRIVATE -O3 -flto -DNDEBUG)
