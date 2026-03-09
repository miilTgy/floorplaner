CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -pedantic
INCLUDES = -Iinclude
PYTHON ?= python

SRC_DIR = src
BUILD_DIR = build
BIN_DIR = bin

PARSER_SRC = $(SRC_DIR)/parser.cc
# ORDERER_SRC = $(SRC_DIR)/orderer.cc
ORDERER_SRC = $(SRC_DIR)/orderer2.cc
# PLANER_SRC = $(SRC_DIR)/init_planer.cc
# PLANER_SRC = $(SRC_DIR)/init_planer_admissible.cc
PLANER_SRC = $(SRC_DIR)/init_fp_bstar.cc
BSTAR2FP_SRC = $(SRC_DIR)/bstar_tree2fp.cc
SA_SRC = $(SRC_DIR)/sa.cc
# FP2BSTAR_SRC = $(SRC_DIR)/fp2bstar_tree.cc
WRITER_SRC = $(SRC_DIR)/writer.cc
MAIN_SRC = $(SRC_DIR)/main.cc

SRCS := $(PARSER_SRC) $(ORDERER_SRC) $(PLANER_SRC) $(BSTAR2FP_SRC) $(SA_SRC) $(WRITER_SRC) $(MAIN_SRC)
OBJS := $(patsubst $(SRC_DIR)/%.cc,$(BUILD_DIR)/%.o,$(SRCS))

TARGET = $(BIN_DIR)/floorplan

.PHONY: all floorplan clean run visualize

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
	./floorplan $(INPUT) $(T) --mode sa

visualize:
	@if [ -z "$(INPUT)" ] || [ -z "$(SOL)" ]; then \
		echo "Usage: make visualize INPUT=<input.txt> SOL=<solution.txt> [SAVE=out.png] [TITLE=title] [DPI=150] [SHOW_PINS=1] [PIN_LABELS=1]"; \
		exit 1; \
	fi
	$(PYTHON) visualizer.py $(INPUT) $(SOL) \
		$(if $(SAVE),--save $(SAVE),) \
		$(if $(TITLE),--title "$(TITLE)",) \
		$(if $(DPI),--dpi $(DPI),) \
		$(if $(filter 1,$(SHOW_PINS)),--show-pins,) \
		$(if $(filter 1,$(PIN_LABELS)),--pin-labels,)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) floorplan
