#include "init_planer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

constexpr double kEps = 1e-9;

struct Segment {
  double x_l;
  double x_r;
  double h;
};

struct PlacedRect {
  int block_id;
  double x;
  double y;
  int rotate;
  double w;
  double h;
};

struct Candidate {
  double x;
  double y;
  int rotate;
  double w;
  double h;
  double H_after;
  double hpwl_after;
  double cost_after;
};

struct Pose {
  double x = 0.0;
  double y = 0.0;
  int rotate = 0;
  bool present = false;
};

struct CandidateDebug {
  int rotate = 0;
  double x_seed = 0.0;
  double y_seed = 0.0;
  double x_compact = 0.0;
  double y_compact = 0.0;
  double H_after = 0.0;
  double hpwl_after = 0.0;
  double cost_after = 0.0;
  bool legal = false;
  bool chosen = false;
};

struct IterDebug {
  int iter = 0;
  int block_id = -1;
  std::string block_name;
  std::vector<CandidateDebug> cands;
  double chosen_H = 0.0;
  double chosen_hpwl = 0.0;
  double chosen_cost = 0.0;
};

struct DebugStore {
  bool enabled = false;
  std::vector<IterDebug> iters;
};

DebugStore g_debug_store;

bool debug_enabled() {
  const char *env = std::getenv("DEBUG");
  return env != nullptr && std::string(env) != "0";
}

void clear_debug_store(bool enabled_after_clear) {
  g_debug_store.enabled = enabled_after_clear;
  g_debug_store.iters.clear();
}

bool approx_eq(double a, double b) { return std::abs(a - b) <= kEps; }

bool overlap_1d(double l1, double r1, double l2, double r2) {
  return l1 < r2 - kEps && r1 > l2 + kEps;
}

bool overlap_2d(double ax, double ay, double aw, double ah, double bx, double by, double bw,
                double bh) {
  return overlap_1d(ax, ax + aw, bx, bx + bw) && overlap_1d(ay, ay + ah, by, by + bh);
}

void dims_for_rotate(const Block &b, int rotate, double &w_used, double &h_used) {
  if (rotate == 0) {
    w_used = static_cast<double>(b.w);
    h_used = static_cast<double>(b.h);
  } else {
    w_used = static_cast<double>(b.h);
    h_used = static_cast<double>(b.w);
  }
}

void rotated_offset(const Pin &pin, int rotate, double &dx_rot, double &dy_rot) {
  if (rotate == 0) {
    dx_rot = pin.dx;
    dy_rot = pin.dy;
  } else {
    dx_rot = -pin.dy;
    dy_rot = pin.dx;
  }
}

void pin_abs_from_pose(const Block &block, const Pin &pin, double x, double y, int rotate,
                       double &out_x, double &out_y) {
  double w_used = 0.0;
  double h_used = 0.0;
  dims_for_rotate(block, rotate, w_used, h_used);

  const double cx = x + w_used * 0.5;
  const double cy = y + h_used * 0.5;

  double dx_rot = 0.0;
  double dy_rot = 0.0;
  rotated_offset(pin, rotate, dx_rot, dy_rot);

  out_x = cx + dx_rot;
  out_y = cy + dy_rot;
}

bool is_legal_rect(const std::vector<PlacedRect> &placed_rects, double chip_w, double x, double y,
                   double w, double h) {
  if (x < -kEps || y < -kEps) {
    return false;
  }
  if (x + w > chip_w + kEps) {
    return false;
  }

  for (const PlacedRect &r : placed_rects) {
    if (overlap_2d(x, y, w, h, r.x, r.y, r.w, r.h)) {
      return false;
    }
  }
  return true;
}

double contour_lowest_y(const std::vector<Segment> &contour, double x, double w) {
  const double xr = x + w;
  double y = 0.0;
  for (const Segment &seg : contour) {
    if (overlap_1d(seg.x_l, seg.x_r, x, xr)) {
      y = std::max(y, seg.h);
    }
  }
  return y;
}

