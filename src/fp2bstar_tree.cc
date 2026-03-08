#include "fp2bstar_tree.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <set>

namespace {

constexpr double kEps = 1e-9;

struct BlockPose {
  int block_id = -1;
  double x = 0.0;
  double y = 0.0;
  double w = 0.0;
  double h = 0.0;
  int rotate = 0;
  int x_col = -1;
};

bool approx_eq(double a, double b) { return std::abs(a - b) <= kEps; }

long long quantize(double v) { return std::llround(v * 1e9); }

bool has_vertical_overlap_open(const BlockPose &a, const BlockPose &b) {
  const double a_low = a.y;
  const double a_high = a.y + a.h;
  const double b_low = b.y;
  const double b_high = b.y + b.h;
  return a_low < b_high - kEps && a_high > b_low + kEps;
}

bool is_above_adjacent_same_x(const BlockPose &a, const BlockPose &b) {
  return approx_eq(b.x, a.x) && approx_eq(b.y, a.y + a.h);
}

std::string block_missing_err(int block_id) {
  return "fp2bstar_tree: missing floorplan item for block id=" + std::to_string(block_id);
}

std::vector<BlockPose> build_pose_table(const Problem &P, const FloorplanResult &fp) {
  const int n = static_cast<int>(P.blocks.size());
  std::vector<BlockPose> poses(static_cast<size_t>(n));
  std::vector<bool> seen(static_cast<size_t>(n), false);

  if (static_cast<int>(fp.items.size()) != n) {
    throw std::runtime_error("fp2bstar_tree: fp.items size mismatch with block count");
  }

  for (const FloorplanItem &it : fp.items) {
    const int id = it.block_id;
    if (id < 0 || id >= n) {
      throw std::runtime_error("fp2bstar_tree: invalid block id in fp.items");
    }
    if (seen[static_cast<size_t>(id)]) {
      throw std::runtime_error("fp2bstar_tree: duplicate block id in fp.items");
    }
    seen[static_cast<size_t>(id)] = true;
    poses[static_cast<size_t>(id)] =
        BlockPose{id, it.x, it.y, it.w_used, it.h_used, it.rotate, -1};
  }

  for (int id = 0; id < n; ++id) {
    if (!seen[static_cast<size_t>(id)]) {
      throw std::runtime_error(block_missing_err(id));
    }
  }
  return poses;
}

int find_bottom_left_module(const std::vector<BlockPose> &poses) {
  int best = -1;
  for (const BlockPose &p : poses) {
    if (best < 0) {
      best = p.block_id;
      continue;
    }
    const BlockPose &b = poses[static_cast<size_t>(best)];
    if (p.y < b.y - kEps) {
      best = p.block_id;
    } else if (approx_eq(p.y, b.y)) {
      if (p.x < b.x - kEps) {
        best = p.block_id;
      } else if (approx_eq(p.x, b.x) && p.block_id < b.block_id) {
        best = p.block_id;
      }
    }
  }
  return best;
}

void assign_x_columns(std::vector<BlockPose> &poses, std::vector<double> &unique_xs) {
  std::vector<int> ids;
  ids.reserve(poses.size());
  for (const BlockPose &p : poses) {
    ids.push_back(p.block_id);
  }
  std::sort(ids.begin(), ids.end(), [&](int a, int b) {
    if (!approx_eq(poses[static_cast<size_t>(a)].x, poses[static_cast<size_t>(b)].x)) {
      return poses[static_cast<size_t>(a)].x < poses[static_cast<size_t>(b)].x;
    }
    if (!approx_eq(poses[static_cast<size_t>(a)].y, poses[static_cast<size_t>(b)].y)) {
      return poses[static_cast<size_t>(a)].y < poses[static_cast<size_t>(b)].y;
    }
    return a < b;
  });

  unique_xs.clear();
  for (int id : ids) {
    const double x = poses[static_cast<size_t>(id)].x;
    if (unique_xs.empty() || !approx_eq(unique_xs.back(), x)) {
      unique_xs.push_back(x);
    }
    poses[static_cast<size_t>(id)].x_col = static_cast<int>(unique_xs.size()) - 1;
  }
}

int find_x_column_for_value(const std::vector<double> &xs, double target) {
  const auto it = std::lower_bound(xs.begin(), xs.end(), target - kEps);
  if (it != xs.end() && approx_eq(*it, target)) {
    return static_cast<int>(it - xs.begin());
  }
  if (it != xs.begin()) {
    const auto it2 = std::prev(it);
    if (approx_eq(*it2, target)) {
      return static_cast<int>(it2 - xs.begin());
    }
  }
  return -1;
}

class ColumnIndex {
 public:
  ColumnIndex() = default;

