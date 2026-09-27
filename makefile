CXX := g++
CXXFLAGS := -std=c++17 -O3 -march=native -Wall -Wextra
INCLUDES := -Iinclude

BIN_DIR := bin

.PHONY: all test benchmark run-all clean

all: $(BIN_DIR)/main $(BIN_DIR)/test_orderbook $(BIN_DIR)/benchmark

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/main: src/main.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@ -lpthread

$(BIN_DIR)/test_orderbook: src/test_orderbook.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(BIN_DIR)/benchmark: src/benchmark.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

test: $(BIN_DIR)/test_orderbook
	./$(BIN_DIR)/test_orderbook

benchmark: $(BIN_DIR)/benchmark
	./$(BIN_DIR)/benchmark

run-all: all test benchmark
	./$(BIN_DIR)/main

clean:
	rm -rf $(BIN_DIR)