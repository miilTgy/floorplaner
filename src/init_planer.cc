#include "init_planer.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr double kAlpha = 0.5;
constexpr double kEps = 1e-9;

struct CandidatePoint {
  double x;
  double y;
  int rotate;
  bool is_pin_align;
  int base_corner_index;
  CornerType base_corner_type;
  int src_pin_id;
  int ref_pin_id;
  std::string src_pin_name;
  std::string ref_pin_name;
};

struct LegalCandidateDebug {
  double x;
  double y;
  int rotate;
  bool is_pin_align;
  int base_corner_index;
  CornerType base_corner_type;
  int src_pin_id;
  int ref_pin_id;
  std::string src_pin_name;
  std::string ref_pin_name;
  double delta_h;
  double delta_hpwl;
  double score;
  bool chosen;
};

struct RotationDebug {
  int rotate;
  double w;
  double h;
  std::vector<CandidatePoint> block_corner_candidates;
  std::vector<CandidatePoint> pin_align_candidates;
};

struct IterDebug {
  int iter;
  int block_id;
  std::string block_name;
  std::vector<RotationDebug> rotations;
  std::vector<LegalCandidateDebug> legal_candidates;
  double H_before;
  double H_after;
};

struct InitPlanerDebugStore {
  bool enabled;
  std::vector<IterDebug> iters;
};

InitPlanerDebugStore g_debug_store;

bool debug_enabled() {
  const char *env = std::getenv("DEBUG");
  return env != nullptr && std::string(env) != "0";
}

