CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
.PHONY: all test clean
all: ../build/chronicle
../build:
	mkdir -p ../build
../build/chronicle: main.cpp core.hpp | ../build
	$(CXX) $(CXXFLAGS) -I. main.cpp -o $@
../build/chronicle-tests: test.cpp core.hpp | ../build
	$(CXX) $(CXXFLAGS) -I. test.cpp -o $@
test: ../build/chronicle-tests
	../build/chronicle-tests
clean:
	rm -f ../build/chronicle ../build/chronicle-tests