double downward_y_from_rects(const std::vector<PlacedRect> &placed_rects, double x, double w) {
  const double xr = x + w;
  double y = 0.0;
  for (const PlacedRect &r : placed_rects) {
    if (overlap_1d(r.x, r.x + r.w, x, xr)) {
      y = std::max(y, r.y + r.h);
    }
  }
  return y;
}

std::vector<double> sort_unique_eps(std::vector<double> values) {
  std::sort(values.begin(), values.end());
  std::vector<double> out;
  out.reserve(values.size());
  for (double v : values) {
    if (out.empty() || !approx_eq(v, out.back())) {
      out.push_back(v);
    }
  }
  return out;
}

double left_compact_x(const std::vector<PlacedRect> &placed_rects, double chip_w, double y,
                      double w, double h, double fallback_x) {
  std::vector<double> xs;
  xs.reserve(placed_rects.size() + 1);
  xs.push_back(0.0);
  for (const PlacedRect &r : placed_rects) {
    xs.push_back(r.x + r.w);
  }
  xs = sort_unique_eps(std::move(xs));

  for (double cand_x : xs) {
    if (cand_x < -kEps) {
      continue;
    }
    if (cand_x + w > chip_w + kEps) {
      continue;
    }
    if (is_legal_rect(placed_rects, chip_w, cand_x, y, w, h)) {
      return cand_x <= kEps ? 0.0 : cand_x;
    }
  }
  return fallback_x;
}

std::pair<double, double> compact_down_left_fixed_point(const std::vector<PlacedRect> &placed_rects,
                                                         const std::vector<Segment> &contour,
                                                         double chip_w, double x_seed, double w,
                                                         double h) {
  double x = x_seed;
  double y = contour_lowest_y(contour, x, w);

  for (int iter = 0; iter < 256; ++iter) {
    const double y_down = downward_y_from_rects(placed_rects, x, w);
    const double x_left = left_compact_x(placed_rects, chip_w, y_down, w, h, x);
    const double y_after = downward_y_from_rects(placed_rects, x_left, w);

    if (approx_eq(x_left, x) && approx_eq(y_after, y)) {
      x = x_left;
      y = y_after;
      break;
    }

    x = x_left;
    y = y_after;
  }

  if (std::abs(x) <= kEps) {
    x = 0.0;
  }
  if (std::abs(y) <= kEps) {
    y = 0.0;
  }
  return {x, y};
}

std::vector<Segment> rebuild_contour(double chip_w, const std::vector<PlacedRect> &placed_rects) {
  std::vector<double> xs;
  xs.reserve(placed_rects.size() * 2 + 2);
  xs.push_back(0.0);
  xs.push_back(chip_w);
  for (const PlacedRect &r : placed_rects) {
    xs.push_back(r.x);
    xs.push_back(r.x + r.w);
  }
  xs = sort_unique_eps(std::move(xs));

  std::vector<Segment> raw;
  raw.reserve(xs.size());
  for (size_t i = 0; i + 1 < xs.size(); ++i) {
    const double l = xs[i];
    const double r = xs[i + 1];
    if (r <= l + kEps) {
      continue;
    }
    const double xm = (l + r) * 0.5;
    double h = 0.0;
    for (const PlacedRect &rect : placed_rects) {
      if (xm > rect.x + kEps && xm < rect.x + rect.w - kEps) {
        h = std::max(h, rect.y + rect.h);
      }
    }
    raw.push_back(Segment{l, r, h});
  }

  if (raw.empty()) {
    return {Segment{0.0, chip_w, 0.0}};
  }

  std::vector<Segment> merged;
  merged.reserve(raw.size());
  merged.push_back(raw.front());
  for (size_t i = 1; i < raw.size(); ++i) {
    Segment &tail = merged.back();
    const Segment &cur = raw[i];
    if (approx_eq(tail.h, cur.h) && approx_eq(tail.x_r, cur.x_l)) {
      tail.x_r = cur.x_r;
    } else {
      merged.push_back(cur);
    }
  }

  return merged;
}

