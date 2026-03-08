#include "init_planer.h"

#include "bstar_tree.h"
#include "bstar_tree2fp.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace {

constexpr double kEps = 1e-9;
constexpr const char *kTreeDumpFilename = "init_fp_bstar_tree.txt";

struct CandidateEval {
  FloorplanResult fp;
  double cost = std::numeric_limits<double>::infinity();
  double layout_width = 0.0;
};

struct CandidateChoice {
  BStarTree tree;
  std::vector<int> rotate;
  FloorplanResult fp;
  double cost = std::numeric_limits<double>::infinity();
  int parent_block_id = -1;
  int parent_order = -1;
  bool as_left = true;
  int new_rotate = 0;
  bool valid = false;
};

struct RemapContext {
  Problem sub_problem;
  BStarTree sub_tree;
  std::vector<int> sub_rotate;
  std::vector<int> local_to_global;
  std::unordered_map<int, int> global_to_local;
  size_t placed_net_count = 0;
};

std::vector<std::string> g_debug_lines;

bool approx_eq(double a, double b) { return std::abs(a - b) <= kEps; }

void debug_log(const std::string &line) { g_debug_lines.push_back(line); }

std::string join_ints(const std::vector<int> &vals) {
  if (vals.empty()) {
    return "[]";
  }
  std::ostringstream oss;
  oss << "[";
  for (size_t i = 0; i < vals.size(); ++i) {
    if (i > 0) {
      oss << " ";
    }
    oss << vals[i];
  }
  oss << "]";
  return oss.str();
}

void validate_perm(const Problem &P, const std::vector<int> &perm) {
  const int n = static_cast<int>(P.blocks.size());
  if (static_cast<int>(perm.size()) != n) {
    throw std::runtime_error("init_fp_bstar: perm size mismatch with block count");
  }

  std::vector<bool> seen(static_cast<size_t>(n), false);
  for (size_t i = 0; i < perm.size(); ++i) {
    const int id = perm[i];
    if (id < 0 || id >= n) {
      throw std::runtime_error("init_fp_bstar: perm contains out-of-range block id at index " +
                               std::to_string(i));
    }
    if (seen[static_cast<size_t>(id)]) {
      throw std::runtime_error("init_fp_bstar: perm contains duplicate block id=" +
                               std::to_string(id));
    }
    seen[static_cast<size_t>(id)] = true;
  }
}

double eval_floorplan_cost(FloorplanResult &fp, size_t net_count) {
  if (!std::isfinite(fp.H) || !std::isfinite(fp.hpwl) || fp.H < -kEps || fp.hpwl < -kEps) {
    throw std::runtime_error("init_fp_bstar: invalid H/hpwl in candidate floorplan");
  }

  double cost = fp.H;
  if (net_count > 0) {
    cost += fp.hpwl / static_cast<double>(net_count);
  }
  if (!std::isfinite(cost)) {
    throw std::runtime_error("init_fp_bstar: non-finite candidate cost");
  }
  fp.cost = cost;
  return cost;
}

double compute_layout_width(const FloorplanResult &fp) {
  if (fp.items.empty()) {
    return 0.0;
  }
  double wmax = 0.0;
  for (const FloorplanItem &item : fp.items) {
    if (!std::isfinite(item.x) || !std::isfinite(item.w_used)) {
      throw std::runtime_error("init_fp_bstar: non-finite x/w_used in floorplan item");
    }
    wmax = std::max(wmax, item.x + item.w_used);
  }
  return wmax;
}

void dfs_collect_ptrs(const BStarNode *node,
                      std::unordered_set<const BStarNode *> &seen_ptrs,
                      std::vector<const BStarNode *> &out) {
  if (node == nullptr) {
    return;
  }
  if (!seen_ptrs.insert(node).second) {
    throw std::runtime_error("init_fp_bstar: cycle detected in B*-tree pointers");
  }
  out.push_back(node);
  dfs_collect_ptrs(node->left, seen_ptrs, out);
  dfs_collect_ptrs(node->right, seen_ptrs, out);
}

