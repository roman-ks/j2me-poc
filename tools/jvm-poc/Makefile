CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2

SRCS := main.cpp class_file.cpp bytecode.cpp frame.cpp interpreter.cpp
OBJS := $(SRCS:.cpp=.o)

all: jvm-poc

jvm-poc: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

%.o: %.cpp *.hpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) jvm-poc

.PHONY: all clean
