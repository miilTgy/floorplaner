#pragma once

#include "common.h"
#include "bstar_tree.h"

FloorplanResult bstar_tree_to_floorplan(const Problem &P, const BStarTree &tree,
                                        const std::vector<int> &rotate);

void dump_bstar_tree2fp_debug(const FloorplanResult &fp, const std::string &filename);
