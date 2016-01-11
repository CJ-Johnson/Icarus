
TARGET  := bin/$(shell basename `pwd`)

SOURCES := $(shell find src -name *.cpp 2>/dev/null)
OBJECTS := $(patsubst src/%.cpp,build/%.o,$(SOURCES))
DEPENDS := $(SOURCES:src/%.cpp=build/%.d)

COMPILER := clang++
BUILD_FLAGS := -g -O0 -D DEBUG
STDS = -std=c++11
WARN = -Wall -Wextra -Wconversion -Werror
OPTS = -iquote$(shell pwd)/src
LLVM_CXX = $(shell llvm-config --cxxflags)
LLVM_LINK = $(shell llvm-config --cxxflags --ldflags --system-libs --libs core)
all: $(TARGET)

.PHONY: release
release: BUILD_FLAGS := -O3
release: $(TARGET)

build/%.o: src/%.cpp
	@mkdir -p $(@D)
	@$(COMPILER) -MM $(STDS) $(OPTS) $(LLVM_CXX) src/$*.cpp -MF build/$*.d
	@$(COMPILER) $(STDS) $(OPTS) $(WARN) $(BUILD_FLAGS) $(LLVM_CXX) -c src/$*.cpp -o build/$*.o

-include $(DEPENDS)

$(TARGET): $(OBJECTS)
	@$(COMPILER) $(LLVM_LINK) $(OBJECTS) -o $@

unity:
	@mkdir -p build
	@mkdir -p bin
	@rm -f build/unity.cpp
	@printf '$(patsubst src/%.cpp,#include "%.cpp"\n,$(SOURCES))' > build/unity.cpp
	@$(COMPILER) $(STDS) $(OPTS) $(WARN) $(BUILD_FLAGS) $(LLVM) build/unity.cpp -o $(TARGET)

clean:
	@rm -f $(TARGET) $(OBJECTS) $(DEPENDS)

help:
	@echo "TARGET  : $(TARGET)"
	@echo "SOURCES : $(SOURCES)"
	@echo "OBJECTS : $(OBJECTS)"
	@echo "DEPENDS : $(DEPENDS)"
	@echo "LLVM_CXX : $(LLVM_CXX)"
	@echo "LLVM_LINK : $(LLVM_LINK)"

wc:
	@wc src/*.* src/AST/*.* src/Type/*.*
