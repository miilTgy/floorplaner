#include "bstar_tree2fp.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace {

constexpr double kEps = 1e-9;

struct Segment {
  double x_l = 0.0;
  double x_r = 0.0;
  double h = 0.0;
};

struct PlacementState {
  const Problem *problem = nullptr;
  const std::vector<int> *rotate = nullptr;
  std::vector<double> x;
  std::vector<double> y;
  std::vector<double> w_used;
  std::vector<double> h_used;
  std::vector<bool> placed;
  std::vector<Segment> contour;
};

enum class ChildRelation { kRoot = 0, kLeft = 1, kRight = 2 };

bool approx_eq(double a, double b) { return std::abs(a - b) <= kEps; }

bool overlap_1d_open(double l1, double r1, double l2, double r2) {
  return l1 < r2 - kEps && r1 > l2 + kEps;
}

double compute_layout_width(const FloorplanResult &fp) {
  double width = 0.0;
  for (const FloorplanItem &it : fp.items) {
    if (!std::isfinite(it.x) || !std::isfinite(it.w_used)) {
      throw std::runtime_error("bstar_tree2fp: non-finite x/w_used while computing layout width");
    }
    width = std::max(width, it.x + it.w_used);
  }
  return width;
}

void validate_layout_width(const Problem &P, const FloorplanResult &fp) {
  const double layout_width = compute_layout_width(fp);
  const double chip_w = static_cast<double>(P.chipW);
  if (layout_width > chip_w + kEps) {
    std::ostringstream oss;
    oss << "bstar_tree2fp: layout width exceeds chipW, layout_width=" << layout_width
        << ", chipW=" << chip_w;
    throw std::runtime_error(oss.str());
  }
}

std::pair<double, double> used_size(const Problem &P, int block_id, int rotate) {
  if (block_id < 0 || static_cast<size_t>(block_id) >= P.blocks.size()) {
    throw std::runtime_error("bstar_tree2fp: block_id out of range: " +
                             std::to_string(block_id));
  }
  if (rotate != 0 && rotate != 1) {
    throw std::runtime_error("bstar_tree2fp: rotate must be 0 or 1 for block_id=" +
                             std::to_string(block_id));
  }

  const Block &b = P.blocks[static_cast<size_t>(block_id)];
  if (rotate == 0) {
    return {static_cast<double>(b.w), static_cast<double>(b.h)};
  }
  return {static_cast<double>(b.h), static_cast<double>(b.w)};
}

void validate_rotate(const Problem &P, const std::vector<int> &rotate) {
  if (rotate.size() != P.blocks.size()) {
    throw std::runtime_error("bstar_tree2fp: rotate size mismatch with block count");
  }
  for (size_t i = 0; i < rotate.size(); ++i) {
    if (rotate[i] != 0 && rotate[i] != 1) {
      throw std::runtime_error("bstar_tree2fp: invalid rotate value at block_id=" +
                               std::to_string(static_cast<int>(i)));
    }
  }
}

void collect_tree_nodes_dfs(const BStarNode *node, int block_count,
                            std::vector<bool> &seen_blocks,
                            std::unordered_set<const BStarNode *> &on_stack,
                            std::vector<const BStarNode *> &preorder) {
  if (node == nullptr) {
    return;
  }

  if (!on_stack.insert(node).second) {
    throw std::runtime_error("bstar_tree2fp: cycle detected in B*-tree pointers");
  }

  const int block_id = node->block_id;
  if (block_id < 0 || block_id >= block_count) {
    throw std::runtime_error("bstar_tree2fp: node block_id out of range: " +
                             std::to_string(block_id));
  }
  if (seen_blocks[static_cast<size_t>(block_id)]) {
    throw std::runtime_error("bstar_tree2fp: duplicate block_id in B*-tree: " +
                             std::to_string(block_id));
  }
  seen_blocks[static_cast<size_t>(block_id)] = true;
  preorder.push_back(node);

  collect_tree_nodes_dfs(node->left, block_count, seen_blocks, on_stack, preorder);
  collect_tree_nodes_dfs(node->right, block_count, seen_blocks, on_stack, preorder);

  on_stack.erase(node);
}