std::vector<const BStarNode *> collect_nodes_preorder_checked(const BStarTree &tree) {
  if (tree.root == nullptr) {
    return {};
  }

  std::unordered_set<const BStarNode *> seen_ptrs;
  seen_ptrs.reserve(tree.nodes.size() * 2 + 1);
  std::vector<const BStarNode *> nodes;
  nodes.reserve(tree.nodes.size());
  dfs_collect_ptrs(tree.root, seen_ptrs, nodes);

  if (nodes.size() != tree.nodes.size()) {
    throw std::runtime_error("init_fp_bstar: tree has disconnected nodes");
  }

  std::unordered_set<int> seen_block_ids;
  seen_block_ids.reserve(nodes.size() * 2 + 1);
  for (const BStarNode *node : nodes) {
    if (!seen_block_ids.insert(node->block_id).second) {
      throw std::runtime_error("init_fp_bstar: duplicate block_id in tree");
    }
  }
  return nodes;
}

BStarTree clone_tree(const BStarTree &src, size_t reserve_capacity = 0) {
  BStarTree dst;
  if (src.root == nullptr) {
    if (!src.nodes.empty()) {
      throw std::runtime_error("init_fp_bstar: invalid source tree (null root with nodes)");
    }
    return dst;
  }

  dst.nodes.resize(src.nodes.size());
  if (reserve_capacity > dst.nodes.capacity()) {
    dst.nodes.reserve(reserve_capacity);
  }

  std::unordered_map<const BStarNode *, size_t> index_of;
  index_of.reserve(src.nodes.size() * 2 + 1);
  for (size_t i = 0; i < src.nodes.size(); ++i) {
    index_of[&src.nodes[i]] = i;
    dst.nodes[i].block_id = src.nodes[i].block_id;
    dst.nodes[i].left = nullptr;
    dst.nodes[i].right = nullptr;
  }

  const auto lookup_index = [&](const BStarNode *ptr) -> size_t {
    const auto it = index_of.find(ptr);
    if (it == index_of.end()) {
      throw std::runtime_error("init_fp_bstar: tree pointer not owned by nodes vector");
    }
    return it->second;
  };

  for (size_t i = 0; i < src.nodes.size(); ++i) {
    if (src.nodes[i].left != nullptr) {
      const size_t li = lookup_index(src.nodes[i].left);
      dst.nodes[i].left = &dst.nodes[li];
    }
    if (src.nodes[i].right != nullptr) {
      const size_t ri = lookup_index(src.nodes[i].right);
      dst.nodes[i].right = &dst.nodes[ri];
    }
  }

  dst.root = &dst.nodes[lookup_index(src.root)];
  return dst;
}

BStarTree make_single_node_tree(int block_id) {
  BStarTree tree;
  tree.nodes.resize(1);
  tree.nodes[0].block_id = block_id;
  tree.nodes[0].left = nullptr;
  tree.nodes[0].right = nullptr;
  tree.root = &tree.nodes[0];
  return tree;
}

BStarNode *find_node_by_block_id(BStarTree &tree, int block_id) {
  for (BStarNode &node : tree.nodes) {
    if (node.block_id == block_id) {
      return &node;
    }
  }
  return nullptr;
}