std::vector<double> generate_candidate_xs(const std::vector<Segment> &contour,
                                          const std::vector<PlacedRect> &placed_rects,
                                          double chip_w, double w) {
  std::vector<double> xs;
  xs.reserve(contour.size() + placed_rects.size() * 2 + 1);
  xs.push_back(0.0);
  for (const Segment &seg : contour) {
    xs.push_back(seg.x_l);
  }
  for (const PlacedRect &r : placed_rects) {
    xs.push_back(r.x);
    xs.push_back(r.x + r.w);
  }
  xs = sort_unique_eps(std::move(xs));

  std::vector<double> out;
  out.reserve(xs.size());
  for (double x : xs) {
    if (x < -kEps) {
      continue;
    }
    if (x + w > chip_w + kEps) {
      continue;
    }
    if (std::abs(x) <= kEps) {
      x = 0.0;
    }
    if (out.empty() || !approx_eq(out.back(), x)) {
      out.push_back(x);
    }
  }
  return out;
}

double compute_total_height(const Problem &P, const std::vector<Pose> &poses) {
  double H = 0.0;
  for (size_t i = 0; i < poses.size(); ++i) {
    if (!poses[i].present) {
      continue;
    }
    double w = 0.0;
    double h = 0.0;
    dims_for_rotate(P.blocks[i], poses[i].rotate, w, h);
    H = std::max(H, poses[i].y + h);
  }
  return H;
}

double compute_total_hpwl(const Problem &P, const std::vector<Pose> &poses) {
  double hpwl_total = 0.0;

  for (const Net &net : P.nets) {
    bool has_point = false;
    double xmin = 0.0;
    double xmax = 0.0;
    double ymin = 0.0;
    double ymax = 0.0;

    for (int pid : net.pin_ids) {
      const Pin &pin = P.pins[static_cast<size_t>(pid)];
      const int bid = pin.block_id;
      if (bid < 0 || static_cast<size_t>(bid) >= poses.size() || !poses[static_cast<size_t>(bid)].present) {
        continue;
      }

      double px = 0.0;
      double py = 0.0;
      pin_abs_from_pose(P.blocks[static_cast<size_t>(bid)], pin, poses[static_cast<size_t>(bid)].x,
                        poses[static_cast<size_t>(bid)].y, poses[static_cast<size_t>(bid)].rotate,
                        px, py);

      if (!has_point) {
        xmin = px;
        xmax = px;
        ymin = py;
        ymax = py;
        has_point = true;
      } else {
        xmin = std::min(xmin, px);
        xmax = std::max(xmax, px);
        ymin = std::min(ymin, py);
        ymax = std::max(ymax, py);
      }
    }

    if (has_point) {
      hpwl_total += (xmax - xmin) + (ymax - ymin);
    }
  }

  return hpwl_total;
}

void evaluate_layout_metrics(const Problem &P, const std::vector<Pose> &poses, double &H_out,
                             double &hpwl_out, double &cost_out) {
  H_out = compute_total_height(P, poses);
  hpwl_out = compute_total_hpwl(P, poses);
  if (P.nets.empty()) {
    cost_out = H_out;
  } else {
    cost_out = H_out + hpwl_out / static_cast<double>(P.nets.size());
  }
}

bool better_first_choice(const Candidate &cand, const Candidate &best) {
  if (cand.cost_after < best.cost_after - kEps) {
    return true;
  }
  if (cand.cost_after > best.cost_after + kEps) {
    return false;
  }
  if (cand.H_after < best.H_after - kEps) {
    return true;
  }
  if (cand.H_after > best.H_after + kEps) {
    return false;
  }
  if (cand.y < best.y - kEps) {
    return true;
  }
  if (cand.y > best.y + kEps) {
    return false;
  }
  if (cand.x < best.x - kEps) {
    return true;
  }
  if (cand.x > best.x + kEps) {
    return false;
  }
  return cand.rotate < best.rotate;
}

