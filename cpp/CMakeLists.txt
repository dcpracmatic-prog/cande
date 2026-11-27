cmake_minimum_required(VERSION 3.14)
project(ce_mwis LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Núcleo: evaluador racional exacto (cpp_int). El solver es autocontenido
# en src/solver.cpp (AdjustedSolver es6, con su propio main): el binario se
# construye directamente de ese archivo. Los archivos del solver viejo
# (main.cpp, solver.hpp, solution.hpp) ya se eliminaron del repo.
add_library(ce_core
    src/evaluator.cpp
)
target_include_directories(ce_core PUBLIC include)

add_executable(ce_mwis src/solver.cpp)

# Pruebas propias de C++: regresiones del evaluador racional exacto y del
# solver nuevo (a través de su binario). test_exact usa ce_mwis, así que el
# binario se construye primero.
add_executable(test_exact tests/test_exact.cpp src/evaluator.cpp)
target_include_directories(test_exact PRIVATE include)
target_compile_definitions(test_exact PRIVATE CE_MWIS_BIN="$<TARGET_FILE:ce_mwis>")
add_dependencies(test_exact ce_mwis)

# Flags útiles
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ce_core PRIVATE -Wall -Wextra -O3)
    target_compile_options(ce_mwis PRIVATE -Wall -Wextra -O3)
    target_compile_options(test_exact PRIVATE -Wall -Wextra -O3)
endif()

enable_testing()
add_test(NAME test_exact COMMAND test_exact)