void validate_tree_and_collect_preorder(const Problem &P, const BStarTree &tree,
                                        std::vector<const BStarNode *> &preorder) {
  if (tree.root == nullptr) {
    throw std::runtime_error("bstar_tree2fp: tree.root is null");
  }

  const int n = static_cast<int>(P.blocks.size());
  std::vector<bool> seen_blocks(static_cast<size_t>(n), false);
  std::unordered_set<const BStarNode *> on_stack;
  on_stack.reserve(static_cast<size_t>(n) * 2 + 1);
  preorder.clear();
  preorder.reserve(static_cast<size_t>(n));

  collect_tree_nodes_dfs(tree.root, n, seen_blocks, on_stack, preorder);

  if (static_cast<int>(preorder.size()) != n) {
    std::ostringstream oss;
    oss << "bstar_tree2fp: tree covers " << preorder.size() << " blocks, expected " << n
        << "; missing block_id=";
    bool first = true;
    for (int i = 0; i < n; ++i) {
      if (!seen_blocks[static_cast<size_t>(i)]) {
        if (!first) {
          oss << ",";
        }
        first = false;
        oss << i;
      }
    }
    throw std::runtime_error(oss.str());
  }
}

double query_contour_y(const std::vector<Segment> &contour, double x_l, double x_r) {
  if (!(x_r > x_l + kEps)) {
    throw std::runtime_error("bstar_tree2fp: invalid contour query interval");
  }
  double y = 0.0;
  for (const Segment &seg : contour) {
    if (overlap_1d_open(seg.x_l, seg.x_r, x_l, x_r)) {
      y = std::max(y, seg.h);
    }
  }
  return y;
}

void merge_contour(std::vector<Segment> &contour) {
  if (contour.empty()) {
    return;
  }
  std::sort(contour.begin(), contour.end(), [](const Segment &a, const Segment &b) {
    if (!approx_eq(a.x_l, b.x_l)) {
      return a.x_l < b.x_l;
    }
    if (!approx_eq(a.x_r, b.x_r)) {
      return a.x_r < b.x_r;
    }
    return a.h < b.h;
  });

  std::vector<Segment> merged;
  merged.reserve(contour.size());
  for (const Segment &seg : contour) {
    if (seg.x_r <= seg.x_l + kEps) {
      continue;
    }
    if (seg.h <= kEps) {
      continue;
    }
    if (!merged.empty()) {
      Segment &tail = merged.back();
      if (approx_eq(tail.x_r, seg.x_l) && approx_eq(tail.h, seg.h)) {
        tail.x_r = seg.x_r;
        continue;
      }
    }
    merged.push_back(seg);
  }
  contour.swap(merged);
}

void update_contour(std::vector<Segment> &contour, double x_l, double x_r, double new_top) {
  if (!(x_r > x_l + kEps)) {
    throw std::runtime_error("bstar_tree2fp: invalid contour update interval");
  }
  if (!(new_top >= -kEps)) {
    throw std::runtime_error("bstar_tree2fp: contour new_top must be non-negative");
  }

  std::vector<Segment> updated;
  updated.reserve(contour.size() + 3);

  for (const Segment &seg : contour) {
    if (!overlap_1d_open(seg.x_l, seg.x_r, x_l, x_r)) {
      updated.push_back(seg);
      continue;
    }
    if (seg.x_l < x_l - kEps) {
      updated.push_back(Segment{seg.x_l, x_l, seg.h});
    }
    if (seg.x_r > x_r + kEps) {
      updated.push_back(Segment{x_r, seg.x_r, seg.h});
    }
  }

  updated.push_back(Segment{x_l, x_r, new_top});
  merge_contour(updated);
  contour.swap(updated);
}