  void build(const std::vector<int> &ids, const std::vector<BlockPose> &poses) {
    ids_ = ids;
    std::sort(ids_.begin(), ids_.end(), [&](int a, int b) {
      const BlockPose &pa = poses[static_cast<size_t>(a)];
      const BlockPose &pb = poses[static_cast<size_t>(b)];
      if (!approx_eq(pa.y, pb.y)) {
        return pa.y < pb.y;
      }
      if (!approx_eq(pa.x, pb.x)) {
        return pa.x < pb.x;
      }
      return a < b;
    });

    const int n = static_cast<int>(ids_.size());
    ys_.assign(static_cast<size_t>(n), 0.0);
    tops_.assign(static_cast<size_t>(n), 0.0);
    active_.assign(static_cast<size_t>(n), true);
    pos_of_id_.clear();
    ids_by_qy_.clear();

    for (int i = 0; i < n; ++i) {
      const int id = ids_[static_cast<size_t>(i)];
      const BlockPose &p = poses[static_cast<size_t>(id)];
      ys_[static_cast<size_t>(i)] = p.y;
      tops_[static_cast<size_t>(i)] = p.y + p.h;
      pos_of_id_[id] = i;
      ids_by_qy_[quantize(p.y)].insert(id);
    }

    seg_max_.assign(static_cast<size_t>(4 * std::max(1, n)),
                    -std::numeric_limits<double>::infinity());
    if (n > 0) {
      build_seg(1, 0, n - 1);
    }
  }

  void remove_id(int id) {
    const auto it = pos_of_id_.find(id);
    if (it == pos_of_id_.end()) {
      return;
    }
    const int pos = it->second;
    if (!active_[static_cast<size_t>(pos)]) {
      return;
    }
    active_[static_cast<size_t>(pos)] = false;
    if (!ids_.empty()) {
      update_seg(1, 0, static_cast<int>(ids_.size()) - 1, pos,
                 -std::numeric_limits<double>::infinity());
    }

    const long long qy = quantize(ys_[static_cast<size_t>(pos)]);
    auto map_it = ids_by_qy_.find(qy);
    if (map_it != ids_by_qy_.end()) {
      map_it->second.erase(id);
      if (map_it->second.empty()) {
        ids_by_qy_.erase(map_it);
      }
    }
  }

  int query_left_child(const std::vector<BlockPose> &poses, const BlockPose &parent) const {
    if (ids_.empty()) {
      return -1;
    }

    const double y_upper = parent.y + parent.h;
    const int right =
        static_cast<int>(std::lower_bound(ys_.begin(), ys_.end(), y_upper - kEps) - ys_.begin()) -
        1;
    if (right < 0) {
      return -1;
    }

    const int idx = find_first_overlap_idx(1, 0, static_cast<int>(ids_.size()) - 1, 0, right,
                                           parent.y);
    if (idx < 0) {
      return -1;
    }

    // The segment query guarantees top > parent.y and y < parent.y + parent.h.
    const int candidate_id = ids_[static_cast<size_t>(idx)];
    const BlockPose &cand = poses[static_cast<size_t>(candidate_id)];
    if (!has_vertical_overlap_open(parent, cand)) {
      return -1;
    }
    return candidate_id;
  }

  int query_right_child(double y_target) const {
    const auto it = ids_by_qy_.find(quantize(y_target));
    if (it == ids_by_qy_.end() || it->second.empty()) {
      return -1;
    }
    return *it->second.begin();
  }

