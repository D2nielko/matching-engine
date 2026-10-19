CXX      ?= c++
CXXFLAGS ?= -std=c++20 -O2 -g -Wall -Wextra -Wpedantic -Iinclude

TESTS := $(patsubst tests/%.cpp,build/%,$(wildcard tests/*_test.cpp))
HDRS  := $(wildcard include/me/*.hpp)

.PHONY: all test clean

all: test

build/%: tests/%.cpp $(HDRS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $< -o $@

test: $(TESTS)
	@for t in $(TESTS); do echo "run $$t"; ./$$t || exit 1; done

clean:
	rm -rf build
