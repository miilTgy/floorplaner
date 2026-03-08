#pragma once

#include "common.h"
#include "bstar_tree.h"

BStarTree floorplan_to_bstar_tree(const Problem &P, const FloorplanResult &fp);
void dump_bstar_tree_debug(const BStarTree &tree, const FloorplanResult &fp,
                           const std::string &filename);
