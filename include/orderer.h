#pragma once

#include "common.h"

struct SeedCand {
  int id;
  std::string name;
  int net_count;
  long long area;
  bool chosen;
};

struct IterCand {
  int id;
  std::string name;
  int term;
  int new_;
  int gain;
  bool chosen;
};

struct IterTrace {
  int iter;
  std::vector<IterCand> cands;
};

struct OrderingTrace {
  std::vector<SeedCand> seed_cands;
  std::vector<IterTrace> iters;
  std::vector<int> perm;
};

std::vector<int> build_initial_ordering(const Problem &P);
void dump_ordering_trace();
