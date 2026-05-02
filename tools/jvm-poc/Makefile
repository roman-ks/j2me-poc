CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2
CPPFLAGS ?= -Icore_shim
SDL_CFLAGS ?= $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS ?= $(shell sdl2-config --libs 2>/dev/null)
BUILD_DIR := build

COMMON_SRCS := class_file.cpp bytecode.cpp frame.cpp interpreter.cpp jvm_midlet_app.cpp method_resolution.cpp \
	native_methods/helpers.cpp native_methods/native_runtime.cpp native_methods/display.cpp \
	native_methods/midlet.cpp native_methods/canvas.cpp native_methods/image.cpp native_methods/graphics.cpp \
	native_methods/string.cpp native_methods/dispatch.cpp \
	j2me_port/Canvas.cpp j2me_port/J2MECompat.cpp
COMMON_OBJS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(COMMON_SRCS))
APP_OBJS := $(BUILD_DIR)/main.o $(COMMON_OBJS)
SDL_DEMO_OBJS := $(BUILD_DIR)/demo_sdl_main.o $(BUILD_DIR)/sdl_image_decoder.o $(COMMON_OBJS)
TEST_OBJS := $(BUILD_DIR)/tests.o $(COMMON_OBJS)

all: $(BUILD_DIR)/jvm-poc

$(BUILD_DIR)/jvm-poc: $(APP_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(APP_OBJS)

$(BUILD_DIR)/jvm-poc-tests: $(TEST_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_OBJS)

$(BUILD_DIR)/jvm-poc-sdl-demo: $(SDL_DEMO_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(SDL_DEMO_OBJS) $(SDL_LIBS)

jvm-poc: $(BUILD_DIR)/jvm-poc
jvm-poc-tests: $(BUILD_DIR)/jvm-poc-tests
jvm-poc-sdl-demo: $(BUILD_DIR)/jvm-poc-sdl-demo

test: $(BUILD_DIR)/jvm-poc-tests
	cd ../.. && mvn -q package && tools/jvm-poc/build/jvm-poc-tests

$(BUILD_DIR)/demo_sdl_main.o: demo_sdl_main.cpp *.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SDL_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: %.cpp *.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)/native_methods $(BUILD_DIR)/j2me_port

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all test clean