bool better_general_choice(const Candidate &cand, const Candidate &best) {
  if (cand.cost_after < best.cost_after - kEps) {
    return true;
  }
  if (cand.cost_after > best.cost_after + kEps) {
    return false;
  }
  if (cand.hpwl_after < best.hpwl_after - kEps) {
    return true;
  }
  if (cand.hpwl_after > best.hpwl_after + kEps) {
    return false;
  }
  if (cand.y < best.y - kEps) {
    return true;
  }
  if (cand.y > best.y + kEps) {
    return false;
  }
  if (cand.x < best.x - kEps) {
    return true;
  }
  if (cand.x > best.x + kEps) {
    return false;
  }
  if (cand.rotate != best.rotate) {
    return cand.rotate < best.rotate;
  }
  return false;
}

}  // namespace

void clear_init_planer_debug() { clear_debug_store(false); }

void dump_init_planer_debug(std::ostream &os) {
  if (!g_debug_store.enabled) {
    return;
  }

  for (const IterDebug &iter : g_debug_store.iters) {
    os << "[PLANER] iter=" << iter.iter << " block_id=" << iter.block_id
       << " name=" << iter.block_name << "\n";
    for (const CandidateDebug &cand : iter.cands) {
      os << "[PLANER] cand rotate=" << cand.rotate << " x_seed=" << cand.x_seed
         << " y_seed=" << cand.y_seed << " x=" << cand.x_compact << " y=" << cand.y_compact
         << " legal=" << (cand.legal ? "YES" : "NO");
      if (cand.legal) {
        os << " H=" << cand.H_after << " hpwl=" << cand.hpwl_after << " cost=" << cand.cost_after;
      }
      os << " chosen=" << (cand.chosen ? "YES" : "NO") << "\n";
    }
    os << "[PLANER] iter-end H=" << iter.chosen_H << " hpwl=" << iter.chosen_hpwl
       << " cost=" << iter.chosen_cost << "\n";
  }
}

