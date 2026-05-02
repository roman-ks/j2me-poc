CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2

COMMON_SRCS := class_file.cpp bytecode.cpp frame.cpp interpreter.cpp
COMMON_OBJS := $(COMMON_SRCS:.cpp=.o)
APP_OBJS := main.o $(COMMON_OBJS)
TEST_OBJS := tests.o $(COMMON_OBJS)

all: jvm-poc

jvm-poc: $(APP_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(APP_OBJS)

jvm-poc-tests: $(TEST_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_OBJS)

test: jvm-poc-tests
	cd ../.. && mvn -q package && tools/jvm-poc/jvm-poc-tests

%.o: %.cpp *.hpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f main.o tests.o $(COMMON_OBJS) jvm-poc jvm-poc-tests

.PHONY: all test clean
