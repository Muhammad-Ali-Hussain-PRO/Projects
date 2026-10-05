CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
PROJECTS := veil chronicle tempo
BINS := $(addprefix build/,$(PROJECTS))
TESTS := $(addsuffix -tests,$(BINS))
.PHONY: all test samples clean
all: $(BINS)
build/veil build/veil-tests: veil/json.hpp
build:
	mkdir -p build
build/%: %/main.cpp %/core.hpp | build
	$(CXX) $(CXXFLAGS) -I$* $< -o $@
build/%-tests: %/test.cpp %/core.hpp | build
	$(CXX) $(CXXFLAGS) -I$* $< -o $@
test: $(TESTS)
	./build/veil-tests > build/veil-tests.json
	./build/chronicle-tests > build/chronicle-tests.json
	./build/tempo-tests > build/tempo-tests.json
samples: all
	./build/veil --seed 42 --steps 12 --json > build/veil-seed42.json
	./build/chronicle --seed 42 --steps 24 --json > build/chronicle-seed42.json
	./build/tempo --seed 42 --steps 96 --json > build/tempo-seed42.json
clean:
	rm -rf build
