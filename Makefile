BOARD ?= arduino:samd:mkrwifi1010
BUILD_SH ?= ./build.sh
BUILD_ROOT ?= build
CLANG_FORMAT ?= clang-format
CXX ?= c++
INCLUDE_DIRS ?= --include-dir include

# --werror turns every warning into an error. The few diagnostics emitted by
# the Arduino SAMD core itself are exempted inside build.sh.
STRICT ?= --werror
CLEAN ?=

EXAMPLES := Scan Connect HttpGet TlsGet TcpServer UdpNtp
PROJECT_SOURCES := $(shell find include src test -type f \( -name '*.h' -o -name '*.cc' \) -not -path 'test/build/*')
SKETCHES := dash.ino $(wildcard examples/*/*.ino)

.PHONY: all test format format-check headers-check compile compile-examples \
	strict check clean

all: test compile compile-examples

test:
	$(MAKE) -C test

format:
	$(CLANG_FORMAT) -i $(PROJECT_SOURCES)
	@for f in $(SKETCHES); do \
		$(CLANG_FORMAT) -i --assume-filename=_fmt.cc "$$f"; \
	done

format-check:
	$(CLANG_FORMAT) --dry-run --Werror $(PROJECT_SOURCES)
	@for f in $(SKETCHES); do \
		$(CLANG_FORMAT) --dry-run --Werror --assume-filename=_fmt.cc "$$f"; \
	done

headers-check:
	@for h in src/dash_net.h include/dash/*.h; do \
		printf '#include "%s"\n' "$$h" | \
			$(CXX) -std=c++11 -fsyntax-only -Iinclude -I. -x c++ - || exit 1; \
	done

compile:
	$(BUILD_SH) $(STRICT) $(CLEAN) $(INCLUDE_DIRS) -b $(BOARD) \
		-o $(BUILD_ROOT)/sketch .

compile-examples:
	@for ex in $(EXAMPLES); do \
		echo "compiling examples/$$ex"; \
		$(BUILD_SH) $(STRICT) $(CLEAN) $(INCLUDE_DIRS) -b $(BOARD) \
			-o $(BUILD_ROOT)/$$ex --library . "examples/$$ex" || exit 1; \
	done

# Full rebuild from a cold cache: proves the strict flags pass without help
# from previously compiled objects.
strict: CLEAN := --clean
strict: all

check: format-check headers-check
	$(MAKE) strict

clean:
	$(MAKE) -C test clean
	rm -rf $(BUILD_ROOT)
