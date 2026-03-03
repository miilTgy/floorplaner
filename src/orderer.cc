#include "orderer.h"

namespace {

OrderingTrace g_trace;

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

bool better_seed(const SeedCand &cand, const SeedCand &best) {
  if (cand.net_count != best.net_count) {
    return cand.net_count > best.net_count;
  }
  if (cand.area != best.area) {
    return cand.area > best.area;
  }
  return cand.id < best.id;
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

void mark_chosen_seed(std::vector<SeedCand> &cands, int chosen_id) {
  for (SeedCand &cand : cands) {
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
  os << "[ORDERER] Seed selection\n";
  const SeedCand *seed_ptr = nullptr;
  for (const SeedCand &cand : g_trace.seed_cands) {
    os << "[ORDERER] seed-cand id=" << cand.id << " name=" << cand.name
       << " net_count=" << cand.net_count << " area=" << cand.area
       << " best=" << (cand.chosen ? "YES" : "NO") << "\n";
    if (cand.chosen) {
      seed_ptr = &cand;
    }
  }

  if (seed_ptr != nullptr) {
    os << "[ORDERER] chosen-seed id=" << seed_ptr->id << " name=" << seed_ptr->name
       << " net_count=" << seed_ptr->net_count << " area=" << seed_ptr->area << "\n";
  }

  for (const IterTrace &iter : g_trace.iters) {
    os << "[ORDERER] iter=" << iter.iter << " placed_size=" << iter.iter << "\n";
    IterCand picked{-1, "", 0, 0, 0, false};
    for (const IterCand &cand : iter.cands) {
      os << "[ORDERER] cand id=" << cand.id << " name=" << cand.name
         << " term=" << cand.term << " new=" << cand.new_
         << " gain=" << cand.gain << " best=" << (cand.chosen ? "YES" : "NO")
         << "\n";
      if (cand.chosen) {
        picked = cand;
      }
    }
    if (picked.id >= 0) {
      os << "[ORDERER] pick id=" << picked.id << " name=" << picked.name
         << " term=" << picked.term << " new=" << picked.new_
         << " gain=" << picked.gain << "\n";
    }
  }
}

std::vector<int> build_initial_ordering(const Problem &P) {
  const bool debug = orderer_debug_enabled();

  g_trace.seed_cands.clear();
  g_trace.iters.clear();
  g_trace.perm.clear();

  const int n = static_cast<int>(P.blocks.size());
  if (n == 0) {
    return {};
  }

  std::vector<int> in_count(P.nets.size(), 0);
  std::vector<bool> used(static_cast<size_t>(n), false);
  std::vector<int> perm;
  perm.reserve(static_cast<size_t>(n));

  if (debug) {
    g_trace.seed_cands.reserve(static_cast<size_t>(n));
    g_trace.iters.reserve(static_cast<size_t>(n > 0 ? n - 1 : 0));
    g_trace.perm.reserve(static_cast<size_t>(n));
  }

  SeedCand best_seed{-1, "", -1, -1, false};
  for (int i = 0; i < n; ++i) {
    SeedCand cand{
        i,
        P.blocks[static_cast<size_t>(i)].name,
        static_cast<int>(P.blocks[static_cast<size_t>(i)].net_ids.size()),
        block_area(P.blocks[static_cast<size_t>(i)]),
        false,
    };
    if (best_seed.id < 0 || better_seed(cand, best_seed)) {
      best_seed = cand;
    }
    if (debug) {
      g_trace.seed_cands.push_back(cand);
    }
  }

  if (debug) {
    mark_chosen_seed(g_trace.seed_cands, best_seed.id);
  }

  const auto place_block = [&](int b) {
    used[static_cast<size_t>(b)] = true;
    perm.push_back(b);
    for (int net_id : P.blocks[static_cast<size_t>(b)].net_ids) {
      if (net_id >= 0 && static_cast<size_t>(net_id) < in_count.size()) {
        ++in_count[static_cast<size_t>(net_id)];
      }
    }
  };

  place_block(best_seed.id);

  int iter_no = 1;
  while (static_cast<int>(perm.size()) < n) {
    IterCand best_cand{-1, "", 0, 0, 0, false};
    IterTrace iter_trace;
    if (debug) {
      iter_trace.iter = iter_no;
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

      IterCand cand{
          m,
          P.blocks[static_cast<size_t>(m)].name,
          term,
          new_,
          term - new_,
          false,
      };
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

  if (debug) {
    g_trace.perm = perm;
  }

  return perm;
}
