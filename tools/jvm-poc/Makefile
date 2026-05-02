CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2
SDL_CFLAGS ?= $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS ?= $(shell sdl2-config --libs 2>/dev/null)

COMMON_SRCS := class_file.cpp bytecode.cpp frame.cpp interpreter.cpp jvm_midlet_app.cpp method_resolution.cpp
COMMON_OBJS := $(COMMON_SRCS:.cpp=.o)
APP_OBJS := main.o $(COMMON_OBJS)
SDL_DEMO_OBJS := demo_sdl_main.o $(COMMON_OBJS)
TEST_OBJS := tests.o $(COMMON_OBJS)

all: jvm-poc

jvm-poc: $(APP_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(APP_OBJS)

jvm-poc-tests: $(TEST_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_OBJS)

jvm-poc-sdl-demo: $(SDL_DEMO_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(SDL_DEMO_OBJS) $(SDL_LIBS)

test: jvm-poc-tests
	cd ../.. && mvn -q package && tools/jvm-poc/jvm-poc-tests

demo_sdl_main.o: demo_sdl_main.cpp *.hpp
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -c -o $@ $<

%.o: %.cpp *.hpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f main.o demo_sdl_main.o tests.o $(COMMON_OBJS) jvm-poc jvm-poc-sdl-demo jvm-poc-tests

.PHONY: all test clean
