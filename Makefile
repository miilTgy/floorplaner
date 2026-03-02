CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -pedantic
INCLUDES = -Iinclude

SRC_DIR = src
BUILD_DIR = build
BIN_DIR = bin

SRCS := $(wildcard $(SRC_DIR)/*.cc)
OBJS := $(patsubst $(SRC_DIR)/%.cc,$(BUILD_DIR)/%.o,$(SRCS))

TARGET = $(BIN_DIR)/floorplan

.PHONY: all floorplan clean run

all: floorplan

floorplan: $(TARGET)
	ln -sf $(TARGET) floorplan

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $(OBJS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cc | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

run: floorplan
	./floorplan $(INPUT) $(T) --debug

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) floorplan
