#pragma once

#include "init_planer.h"

#include <string>
#include <vector>

struct SAStats {
  long long total_steps = 0;
  long long accepted = 0;
  long long rejected = 0;
  long long invalid = 0;
  int outer_loop_count = 0;
  std::string stop_reason;
  double final_temperature = 0.0;
};

struct SAResult {
  BStarTree best_tree;
  std::vector<int> best_rotate;
  FloorplanResult best_fp;
  double best_cost = 0.0;
  SAStats stats;
  std::vector<std::string> debug_lines;
  std::string invalid_log_path;
};

SAResult run_sa(const Problem &P, const InitBStarResult &init, double time_limit_seconds,
                bool debug_sa, const std::string &invalid_log_path);
