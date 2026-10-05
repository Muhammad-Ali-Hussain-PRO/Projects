CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
.PHONY: all test clean
all: ../build/veil
../build/veil ../build/veil-tests: json.hpp
../build:
	mkdir -p ../build
../build/veil: main.cpp core.hpp | ../build
	$(CXX) $(CXXFLAGS) -I. main.cpp -o $@
../build/veil-tests: test.cpp core.hpp | ../build
	$(CXX) $(CXXFLAGS) -I. test.cpp -o $@
test: ../build/veil-tests
	../build/veil-tests
clean:
	rm -f ../build/veil ../build/veil-tests