void validate_left_child_geometry(int parent_id, int child_id, double parent_x, double parent_y,
                                  double parent_w, double parent_h, double child_x, double child_y,
                                  double child_w, double child_h) {
  const double parent_right = parent_x + parent_w;
  const double parent_top = parent_y + parent_h;
  const double child_top = child_y + child_h;

  if (!approx_eq(child_x, parent_right)) {
    std::ostringstream oss;
    oss << "bstar_tree2fp: left child is not abutting parent right boundary, parent="
        << parent_id << ", child=" << child_id << ", child_x=" << child_x
        << ", expected_x=" << parent_right;
    throw std::runtime_error(oss.str());
  }

  const double overlap = std::min(parent_top, child_top) - std::max(parent_y, child_y);
  if (!(overlap > kEps)) {
    std::ostringstream oss;
    oss << "bstar_tree2fp: left child has no positive vertical contact with parent, parent="
        << parent_id << ", child=" << child_id << ", overlap=" << overlap;
    throw std::runtime_error(oss.str());
  }

  (void)child_w;
}

void validate_right_child_geometry(int parent_id, int child_id, double parent_x, double parent_y,
                                   double parent_w, double parent_h, double child_x, double child_y,
                                   double child_w, double child_h) {
  const double expected_y = parent_y + parent_h;
  if (!approx_eq(child_x, parent_x)) {
    std::ostringstream oss;
    oss << "bstar_tree2fp: right child is not aligned with parent left boundary, parent="
        << parent_id << ", child=" << child_id << ", child_x=" << child_x
        << ", expected_x=" << parent_x;
    throw std::runtime_error(oss.str());
  }
  if (!approx_eq(child_y, expected_y)) {
    std::ostringstream oss;
    oss << "bstar_tree2fp: right child is not abutting parent top boundary, parent="
        << parent_id << ", child=" << child_id << ", child_y=" << child_y
        << ", expected_y=" << expected_y;
    throw std::runtime_error(oss.str());
  }

  (void)parent_w;
  (void)child_w;
  (void)child_h;
}

void validate_parent_child_geometry(const PlacementState &state, int parent_id, int child_id,
                                    ChildRelation relation, double child_x, double child_y,
                                    double child_w, double child_h) {
  if (relation == ChildRelation::kRoot) {
    return;
  }
  if (parent_id < 0 || static_cast<size_t>(parent_id) >= state.x.size()) {
    throw std::runtime_error("bstar_tree2fp: invalid parent_id during geometry validation");
  }
  if (child_id < 0 || static_cast<size_t>(child_id) >= state.x.size()) {
    throw std::runtime_error("bstar_tree2fp: invalid child_id during geometry validation");
  }
  if (!state.placed[static_cast<size_t>(parent_id)]) {
    throw std::runtime_error("bstar_tree2fp: parent is not placed before child geometry validation");
  }

  const double parent_x = state.x[static_cast<size_t>(parent_id)];
  const double parent_y = state.y[static_cast<size_t>(parent_id)];
  const double parent_w = state.w_used[static_cast<size_t>(parent_id)];
  const double parent_h = state.h_used[static_cast<size_t>(parent_id)];

  if (relation == ChildRelation::kLeft) {
    validate_left_child_geometry(parent_id, child_id, parent_x, parent_y, parent_w, parent_h,
                                 child_x, child_y, child_w, child_h);
    return;
  }
  validate_right_child_geometry(parent_id, child_id, parent_x, parent_y, parent_w, parent_h,
                                child_x, child_y, child_w, child_h);
}

