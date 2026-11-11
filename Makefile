CXX ?= c++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -Iinclude
.PHONY: all test clean
all: build/cpu-learn
build:
	mkdir -p build
build/cpu-learn: apps/cli/main.cpp include/cpuai/learner.hpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@
build/learning-tests: tests/unit/learner_test.cpp include/cpuai/learner.hpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@
test: build/learning-tests
	./build/learning-tests
clean:
	rm -rf build