  void collect_active_ids_near_y(double y_target, std::vector<int> &out) const {
    out.clear();
    if (ids_.empty()) {
      return;
    }
    const auto begin =
        std::lower_bound(ys_.begin(), ys_.end(), y_target - kEps);
    const auto end =
        std::upper_bound(ys_.begin(), ys_.end(), y_target + kEps);
    const int l = static_cast<int>(begin - ys_.begin());
    const int r = static_cast<int>(end - ys_.begin());
    for (int i = l; i < r; ++i) {
      if (active_[static_cast<size_t>(i)]) {
        out.push_back(ids_[static_cast<size_t>(i)]);
      }
    }
  }

 private:
  std::vector<int> ids_;
  std::vector<double> ys_;
  std::vector<double> tops_;
  std::vector<double> seg_max_;
  std::vector<bool> active_;
  std::unordered_map<int, int> pos_of_id_;
  std::unordered_map<long long, std::set<int>> ids_by_qy_;

  void build_seg(int idx, int l, int r) {
    if (l == r) {
      seg_max_[static_cast<size_t>(idx)] = tops_[static_cast<size_t>(l)];
      return;
    }
    const int mid = l + (r - l) / 2;
    build_seg(idx * 2, l, mid);
    build_seg(idx * 2 + 1, mid + 1, r);
    seg_max_[static_cast<size_t>(idx)] = std::max(seg_max_[static_cast<size_t>(idx * 2)],
                                                  seg_max_[static_cast<size_t>(idx * 2 + 1)]);
  }

  void update_seg(int idx, int l, int r, int pos, double val) {
    if (l == r) {
      seg_max_[static_cast<size_t>(idx)] = val;
      return;
    }
    const int mid = l + (r - l) / 2;
    if (pos <= mid) {
      update_seg(idx * 2, l, mid, pos, val);
    } else {
      update_seg(idx * 2 + 1, mid + 1, r, pos, val);
    }
    seg_max_[static_cast<size_t>(idx)] = std::max(seg_max_[static_cast<size_t>(idx * 2)],
                                                  seg_max_[static_cast<size_t>(idx * 2 + 1)]);
  }

