#pragma once

#include "common.h"
#include "bstar_tree.h"

struct InitBStarResult {
  BStarTree tree;
  std::vector<int> rotate;
  FloorplanResult fp;
  double cost = 0.0;
};

InitBStarResult build_initial_bstar_result(const Problem &P,
                                           const std::vector<int> &perm);

FloorplanResult build_initial_floorplan(const Problem &P, const std::vector<int> &perm);
std::string derive_tree_dump_filename();
void dump_bstar_tree_text(const BStarTree &tree, const FloorplanResult &fp,
                          const std::vector<int> &rotate, const std::string &filename);
void dump_init_planer_debug(std::ostream &os);
void clear_init_planer_debug();
