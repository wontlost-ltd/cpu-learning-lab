CXX ?= c++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -Iinclude
PYTHON ?= python3
HEADERS := $(wildcard include/cpuai/*.hpp)
.PHONY: all test clean
all: build/cpu-learn
build:
	mkdir -p build
build/cpu-learn: apps/cli/main.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@
build/learning-tests: tests/unit/learner_test.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@
build/evaluation-tests: tests/unit/evaluation_test.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@
test: build/cpu-learn build/learning-tests build/evaluation-tests
	./build/learning-tests
	./build/evaluation-tests tests/evaluation/square-v1.exam
	$(PYTHON) tests/integration/cli_test.py build/cpu-learn tests/evaluation/square-v1.exam
clean:
	rm -rf build