RemapContext build_subproblem(const Problem &P, const BStarTree &tree,
                              const std::vector<int> &rotate_global) {
  RemapContext ctx;
  const auto preorder_nodes = collect_nodes_preorder_checked(tree);

  ctx.local_to_global.reserve(preorder_nodes.size());
  for (const BStarNode *node : preorder_nodes) {
    ctx.local_to_global.push_back(node->block_id);
  }
  std::sort(ctx.local_to_global.begin(), ctx.local_to_global.end());

  const int sub_n = static_cast<int>(ctx.local_to_global.size());
  ctx.global_to_local.reserve(static_cast<size_t>(sub_n) * 2 + 1);
  for (int i = 0; i < sub_n; ++i) {
    const int global_id = ctx.local_to_global[static_cast<size_t>(i)];
    if (global_id < 0 || static_cast<size_t>(global_id) >= P.blocks.size()) {
      throw std::runtime_error("init_fp_bstar: tree contains out-of-range block id");
    }
    ctx.global_to_local[global_id] = i;
  }

  ctx.sub_tree = clone_tree(tree);
  for (BStarNode &node : ctx.sub_tree.nodes) {
    const auto it = ctx.global_to_local.find(node.block_id);
    if (it == ctx.global_to_local.end()) {
      throw std::runtime_error("init_fp_bstar: failed to remap tree block id");
    }
    node.block_id = it->second;
  }

  ctx.sub_problem.chipW = P.chipW;
  ctx.sub_problem.blocks.resize(static_cast<size_t>(sub_n));

  for (int local_id = 0; local_id < sub_n; ++local_id) {
    const int global_id = ctx.local_to_global[static_cast<size_t>(local_id)];
    const Block &gb = P.blocks[static_cast<size_t>(global_id)];
    Block sb;
    sb.name = gb.name;
    sb.w = gb.w;
    sb.h = gb.h;
    ctx.sub_problem.blocks[static_cast<size_t>(local_id)] = sb;
    ctx.sub_problem.block_id_of[sb.name] = local_id;
  }

  std::vector<int> global_pin_to_local(P.pins.size(), -1);
  for (int local_id = 0; local_id < sub_n; ++local_id) {
    const int global_id = ctx.local_to_global[static_cast<size_t>(local_id)];
    for (int global_pin_id : P.blocks[static_cast<size_t>(global_id)].pin_ids) {
      if (global_pin_id < 0 || static_cast<size_t>(global_pin_id) >= P.pins.size()) {
        throw std::runtime_error("init_fp_bstar: invalid pin id in source problem");
      }
      const Pin &gp = P.pins[static_cast<size_t>(global_pin_id)];
      Pin sp;
      sp.name = gp.name;
      sp.block_id = local_id;
      sp.dx = gp.dx;
      sp.dy = gp.dy;
      const int local_pin_id = static_cast<int>(ctx.sub_problem.pins.size());
      ctx.sub_problem.pins.push_back(sp);
      ctx.sub_problem.pin_id_of[sp.name] = local_pin_id;
      ctx.sub_problem.blocks[static_cast<size_t>(local_id)].pin_ids.push_back(local_pin_id);
      global_pin_to_local[static_cast<size_t>(global_pin_id)] = local_pin_id;
    }
  }

  ctx.sub_problem.nets.reserve(P.nets.size());
  ctx.placed_net_count = 0;
  for (size_t net_id = 0; net_id < P.nets.size(); ++net_id) {
    const Net &gn = P.nets[net_id];
    Net sn;
    sn.name = gn.name;
    std::unordered_set<int> block_seen;
    block_seen.reserve(gn.block_ids.size() * 2 + 1);

    for (int global_pin_id : gn.pin_ids) {
      if (global_pin_id < 0 || static_cast<size_t>(global_pin_id) >= global_pin_to_local.size()) {
        throw std::runtime_error("init_fp_bstar: invalid net pin reference in source problem");
      }
      const int local_pin_id = global_pin_to_local[static_cast<size_t>(global_pin_id)];
      if (local_pin_id < 0) {
        continue;
      }
      sn.pin_ids.push_back(local_pin_id);
      const int local_block_id = ctx.sub_problem.pins[static_cast<size_t>(local_pin_id)].block_id;
      if (block_seen.insert(local_block_id).second) {
        sn.block_ids.push_back(local_block_id);
      }
    }

    if (sn.pin_ids.size() >= 2) {
      ++ctx.placed_net_count;
    }

    const int local_net_id = static_cast<int>(ctx.sub_problem.nets.size());
    ctx.sub_problem.nets.push_back(std::move(sn));
    ctx.sub_problem.net_id_of[gn.name] = local_net_id;

    for (int local_pin_id : ctx.sub_problem.nets[static_cast<size_t>(local_net_id)].pin_ids) {
      ctx.sub_problem.pins[static_cast<size_t>(local_pin_id)].net_ids.push_back(local_net_id);
    }
    for (int local_block_id : ctx.sub_problem.nets[static_cast<size_t>(local_net_id)].block_ids) {
      ctx.sub_problem.blocks[static_cast<size_t>(local_block_id)].net_ids.push_back(local_net_id);
    }
  }

  ctx.sub_rotate.assign(static_cast<size_t>(sub_n), 0);
  for (int local_id = 0; local_id < sub_n; ++local_id) {
    const int global_id = ctx.local_to_global[static_cast<size_t>(local_id)];
    if (global_id < 0 || static_cast<size_t>(global_id) >= rotate_global.size()) {
      throw std::runtime_error("init_fp_bstar: rotate vector is incompatible with block ids");
    }
    const int r = rotate_global[static_cast<size_t>(global_id)];
    if (r != 0 && r != 1) {
      throw std::runtime_error("init_fp_bstar: rotate value must be 0/1");
    }
    ctx.sub_rotate[static_cast<size_t>(local_id)] = r;
  }

  return ctx;
}

