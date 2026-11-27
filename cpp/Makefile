# Makefile simple (alternativa a CMake)
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wextra -Iinclude
LDFLAGS  ?=

# El solver es autocontenido en src/solver.cpp (AdjustedSolver es6, con su
# propio main). El main.cpp y solver.hpp del solver viejo quedaron fuera del
# build (ver NOTA.md).
OBJETIVO = ce_mwis
TEST_BIN = prueba_exacta

.PHONY: all test clean

all: $(OBJETIVO)

# Pruebas propias de C++: regresiones del evaluador racional exacto y del
# solver nuevo (a través de su binario). test_exact.cpp usa ce_mwis, así que
# el binario se construye primero.
test: $(OBJETIVO) $(TEST_BIN)
	./$(TEST_BIN)

$(OBJETIVO): src/solver.cpp
	$(CXX) $(CXXFLAGS) -o $@ src/solver.cpp $(LDFLAGS)

$(TEST_BIN): tests/test_exact.cpp src/evaluator.cpp $(OBJETIVO)
	$(CXX) $(CXXFLAGS) -DCE_MWIS_BIN='"$(CURDIR)/ce_mwis"' -o $@ tests/test_exact.cpp src/evaluator.cpp $(LDFLAGS)

clean:
	rm -f $(OBJETIVO) $(TEST_BIN)
