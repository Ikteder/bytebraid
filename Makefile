CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wconversion
CPPFLAGS ?= -Iinclude

.PHONY: all test clean

all: build/bytebraid

build:
	mkdir -p build

build/bytebraid: src/main.cpp src/analyzer.cpp include/bytebraid/analyzer.hpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/main.cpp src/analyzer.cpp -o $@

build/bytebraid_tests: tests/test_analyzer.cpp src/analyzer.cpp include/bytebraid/analyzer.hpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/test_analyzer.cpp src/analyzer.cpp -o $@

test: build/bytebraid_tests
	./build/bytebraid_tests

clean:
	rm -rf build
