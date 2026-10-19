CXX      ?= c++
CXXFLAGS ?= -std=c++20 -O2 -g -Wall -Wextra -Wpedantic -Iinclude

TESTS := $(patsubst tests/%.cpp,build/%,$(wildcard tests/*_test.cpp))
HDRS  := $(wildcard include/me/*.hpp tests/*.hpp)

.PHONY: all test sanitize clean

all: test

build/%: tests/%.cpp $(HDRS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $< -o $@

test: $(TESTS)
	@for t in $(TESTS); do echo "run $$t"; ./$$t || exit 1; done

# Same tests with address/UB sanitizers and per-step engine invariant checks.
sanitize:
	$(MAKE) clean
	$(MAKE) test CXXFLAGS="-std=c++20 -O1 -g -Wall -Wextra -Wpedantic -Iinclude -fsanitize=address,undefined -fno-omit-frame-pointer -DME_CHECK_INVARIANTS -DME_QUICK"
	$(MAKE) clean

clean:
	rm -rf build
