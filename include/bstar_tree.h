#pragma once

#include "common.h"

struct BStarNode {
  int block_id = -1;
  BStarNode *left = nullptr;   // right-adjacent (abut) child in horizontal B*-tree
  BStarNode *right = nullptr;  // upper-adjacent same-x child with paper upper-bound constraint
};

struct BStarTree {
  BStarNode *root = nullptr;
  std::vector<BStarNode> nodes;
};