CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2

all: classdump

classdump: classdump.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f classdump

.PHONY: all clean