CandidateEval eval_cost_from_tree(const Problem &P, const BStarTree &tree,
                                  const std::vector<int> &rotate_global) {
  CandidateEval out;
  RemapContext remap = build_subproblem(P, tree, rotate_global);
  out.fp = bstar_tree_to_floorplan(remap.sub_problem, remap.sub_tree, remap.sub_rotate);

  const size_t sub_n = remap.local_to_global.size();
  if (out.fp.items.size() != sub_n || out.fp.x.size() != sub_n || out.fp.y.size() != sub_n ||
      out.fp.rotate.size() != sub_n) {
    throw std::runtime_error("init_fp_bstar: subproblem decode returned inconsistent floorplan");
  }

  out.layout_width = compute_layout_width(out.fp);
  out.cost = eval_floorplan_cost(out.fp, remap.placed_net_count);
  return out;
}

bool better_choice(const CandidateChoice &cand, const CandidateChoice &best) {
  if (cand.cost < best.cost - kEps) {
    return true;
  }
  if (cand.cost > best.cost + kEps) {
    return false;
  }
  if (cand.parent_order != best.parent_order) {
    return cand.parent_order < best.parent_order;
  }
  if (cand.as_left != best.as_left) {
    return cand.as_left;  // left first
  }
  return cand.new_rotate < best.new_rotate;  // rotate 0 first
}

void dump_tree_dfs(const BStarNode *node, const std::unordered_map<int, FloorplanItem> &item_of,
                   std::ostream &os) {
  if (node == nullptr) {
    return;
  }
  const auto it = item_of.find(node->block_id);
  if (it == item_of.end()) {
    throw std::runtime_error("init_fp_bstar: tree block missing in final floorplan items");
  }
  const FloorplanItem &item = it->second;
  const int left_id = (node->left == nullptr) ? -1 : node->left->block_id;
  const int right_id = (node->right == nullptr) ? -1 : node->right->block_id;

  os << node->block_id << " " << left_id << " " << right_id << " " << item.x << " " << item.y
     << " " << item.w_used << " " << item.h_used << " " << item.rotate << "\n";

  dump_tree_dfs(node->left, item_of, os);
  dump_tree_dfs(node->right, item_of, os);
}

void dump_bstar_tree_text(const BStarTree &tree, const FloorplanResult &fp,
                          const std::vector<int> &rotate, const std::string &filename) {
  if (tree.root == nullptr) {
    throw std::runtime_error("init_fp_bstar: cannot dump empty B*-tree");
  }
  std::ofstream ofs(filename);
  if (!ofs.is_open()) {
    throw std::runtime_error("init_fp_bstar: failed to open tree dump file: " + filename);
  }
  ofs << std::fixed << std::setprecision(6);

  std::unordered_map<int, FloorplanItem> item_of;
  item_of.reserve(fp.items.size() * 2 + 1);
  for (const FloorplanItem &item : fp.items) {
    item_of[item.block_id] = item;
  }

  for (const BStarNode &node : tree.nodes) {
    const auto it = item_of.find(node.block_id);
    if (it == item_of.end()) {
      throw std::runtime_error("init_fp_bstar: missing item for tree node block_id=" +
                               std::to_string(node.block_id));
    }
    if (node.block_id < 0 || static_cast<size_t>(node.block_id) >= rotate.size()) {
      throw std::runtime_error("init_fp_bstar: rotate vector missing tree block");
    }
    if (it->second.rotate != rotate[static_cast<size_t>(node.block_id)]) {
      throw std::runtime_error("init_fp_bstar: rotate mismatch while dumping tree");
    }
  }

  dump_tree_dfs(tree.root, item_of, ofs);
}

}  // namespace

