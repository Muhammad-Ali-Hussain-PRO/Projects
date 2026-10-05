CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Werror
.PHONY: all test clean
all: tests
tests: tests.cpp validator.hpp ../test_support.hpp
	$(CXX) $(CXXFLAGS) tests.cpp -o tests
test: tests
	./tests > verification.json.tmp
	mv verification.json.tmp verification.json
clean:
	rm -f tests verification.json.tmp
