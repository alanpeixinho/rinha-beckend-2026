cmake_minimum_required(VERSION 3.20)
project(rinha-2026 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenMP REQUIRED)

include(FetchContent)
FetchContent_Declare(
    cpp-httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.1
)
FetchContent_MakeAvailable(cpp-httplib)

add_executable(rinha-2026 src/main.cpp)
target_link_libraries(rinha-2026 PRIVATE httplib::httplib OpenMP::OpenMP_CXX)
target_compile_options(rinha-2026 PRIVATE -fopenmp-simd)
