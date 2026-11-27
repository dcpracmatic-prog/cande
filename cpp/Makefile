# Makefile simple (alternativa a CMake)
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Iinclude
LDFLAGS  ?=
TEST_CXXFLAGS ?= -std=c++17 -O0 -Wall -Wextra -Iinclude
DEPFLAGS ?= -MMD -MP

SRCS = src/evaluator.cpp src/solver.cpp src/main.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = ce_mwis
TEST_TARGET = test_exact

.PHONY: all clean test

all: $(TARGET)

test: $(TARGET) $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): tests/test_exact.cpp src/evaluator.o src/solver.o
	$(CXX) $(TEST_CXXFLAGS) $(DEPFLAGS) -MF $(TEST_TARGET).d -o $@ tests/test_exact.cpp src/evaluator.o src/solver.o $(LDFLAGS)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

-include $(OBJS:.o=.d) $(TEST_TARGET).d

clean:
	rm -f $(OBJS) $(OBJS:.o=.d) $(TARGET) $(TEST_TARGET) $(TEST_TARGET).d
