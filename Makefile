CXX      := g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Wpedantic -Iinclude

BUILD_DIR := build
LIB_SRCS  := src/memory_store.cpp src/parser.cpp src/compiler.cpp src/vm.cpp src/runtime.cpp
LIB_OBJS  := $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(LIB_SRCS))

CLI_BIN   := $(BUILD_DIR)/optrich-vm
BENCH_BIN := $(BUILD_DIR)/bench_vm
TEST_BINS := $(BUILD_DIR)/test_memory $(BUILD_DIR)/test_core_wasm $(BUILD_DIR)/test_mem_opcodes \
             $(BUILD_DIR)/test_gc $(BUILD_DIR)/test_multimodule $(BUILD_DIR)/test_optic_golden

.PHONY: all test test-optic examples bench clean

all: $(CLI_BIN)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(CLI_BIN): $(LIB_OBJS) $(BUILD_DIR)/main.o
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/test_%: tests/test_%.cpp $(LIB_OBJS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< $(LIB_OBJS) -o $@

$(BENCH_BIN): benchmarks/bench_vm.cpp $(LIB_OBJS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< $(LIB_OBJS) -o $@

bench: $(BENCH_BIN)
	$(BENCH_BIN)

test-optic: $(BUILD_DIR)/test_optic_golden
	$(BUILD_DIR)/test_optic_golden

test: $(TEST_BINS)
	@for t in $(TEST_BINS); do \
		echo "=== Running $$t ==="; \
		$$t || exit 1; \
	done
	@echo "All test suites passed!"

examples: $(CLI_BIN)
	@echo "=== Running examples ==="
	@printf "span_demo: "; $(CLI_BIN) examples/span_demo.wat main
	@printf "01_core_wasm: "; $(CLI_BIN) examples/01_core_wasm.wat main
	@printf "02_split_blocks_linked_list: "; $(CLI_BIN) examples/02_split_blocks_linked_list.wat main
	@printf "03_bulk_and_spans: "; $(CLI_BIN) examples/03_bulk_and_spans.wat main
	@echo "--- 04_gc_and_clone_span ---"; $(CLI_BIN) --gc-threshold 32 --gc-stats examples/04_gc_and_clone_span.wat main
	@printf "05_multimodule_sharing: "; $(CLI_BIN) --link strlib=examples/05_multimodule_sharing/strlib.wat examples/05_multimodule_sharing/app.wat main
	@echo "--- 06_polymorphic_interfaces ---"; $(CLI_BIN) --link shape=examples/06_polymorphic_interfaces/shape.wat --link rect=examples/06_polymorphic_interfaces/rect.wat --link square=examples/06_polymorphic_interfaces/square.wat --gc-stats examples/06_polymorphic_interfaces/main.wat main
	@printf "07_zero_copy_split: "; $(CLI_BIN) examples/07_zero_copy_split.wat main
	@printf "08_closure: "; $(CLI_BIN) examples/08_closure.wat main
	@echo "--- 09_rust_trait_dispatch ---"; $(CLI_BIN) --gc-stats examples/09_rust_trait_dispatch.wat main

clean:
	rm -rf $(BUILD_DIR)
