CXX ?= g++
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wextra -Wpedantic

.PHONY: all test parity clean
all: build/windrunner

build/windrunner: src/main.cpp src/windrunner.hpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) src/main.cpp -o $@

build/tests: src/tests.cpp src/windrunner.hpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) src/tests.cpp -o $@

test: build/tests
	./build/tests

parity: build/windrunner
	node verify_mirror.mjs

clean:
	rm -rf build
