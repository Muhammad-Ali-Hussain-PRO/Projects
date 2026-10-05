CXX ?= g++
CPPFLAGS := -Iinclude
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror
LDFLAGS ?=

.PHONY: all test sanitize examples schema-check clean
all: build/aether
build:
	mkdir -p build
build/aether: src/main.cpp src/world.cpp include/aether/world.hpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/main.cpp src/world.cpp $(LDFLAGS) -o $@
build/aether_tests: tests/test_world.cpp src/world.cpp include/aether/world.hpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/test_world.cpp src/world.cpp $(LDFLAGS) -o $@
test: build/aether_tests build/aether
	./build/aether_tests
	python3 tests/check_cli.py ./build/aether
schema-check: examples
	python3 tests/check_schema.py docs/output-schema.json examples/*.json
sanitize:
	$(CXX) $(CPPFLAGS) -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_world.cpp src/world.cpp -o build/aether_tests_sanitize
	ASAN_OPTIONS=detect_leaks=0 ./build/aether_tests_sanitize
examples: build/aether
	./build/aether --seed 42 --steps 2400 --json > examples/island-closed.json
	./build/aether --seed 42 --steps 2400 --valve gate=on --json > examples/island-open.json
	./build/aether --seed 7 --steps 1200 --prompt 'floating island; platforms 6; bridges on; water off; bodies 5; wind 2' --json > examples/wind-platforms.json
clean:
	rm -rf build
