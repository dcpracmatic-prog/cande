cmake_minimum_required(VERSION 3.14)
project(ce_mwis LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(ce_core
    src/evaluator.cpp
    src/solver.cpp
)
target_include_directories(ce_core PUBLIC include)

add_executable(ce_mwis src/main.cpp)
target_link_libraries(ce_mwis PRIVATE ce_core)

# Flags útiles
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ce_core PRIVATE -Wall -Wextra -O2)
    target_compile_options(ce_mwis PRIVATE -Wall -Wextra -O2)
endif()