void clear_debug_store(bool enable_after_clear) {
  g_debug_store.enabled = enable_after_clear;
  g_debug_store.iters.clear();
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

void pin_abs_from_block_pose(const Block &block, const Pin &pin, double x, double y, int rotate,
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

bool is_inside_rect(double x, double y, double rx, double ry, double rw, double rh) {
  return x > rx + kEps && x < rx + rw - kEps && y > ry + kEps && y < ry + rh - kEps;
}

bool is_candidate_legal(const Problem &P, const std::vector<bool> &placed,
                        const std::vector<double> &x_of, const std::vector<double> &y_of,
                        const std::vector<double> &w_used_of,
                        const std::vector<double> &h_used_of, double x, double y, double w,
                        double h) {
  if (x < -kEps || y < -kEps) {
    return false;
  }
  if (x + w > static_cast<double>(P.chipW) + kEps) {
    return false;
  }

  const int n = static_cast<int>(P.blocks.size());
  for (int i = 0; i < n; ++i) {
    if (!placed[static_cast<size_t>(i)]) {
      continue;
    }
    const double xi = x_of[static_cast<size_t>(i)];
    const double yi = y_of[static_cast<size_t>(i)];
    const double wi = w_used_of[static_cast<size_t>(i)];
    const double hi = h_used_of[static_cast<size_t>(i)];

    const bool overlap = (x < xi + wi - kEps) && (x + w > xi + kEps) && (y < yi + hi - kEps) &&
                         (y + h > yi + kEps);
    if (overlap) {
      return false;
    }
  }
  return true;
}

bool build_net_bbox(const Problem &P, int net_id, const std::vector<bool> &placed,
                    const std::vector<double> &x_of, const std::vector<double> &y_of,
                    const std::vector<int> &rot_of, int candidate_block_id,
                    bool include_candidate, double cand_x, double cand_y,
                    int cand_rotate, double &xmin, double &xmax,
                    double &ymin, double &ymax) {
  bool has_point = false;
  xmin = 0.0;
  xmax = 0.0;
  ymin = 0.0;
  ymax = 0.0;

  const Net &net = P.nets[static_cast<size_t>(net_id)];
  for (int pid : net.pin_ids) {
    const Pin &pin = P.pins[static_cast<size_t>(pid)];
    const int bid = pin.block_id;

    double px = 0.0;
    double py = 0.0;
    bool use_point = false;

    if (include_candidate && bid == candidate_block_id) {
      pin_abs_from_block_pose(P.blocks[static_cast<size_t>(bid)], pin, cand_x, cand_y, cand_rotate,
                              px, py);
      use_point = true;
    } else if (bid >= 0 && static_cast<size_t>(bid) < placed.size() &&
               placed[static_cast<size_t>(bid)]) {
      pin_abs_from_block_pose(P.blocks[static_cast<size_t>(bid)], pin,
                              x_of[static_cast<size_t>(bid)], y_of[static_cast<size_t>(bid)],
                              rot_of[static_cast<size_t>(bid)], px, py);
      use_point = true;
    }

    if (!use_point) {
      continue;
    }

    if (!has_point) {
      xmin = px;
      xmax = px;
      ymin = py;
      ymax = py;
      has_point = true;
    } else {
      if (px < xmin) {
        xmin = px;
      }
      if (px > xmax) {
        xmax = px;
      }
      if (py < ymin) {
        ymin = py;
      }
      if (py > ymax) {
        ymax = py;
      }
    }
  }

  return has_point;
}

double net_hpwl_from_bbox(bool has_bbox, double xmin, double xmax, double ymin, double ymax) {
  if (!has_bbox) {
    return 0.0;
  }
  return (xmax - xmin) + (ymax - ymin);
}

double compute_delta_hpwl(const Problem &P, int block_id, const std::vector<bool> &placed,
                          const std::vector<double> &x_of, const std::vector<double> &y_of,
                          const std::vector<int> &rot_of, double cand_x, double cand_y,
                          int cand_rotate) {
  double delta = 0.0;
  const Block &block = P.blocks[static_cast<size_t>(block_id)];

  for (int net_id : block.net_ids) {
    double xmin_before = 0.0;
    double xmax_before = 0.0;
    double ymin_before = 0.0;
    double ymax_before = 0.0;

    const bool has_before = build_net_bbox(P, net_id, placed, x_of, y_of, rot_of, block_id, false,
                                           0.0, 0.0, 0, xmin_before, xmax_before, ymin_before,
                                           ymax_before);
    const double hpwl_before =
        net_hpwl_from_bbox(has_before, xmin_before, xmax_before, ymin_before, ymax_before);

    double xmin_after = 0.0;
    double xmax_after = 0.0;
    double ymin_after = 0.0;
    double ymax_after = 0.0;
    const bool has_after = build_net_bbox(P, net_id, placed, x_of, y_of, rot_of, block_id, true,
                                          cand_x, cand_y, cand_rotate, xmin_after, xmax_after,
                                          ymin_after, ymax_after);
    const double hpwl_after =
        net_hpwl_from_bbox(has_after, xmin_after, xmax_after, ymin_after, ymax_after);

    delta += (hpwl_after - hpwl_before);
  }

  return delta;
}

double compute_total_hpwl(const Problem &P, const std::vector<double> &x_of,
                          const std::vector<double> &y_of, const std::vector<int> &rot_of) {
  double hpwl_total = 0.0;
  const int net_n = static_cast<int>(P.nets.size());
  std::vector<bool> all_placed(P.blocks.size(), true);

  for (int net_id = 0; net_id < net_n; ++net_id) {
    double xmin = 0.0;
    double xmax = 0.0;
    double ymin = 0.0;
    double ymax = 0.0;
    const bool has_bbox = build_net_bbox(P, net_id, all_placed, x_of, y_of, rot_of, -1, false,
                                         0.0, 0.0, 0, xmin, xmax, ymin, ymax);
    hpwl_total += net_hpwl_from_bbox(has_bbox, xmin, xmax, ymin, ymax);
  }

  return hpwl_total;
}

void remove_corner_at(std::vector<BlockCorner> &corners, int idx) {
  if (idx < 0 || static_cast<size_t>(idx) >= corners.size()) {
    return;
  }
  corners.erase(corners.begin() + idx);
}

void cleanup_corners_inside_blocks(std::vector<BlockCorner> &corners,
                                   const std::vector<bool> &placed,
                                   const std::vector<double> &x_of,
                                   const std::vector<double> &y_of,
                                   const std::vector<double> &w_used_of,
                                   const std::vector<double> &h_used_of) {
  std::vector<BlockCorner> kept;
  kept.reserve(corners.size());

  for (const BlockCorner &c : corners) {
    bool inside = false;
    for (size_t i = 0; i < placed.size(); ++i) {
      if (!placed[i]) {
        continue;
      }
      if (is_inside_rect(c.x, c.y, x_of[i], y_of[i], w_used_of[i], h_used_of[i])) {
        inside = true;
        break;
      }
    }
    if (!inside) {
      kept.push_back(c);
    }
  }

  corners.swap(kept);
}

bool better_first_block(double H_cand, double x_cand, double y_cand, int r_cand,
                        double H_best, double x_best, double y_best, int r_best) {
  if (H_cand != H_best) {
    return H_cand < H_best;
  }
  if (x_cand != x_best) {
    return x_cand < x_best;
  }
  if (y_cand != y_best) {
    return y_cand < y_best;
  }
  return r_cand < r_best;
}

bool better_candidate(double score_cand, double dh_cand, double dhpwl_cand, double x_cand,
                      double y_cand, int r_cand, double score_best, double dh_best,
                      double dhpwl_best, double x_best, double y_best, int r_best) {
  if (score_cand < score_best - kEps) {
    return true;
  }
  if (score_cand > score_best + kEps) {
    return false;
  }

  if (dh_cand < dh_best - kEps) {
    return true;
  }
  if (dh_cand > dh_best + kEps) {
    return false;
  }

  if (dhpwl_cand < dhpwl_best - kEps) {
    return true;
  }
  if (dhpwl_cand > dhpwl_best + kEps) {
    return false;
  }

  if (x_cand < x_best - kEps) {
    return true;
  }
  if (x_cand > x_best + kEps) {
    return false;
  }

  if (y_cand < y_best - kEps) {
    return true;
  }
  if (y_cand > y_best + kEps) {
    return false;
  }

  return r_cand < r_best;
}

}  // namespace

void clear_init_planer_debug() { clear_debug_store(false); }

void dump_init_planer_debug(std::ostream &os) {
  if (!g_debug_store.enabled) {
    return;
  }

  for (const IterDebug &iter : g_debug_store.iters) {
    os << "[PLANER] iter=" << iter.iter << " block_id=" << iter.block_id
       << " name=" << iter.block_name << " H_before=" << iter.H_before << "\n";

    for (const RotationDebug &rot : iter.rotations) {
      os << "[PLANER] rotate=" << rot.rotate << " w=" << rot.w << " h=" << rot.h << "\n";

      for (const CandidatePoint &c : rot.block_corner_candidates) {
        os << "[PLANER]   block-corner cand x=" << c.x << " y=" << c.y
           << " base_idx=" << c.base_corner_index << " base_type="
           << (c.base_corner_type == CornerType::RIGHT_DOWN ? "RIGHT_DOWN" : "LEFT_UP")
           << "\n";
      }

      for (const CandidatePoint &c : rot.pin_align_candidates) {
        os << "[PLANER]   pin-align cand x=" << c.x << " y=" << c.y
           << " base_idx=" << c.base_corner_index << " base_type="
           << (c.base_corner_type == CornerType::RIGHT_DOWN ? "RIGHT_DOWN" : "LEFT_UP")
           << " src_pin=" << c.src_pin_name << "(" << c.src_pin_id << ")"
           << " ref_pin=" << c.ref_pin_name << "(" << c.ref_pin_id << ")" << "\n";
      }
    }

    for (const LegalCandidateDebug &c : iter.legal_candidates) {
      os << "[PLANER] legal x=" << c.x << " y=" << c.y << " rotate=" << c.rotate
         << " is_pin_align=" << (c.is_pin_align ? "YES" : "NO")
         << " src_pin="
         << (c.src_pin_name.empty() ? "-" : c.src_pin_name) << "(" << c.src_pin_id << ")"
         << " ref_pin="
         << (c.ref_pin_name.empty() ? "-" : c.ref_pin_name) << "(" << c.ref_pin_id << ")"
         << " dH=" << c.delta_h << " dHPWL=" << c.delta_hpwl
         << " score=" << c.score << " chosen=" << (c.chosen ? "YES" : "NO") << "\n";
    }

    os << "[PLANER] iter-end H=" << iter.H_after << "\n";
  }
}

FloorplanResult build_initial_floorplan(const Problem &P, const std::vector<int> &perm) {
  const bool debug = debug_enabled();
  clear_debug_store(debug);

  const int n = static_cast<int>(P.blocks.size());
  if (static_cast<int>(perm.size()) != n) {
    throw std::runtime_error("init_planer: perm size mismatch with block count");
  }

  std::vector<bool> seen(static_cast<size_t>(n), false);
  for (int bid : perm) {
    if (bid < 0 || bid >= n) {
      throw std::runtime_error("init_planer: perm contains invalid block id");
    }
    if (seen[static_cast<size_t>(bid)]) {
      throw std::runtime_error("init_planer: perm contains duplicate block id");
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

  std::vector<bool> placed(static_cast<size_t>(n), false);
  std::vector<double> w_used_of(static_cast<size_t>(n), 0.0);
  std::vector<double> h_used_of(static_cast<size_t>(n), 0.0);

  double current_H = 0.0;
  std::vector<BlockCorner> corners;
  corners.reserve(static_cast<size_t>(2 * n + 8));

  for (int t = 0; t < n; ++t) {
    const int block_id = perm[static_cast<size_t>(t)];
    const Block &block = P.blocks[static_cast<size_t>(block_id)];

    if (t == 0) {
      bool has_best = false;
      int best_rotate = 0;
      double best_x = 0.0;
      double best_y = 0.0;
      double best_H = 0.0;
      double best_w = 0.0;
      double best_h = 0.0;

      for (int r = 0; r <= 1; ++r) {
        double w = 0.0;
        double h = 0.0;
        dims_for_rotate(block, r, w, h);
        if (w > static_cast<double>(P.chipW) + kEps) {
          continue;
        }

        const double cand_H = h;
        if (!has_best || better_first_block(cand_H, 0.0, 0.0, r, best_H, best_x, best_y,
                                            best_rotate)) {
          has_best = true;
          best_rotate = r;
          best_x = 0.0;
          best_y = 0.0;
          best_H = cand_H;
          best_w = w;
          best_h = h;
        }
      }

      if (!has_best) {
        throw std::runtime_error("init_planer: first block cannot be placed within chip width");
      }

      result.x[static_cast<size_t>(block_id)] = best_x;
      result.y[static_cast<size_t>(block_id)] = best_y;
      result.rotate[static_cast<size_t>(block_id)] = best_rotate;
      w_used_of[static_cast<size_t>(block_id)] = best_w;
      h_used_of[static_cast<size_t>(block_id)] = best_h;
      placed[static_cast<size_t>(block_id)] = true;
      current_H = best_H;

      result.items.push_back(
          FloorplanItem{block_id, best_x, best_y, best_rotate, best_w, best_h});

      corners.push_back(BlockCorner{best_x + best_w, best_y, CornerType::RIGHT_DOWN, block_id});
      corners.push_back(BlockCorner{best_x, best_y + best_h, CornerType::LEFT_UP, block_id});

      if (debug) {
        IterDebug iter;
        iter.iter = t;
        iter.block_id = block_id;
        iter.block_name = block.name;
        iter.H_before = 0.0;
        iter.H_after = current_H;

        LegalCandidateDebug chosen;
        chosen.x = best_x;
        chosen.y = best_y;
        chosen.rotate = best_rotate;
        chosen.is_pin_align = false;
        chosen.base_corner_index = -1;
        chosen.base_corner_type = CornerType::RIGHT_DOWN;
        chosen.src_pin_id = -1;
        chosen.ref_pin_id = -1;
        chosen.src_pin_name.clear();
        chosen.ref_pin_name.clear();
        chosen.delta_h = current_H;
        chosen.delta_hpwl = 0.0;
        chosen.score = chosen.delta_h;
        chosen.chosen = true;
        iter.legal_candidates.push_back(chosen);

        g_debug_store.iters.push_back(iter);
      }

      continue;
    }

    double best_score = 0.0;
    double best_delta_h = 0.0;
    double best_delta_hpwl = 0.0;
    CandidatePoint best_cand{};
    double best_w = 0.0;
    double best_h = 0.0;
    bool has_best = false;

    IterDebug iter_debug;
    if (debug) {
      iter_debug.iter = t;
      iter_debug.block_id = block_id;
      iter_debug.block_name = block.name;
      iter_debug.H_before = current_H;
    }

    std::vector<LegalCandidateDebug> legal_debug_entries;

    for (int r = 0; r <= 1; ++r) {
      double w = 0.0;
      double h = 0.0;
      dims_for_rotate(block, r, w, h);

      RotationDebug rot_debug;
      if (debug) {
        rot_debug.rotate = r;
        rot_debug.w = w;
        rot_debug.h = h;
      }

      std::vector<CandidatePoint> candidates;
      candidates.reserve(corners.size() * 4 + 4);

      for (int ci = 0; ci < static_cast<int>(corners.size()); ++ci) {
        const BlockCorner &corner = corners[static_cast<size_t>(ci)];

        CandidatePoint base;
        base.x = corner.x;
        base.y = corner.y;
        base.rotate = r;
        base.is_pin_align = false;
        base.base_corner_index = ci;
        base.base_corner_type = corner.type;
        base.src_pin_id = -1;
        base.ref_pin_id = -1;
        base.src_pin_name.clear();
        base.ref_pin_name.clear();
        candidates.push_back(base);
        if (debug) {
          rot_debug.block_corner_candidates.push_back(base);
        }

        for (int src_pid : block.pin_ids) {
          const Pin &src_pin = P.pins[static_cast<size_t>(src_pid)];
          double dx_rot = 0.0;
          double dy_rot = 0.0;
          rotated_offset(src_pin, r, dx_rot, dy_rot);

          for (int net_id : src_pin.net_ids) {
            const Net &net = P.nets[static_cast<size_t>(net_id)];
            for (int ref_pid : net.pin_ids) {
              if (ref_pid == src_pid) {
                continue;
              }
              const Pin &ref_pin = P.pins[static_cast<size_t>(ref_pid)];
              const int ref_block_id = ref_pin.block_id;
              if (ref_block_id < 0 || static_cast<size_t>(ref_block_id) >= placed.size() ||
                  !placed[static_cast<size_t>(ref_block_id)]) {
                continue;
              }

              double ref_x = 0.0;
              double ref_y = 0.0;
              pin_abs_from_block_pose(P.blocks[static_cast<size_t>(ref_block_id)], ref_pin,
                                      result.x[static_cast<size_t>(ref_block_id)],
                                      result.y[static_cast<size_t>(ref_block_id)],
                                      result.rotate[static_cast<size_t>(ref_block_id)], ref_x,
                                      ref_y);

              CandidatePoint aligned;
              aligned.rotate = r;
              aligned.is_pin_align = true;
              aligned.base_corner_index = ci;
              aligned.base_corner_type = corner.type;
              aligned.src_pin_id = src_pid;
              aligned.ref_pin_id = ref_pid;
              aligned.src_pin_name = src_pin.name;
              aligned.ref_pin_name = ref_pin.name;

              if (corner.type == CornerType::RIGHT_DOWN) {
                aligned.x = ref_x - w * 0.5 - dx_rot;
                aligned.y = corner.y;
              } else {
                aligned.x = corner.x;
                aligned.y = ref_y - h * 0.5 - dy_rot;
              }

              candidates.push_back(aligned);
              if (debug) {
                rot_debug.pin_align_candidates.push_back(aligned);
              }
            }
          }
        }
      }

      for (const CandidatePoint &cand : candidates) {
        if (!is_candidate_legal(P, placed, result.x, result.y, w_used_of, h_used_of, cand.x,
                                cand.y, w, h)) {
          continue;
        }

        const double H_after = std::max(current_H, cand.y + h);
        const double delta_h = H_after - current_H;
        const double delta_hpwl =
            compute_delta_hpwl(P, block_id, placed, result.x, result.y, result.rotate, cand.x,
                               cand.y, r);
        const double avg_delta_hpwl =
            P.nets.empty() ? 0.0 : (delta_hpwl / static_cast<double>(P.nets.size()));
        const double score = 2.0 * (1.0 - kAlpha) * delta_h + 2.0 * kAlpha * avg_delta_hpwl;

        if (!has_best || better_candidate(score, delta_h, delta_hpwl, cand.x, cand.y, r,
                                          best_score, best_delta_h, best_delta_hpwl, best_cand.x,
                                          best_cand.y, best_cand.rotate)) {
          has_best = true;
          best_score = score;
          best_delta_h = delta_h;
          best_delta_hpwl = delta_hpwl;
          best_cand = cand;
          best_w = w;
          best_h = h;
        }

        if (debug) {
          LegalCandidateDebug d;
          d.x = cand.x;
          d.y = cand.y;
          d.rotate = cand.rotate;
          d.is_pin_align = cand.is_pin_align;
          d.base_corner_index = cand.base_corner_index;
          d.base_corner_type = cand.base_corner_type;
          d.src_pin_id = cand.src_pin_id;
          d.ref_pin_id = cand.ref_pin_id;
          d.src_pin_name = cand.src_pin_name;
          d.ref_pin_name = cand.ref_pin_name;
          d.delta_h = delta_h;
          d.delta_hpwl = delta_hpwl;
          d.score = score;
          d.chosen = false;
          legal_debug_entries.push_back(d);
        }
      }

      if (debug) {
        iter_debug.rotations.push_back(rot_debug);
      }
    }

    if (!has_best) {
      throw std::runtime_error("init_planer: no legal candidate for block id=" +
                               std::to_string(block_id) + " name=" + block.name);
    }

    const double chosen_x = best_cand.x;
    const double chosen_y = best_cand.y;
    const int chosen_r = best_cand.rotate;

    result.x[static_cast<size_t>(block_id)] = chosen_x;
    result.y[static_cast<size_t>(block_id)] = chosen_y;
    result.rotate[static_cast<size_t>(block_id)] = chosen_r;
    w_used_of[static_cast<size_t>(block_id)] = best_w;
    h_used_of[static_cast<size_t>(block_id)] = best_h;
    placed[static_cast<size_t>(block_id)] = true;

    result.items.push_back(
        FloorplanItem{block_id, chosen_x, chosen_y, chosen_r, best_w, best_h});

    current_H = std::max(current_H, chosen_y + best_h);

    remove_corner_at(corners, best_cand.base_corner_index);

    corners.push_back(
        BlockCorner{chosen_x + best_w, chosen_y, CornerType::RIGHT_DOWN, block_id});
    corners.push_back(
        BlockCorner{chosen_x, chosen_y + best_h, CornerType::LEFT_UP, block_id});

    cleanup_corners_inside_blocks(corners, placed, result.x, result.y, w_used_of, h_used_of);

    if (debug) {
      for (LegalCandidateDebug &d : legal_debug_entries) {
        if (std::abs(d.x - best_cand.x) <= kEps && std::abs(d.y - best_cand.y) <= kEps &&
            d.rotate == best_cand.rotate && d.is_pin_align == best_cand.is_pin_align &&
            d.base_corner_index == best_cand.base_corner_index) {
          d.chosen = true;
          break;
        }
      }

      iter_debug.legal_candidates = std::move(legal_debug_entries);
      iter_debug.H_after = current_H;
      g_debug_store.iters.push_back(std::move(iter_debug));
    }
  }

  result.H = current_H;
  result.hpwl = compute_total_hpwl(P, result.x, result.y, result.rotate);
  if (!P.nets.empty()) {
    result.cost = result.H + result.hpwl / static_cast<double>(P.nets.size());
  } else {
    result.cost = result.H;
  }

  return result;
}
