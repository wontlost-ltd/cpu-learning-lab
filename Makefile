CXX ?= c++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -Iinclude
PYTHON ?= python3
HEADERS := $(wildcard include/cpuai/*.hpp)
.PHONY: all test benchmark clean
all: build/cpu-learn build/cpu-listen
build:
	mkdir -p build
build/cpu-learn: apps/cli/main.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@
build/cpu-listen: apps/listen/main.cpp $(HEADERS) $(wildcard adapters/audio/*.hpp) | build
	$(CXX) $(CPPFLAGS) -Iadapters/audio $(CXXFLAGS) $< -o $@
build/learning-tests: tests/unit/learner_test.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@
build/evaluation-tests: tests/unit/evaluation_test.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@
build/runtime-tests: tests/unit/runtime_test.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@
build/skills-tests: tests/unit/skills_test.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@
build/observations-tests: tests/unit/observations_test.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@
build/audio-stream-tests: tests/unit/audio_stream_test.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@
test: build/cpu-listen build/audio-stream-tests build/observations-tests build/cpu-learn build/learning-tests build/evaluation-tests build/runtime-tests build/skills-tests
	./build/audio-stream-tests
	$(PYTHON) tests/integration/listen_cli_test.py build/cpu-listen build/cpu-learn
	./build/observations-tests
	$(PYTHON) tests/integration/audio_cli_test.py build/cpu-learn
	./build/skills-tests
	./build/runtime-tests
	./build/learning-tests
	./build/evaluation-tests tests/evaluation/square-v1.exam
	$(PYTHON) tests/integration/cli_test.py build/cpu-learn tests/evaluation/square-v1.exam
	$(PYTHON) tests/integration/skills_cli_test.py build/cpu-learn tests/evaluation
build/search-benchmark: benchmarks/search_benchmark.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@
build/audio-benchmark: benchmarks/audio_benchmark.cpp $(HEADERS) | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@
benchmark: build/search-benchmark
	./build/search-benchmark
clean:
	rm -rf build
