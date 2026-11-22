CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -Iinclude -isystem third_party
LDLIBS   := -pthread
ifeq ($(OS),Windows_NT)
LDLIBS   += -lws2_32
endif
LIB_SRC  := $(filter-out src/main.cpp src/server.cpp, $(wildcard src/*.cpp))
LIB_OBJ  := $(LIB_SRC:src/%.cpp=build/%.o)

all: build/chronosync build/tests build/benchmark build/server

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/chronosync: $(LIB_OBJ) src/main.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)

build/tests: $(LIB_OBJ) tests/test_main.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)

build/benchmark: $(LIB_OBJ) benchmark/benchmark.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)

# Web server (REST API + frontend). Compiling httplib takes a little while.
build/server: $(LIB_OBJ) src/server.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)

build:
	mkdir -p build

run: build/chronosync
	./build/chronosync
serve: build/server
	./build/server
test: build/tests
	./build/tests
bench: build/benchmark
	./build/benchmark
clean:
	rm -rf build

.PHONY: all run serve test bench clean
