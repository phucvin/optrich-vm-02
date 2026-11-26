CXX      := g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Wpedantic -Iinclude

BUILD_DIR := build
LIB_SRCS  := src/memory_store.cpp src/parser.cpp src/compiler.cpp src/vm.cpp src/runtime.cpp
LIB_OBJS  := $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(LIB_SRCS))

CLI_BIN   := $(BUILD_DIR)/optrich-vm
TEST_BINS := $(BUILD_DIR)/test_memory $(BUILD_DIR)/test_core_wasm $(BUILD_DIR)/test_mem_opcodes \
             $(BUILD_DIR)/test_gc $(BUILD_DIR)/test_multimodule

.PHONY: all test clean

all: $(CLI_BIN)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(CLI_BIN): $(LIB_OBJS) $(BUILD_DIR)/main.o
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/test_%: tests/test_%.cpp $(LIB_OBJS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< $(LIB_OBJS) -o $@

test: $(TEST_BINS)
	@for t in $(TEST_BINS); do \
		echo "=== Running $$t ==="; \
		$$t || exit 1; \
	done
	@echo "All test suites passed!"

clean:
	rm -rf $(BUILD_DIR)
