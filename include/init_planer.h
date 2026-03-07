#pragma once

#include "common.h"

FloorplanResult build_initial_floorplan(const Problem &P, const std::vector<int> &perm);
void dump_init_planer_debug(std::ostream &os);
void clear_init_planer_debug();
