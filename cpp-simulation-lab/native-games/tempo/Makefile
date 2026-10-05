CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
.PHONY: all test clean
all: ../build/tempo
../build:
	mkdir -p ../build
../build/tempo: main.cpp core.hpp | ../build
	$(CXX) $(CXXFLAGS) -I. main.cpp -o $@
../build/tempo-tests: test.cpp core.hpp | ../build
	$(CXX) $(CXXFLAGS) -I. test.cpp -o $@
test: ../build/tempo-tests
	../build/tempo-tests
clean:
	rm -f ../build/tempo ../build/tempo-tests
