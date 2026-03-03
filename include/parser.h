#pragma once

#include "common.h"

Problem parse_problem(const std::string &path);
void dump_problem(const Problem &p, std::ostream &os);