void place_subtree(const BStarNode *node, double x_target, PlacementState &state, int parent_id,
                  ChildRelation relation) {
  if (node == nullptr) {
    return;
  }

  const int block_id = node->block_id;
  if (state.placed[static_cast<size_t>(block_id)]) {
    throw std::runtime_error("bstar_tree2fp: block visited more than once during placement: " +
                             std::to_string(block_id));
  }

  const int r = (*state.rotate)[static_cast<size_t>(block_id)];
  const auto size = used_size(*state.problem, block_id, r);
  const double w = size.first;
  const double h = size.second;

  const double x = (std::abs(x_target) <= kEps) ? 0.0 : x_target;
  const double y = query_contour_y(state.contour, x, x + w);
  const double y_norm = (std::abs(y) <= kEps) ? 0.0 : y;
  const double top = y_norm + h;

  if (x < -kEps || y_norm < -kEps) {
    throw std::runtime_error("bstar_tree2fp: negative placement coordinate for block_id=" +
                             std::to_string(block_id));
  }

  state.x[static_cast<size_t>(block_id)] = x;
  state.y[static_cast<size_t>(block_id)] = y_norm;
  state.w_used[static_cast<size_t>(block_id)] = w;
  state.h_used[static_cast<size_t>(block_id)] = h;
  state.placed[static_cast<size_t>(block_id)] = true;

  validate_parent_child_geometry(state, parent_id, block_id, relation, x, y_norm, w, h);

  update_contour(state.contour, x, x + w, top);

  place_subtree(node->left, x + w, state, block_id, ChildRelation::kLeft);
  place_subtree(node->right, x, state, block_id, ChildRelation::kRight);
}

void validate_tree_geometry_edges_dfs(const Problem &P, const BStarNode *node,
                                      const FloorplanResult &fp) {
  if (node == nullptr) {
    return;
  }

  const int parent_id = node->block_id;
  const int parent_rotate = fp.rotate[static_cast<size_t>(parent_id)];
  const auto parent_size = used_size(P, parent_id, parent_rotate);
  const double parent_x = fp.x[static_cast<size_t>(parent_id)];
  const double parent_y = fp.y[static_cast<size_t>(parent_id)];
  const double parent_w = parent_size.first;
  const double parent_h = parent_size.second;

  if (node->left != nullptr) {
    const int child_id = node->left->block_id;
    const int child_rotate = fp.rotate[static_cast<size_t>(child_id)];
    const auto child_size = used_size(P, child_id, child_rotate);
    validate_left_child_geometry(parent_id, child_id, parent_x, parent_y, parent_w, parent_h,
                                 fp.x[static_cast<size_t>(child_id)],
                                 fp.y[static_cast<size_t>(child_id)], child_size.first,
                                 child_size.second);
  }
  if (node->right != nullptr) {
    const int child_id = node->right->block_id;
    const int child_rotate = fp.rotate[static_cast<size_t>(child_id)];
    const auto child_size = used_size(P, child_id, child_rotate);
    validate_right_child_geometry(parent_id, child_id, parent_x, parent_y, parent_w, parent_h,
                                  fp.x[static_cast<size_t>(child_id)],
                                  fp.y[static_cast<size_t>(child_id)], child_size.first,
                                  child_size.second);
  }

  validate_tree_geometry_edges_dfs(P, node->left, fp);
  validate_tree_geometry_edges_dfs(P, node->right, fp);
}

void rotate_pin_offset(const Pin &pin, int rotate, double &dx_rot, double &dy_rot) {
  if (rotate == 0) {
    dx_rot = pin.dx;
    dy_rot = pin.dy;
  } else if (rotate == 1) {
    dx_rot = -pin.dy;
    dy_rot = pin.dx;
  } else {
    throw std::runtime_error("bstar_tree2fp: invalid rotate when rotating pin offset");
  }
}

