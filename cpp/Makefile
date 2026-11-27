# Makefile simple (alternativa a CMake)
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wextra -Iinclude
LDFLAGS  ?=

# El solver es autocontenido en src/solver.cpp (AdjustedSolver es6, con su
# propio main). El main.cpp y solver.hpp del solver viejo quedaron fuera del
# build (ver NOTA.md).
TARGET = ce_mwis
TEST_BIN = test_exact

.PHONY: all clean test

all: $(TARGET)

# Pruebas propias de C++: regresiones del evaluador racional exacto y del
# solver nuevo (a través de su binario). test_exact.cpp usa ce_mwis, así que
# el binario se construye primero.
test: $(TARGET) $(TEST_BIN)
	./$(TEST_BIN)

$(TARGET): src/solver.cpp
	$(CXX) $(CXXFLAGS) -o $@ src/solver.cpp $(LDFLAGS)

$(TEST_BIN): tests/test_exact.cpp src/evaluator.cpp $(TARGET)
	$(CXX) $(CXXFLAGS) -DCE_MWIS_BIN='"$(CURDIR)/ce_mwis"' -o $@ tests/test_exact.cpp src/evaluator.cpp $(LDFLAGS)

clean:
	rm -f $(TARGET) $(TEST_BIN)
