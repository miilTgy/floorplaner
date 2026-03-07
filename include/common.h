#pragma once

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct Block {
  std::string name;
  int w;
  int h;
  std::vector<int> pin_ids;
  std::vector<int> net_ids;
};

struct Pin {
  std::string name;
  int block_id;
  double dx;
  double dy;
  std::vector<int> net_ids;
};

struct Net {
  std::string name;
  std::vector<int> pin_ids;
  std::vector<int> block_ids;
};

struct Problem {
  int chipW;
  std::vector<Block> blocks;
  std::vector<Pin> pins;
  std::vector<Net> nets;
  std::unordered_map<std::string, int> block_id_of;
  std::unordered_map<std::string, int> pin_id_of;
  std::unordered_map<std::string, int> net_id_of;
};

enum class CornerType {
  RIGHT_DOWN,
  LEFT_UP,
};

struct BlockCorner {
  double x;
  double y;
  CornerType type;
  int owner_block_id;
};

struct FloorplanItem {
  int block_id;
  double x;
  double y;
  int rotate;
  double w_used;
  double h_used;
};

struct FloorplanResult {
  std::vector<double> x;
  std::vector<double> y;
  std::vector<int> rotate;
  double H;
  double hpwl;
  double cost;
  std::vector<FloorplanItem> items;
};