double compute_hpwl(const Problem &P, const FloorplanResult &fp) {
  double hpwl = 0.0;
  for (const Net &net : P.nets) {
    bool has_pin = false;
    double min_x = 0.0;
    double max_x = 0.0;
    double min_y = 0.0;
    double max_y = 0.0;

    for (int pin_id : net.pin_ids) {
      if (pin_id < 0 || static_cast<size_t>(pin_id) >= P.pins.size()) {
        throw std::runtime_error("bstar_tree2fp: pin_id out of range in net");
      }
      const Pin &pin = P.pins[static_cast<size_t>(pin_id)];
      const int block_id = pin.block_id;
      if (block_id < 0 || static_cast<size_t>(block_id) >= P.blocks.size()) {
        throw std::runtime_error("bstar_tree2fp: pin references invalid block_id");
      }

      const int r = fp.rotate[static_cast<size_t>(block_id)];
      const auto size = used_size(P, block_id, r);
      const double cx = fp.x[static_cast<size_t>(block_id)] + size.first * 0.5;
      const double cy = fp.y[static_cast<size_t>(block_id)] + size.second * 0.5;

      double dx_rot = 0.0;
      double dy_rot = 0.0;
      rotate_pin_offset(pin, r, dx_rot, dy_rot);

      const double px = cx + dx_rot;
      const double py = cy + dy_rot;
      if (!std::isfinite(px) || !std::isfinite(py)) {
        throw std::runtime_error("bstar_tree2fp: non-finite pin coordinate in HPWL");
      }

      if (!has_pin) {
        min_x = max_x = px;
        min_y = max_y = py;
        has_pin = true;
      } else {
        min_x = std::min(min_x, px);
        max_x = std::max(max_x, px);
        min_y = std::min(min_y, py);
        max_y = std::max(max_y, py);
      }
    }

    if (has_pin) {
      hpwl += (max_x - min_x) + (max_y - min_y);
    }
  }
  return hpwl;
}

void validate_no_overlap(const FloorplanResult &fp) {
  const size_t n = fp.items.size();
  for (size_t i = 0; i < n; ++i) {
    const FloorplanItem &a = fp.items[i];
    const double a_l = a.x;
    const double a_r = a.x + a.w_used;
    const double a_b = a.y;
    const double a_t = a.y + a.h_used;
    for (size_t j = i + 1; j < n; ++j) {
      const FloorplanItem &b = fp.items[j];
      const double b_l = b.x;
      const double b_r = b.x + b.w_used;
      const double b_b = b.y;
      const double b_t = b.y + b.h_used;
      const bool overlap = overlap_1d_open(a_l, a_r, b_l, b_r) &&
                           overlap_1d_open(a_b, a_t, b_b, b_t);
      if (overlap) {
        throw std::runtime_error("bstar_tree2fp: overlap detected between block_id=" +
                                 std::to_string(a.block_id) + " and block_id=" +
                                 std::to_string(b.block_id));
      }
    }
  }
}

void validate_floorplan_output(const Problem &P, const FloorplanResult &fp) {
  const size_t n = P.blocks.size();
  if (fp.x.size() != n || fp.y.size() != n || fp.rotate.size() != n) {
    throw std::runtime_error("bstar_tree2fp: x/y/rotate size mismatch");
  }
  if (fp.items.size() != n) {
    throw std::runtime_error("bstar_tree2fp: items size mismatch");
  }

  std::vector<bool> seen(n, false);
  double h_expect = 0.0;
  for (const FloorplanItem &it : fp.items) {
    const int id = it.block_id;
    if (id < 0 || static_cast<size_t>(id) >= n) {
      throw std::runtime_error("bstar_tree2fp: invalid block_id in items");
    }
    if (seen[static_cast<size_t>(id)]) {
      throw std::runtime_error("bstar_tree2fp: duplicate block_id in items");
    }
    seen[static_cast<size_t>(id)] = true;

    if (fp.rotate[static_cast<size_t>(id)] != it.rotate) {
      throw std::runtime_error("bstar_tree2fp: rotate inconsistency for block_id=" +
                               std::to_string(id));
    }
    if (!approx_eq(fp.x[static_cast<size_t>(id)], it.x) ||
        !approx_eq(fp.y[static_cast<size_t>(id)], it.y)) {
      throw std::runtime_error("bstar_tree2fp: coordinate inconsistency for block_id=" +
                               std::to_string(id));
    }
    if (!std::isfinite(it.x) || !std::isfinite(it.y) || !std::isfinite(it.w_used) ||
        !std::isfinite(it.h_used)) {
      throw std::runtime_error("bstar_tree2fp: non-finite item value for block_id=" +
                               std::to_string(id));
    }
    if (it.x < -kEps || it.y < -kEps) {
      throw std::runtime_error("bstar_tree2fp: negative coordinate for block_id=" +
                               std::to_string(id));
    }
    if (it.w_used <= kEps || it.h_used <= kEps) {
      throw std::runtime_error("bstar_tree2fp: non-positive used size for block_id=" +
                               std::to_string(id));
    }
    h_expect = std::max(h_expect, it.y + it.h_used);
  }

  for (size_t i = 0; i < n; ++i) {
    if (!seen[i]) {
      throw std::runtime_error("bstar_tree2fp: block missing from items: block_id=" +
                               std::to_string(static_cast<int>(i)));
    }
  }

  if (!approx_eq(fp.H, h_expect)) {
    throw std::runtime_error("bstar_tree2fp: H inconsistent with items");
  }
  validate_layout_width(P, fp);
  validate_no_overlap(fp);
}

}  // namespace

