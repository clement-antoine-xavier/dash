CXX ?= c++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -Wpedantic -Wshadow -Wcast-align \
	-Wconversion -Wsign-conversion -Wdouble-promotion -Wundef \
	-Wformat=2 -Wformat-security -Werror -O2 -g

ROOT := ..
CPPFLAGS ?= -I$(ROOT)/include -I$(ROOT)/test

BUILD := build
COMMON := $(ROOT)/src/dash/nina_frame.cc $(ROOT)/test/sim/fake_nina.cc
BINS := $(BUILD)/frame_codec_test $(BUILD)/station_test $(BUILD)/socket_test

HEADERS := $(wildcard $(ROOT)/include/dash/*.h) $(wildcard $(ROOT)/test/sim/*.h)

all: test

$(BUILD)/frame_codec_test: $(COMMON) $(ROOT)/test/frame_codec_test.cc $(HEADERS)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(COMMON) $(ROOT)/test/frame_codec_test.cc -o $@

$(BUILD)/station_test: $(COMMON) $(ROOT)/src/dash/nina_wifi.cc $(ROOT)/test/station_test.cc $(HEADERS)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(COMMON) $(ROOT)/src/dash/nina_wifi.cc $(ROOT)/test/station_test.cc -o $@

$(BUILD)/socket_test: $(COMMON) $(ROOT)/src/dash/nina_socket.cc $(ROOT)/test/socket_test.cc $(HEADERS)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(COMMON) $(ROOT)/src/dash/nina_socket.cc $(ROOT)/test/socket_test.cc -o $@

test: $(BINS)
	./$(BUILD)/frame_codec_test
	./$(BUILD)/station_test
	./$(BUILD)/socket_test

clean:
	rm -rf $(BUILD)

.PHONY: all test clean