  int find_first_overlap_idx(int idx, int l, int r, int ql, int qr, double y_low) const {
    if (qr < l || r < ql) {
      return -1;
    }
    if (seg_max_[static_cast<size_t>(idx)] <= y_low + kEps) {
      return -1;
    }
    if (l == r) {
      if (!active_[static_cast<size_t>(l)]) {
        return -1;
      }
      return l;
    }

    const int mid = l + (r - l) / 2;
    const int left = find_first_overlap_idx(idx * 2, l, mid, ql, qr, y_low);
    if (left >= 0) {
      return left;
    }
    return find_first_overlap_idx(idx * 2 + 1, mid + 1, r, ql, qr, y_low);
  }
};

struct NodeLookupTrace {
  int node_id = -1;
  int left_id = -1;
  int right_id = -1;
  int left_lookup_step = 0;
  int right_lookup_step = 0;
  std::vector<int> left_adjacent_ids;
  bool has_right_upper_bound = false;
  double right_upper_bound = std::numeric_limits<double>::infinity();
};

std::unordered_map<long long, std::vector<int>> build_right_edge_buckets(
    const std::vector<BlockPose> &poses) {
  std::unordered_map<long long, std::vector<int>> buckets;
  buckets.reserve(poses.size() * 2 + 1);
  for (const BlockPose &p : poses) {
    buckets[quantize(p.x + p.w)].push_back(p.block_id);
  }
  for (auto &kv : buckets) {
    auto &ids = kv.second;
    std::sort(ids.begin(), ids.end(), [&](int a, int b) {
      const BlockPose &pa = poses[static_cast<size_t>(a)];
      const BlockPose &pb = poses[static_cast<size_t>(b)];
      if (!approx_eq(pa.y, pb.y)) {
        return pa.y < pb.y;
      }
      if (!approx_eq(pa.x, pb.x)) {
        return pa.x < pb.x;
      }
      return a < b;
    });
  }
  return buckets;
}

std::vector<int> find_left_adjacent_modules(
    const BlockPose &a, const std::vector<BlockPose> &poses,
    const std::unordered_map<long long, std::vector<int>> &right_edge_buckets) {
  std::vector<int> out;
  out.reserve(4);

  const long long qx = quantize(a.x);
  std::unordered_set<int> dedup;
  dedup.reserve(8);

  for (long long q = qx - 1; q <= qx + 1; ++q) {
    const auto it = right_edge_buckets.find(q);
    if (it == right_edge_buckets.end()) {
      continue;
    }
    for (int id : it->second) {
      if (id == a.block_id) {
        continue;
      }
      if (!dedup.insert(id).second) {
        continue;
      }
      const BlockPose &l = poses[static_cast<size_t>(id)];
      if (!approx_eq(l.x + l.w, a.x)) {
        continue;
      }
      if (!has_vertical_overlap_open(a, l)) {
        continue;
      }
      out.push_back(id);
    }
  }

  std::sort(out.begin(), out.end(), [&](int id1, int id2) {
    const BlockPose &p1 = poses[static_cast<size_t>(id1)];
    const BlockPose &p2 = poses[static_cast<size_t>(id2)];
    if (!approx_eq(p1.y, p2.y)) {
      return p1.y < p2.y;
    }
    if (!approx_eq(p1.x, p2.x)) {
      return p1.x < p2.x;
    }
    return id1 < id2;
  });
  return out;
}

std::string join_ids(const std::vector<int> &ids) {
  if (ids.empty()) {
    return "[]";
  }
  std::ostringstream oss;
  oss << "[";
  for (size_t i = 0; i < ids.size(); ++i) {
    if (i > 0) {
      oss << ",";
    }
    oss << ids[i];
  }
  oss << "]";
  return oss.str();
}

std::string build_failure_diagnostics(
    int root_id, const std::vector<BlockPose> &poses,
    const std::vector<bool> &visited, const std::vector<int> &visit_step,
    const std::vector<NodeLookupTrace> &traces) {
  std::ostringstream oss;
  oss << "fp2bstar_tree: tree construction does not cover all blocks\n";
  oss << "root_id=" << root_id << "\n";
  oss << "lookup_diagnostics:\n";

  for (const NodeLookupTrace &tr : traces) {
    const BlockPose &a = poses[static_cast<size_t>(tr.node_id)];

    int left_not_right_adjacent = 0;
    int left_x_not_equal = 0;
    int left_no_contact = 0;
    int left_visited = 0;
    int left_eligible = 0;

    int right_not_above_adjacent = 0;
    int right_x_not_equal = 0;
    int right_y_not_equal = 0;
    int right_upper_bound_fail = 0;
    int right_visited = 0;
    int right_eligible = 0;

    for (const BlockPose &b : poses) {
      if (b.block_id == tr.node_id) {
        continue;
      }

      const bool visited_for_left =
          visit_step[static_cast<size_t>(b.block_id)] > 0 &&
          visit_step[static_cast<size_t>(b.block_id)] <= tr.left_lookup_step;
      if (visited_for_left) {
        ++left_visited;
      } else if (!approx_eq(b.x, a.x + a.w)) {
        ++left_not_right_adjacent;
        ++left_x_not_equal;
      } else if (!has_vertical_overlap_open(a, b)) {
        ++left_not_right_adjacent;
        ++left_no_contact;
      } else {
        ++left_eligible;
      }

      const bool visited_for_right =
          visit_step[static_cast<size_t>(b.block_id)] > 0 &&
          visit_step[static_cast<size_t>(b.block_id)] <= tr.right_lookup_step;
      if (visited_for_right) {
        ++right_visited;
        continue;
      }

      const bool x_eq = approx_eq(b.x, a.x);
      const bool y_eq = approx_eq(b.y, a.y + a.h);
      if (!x_eq) {
        ++right_not_above_adjacent;
        ++right_x_not_equal;
        continue;
      }
      if (!y_eq) {
        ++right_not_above_adjacent;
        ++right_y_not_equal;
        continue;
      }
      if (tr.has_right_upper_bound && !(b.y < tr.right_upper_bound - kEps)) {
        ++right_upper_bound_fail;
        continue;
      }
      ++right_eligible;
    }

    oss << "  node " << tr.node_id << ": left_id=" << tr.left_id
        << ", right_id=" << tr.right_id << "\n";
    oss << "    left_lookup: visited=" << left_visited
        << ", not_right_adjacent=" << left_not_right_adjacent
        << ", x_not_equal=" << left_x_not_equal
        << ", no_vertical_contact=" << left_no_contact
        << ", eligible=" << left_eligible << "\n";
    oss << "    right_lookup: visited=" << right_visited
        << ", not_above_adjacent=" << right_not_above_adjacent
        << ", x_not_equal=" << right_x_not_equal
        << ", y_not_equal=" << right_y_not_equal
        << ", upper_bound_fail=" << right_upper_bound_fail
        << ", eligible=" << right_eligible << "\n";
    oss << "    left_adjacent_modules=" << join_ids(tr.left_adjacent_ids);
    if (tr.has_right_upper_bound) {
      oss << ", right_upper_bound=" << tr.right_upper_bound;
    } else {
      oss << ", right_upper_bound=INF";
    }
    oss << "\n";
  }

  std::vector<int> unvisited_ids;
  for (size_t i = 0; i < visited.size(); ++i) {
    if (!visited[i]) {
      unvisited_ids.push_back(static_cast<int>(i));
    }
  }
  oss << "unvisited_blocks=" << join_ids(unvisited_ids);
  return oss.str();
}

void dump_dfs(const BStarNode *node, const std::vector<BlockPose> &poses,
              std::vector<bool> &seen, std::ostream &os) {
  if (node == nullptr) {
    return;
  }
  const int id = node->block_id;
  if (id < 0 || static_cast<size_t>(id) >= seen.size()) {
    throw std::runtime_error("fp2bstar_tree: invalid node id during dump");
  }
  if (seen[static_cast<size_t>(id)]) {
    throw std::runtime_error("fp2bstar_tree: cycle or duplicate node detected during dump");
  }
  seen[static_cast<size_t>(id)] = true;

  const int left_id = node->left ? node->left->block_id : -1;
  const int right_id = node->right ? node->right->block_id : -1;
  const BlockPose &p = poses[static_cast<size_t>(id)];
  os << id << " " << left_id << " " << right_id << " " << p.x << " " << p.y << " " << p.w
     << " " << p.h << " " << p.rotate << "\n";
  dump_dfs(node->left, poses, seen, os);
  dump_dfs(node->right, poses, seen, os);
}

}  // namespace