FloorplanResult build_initial_floorplan(const Problem &P, const std::vector<int> &perm) {
  clear_init_planer_debug();
  validate_perm(P, perm);

  const int n = static_cast<int>(P.blocks.size());
  if (n == 0) {
    FloorplanResult empty;
    empty.H = 0.0;
    empty.hpwl = 0.0;
    empty.cost = 0.0;
    return empty;
  }

  std::ostringstream perm_ss;
  perm_ss << "[INIT_FP_BSTAR] perm=" << join_ints(perm);
  debug_log(perm_ss.str());

  const int root_block = perm[0];
  CandidateChoice current_best;
  std::vector<int> base_rotate(static_cast<size_t>(n), 0);

  for (int r = 0; r <= 1; ++r) {
    BStarTree root_tree = make_single_node_tree(root_block);
    std::vector<int> rotate_try = base_rotate;
    rotate_try[static_cast<size_t>(root_block)] = r;

    try {
      CandidateEval eval = eval_cost_from_tree(P, root_tree, rotate_try);
      std::ostringstream ss;
      ss << "[INIT_FP_BSTAR] seed-candidate block=" << root_block << " rotate=" << r
         << " H=" << eval.fp.H << " hpwl=" << eval.fp.hpwl << " cost=" << eval.cost
         << " layout_width=" << eval.layout_width << " chipW=" << P.chipW << " valid=YES";
      debug_log(ss.str());

      CandidateChoice cand;
      cand.tree = std::move(root_tree);
      cand.rotate = std::move(rotate_try);
      cand.fp = std::move(eval.fp);
      cand.cost = eval.cost;
      cand.parent_order = 0;
      cand.as_left = true;
      cand.new_rotate = r;
      cand.valid = true;

      if (!current_best.valid || cand.cost < current_best.cost - kEps ||
          (approx_eq(cand.cost, current_best.cost) && cand.new_rotate < current_best.new_rotate)) {
        current_best = std::move(cand);
      }
    } catch (const std::exception &e) {
      std::ostringstream ss;
      ss << "[INIT_FP_BSTAR] seed-candidate block=" << root_block << " rotate=" << r
         << " chipW=" << P.chipW << " valid=NO reason=" << e.what();
      debug_log(ss.str());
    }
  }

  if (!current_best.valid) {
    throw std::runtime_error("init_fp_bstar: failed to initialize root candidate");
  }

  std::ostringstream seed_pick;
  seed_pick << "[INIT_FP_BSTAR] seed-picked block=" << root_block
            << " rotate=" << current_best.new_rotate << " cost=" << current_best.cost;
  debug_log(seed_pick.str());

  for (int step = 1; step < n; ++step) {
    const int new_block = perm[static_cast<size_t>(step)];
    const auto parent_nodes = collect_nodes_preorder_checked(current_best.tree);

    std::ostringstream iter_hdr;
    iter_hdr << "[INIT_FP_BSTAR] step=" << step << " insert_block=" << new_block
             << " parent_count=" << parent_nodes.size();
    debug_log(iter_hdr.str());

    CandidateChoice round_best;
    int valid_count = 0;
    int tried_count = 0;

    for (size_t pidx = 0; pidx < parent_nodes.size(); ++pidx) {
      const int parent_block = parent_nodes[pidx]->block_id;

      for (int side = 0; side < 2; ++side) {
        const bool as_left = (side == 0);
        if (as_left && parent_nodes[pidx]->left != nullptr) {
          continue;
        }
        if (!as_left && parent_nodes[pidx]->right != nullptr) {
          continue;
        }

        for (int r = 0; r <= 1; ++r) {
          ++tried_count;
          try {
            BStarTree cand_tree =
                clone_tree(current_best.tree, static_cast<size_t>(n));
            cand_tree.nodes.push_back(BStarNode{});
            BStarNode *new_node = &cand_tree.nodes.back();
            new_node->block_id = new_block;
            new_node->left = nullptr;
            new_node->right = nullptr;

            BStarNode *parent = find_node_by_block_id(cand_tree, parent_block);
            if (parent == nullptr) {
              throw std::runtime_error("parent not found after clone");
            }
            if (as_left) {
              if (parent->left != nullptr) {
                throw std::runtime_error("left slot unexpectedly occupied");
              }
              parent->left = new_node;
            } else {
              if (parent->right != nullptr) {
                throw std::runtime_error("right slot unexpectedly occupied");
              }
              parent->right = new_node;
            }

            std::vector<int> cand_rotate = current_best.rotate;
            cand_rotate[static_cast<size_t>(new_block)] = r;

            CandidateEval eval = eval_cost_from_tree(P, cand_tree, cand_rotate);
            ++valid_count;

            std::ostringstream ss;
            ss << "[INIT_FP_BSTAR] candidate parent=" << parent_block
               << " side=" << (as_left ? "left" : "right") << " rotate=" << r
               << " H=" << eval.fp.H << " hpwl=" << eval.fp.hpwl << " cost=" << eval.cost
               << " layout_width=" << eval.layout_width << " chipW=" << P.chipW
               << " valid=YES";
            debug_log(ss.str());

            CandidateChoice cand;
            cand.tree = std::move(cand_tree);
            cand.rotate = std::move(cand_rotate);
            cand.fp = std::move(eval.fp);
            cand.cost = eval.cost;
            cand.parent_block_id = parent_block;
            cand.parent_order = static_cast<int>(pidx);
            cand.as_left = as_left;
            cand.new_rotate = r;
            cand.valid = true;

            if (!round_best.valid || better_choice(cand, round_best)) {
              round_best = std::move(cand);
            }
          } catch (const std::exception &e) {
            std::ostringstream ss;
            ss << "[INIT_FP_BSTAR] candidate parent=" << parent_block
               << " side=" << (as_left ? "left" : "right") << " rotate=" << r
               << " chipW=" << P.chipW << " valid=NO reason=" << e.what();
            debug_log(ss.str());
          }
        }
      }
    }

    std::ostringstream iter_stat;
    iter_stat << "[INIT_FP_BSTAR] step=" << step << " tried=" << tried_count
              << " valid=" << valid_count;
    debug_log(iter_stat.str());

    if (!round_best.valid) {
      throw std::runtime_error("init_fp_bstar: no valid candidate at step=" +
                               std::to_string(step) + " block_id=" +
                               std::to_string(new_block));
    }

    std::ostringstream choose_ss;
    choose_ss << "[INIT_FP_BSTAR] pick parent=" << round_best.parent_block_id
              << " side=" << (round_best.as_left ? "left" : "right")
              << " rotate=" << round_best.new_rotate << " cost=" << round_best.cost;
    debug_log(choose_ss.str());

    current_best = std::move(round_best);
  }

  FloorplanResult final_fp = bstar_tree_to_floorplan(P, current_best.tree, current_best.rotate);
  if (final_fp.items.size() != P.blocks.size()) {
    throw std::runtime_error("init_fp_bstar: final decode does not cover all blocks");
  }
  const double final_width = compute_layout_width(final_fp);
  eval_floorplan_cost(final_fp, P.nets.size());

  dump_bstar_tree_text(current_best.tree, final_fp, current_best.rotate, kTreeDumpFilename);
  debug_log(std::string("[INIT_FP_BSTAR] tree_dump=") + kTreeDumpFilename);

  std::ostringstream done_ss;
  done_ss << "[INIT_FP_BSTAR] done nodes=" << current_best.tree.nodes.size()
          << " H=" << final_fp.H << " hpwl=" << final_fp.hpwl << " cost=" << final_fp.cost
          << " layout_width=" << final_width << " chipW=" << P.chipW;
  debug_log(done_ss.str());

  return final_fp;
}

void dump_init_planer_debug(std::ostream &os) {
  for (const std::string &line : g_debug_lines) {
    os << line << "\n";
  }
}

void clear_init_planer_debug() { g_debug_lines.clear(); }