FloorplanResult build_initial_floorplan(const Problem &P, const std::vector<int> &perm) {
  const bool debug = debug_enabled();
  clear_debug_store(debug);

  const int n = static_cast<int>(P.blocks.size());
  if (static_cast<int>(perm.size()) != n) {
    throw std::runtime_error("init_planer_admissible: perm size mismatch with block count");
  }

  std::vector<bool> seen(static_cast<size_t>(n), false);
  for (int bid : perm) {
    if (bid < 0 || bid >= n) {
      throw std::runtime_error("init_planer_admissible: perm contains invalid block id");
    }
    if (seen[static_cast<size_t>(bid)]) {
      throw std::runtime_error("init_planer_admissible: perm contains duplicate block id");
    }
    seen[static_cast<size_t>(bid)] = true;
  }

  FloorplanResult result;
  result.x.assign(static_cast<size_t>(n), 0.0);
  result.y.assign(static_cast<size_t>(n), 0.0);
  result.rotate.assign(static_cast<size_t>(n), 0);
  result.H = 0.0;
  result.hpwl = 0.0;
  result.cost = 0.0;
  result.items.clear();
  result.items.reserve(static_cast<size_t>(n));

  if (n == 0) {
    return result;
  }

  const double chip_w = static_cast<double>(P.chipW);

  std::vector<bool> placed(static_cast<size_t>(n), false);
  std::vector<PlacedRect> placed_rects;
  placed_rects.reserve(static_cast<size_t>(n));

  std::vector<Segment> contour = {Segment{0.0, chip_w, 0.0}};

  for (int t = 0; t < n; ++t) {
    const int block_id = perm[static_cast<size_t>(t)];
    const Block &block = P.blocks[static_cast<size_t>(block_id)];

    IterDebug iter_debug;
    int chosen_debug_index = -1;
    if (debug) {
      iter_debug.iter = t;
      iter_debug.block_id = block_id;
      iter_debug.block_name = block.name;
    }

    bool has_best = false;
    Candidate best{};

    std::vector<Pose> base_poses(static_cast<size_t>(n));
    for (const PlacedRect &r : placed_rects) {
      base_poses[static_cast<size_t>(r.block_id)] = Pose{r.x, r.y, r.rotate, true};
    }

    for (int r = 0; r <= 1; ++r) {
      double w = 0.0;
      double h = 0.0;
      dims_for_rotate(block, r, w, h);
      if (w > chip_w + kEps) {
        continue;
      }

      std::vector<double> candidate_xs;
      if (t == 0) {
        candidate_xs.push_back(0.0);
      } else {
        candidate_xs = generate_candidate_xs(contour, placed_rects, chip_w, w);
      }

      for (double x_seed : candidate_xs) {
        const double y_seed =
            (t == 0) ? 0.0 : contour_lowest_y(contour, x_seed, w);
        double cand_x = x_seed;
        double cand_y = y_seed;

        if (t > 0) {
          const auto compacted =
              compact_down_left_fixed_point(placed_rects, contour, chip_w, x_seed, w, h);
          cand_x = compacted.first;
          cand_y = compacted.second;
        }

        const bool legal = is_legal_rect(placed_rects, chip_w, cand_x, cand_y, w, h);
        CandidateDebug dbg_row;
        if (debug) {
          dbg_row.rotate = r;
          dbg_row.x_seed = x_seed;
          dbg_row.y_seed = y_seed;
          dbg_row.x_compact = cand_x;
          dbg_row.y_compact = cand_y;
          dbg_row.legal = legal;
        }

        if (!legal) {
          if (debug) {
            iter_debug.cands.push_back(dbg_row);
          }
          continue;
        }

        std::vector<Pose> poses = base_poses;
        poses[static_cast<size_t>(block_id)] = Pose{cand_x, cand_y, r, true};

        Candidate cand;
        cand.x = cand_x;
        cand.y = cand_y;
        cand.rotate = r;
        cand.w = w;
        cand.h = h;
        evaluate_layout_metrics(P, poses, cand.H_after, cand.hpwl_after, cand.cost_after);

        if (!has_best ||
            (t == 0 ? better_first_choice(cand, best)
                    : better_general_choice(cand, best))) {
          has_best = true;
          best = cand;
          if (debug) {
            chosen_debug_index = static_cast<int>(iter_debug.cands.size());
          }
        }

        if (debug) {
          dbg_row.H_after = cand.H_after;
          dbg_row.hpwl_after = cand.hpwl_after;
          dbg_row.cost_after = cand.cost_after;
          iter_debug.cands.push_back(dbg_row);
        }
      }
    }

    if (!has_best) {
      throw std::runtime_error("init_planer_admissible: no legal admissible candidate for block id=" +
                               std::to_string(block_id) + " name=" + block.name);
    }

    placed[static_cast<size_t>(block_id)] = true;
    result.x[static_cast<size_t>(block_id)] = best.x;
    result.y[static_cast<size_t>(block_id)] = best.y;
    result.rotate[static_cast<size_t>(block_id)] = best.rotate;
    result.items.push_back(FloorplanItem{block_id, best.x, best.y, best.rotate, best.w, best.h});

    placed_rects.push_back(
        PlacedRect{block_id, best.x, best.y, best.rotate, best.w, best.h});
    contour = rebuild_contour(chip_w, placed_rects);

    if (debug) {
      if (chosen_debug_index >= 0 &&
          static_cast<size_t>(chosen_debug_index) < iter_debug.cands.size()) {
        iter_debug.cands[static_cast<size_t>(chosen_debug_index)].chosen = true;
      }
      iter_debug.chosen_H = best.H_after;
      iter_debug.chosen_hpwl = best.hpwl_after;
      iter_debug.chosen_cost = best.cost_after;
      g_debug_store.iters.push_back(std::move(iter_debug));
    }
  }

  std::vector<Pose> final_poses(static_cast<size_t>(n));
  for (const PlacedRect &r : placed_rects) {
    final_poses[static_cast<size_t>(r.block_id)] = Pose{r.x, r.y, r.rotate, true};
  }
  evaluate_layout_metrics(P, final_poses, result.H, result.hpwl, result.cost);
  return result;
}