FloorplanResult bstar_tree_to_floorplan(const Problem &P, const BStarTree &tree,
                                        const std::vector<int> &rotate) {
  validate_rotate(P, rotate);

  std::vector<const BStarNode *> preorder;
  validate_tree_and_collect_preorder(P, tree, preorder);

  const size_t n = P.blocks.size();
  PlacementState state;
  state.problem = &P;
  state.rotate = &rotate;
  state.x.assign(n, 0.0);
  state.y.assign(n, 0.0);
  state.w_used.assign(n, 0.0);
  state.h_used.assign(n, 0.0);
  state.placed.assign(n, false);
  state.contour.clear();

  place_subtree(tree.root, 0.0, state, -1, ChildRelation::kRoot);

  for (size_t i = 0; i < n; ++i) {
    if (!state.placed[i]) {
      throw std::runtime_error("bstar_tree2fp: unplaced block_id=" +
                               std::to_string(static_cast<int>(i)));
    }
  }

  FloorplanResult fp;
  fp.x = state.x;
  fp.y = state.y;
  fp.rotate = rotate;
  fp.H = 0.0;
  fp.hpwl = 0.0;
  fp.cost = 0.0;
  fp.items.clear();
  fp.items.reserve(n);

  for (size_t id = 0; id < n; ++id) {
    fp.items.push_back(FloorplanItem{static_cast<int>(id), fp.x[id], fp.y[id], fp.rotate[id],
                                     state.w_used[id], state.h_used[id]});
    fp.H = std::max(fp.H, fp.y[id] + state.h_used[id]);
  }

  validate_tree_geometry_edges_dfs(P, tree.root, fp);
  validate_layout_width(P, fp);
  fp.hpwl = compute_hpwl(P, fp);
  if (P.nets.empty()) {
    fp.cost = fp.H;
  } else {
    fp.cost = fp.H + fp.hpwl / static_cast<double>(P.nets.size());
  }

  validate_floorplan_output(P, fp);
  return fp;
}

void dump_bstar_tree2fp_debug(const FloorplanResult &fp, const std::string &filename) {
  std::ofstream ofs(filename);
  if (!ofs.is_open()) {
    throw std::runtime_error("bstar_tree2fp: failed to open debug output file: " + filename);
  }

  std::vector<FloorplanItem> items = fp.items;
  std::sort(items.begin(), items.end(),
            [](const FloorplanItem &a, const FloorplanItem &b) { return a.block_id < b.block_id; });

  ofs << std::fixed << std::setprecision(6);
  for (const FloorplanItem &it : items) {
    ofs << it.block_id << " " << it.x << " " << it.y << " " << it.rotate << " " << it.w_used
        << " " << it.h_used << "\n";
  }
}
