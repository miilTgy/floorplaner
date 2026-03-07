#include "orderer.h"

namespace {

constexpr double kSeedLambda = 0.7;
constexpr double kEps = 1e-12;

struct SeedTrace {
  int id;
  std::string name;
  int net_count;
  long long area;
  double normalized_area;
  double normalized_net_count;
  double seed_score;
  bool chosen;
};

struct Orderer2Trace {
  std::vector<SeedTrace> seed_cands;
  std::vector<IterTrace> iters;
  std::vector<int> perm;
  int chosen_seed_id = -1;
};

Orderer2Trace g_trace;

bool orderer_debug_enabled() {
#ifdef ORDERER_DEBUG
  return true;
#else
  const char *env = std::getenv("DEBUG");
  return env != nullptr && std::string(env) != "0";
#endif
}

long long block_area(const Block &b) {
  return static_cast<long long>(b.w) * static_cast<long long>(b.h);
}

double normalize_value(double value, double vmin, double vmax) {
  if (std::abs(vmax - vmin) <= kEps) {
    return 1.0;
  }
  return (value - vmin) / (vmax - vmin);
}

bool better_seed(double score_a, long long area_a, int net_count_a, int id_a, double score_b,
                 long long area_b, int net_count_b, int id_b) {
  if (score_a > score_b + kEps) {
    return true;
  }
  if (score_a + kEps < score_b) {
    return false;
  }
  if (area_a != area_b) {
    return area_a > area_b;
  }
  if (net_count_a != net_count_b) {
    return net_count_a > net_count_b;
  }
  return id_a < id_b;
}

bool better_iter_cand(const IterCand &cand, const IterCand &best) {
  if (cand.gain != best.gain) {
    return cand.gain > best.gain;
  }
  if (cand.term != best.term) {
    return cand.term > best.term;
  }
  if (cand.new_ != best.new_) {
    return cand.new_ < best.new_;
  }
  return cand.id < best.id;
}

void mark_chosen_seed(std::vector<SeedTrace> &cands, int chosen_id) {
  for (SeedTrace &cand : cands) {
    cand.chosen = (cand.id == chosen_id);
  }
}

void mark_chosen_iter(std::vector<IterCand> &cands, int chosen_id) {
  for (IterCand &cand : cands) {
    cand.chosen = (cand.id == chosen_id);
  }
}

}  // namespace

void dump_ordering_trace() {
  std::ostream &os = std::cout;
  if (g_trace.seed_cands.empty()) {
    return;
  }

  os << "[ORDERER] Seed selection\n";
  const SeedTrace *chosen_seed = nullptr;
  for (const SeedTrace &cand : g_trace.seed_cands) {
    os << "[ORDERER] seed-cand id=" << cand.id << " name=" << cand.name
       << " area=" << cand.area << " net_count=" << cand.net_count
       << " n_area=" << cand.normalized_area << " n_net=" << cand.normalized_net_count
       << " seed_score=" << cand.seed_score << " best=" << (cand.chosen ? "YES" : "NO") << "\n";
    if (cand.chosen) {
      chosen_seed = &cand;
    }
  }

  if (chosen_seed != nullptr) {
    os << "[ORDERER] chosen-seed id=" << chosen_seed->id << " name=" << chosen_seed->name
       << " area=" << chosen_seed->area << " net_count=" << chosen_seed->net_count
       << " seed_score=" << chosen_seed->seed_score << "\n";
  }

  for (const IterTrace &iter : g_trace.iters) {
    os << "[ORDERER] iter=" << iter.iter << " placed_size=" << iter.iter << "\n";
    IterCand picked{-1, "", 0, 0, 0, false};
    for (const IterCand &cand : iter.cands) {
      os << "[ORDERER] cand id=" << cand.id << " name=" << cand.name << " term=" << cand.term
         << " new=" << cand.new_ << " gain=" << cand.gain
         << " best=" << (cand.chosen ? "YES" : "NO") << "\n";
      if (cand.chosen) {
        picked = cand;
      }
    }
    if (picked.id >= 0) {
      os << "[ORDERER] pick id=" << picked.id << " name=" << picked.name
         << " term=" << picked.term << " new=" << picked.new_ << " gain=" << picked.gain
         << "\n";
    }
  }
}