BStarTree floorplan_to_bstar_tree(const Problem &P, const FloorplanResult &fp) {
  const int n = static_cast<int>(P.blocks.size());
  BStarTree tree;
  tree.nodes.resize(static_cast<size_t>(n));
  if (n == 0) {
    tree.root = nullptr;
    return tree;
  }

  std::vector<BlockPose> poses = build_pose_table(P, fp);
  const auto right_edge_buckets = build_right_edge_buckets(poses);

  std::vector<double> unique_xs;
  assign_x_columns(poses, unique_xs);
  const int m = static_cast<int>(unique_xs.size());

  std::vector<std::vector<int>> ids_per_col(static_cast<size_t>(m));
  for (const BlockPose &p : poses) {
    ids_per_col[static_cast<size_t>(p.x_col)].push_back(p.block_id);
  }

  std::vector<ColumnIndex> cols(static_cast<size_t>(m));
  for (int c = 0; c < m; ++c) {
    cols[static_cast<size_t>(c)].build(ids_per_col[static_cast<size_t>(c)], poses);
  }

  for (int i = 0; i < n; ++i) {
    tree.nodes[static_cast<size_t>(i)].block_id = i;
    tree.nodes[static_cast<size_t>(i)].left = nullptr;
    tree.nodes[static_cast<size_t>(i)].right = nullptr;
  }

  std::vector<bool> visited(static_cast<size_t>(n), false);
  std::vector<int> visit_step(static_cast<size_t>(n), 0);
  int visit_clock = 0;
  std::vector<NodeLookupTrace> traces;
  traces.reserve(static_cast<size_t>(n));

  std::function<BStarNode *(int)> build_subtree = [&](int node_id) -> BStarNode * {
    if (node_id < 0) {
      return nullptr;
    }
    if (visited[static_cast<size_t>(node_id)]) {
      return nullptr;
    }
    visited[static_cast<size_t>(node_id)] = true;
    ++visit_clock;
    visit_step[static_cast<size_t>(node_id)] = visit_clock;

    const BlockPose &cur = poses[static_cast<size_t>(node_id)];
    cols[static_cast<size_t>(cur.x_col)].remove_id(node_id);

    BStarNode *node = &tree.nodes[static_cast<size_t>(node_id)];
    node->left = nullptr;
    node->right = nullptr;

    NodeLookupTrace trace;
    trace.node_id = node_id;
    trace.left_lookup_step = visit_clock;

    const double target_x_left = cur.x + cur.w;
    const int left_col = find_x_column_for_value(unique_xs, target_x_left);
    if (left_col >= 0) {
      const int left_id =
          cols[static_cast<size_t>(left_col)].query_left_child(poses, cur);
      if (left_id >= 0) {
        trace.left_id = left_id;
        node->left = build_subtree(left_id);
      }
    }

    trace.right_lookup_step = visit_clock;
    trace.left_adjacent_ids =
        find_left_adjacent_modules(cur, poses, right_edge_buckets);
    if (!trace.left_adjacent_ids.empty()) {
      trace.has_right_upper_bound = true;
      trace.right_upper_bound = std::numeric_limits<double>::infinity();
      for (int lid : trace.left_adjacent_ids) {
        const BlockPose &lp = poses[static_cast<size_t>(lid)];
        trace.right_upper_bound = std::min(trace.right_upper_bound, lp.y + lp.h);
      }
    }

    const double target_y_right = cur.y + cur.h;
    std::vector<int> right_candidates;
    cols[static_cast<size_t>(cur.x_col)].collect_active_ids_near_y(
        target_y_right, right_candidates);

    int right_id = -1;
    for (int cand_id : right_candidates) {
      const BlockPose &cand = poses[static_cast<size_t>(cand_id)];
      if (!is_above_adjacent_same_x(cur, cand)) {
        continue;
      }
      if (trace.has_right_upper_bound &&
          !(cand.y < trace.right_upper_bound - kEps)) {
        continue;
      }
      right_id = cand_id;
      break;
    }

    if (right_id >= 0) {
      trace.right_id = right_id;
      node->right = build_subtree(right_id);
    }

    traces.push_back(trace);
    return node;
  };

  const int root_id = find_bottom_left_module(poses);
  if (root_id < 0) {
    throw std::runtime_error("fp2bstar_tree: cannot determine root");
  }
  tree.root = build_subtree(root_id);

  for (int i = 0; i < n; ++i) {
    if (!visited[static_cast<size_t>(i)]) {
      throw std::runtime_error(
          build_failure_diagnostics(root_id, poses, visited, visit_step, traces));
    }
  }
  return tree;
}

