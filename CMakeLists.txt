cmake_minimum_required(VERSION 3.20)
project(rinha-2026 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(rinha-2026 src/main.cpp src/server.cpp)
target_compile_options(rinha-2026 PRIVATE -O3 -flto -DNDEBUG -funroll-loops)
target_link_options(rinha-2026 PRIVATE -flto)