std::vector<int> build_initial_ordering(const Problem &P) {
  const bool debug = orderer_debug_enabled();

  g_trace.seed_cands.clear();
  g_trace.iters.clear();
  g_trace.perm.clear();
  g_trace.chosen_seed_id = -1;

  const int n = static_cast<int>(P.blocks.size());
  if (n == 0) {
    return {};
  }

  std::vector<long long> areas(static_cast<size_t>(n), 0);
  std::vector<int> net_counts(static_cast<size_t>(n), 0);
  long long area_min = 0;
  long long area_max = 0;
  int net_min = 0;
  int net_max = 0;

  for (int i = 0; i < n; ++i) {
    areas[static_cast<size_t>(i)] = block_area(P.blocks[static_cast<size_t>(i)]);
    net_counts[static_cast<size_t>(i)] =
        static_cast<int>(P.blocks[static_cast<size_t>(i)].net_ids.size());
    if (i == 0) {
      area_min = area_max = areas[static_cast<size_t>(i)];
      net_min = net_max = net_counts[static_cast<size_t>(i)];
    } else {
      area_min = std::min(area_min, areas[static_cast<size_t>(i)]);
      area_max = std::max(area_max, areas[static_cast<size_t>(i)]);
      net_min = std::min(net_min, net_counts[static_cast<size_t>(i)]);
      net_max = std::max(net_max, net_counts[static_cast<size_t>(i)]);
    }
  }

  double best_seed_score = -1.0;
  long long best_seed_area = -1;
  int best_seed_net_count = -1;
  int best_seed_id = -1;

  g_trace.seed_cands.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    const double n_area =
        normalize_value(static_cast<double>(areas[static_cast<size_t>(i)]),
                        static_cast<double>(area_min), static_cast<double>(area_max));
    const double n_net =
        normalize_value(static_cast<double>(net_counts[static_cast<size_t>(i)]),
                        static_cast<double>(net_min), static_cast<double>(net_max));
    const double score = kSeedLambda * n_area + (1.0 - kSeedLambda) * n_net;

    if (best_seed_id < 0 ||
        better_seed(score, areas[static_cast<size_t>(i)], net_counts[static_cast<size_t>(i)], i,
                    best_seed_score, best_seed_area, best_seed_net_count, best_seed_id)) {
      best_seed_score = score;
      best_seed_area = areas[static_cast<size_t>(i)];
      best_seed_net_count = net_counts[static_cast<size_t>(i)];
      best_seed_id = i;
    }

    g_trace.seed_cands.push_back(
        SeedTrace{i,
                  P.blocks[static_cast<size_t>(i)].name,
                  net_counts[static_cast<size_t>(i)],
                  areas[static_cast<size_t>(i)],
                  n_area,
                  n_net,
                  score,
                  false});
  }
  g_trace.chosen_seed_id = best_seed_id;
  mark_chosen_seed(g_trace.seed_cands, best_seed_id);

  std::vector<int> in_count(P.nets.size(), 0);
  std::vector<bool> used(static_cast<size_t>(n), false);
  std::vector<int> perm;
  perm.reserve(static_cast<size_t>(n));

  const auto place_block = [&](int b) {
    used[static_cast<size_t>(b)] = true;
    perm.push_back(b);
    for (int net_id : P.blocks[static_cast<size_t>(b)].net_ids) {
      if (net_id >= 0 && static_cast<size_t>(net_id) < in_count.size()) {
        ++in_count[static_cast<size_t>(net_id)];
      }
    }
  };

  place_block(best_seed_id);

  int iter_no = 1;
  while (static_cast<int>(perm.size()) < n) {
    IterCand best_cand{-1, "", 0, 0, 0, false};
    IterTrace iter_trace;
    iter_trace.iter = iter_no;
    if (debug) {
      iter_trace.cands.reserve(static_cast<size_t>(n - static_cast<int>(perm.size())));
    }

    for (int m = 0; m < n; ++m) {
      if (used[static_cast<size_t>(m)]) {
        continue;
      }

      int term = 0;
      int new_ = 0;
      for (int net_id : P.blocks[static_cast<size_t>(m)].net_ids) {
        if (net_id < 0 || static_cast<size_t>(net_id) >= in_count.size()) {
          continue;
        }
        const int c = in_count[static_cast<size_t>(net_id)];
        if (c == 1) {
          ++term;
        } else if (c == 0) {
          ++new_;
        }
      }

      IterCand cand{m, P.blocks[static_cast<size_t>(m)].name, term, new_, term - new_, false};
      if (best_cand.id < 0 || better_iter_cand(cand, best_cand)) {
        best_cand = cand;
      }
      if (debug) {
        iter_trace.cands.push_back(cand);
      }
    }

    place_block(best_cand.id);
    if (debug) {
      mark_chosen_iter(iter_trace.cands, best_cand.id);
      g_trace.iters.push_back(std::move(iter_trace));
    }

    ++iter_no;
  }

  g_trace.perm = perm;
  return perm;
}
