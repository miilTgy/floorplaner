#pragma once

#include "common.h"

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

Problem parse_problem(const std::string &path);
void dump_problem(const Problem &p, std::ostream &os);