void dump_bstar_tree_debug(const BStarTree &tree, const FloorplanResult &fp,
                           const std::string &filename) {
  std::ofstream ofs(filename);
  if (!ofs) {
    throw std::runtime_error("fp2bstar_tree: failed to open debug output file: " + filename);
  }
  ofs << std::fixed << std::setprecision(2);

  if (tree.root == nullptr) {
    return;
  }

  if (tree.nodes.empty()) {
    throw std::runtime_error("fp2bstar_tree: root exists but nodes array is empty");
  }

  std::vector<BlockPose> poses;
  poses.resize(tree.nodes.size());
  std::vector<bool> seen(static_cast<size_t>(tree.nodes.size()), false);

  for (const FloorplanItem &it : fp.items) {
    const int id = it.block_id;
    if (id < 0 || static_cast<size_t>(id) >= poses.size()) {
      throw std::runtime_error("fp2bstar_tree: invalid block id in floorplan dump");
    }
    if (seen[static_cast<size_t>(id)]) {
      throw std::runtime_error("fp2bstar_tree: duplicate block id in floorplan dump");
    }
    seen[static_cast<size_t>(id)] = true;
    poses[static_cast<size_t>(id)] =
        BlockPose{id, it.x, it.y, it.w_used, it.h_used, it.rotate, -1};
  }

  for (size_t i = 0; i < seen.size(); ++i) {
    if (!seen[i]) {
      throw std::runtime_error("fp2bstar_tree: missing block in floorplan dump: block_id=" +
                               std::to_string(static_cast<int>(i)));
    }
  }

  std::fill(seen.begin(), seen.end(), false);
  dump_dfs(tree.root, poses, seen, ofs);
}
