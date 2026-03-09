#include "sa.h"

#include "bstar_tree2fp.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace {

constexpr int kNullIndex = -1;
constexpr double kProbEps = 1e-12;
constexpr double kCostCmpEps = 1e-12;
constexpr double kInitialTemperature = 100.0;  // Initial SA temperature before the first outer loop.
constexpr double kCoolingRate = 0.90;  // Temperature multiplier applied after each outer loop.
constexpr double kMinTemperature = 1e-3;  // Stop SA once the temperature falls below this threshold.
constexpr int kMaxOuterLoops = 400;  // Maximum number of temperature levels explored by SA.
constexpr int kMovesPerTemperatureMin = 50;  // Minimum candidate evaluations performed per temperature.
constexpr int kMovesPerTemperatureScale = 10;  // Additional candidate evaluations contributed by each block.
constexpr double kRotateProb = 0.3;  // Sampling weight assigned to Rotate perturbations.
constexpr double kMoveProb = 0.4;  // Sampling weight assigned to Move perturbations.
constexpr double kSwapProb = 0.3;  // Sampling weight assigned to Swap perturbations.
constexpr unsigned int kRandomSeed = 0;  // Fixed RNG seed used to keep SA runs deterministic.
constexpr double kStagnationEpsilon = 1e-9;  // Best-cost delta treated as "no meaningful improvement".
constexpr int kStagnationOuterLoops = 800;  // Number of consecutive stagnant outer loops allowed before stop.

struct EditableNode {
  int block_id = -1;
  int parent = kNullIndex;
  int left = kNullIndex;
  int right = kNullIndex;
};

struct EditableBStar {
  int root = kNullIndex;
  std::vector<EditableNode> nodes;
};

enum class ReinsertPositionKind {
  kExternalLeft,
  kExternalRight,
  kInternalLeft,
  kInternalRight,
};

enum class FailureStage {
  kNone,
  kTreeStructure,
  kDecode,
  kGeometric,
  kOverlap,
};

enum class OperationType {
  kRotate,
  kSwap,
  kMove,
};

struct ReinsertPosition {
  ReinsertPositionKind kind = ReinsertPositionKind::kExternalLeft;
  int parent = kNullIndex;
  int old_child = kNullIndex;
};

struct ValidationResult {
  bool ok = true;
  std::string reason;
};

struct CandidateEvaluation {
  bool valid = false;
  FloorplanResult fp;
  double cost = std::numeric_limits<double>::infinity();
  FailureStage stage = FailureStage::kNone;
  std::string reason;
};

struct StopDecision {
  bool should_stop = false;
  std::string stop_reason;
};

struct OperationContext {
  OperationType type = OperationType::kRotate;
  long long step = 0;
  int outer_loop = 0;
  double temperature = 0.0;

  int rotate_block = -1;
  int rotate_before = -1;
  int rotate_after = -1;

  int swap_node_a = -1;
  int swap_node_b = -1;
  int swap_block_a = -1;
  int swap_block_b = -1;

  int move_node = -1;
  int move_block = -1;
  int move_parent_before = -1;
  int move_left_before = -1;
  int move_right_before = -1;
  bool has_position = false;
  ReinsertPosition position;

  std::string tree_summary_before;
  std::string tree_summary_after_delete;
  std::string tree_summary_after_reinsert;
};

struct SAStateInternal {
  EditableBStar current_tree;
  EditableBStar best_tree;
  std::vector<int> current_rotate;
  std::vector<int> best_rotate;
  FloorplanResult current_fp;
  FloorplanResult best_fp;
  double current_cost = 0.0;
  double best_cost = 0.0;
  double temperature = 0.0;
  long long total_steps = 0;
  long long accepted = 0;
  long long rejected = 0;
  long long invalid = 0;
  int outer_loop_count = 0;
  int stagnation_count = 0;
  std::vector<std::string> debug_lines;
};

bool contains(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

std::string op_type_to_string(OperationType type) {
  switch (type) {
    case OperationType::kRotate:
      return "Rotate";
    case OperationType::kSwap:
      return "Swap";
    case OperationType::kMove:
      return "Move";
  }
  return "Unknown";
}

std::string failure_stage_to_string(FailureStage stage) {
  switch (stage) {
    case FailureStage::kNone:
      return "none";
    case FailureStage::kTreeStructure:
      return "tree_structure_failed";
    case FailureStage::kDecode:
      return "decode_failed";
    case FailureStage::kGeometric:
      return "geometric_constraint_failed";
    case FailureStage::kOverlap:
      return "overlap_failed";
  }
  return "unknown_failed";
}

std::string position_kind_to_string(ReinsertPositionKind kind) {
  switch (kind) {
    case ReinsertPositionKind::kExternalLeft:
      return "EXTERNAL_LEFT";
    case ReinsertPositionKind::kExternalRight:
      return "EXTERNAL_RIGHT";
    case ReinsertPositionKind::kInternalLeft:
      return "INTERNAL_LEFT";
    case ReinsertPositionKind::kInternalRight:
      return "INTERNAL_RIGHT";
  }
  return "UNKNOWN_POSITION";
}

double recompute_cost(FloorplanResult &fp, size_t net_count) {
  double cost = fp.H;
  if (net_count > 0) {
    cost += fp.hpwl / static_cast<double>(net_count);
  }
  fp.cost = cost;
  return cost;
}

std::string node_token(const EditableBStar &tree, int idx) {
  if (idx == kNullIndex) {
    return "null";
  }
  if (idx < 0 || static_cast<size_t>(idx) >= tree.nodes.size()) {
    return "invalid(" + std::to_string(idx) + ")";
  }
  const EditableNode &node = tree.nodes[static_cast<size_t>(idx)];
  std::ostringstream oss;
  oss << idx << "(b=" << node.block_id << ",p=" << node.parent << ",l=" << node.left
      << ",r=" << node.right << ")";
  return oss.str();
}

std::string tree_summary_relaxed(const EditableBStar &tree) {
  std::ostringstream oss;
  oss << "root=" << node_token(tree, tree.root) << " nodes=" << tree.nodes.size() << " preorder=[";

  if (tree.root != kNullIndex && !tree.nodes.empty()) {
    std::vector<int> stack;
    stack.push_back(tree.root);
    std::vector<bool> seen(tree.nodes.size(), false);
    bool first = true;
    int guard = 0;
    const int guard_limit = static_cast<int>(tree.nodes.size()) * 4 + 8;

    while (!stack.empty() && guard < guard_limit) {
      ++guard;
      const int idx = stack.back();
      stack.pop_back();
      if (!first) {
        oss << " ";
      }
      first = false;

      if (idx < 0 || static_cast<size_t>(idx) >= tree.nodes.size()) {
        oss << "invalid(" << idx << ")";
        continue;
      }
      if (seen[static_cast<size_t>(idx)]) {
        oss << "revisit(" << idx << ")";
        continue;
      }

      seen[static_cast<size_t>(idx)] = true;
      const EditableNode &node = tree.nodes[static_cast<size_t>(idx)];
      oss << idx << "(b=" << node.block_id << ",p=" << node.parent << ",l=" << node.left
          << ",r=" << node.right << ")";
      if (node.right != kNullIndex) {
        stack.push_back(node.right);
      }
      if (node.left != kNullIndex) {
        stack.push_back(node.left);
      }
    }

    if (guard >= guard_limit) {
      oss << " ...";
    }
  }

  oss << "]";
  return oss.str();
}

EditableBStar to_editable_tree(const BStarTree &src) {
  EditableBStar dst;
  if (src.root == nullptr) {
    if (!src.nodes.empty()) {
      throw std::runtime_error("sa: source B*-tree has null root with non-empty nodes");
    }
    return dst;
  }

  dst.nodes.resize(src.nodes.size());
  std::unordered_map<const BStarNode *, int> index_of;
  index_of.reserve(src.nodes.size() * 2 + 1);

  for (size_t i = 0; i < src.nodes.size(); ++i) {
    index_of[&src.nodes[i]] = static_cast<int>(i);
    dst.nodes[i].block_id = src.nodes[i].block_id;
  }

  const auto lookup_index = [&](const BStarNode *ptr) -> int {
    const auto it = index_of.find(ptr);
    if (it == index_of.end()) {
      throw std::runtime_error("sa: source B*-tree child pointer is not owned by nodes vector");
    }
    return it->second;
  };

  dst.root = lookup_index(src.root);
  for (size_t i = 0; i < src.nodes.size(); ++i) {
    if (src.nodes[i].left != nullptr) {
      dst.nodes[i].left = lookup_index(src.nodes[i].left);
    }
    if (src.nodes[i].right != nullptr) {
      dst.nodes[i].right = lookup_index(src.nodes[i].right);
    }
  }

  for (size_t i = 0; i < dst.nodes.size(); ++i) {
    const EditableNode &node = dst.nodes[i];
    if (node.left != kNullIndex) {
      EditableNode &child = dst.nodes[static_cast<size_t>(node.left)];
      if (child.parent != kNullIndex) {
        throw std::runtime_error("sa: source B*-tree child has multiple parents");
      }
      child.parent = static_cast<int>(i);
    }
    if (node.right != kNullIndex) {
      EditableNode &child = dst.nodes[static_cast<size_t>(node.right)];
      if (child.parent != kNullIndex) {
        throw std::runtime_error("sa: source B*-tree child has multiple parents");
      }
      child.parent = static_cast<int>(i);
    }
  }

  return dst;
}

BStarTree to_public_tree(const EditableBStar &src) {
  BStarTree dst;
  if (src.nodes.empty()) {
    if (src.root != kNullIndex) {
      throw std::runtime_error("sa: editable tree has root with no nodes");
    }
    return dst;
  }
  if (src.root < 0 || static_cast<size_t>(src.root) >= src.nodes.size()) {
    throw std::runtime_error("sa: editable tree root is out of range");
  }

  dst.nodes.resize(src.nodes.size());
  for (size_t i = 0; i < src.nodes.size(); ++i) {
    dst.nodes[i].block_id = src.nodes[i].block_id;
  }
  for (size_t i = 0; i < src.nodes.size(); ++i) {
    const EditableNode &node = src.nodes[i];
    if (node.left != kNullIndex) {
      if (node.left < 0 || static_cast<size_t>(node.left) >= src.nodes.size()) {
        throw std::runtime_error("sa: editable tree left child index is out of range");
      }
      dst.nodes[i].left = &dst.nodes[static_cast<size_t>(node.left)];
    }
    if (node.right != kNullIndex) {
      if (node.right < 0 || static_cast<size_t>(node.right) >= src.nodes.size()) {
        throw std::runtime_error("sa: editable tree right child index is out of range");
      }
      dst.nodes[i].right = &dst.nodes[static_cast<size_t>(node.right)];
    }
  }
  dst.root = &dst.nodes[static_cast<size_t>(src.root)];
  return dst;
}

ValidationResult validate_tree_structure_common(const EditableBStar &tree, int isolated_idx,
                                                bool allow_isolated) {
  ValidationResult result;
  const int n = static_cast<int>(tree.nodes.size());
  if (n == 0) {
    if (tree.root != kNullIndex) {
      result.ok = false;
      result.reason = "sa: tree has root but no nodes";
    }
    return result;
  }
  if (tree.root < 0 || tree.root >= n) {
    result.ok = false;
    result.reason = "sa: tree root is invalid";
    return result;
  }
  if (allow_isolated) {
    if (isolated_idx < 0 || isolated_idx >= n) {
      result.ok = false;
      result.reason = "sa: isolated node index is invalid";
      return result;
    }
    if (tree.root == isolated_idx) {
      result.ok = false;
      result.reason = "sa: isolated node cannot be the root";
      return result;
    }
    const EditableNode &isolated = tree.nodes[static_cast<size_t>(isolated_idx)];
    if (isolated.parent != kNullIndex || isolated.left != kNullIndex || isolated.right != kNullIndex) {
      result.ok = false;
      result.reason = "sa: isolated node is still attached after delete";
      return result;
    }
  }

  std::vector<int> visit_state(static_cast<size_t>(n), 0);
  std::vector<int> parent_edge_count(static_cast<size_t>(n), 0);
  std::vector<bool> reachable(static_cast<size_t>(n), false);
  std::vector<bool> seen_block(static_cast<size_t>(n), false);

  for (int i = 0; i < n; ++i) {
    const int block_id = tree.nodes[static_cast<size_t>(i)].block_id;
    if (block_id < 0 || block_id >= n) {
      result.ok = false;
      result.reason = "sa: block_id out of range at node index " + std::to_string(i);
      return result;
    }
    if (seen_block[static_cast<size_t>(block_id)]) {
      result.ok = false;
      result.reason = "sa: duplicate block_id in editable tree: " + std::to_string(block_id);
      return result;
    }
    seen_block[static_cast<size_t>(block_id)] = true;
  }

  const auto fail = [&](const std::string &reason) -> ValidationResult {
    return ValidationResult{false, reason};
  };

  std::function<ValidationResult(int)> dfs = [&](int idx) -> ValidationResult {
    if (idx < 0 || idx >= n) {
      return fail("sa: child index out of range: " + std::to_string(idx));
    }
    if (allow_isolated && idx == isolated_idx) {
      return fail("sa: isolated node is still reachable from root");
    }
    if (visit_state[static_cast<size_t>(idx)] == 1) {
      return fail("sa: cycle detected in editable tree");
    }
    if (visit_state[static_cast<size_t>(idx)] == 2) {
      return fail("sa: duplicate node reference detected in editable tree");
    }

    visit_state[static_cast<size_t>(idx)] = 1;
    reachable[static_cast<size_t>(idx)] = true;

    const EditableNode &node = tree.nodes[static_cast<size_t>(idx)];
    const int children[2] = {node.left, node.right};
    for (int child : children) {
      if (child == kNullIndex) {
        continue;
      }
      if (child == idx) {
        return fail("sa: node points to itself at index " + std::to_string(idx));
      }
      if (child < 0 || child >= n) {
        return fail("sa: child index out of range: " + std::to_string(child));
      }
      if (allow_isolated && child == isolated_idx) {
        return fail("sa: isolated node remains attached after delete");
      }
      parent_edge_count[static_cast<size_t>(child)] += 1;
      if (parent_edge_count[static_cast<size_t>(child)] > 1) {
        return fail("sa: node has multiple parents: " + std::to_string(child));
      }
      if (tree.nodes[static_cast<size_t>(child)].parent != idx) {
        return fail("sa: child pointer and parent pointer mismatch at child index " +
                    std::to_string(child));
      }
      ValidationResult child_result = dfs(child);
      if (!child_result.ok) {
        return child_result;
      }
    }

    visit_state[static_cast<size_t>(idx)] = 2;
    return ValidationResult{};
  };

  if (tree.nodes[static_cast<size_t>(tree.root)].parent != kNullIndex) {
    result.ok = false;
    result.reason = "sa: root parent must be null";
    return result;
  }

  result = dfs(tree.root);
  if (!result.ok) {
    return result;
  }

  for (int i = 0; i < n; ++i) {
    const EditableNode &node = tree.nodes[static_cast<size_t>(i)];
    const bool is_isolated = allow_isolated && i == isolated_idx;
    if (is_isolated) {
      if (reachable[static_cast<size_t>(i)]) {
        return fail("sa: isolated node was still reached after delete");
      }
      if (parent_edge_count[static_cast<size_t>(i)] != 0) {
        return fail("sa: isolated node still has a parent edge after delete");
      }
      continue;
    }

    if (!reachable[static_cast<size_t>(i)]) {
      return fail("sa: node is unreachable from root: " + std::to_string(i));
    }
    if (i == tree.root) {
      if (parent_edge_count[static_cast<size_t>(i)] != 0) {
        return fail("sa: root must not have an incoming parent edge");
      }
      if (node.parent != kNullIndex) {
        return fail("sa: root parent must be null");
      }
    } else {
      if (node.parent == kNullIndex) {
        return fail("sa: non-root node is missing parent: " + std::to_string(i));
      }
      if (parent_edge_count[static_cast<size_t>(i)] != 1) {
        return fail("sa: non-root node must have exactly one parent edge: " + std::to_string(i));
      }
      if (node.parent < 0 || node.parent >= n) {
        return fail("sa: parent index out of range at node index " + std::to_string(i));
      }
      const EditableNode &parent = tree.nodes[static_cast<size_t>(node.parent)];
      if (parent.left != i && parent.right != i) {
        return fail("sa: parent pointer mismatch at node index " + std::to_string(i));
      }
    }
  }

  return result;
}

ValidationResult validate_tree_structure(const EditableBStar &tree) {
  return validate_tree_structure_common(tree, kNullIndex, false);
}

ValidationResult validate_tree_structure_after_delete(const EditableBStar &tree, int isolated_idx) {
  return validate_tree_structure_common(tree, isolated_idx, true);
}

std::vector<int> collect_reachable_preorder(const EditableBStar &tree) {
  std::vector<int> out;
  if (tree.root == kNullIndex || tree.nodes.empty()) {
    return out;
  }
  std::vector<int> stack;
  stack.push_back(tree.root);
  while (!stack.empty()) {
    const int idx = stack.back();
    stack.pop_back();
    out.push_back(idx);
    const EditableNode &node = tree.nodes[static_cast<size_t>(idx)];
    if (node.right != kNullIndex) {
      stack.push_back(node.right);
    }
    if (node.left != kNullIndex) {
      stack.push_back(node.left);
    }
  }
  return out;
}

FailureStage classify_decoder_failure(const std::string &reason) {
  if (contains(reason, "overlap detected")) {
    return FailureStage::kOverlap;
  }
  if (contains(reason, "left child") || contains(reason, "right child") ||
      contains(reason, "positive vertical contact") ||
      contains(reason, "parent is not placed before child geometry validation")) {
    return FailureStage::kGeometric;
  }
  return FailureStage::kDecode;
}

CandidateEvaluation evaluate_candidate(const Problem &P, const EditableBStar &tree,
                                       const std::vector<int> &rotate) {
  CandidateEvaluation eval;

  const ValidationResult tree_validation = validate_tree_structure(tree);
  if (!tree_validation.ok) {
    eval.stage = FailureStage::kTreeStructure;
    eval.reason = tree_validation.reason;
    return eval;
  }

  try {
    BStarTree public_tree = to_public_tree(tree);
    eval.fp = bstar_tree_to_floorplan(P, public_tree, rotate);
    eval.cost = recompute_cost(eval.fp, P.nets.size());
    eval.valid = true;
    return eval;
  } catch (const std::exception &e) {
    eval.stage = classify_decoder_failure(e.what());
    eval.reason = e.what();
    return eval;
  }
}

int choose_uniform_index(std::mt19937 &rng, int count) {
  if (count <= 0) {
    throw std::runtime_error("sa: cannot sample from an empty range");
  }
  std::uniform_int_distribution<int> dist(0, count - 1);
  return dist(rng);
}

double random_unit(std::mt19937 &rng) {
  std::uniform_real_distribution<double> dist(0.0, 1.0);
  return dist(rng);
}

OperationType choose_operation(int block_count, std::mt19937 &rng) {
  std::vector<std::pair<OperationType, double>> options;
  if (block_count > 0) {
    options.push_back({OperationType::kRotate, kRotateProb});
  }
  if (block_count >= 2) {
    options.push_back({OperationType::kSwap, kSwapProb});
    options.push_back({OperationType::kMove, kMoveProb});
  }
  if (options.empty()) {
    throw std::runtime_error("sa: no perturbation operators are available");
  }

  double total_weight = 0.0;
  for (const auto &option : options) {
    total_weight += std::max(0.0, option.second);
  }

  if (total_weight <= kProbEps) {
    return options[static_cast<size_t>(choose_uniform_index(rng, static_cast<int>(options.size())))]
        .first;
  }

  std::uniform_real_distribution<double> dist(0.0, total_weight);
  double draw = dist(rng);
  double prefix = 0.0;
  for (const auto &option : options) {
    prefix += std::max(0.0, option.second);
    if (draw <= prefix + kProbEps) {
      return option.first;
    }
  }
  return options.back().first;
}

void validate_rotate_effect(const std::vector<int> &before, const std::vector<int> &after,
                            int block_id) {
  if (before.size() != after.size()) {
    throw std::runtime_error("sa: rotate changed vector size");
  }
  int diff_count = 0;
  for (size_t i = 0; i < before.size(); ++i) {
    if (before[i] != after[i]) {
      ++diff_count;
      if (static_cast<int>(i) != block_id) {
        throw std::runtime_error("sa: rotate changed an unexpected block rotation");
      }
    }
  }
  if (diff_count != 1) {
    throw std::runtime_error("sa: rotate must change exactly one block rotation");
  }
}

void validate_swap_effect(const EditableBStar &before, const EditableBStar &after, int node_a,
                          int node_b) {
  if (before.nodes.size() != after.nodes.size()) {
    throw std::runtime_error("sa: swap changed tree size");
  }
  int changed = 0;
  for (size_t i = 0; i < before.nodes.size(); ++i) {
    const EditableNode &x = before.nodes[i];
    const EditableNode &y = after.nodes[i];
    if (x.block_id != y.block_id) {
      ++changed;
      if (static_cast<int>(i) != node_a && static_cast<int>(i) != node_b) {
        throw std::runtime_error("sa: swap changed an unexpected node block_id");
      }
    }
    if (x.parent != y.parent || x.left != y.left || x.right != y.right) {
      throw std::runtime_error("sa: swap changed tree topology");
    }
  }
  if (changed != 2) {
    throw std::runtime_error("sa: swap must change exactly two node block_ids");
  }
}

void toggle_rotate(std::vector<int> &rotate, int block_id) {
  if (block_id < 0 || static_cast<size_t>(block_id) >= rotate.size()) {
    throw std::runtime_error("sa: rotate block_id is out of range");
  }
  const int old_value = rotate[static_cast<size_t>(block_id)];
  if (old_value != 0 && old_value != 1) {
    throw std::runtime_error("sa: rotate value must be 0 or 1");
  }
  rotate[static_cast<size_t>(block_id)] = 1 - old_value;
}

void swap_blocks(EditableBStar &tree, int node_a, int node_b) {
  if (node_a == node_b) {
    throw std::runtime_error("sa: swap nodes must be distinct");
  }
  if (node_a < 0 || static_cast<size_t>(node_a) >= tree.nodes.size() || node_b < 0 ||
      static_cast<size_t>(node_b) >= tree.nodes.size()) {
    throw std::runtime_error("sa: swap node index is out of range");
  }
  std::swap(tree.nodes[static_cast<size_t>(node_a)].block_id,
            tree.nodes[static_cast<size_t>(node_b)].block_id);
}

void replace_child(EditableBStar &tree, int parent, int old_child, int new_child) {
  if (parent < 0 || static_cast<size_t>(parent) >= tree.nodes.size()) {
    throw std::runtime_error("sa: parent index is out of range");
  }
  EditableNode &parent_node = tree.nodes[static_cast<size_t>(parent)];
  if (parent_node.left == old_child) {
    parent_node.left = new_child;
    return;
  }
  if (parent_node.right == old_child) {
    parent_node.right = new_child;
    return;
  }
  throw std::runtime_error("sa: old child is not attached to the requested parent");
}

void clear_node_links(EditableBStar &tree, int node_idx) {
  if (node_idx < 0 || static_cast<size_t>(node_idx) >= tree.nodes.size()) {
    throw std::runtime_error("sa: node index is out of range");
  }
  EditableNode &node = tree.nodes[static_cast<size_t>(node_idx)];
  node.parent = kNullIndex;
  node.left = kNullIndex;
  node.right = kNullIndex;
}

void delete_node_from_tree(EditableBStar &tree, int node_idx) {
  if (node_idx < 0 || static_cast<size_t>(node_idx) >= tree.nodes.size()) {
    throw std::runtime_error("sa: delete node index is out of range");
  }
  if (node_idx == tree.root) {
    throw std::runtime_error("sa: move must not delete the root");
  }

  EditableNode &u = tree.nodes[static_cast<size_t>(node_idx)];
  const int parent = u.parent;
  if (parent == kNullIndex) {
    throw std::runtime_error("sa: delete node must have a parent");
  }

  const int left = u.left;
  const int right = u.right;

  if (left == kNullIndex && right == kNullIndex) {
    replace_child(tree, parent, node_idx, kNullIndex);
    clear_node_links(tree, node_idx);
    return;
  }

  if (left == kNullIndex || right == kNullIndex) {
    const int child = (left != kNullIndex) ? left : right;
    replace_child(tree, parent, node_idx, child);
    tree.nodes[static_cast<size_t>(child)].parent = parent;
    clear_node_links(tree, node_idx);
    return;
  }

  replace_child(tree, parent, node_idx, right);
  tree.nodes[static_cast<size_t>(right)].parent = parent;

  int cur = right;
  while (tree.nodes[static_cast<size_t>(cur)].left != kNullIndex) {
    cur = tree.nodes[static_cast<size_t>(cur)].left;
  }
  tree.nodes[static_cast<size_t>(cur)].left = left;
  tree.nodes[static_cast<size_t>(left)].parent = cur;

  clear_node_links(tree, node_idx);
}

std::vector<ReinsertPosition> enumerate_reinsert_positions_after_delete(const EditableBStar &tree,
                                                                        int isolated_idx) {
  const ValidationResult reduced_validation = validate_tree_structure_after_delete(tree, isolated_idx);
  if (!reduced_validation.ok) {
    throw std::runtime_error("sa: reduced tree is invalid before position enumeration: " +
                             reduced_validation.reason);
  }

  std::vector<ReinsertPosition> positions;
  const std::vector<int> preorder = collect_reachable_preorder(tree);
  positions.reserve(preorder.size() * 4);

  for (int parent : preorder) {
    const EditableNode &node = tree.nodes[static_cast<size_t>(parent)];
    if (node.left == kNullIndex) {
      positions.push_back(
          ReinsertPosition{ReinsertPositionKind::kExternalLeft, parent, kNullIndex});
    } else {
      positions.push_back(
          ReinsertPosition{ReinsertPositionKind::kInternalLeft, parent, node.left});
    }

    if (node.right == kNullIndex) {
      positions.push_back(
          ReinsertPosition{ReinsertPositionKind::kExternalRight, parent, kNullIndex});
    } else {
      positions.push_back(
          ReinsertPosition{ReinsertPositionKind::kInternalRight, parent, node.right});
    }
  }

  return positions;
}

void reinsert_node_into_tree(EditableBStar &tree, int node_idx, const ReinsertPosition &position) {
  if (node_idx < 0 || static_cast<size_t>(node_idx) >= tree.nodes.size()) {
    throw std::runtime_error("sa: reinsert node index is out of range");
  }
  if (position.parent < 0 || static_cast<size_t>(position.parent) >= tree.nodes.size()) {
    throw std::runtime_error("sa: reinsert parent index is out of range");
  }
  if (node_idx == tree.root) {
    throw std::runtime_error("sa: reinsert must not replace the root");
  }

  EditableNode &u = tree.nodes[static_cast<size_t>(node_idx)];
  if (u.parent != kNullIndex || u.left != kNullIndex || u.right != kNullIndex) {
    throw std::runtime_error("sa: reinsert node must be isolated");
  }

  EditableNode &parent = tree.nodes[static_cast<size_t>(position.parent)];
  switch (position.kind) {
    case ReinsertPositionKind::kExternalLeft:
      if (parent.left != kNullIndex) {
        throw std::runtime_error("sa: EXTERNAL_LEFT requires an empty left slot");
      }
      parent.left = node_idx;
      u.parent = position.parent;
      break;

    case ReinsertPositionKind::kExternalRight:
      if (parent.right != kNullIndex) {
        throw std::runtime_error("sa: EXTERNAL_RIGHT requires an empty right slot");
      }
      parent.right = node_idx;
      u.parent = position.parent;
      break;

    case ReinsertPositionKind::kInternalLeft:
      if (position.old_child == kNullIndex || parent.left != position.old_child) {
        throw std::runtime_error("sa: INTERNAL_LEFT requires parent.left == old_child");
      }
      parent.left = node_idx;
      u.parent = position.parent;
      u.left = position.old_child;
      tree.nodes[static_cast<size_t>(position.old_child)].parent = node_idx;
      break;

    case ReinsertPositionKind::kInternalRight:
      if (position.old_child == kNullIndex || parent.right != position.old_child) {
        throw std::runtime_error("sa: INTERNAL_RIGHT requires parent.right == old_child");
      }
      parent.right = node_idx;
      u.parent = position.parent;
      u.right = position.old_child;
      tree.nodes[static_cast<size_t>(position.old_child)].parent = node_idx;
      break;
  }
}

void append_debug_line(SAStateInternal &state, const std::string &line, bool enabled) {
  if (enabled) {
    state.debug_lines.push_back(line);
  }
}

void write_invalid_log_entry(std::ostream &os, const OperationContext &ctx, FailureStage stage,
                             const std::string &reason, const EditableBStar &tree_after_failure) {
  os << "step=" << ctx.step << " outer_loop=" << ctx.outer_loop
     << " temperature=" << ctx.temperature << " op=" << op_type_to_string(ctx.type) << "\n";
  if (ctx.type == OperationType::kRotate) {
    os << "rotate_block=" << ctx.rotate_block << " rotate_before=" << ctx.rotate_before
       << " rotate_after=" << ctx.rotate_after << "\n";
  } else if (ctx.type == OperationType::kSwap) {
    os << "swap_node_a=" << ctx.swap_node_a << " swap_block_a=" << ctx.swap_block_a
       << " swap_node_b=" << ctx.swap_node_b << " swap_block_b=" << ctx.swap_block_b << "\n";
  } else if (ctx.type == OperationType::kMove) {
    os << "move_node=" << ctx.move_node << " move_block=" << ctx.move_block
       << " move_parent_before=" << ctx.move_parent_before
       << " move_left_before=" << ctx.move_left_before
       << " move_right_before=" << ctx.move_right_before << "\n";
    if (ctx.has_position) {
      os << "reinsert_position=" << position_kind_to_string(ctx.position.kind)
         << " reinsert_parent=" << ctx.position.parent
         << " reinsert_old_child=" << ctx.position.old_child << "\n";
    }
  }
  os << "failure_stage=" << failure_stage_to_string(stage) << "\n";
  os << "reason=" << reason << "\n";
  os << "tree_before=" << ctx.tree_summary_before << "\n";
  if (!ctx.tree_summary_after_delete.empty()) {
    os << "tree_after_delete=" << ctx.tree_summary_after_delete << "\n";
  }
  if (!ctx.tree_summary_after_reinsert.empty()) {
    os << "tree_after_reinsert=" << ctx.tree_summary_after_reinsert << "\n";
  }
  os << "tree_after_failure=" << tree_summary_relaxed(tree_after_failure) << "\n";
  os << "---\n";
}

StopDecision should_stop_sa(const SAStateInternal &state, double time_limit_seconds,
                            double elapsed_seconds) {
  if (time_limit_seconds > 0.0 && elapsed_seconds >= time_limit_seconds) {
    return {true, "time_limit"};
  }
  if (state.temperature < kMinTemperature) {
    return {true, "min_temperature"};
  }
  if (state.outer_loop_count >= kMaxOuterLoops) {
    return {true, "max_outer_loops"};
  }
  if (state.stagnation_count >= kStagnationOuterLoops) {
    return {true, "stagnation"};
  }
  return {};
}

std::string floorplan_summary(const FloorplanResult &fp) {
  std::ostringstream oss;
  oss << "H=" << fp.H << " hpwl=" << fp.hpwl << " cost=" << fp.cost << " items=" << fp.items.size();
  return oss.str();
}

int compute_moves_per_temperature(size_t block_count) {
  const int scaled = static_cast<int>(block_count) * kMovesPerTemperatureScale;
  return (scaled > kMovesPerTemperatureMin) ? scaled : kMovesPerTemperatureMin;
}

void validate_config(double time_limit_seconds) {
  if (time_limit_seconds < 0.0) {
    throw std::runtime_error("sa: time_limit_seconds must be non-negative");
  }
  if (!(kInitialTemperature > 0.0)) {
    throw std::runtime_error("sa: initial_temperature must be positive");
  }
  if (!(kCoolingRate > 0.0 && kCoolingRate < 1.0)) {
    throw std::runtime_error("sa: cooling_rate must be in the interval (0, 1)");
  }
  if (kMinTemperature < 0.0) {
    throw std::runtime_error("sa: min_temperature must be non-negative");
  }
  if (kMaxOuterLoops <= 0) {
    throw std::runtime_error("sa: max_outer_loops must be positive");
  }
  if (kMovesPerTemperatureMin <= 0 || kMovesPerTemperatureScale <= 0) {
    throw std::runtime_error("sa: moves_per_temperature must be positive");
  }
  if (kRotateProb < 0.0 || kMoveProb < 0.0 || kSwapProb < 0.0) {
    throw std::runtime_error("sa: operator probabilities must be non-negative");
  }
  if (kRotateProb + kMoveProb + kSwapProb <= kProbEps) {
    throw std::runtime_error("sa: at least one operator probability must be positive");
  }
  if (kStagnationEpsilon < 0.0) {
    throw std::runtime_error("sa: stagnation_epsilon must be non-negative");
  }
  if (kStagnationOuterLoops <= 0) {
    throw std::runtime_error("sa: stagnation_outer_loops must be positive");
  }
}

}  // namespace

SAResult run_sa(const Problem &P, const InitBStarResult &init, double time_limit_seconds,
                bool debug_sa, const std::string &invalid_log_path) {
  validate_config(time_limit_seconds);

  SAResult result;
  result.invalid_log_path = invalid_log_path.empty() ? "invalid_sa_moves.log" : invalid_log_path;

  std::ofstream invalid_log(result.invalid_log_path.c_str(), std::ios::out | std::ios::trunc);
  if (!invalid_log.is_open()) {
    throw std::runtime_error("sa: failed to open invalid log file: " + result.invalid_log_path);
  }

  SAStateInternal state;
  state.temperature = kInitialTemperature;
  state.current_tree = to_editable_tree(init.tree);
  state.current_rotate = init.rotate;

  if (P.blocks.empty()) {
    result.best_tree = init.tree;
    result.best_rotate = init.rotate;
    result.best_fp = init.fp;
    result.best_cost = init.cost;
    result.stats.stop_reason = "empty_problem";
    result.stats.final_temperature = state.temperature;
    result.debug_lines = state.debug_lines;
    return result;
  }

  CandidateEvaluation init_eval = evaluate_candidate(P, state.current_tree, state.current_rotate);
  if (!init_eval.valid) {
    invalid_log << "initial_state_invalid stage=" << failure_stage_to_string(init_eval.stage)
                << " reason=" << init_eval.reason << "\n";
    throw std::runtime_error("sa: initial state is invalid: " + init_eval.reason);
  }

  state.current_fp = init_eval.fp;
  state.current_cost = init_eval.cost;
  state.best_tree = state.current_tree;
  state.best_rotate = state.current_rotate;
  state.best_fp = state.current_fp;
  state.best_cost = state.current_cost;

  append_debug_line(state, "[SA] init tree " + tree_summary_relaxed(state.current_tree),
                    debug_sa);
  append_debug_line(state, "[SA] init floorplan " + floorplan_summary(state.current_fp),
                    debug_sa);
  append_debug_line(state, "[SA] init legality=YES cost=" + std::to_string(state.current_cost),
                    debug_sa);

  const int moves_per_temperature = compute_moves_per_temperature(P.blocks.size());
  std::mt19937 rng(kRandomSeed);
  const auto start_time = std::chrono::steady_clock::now();

  std::string stop_reason;
  while (true) {
    const double elapsed_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
    StopDecision stop = should_stop_sa(state, time_limit_seconds, elapsed_seconds);
    if (stop.should_stop) {
      stop_reason = stop.stop_reason;
      break;
    }

    const double loop_temperature = state.temperature;
    const double best_cost_prev = state.best_cost;
    long long loop_attempts = 0;
    long long loop_accepted = 0;
    long long loop_rejected = 0;
    long long loop_invalid = 0;
    bool time_limit_hit = false;

    for (int move_idx = 0; move_idx < moves_per_temperature; ++move_idx) {
      const double inner_elapsed =
          std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
      if (time_limit_seconds > 0.0 && inner_elapsed >= time_limit_seconds) {
        time_limit_hit = true;
        stop_reason = "time_limit";
        break;
      }

      ++state.total_steps;
      ++loop_attempts;

      EditableBStar candidate_tree = state.current_tree;
      std::vector<int> candidate_rotate = state.current_rotate;

      OperationContext op_ctx;
      op_ctx.step = state.total_steps;
      op_ctx.outer_loop = state.outer_loop_count + 1;
      op_ctx.temperature = loop_temperature;
      op_ctx.tree_summary_before = tree_summary_relaxed(state.current_tree);

      CandidateEvaluation eval;

      try {
        const OperationType op = choose_operation(static_cast<int>(candidate_tree.nodes.size()), rng);
        op_ctx.type = op;

        if (op == OperationType::kRotate) {
          const int block_id = choose_uniform_index(rng, static_cast<int>(candidate_rotate.size()));
          op_ctx.rotate_block = block_id;
          op_ctx.rotate_before = candidate_rotate[static_cast<size_t>(block_id)];
          const std::vector<int> rotate_before = candidate_rotate;
          toggle_rotate(candidate_rotate, block_id);
          op_ctx.rotate_after = candidate_rotate[static_cast<size_t>(block_id)];
          validate_rotate_effect(rotate_before, candidate_rotate, block_id);
          eval = evaluate_candidate(P, candidate_tree, candidate_rotate);
        } else if (op == OperationType::kSwap) {
          const int n = static_cast<int>(candidate_tree.nodes.size());
          const int node_a = choose_uniform_index(rng, n);
          int node_b = choose_uniform_index(rng, n - 1);
          if (node_b >= node_a) {
            ++node_b;
          }
          op_ctx.swap_node_a = node_a;
          op_ctx.swap_node_b = node_b;
          op_ctx.swap_block_a = candidate_tree.nodes[static_cast<size_t>(node_a)].block_id;
          op_ctx.swap_block_b = candidate_tree.nodes[static_cast<size_t>(node_b)].block_id;
          const EditableBStar tree_before_swap = candidate_tree;
          swap_blocks(candidate_tree, node_a, node_b);
          validate_swap_effect(tree_before_swap, candidate_tree, node_a, node_b);
          eval = evaluate_candidate(P, candidate_tree, candidate_rotate);
        } else {
          const std::vector<int> preorder = collect_reachable_preorder(candidate_tree);
          if (preorder.size() < 2) {
            throw std::runtime_error("sa: move requires at least one non-root node");
          }
          const int move_choice = choose_uniform_index(rng, static_cast<int>(preorder.size()) - 1);
          const int move_node = preorder[static_cast<size_t>(move_choice + 1)];
          const EditableNode &move_before = candidate_tree.nodes[static_cast<size_t>(move_node)];
          op_ctx.move_node = move_node;
          op_ctx.move_block = move_before.block_id;
          op_ctx.move_parent_before = move_before.parent;
          op_ctx.move_left_before = move_before.left;
          op_ctx.move_right_before = move_before.right;

          delete_node_from_tree(candidate_tree, move_node);
          const ValidationResult delete_validation =
              validate_tree_structure_after_delete(candidate_tree, move_node);
          if (!delete_validation.ok) {
            eval.stage = FailureStage::kTreeStructure;
            eval.reason = delete_validation.reason;
          } else {
            op_ctx.tree_summary_after_delete = tree_summary_relaxed(candidate_tree);
            const std::vector<ReinsertPosition> positions =
                enumerate_reinsert_positions_after_delete(candidate_tree, move_node);
            if (positions.empty()) {
              eval.stage = FailureStage::kTreeStructure;
              eval.reason = "sa: no legal reinsert positions are available after delete";
            } else {
              const ReinsertPosition position =
                  positions[static_cast<size_t>(choose_uniform_index(rng, static_cast<int>(positions.size())))];
              op_ctx.has_position = true;
              op_ctx.position = position;
              reinsert_node_into_tree(candidate_tree, move_node, position);
              op_ctx.tree_summary_after_reinsert = tree_summary_relaxed(candidate_tree);
              eval = evaluate_candidate(P, candidate_tree, candidate_rotate);
            }
          }
        }
      } catch (const std::exception &e) {
        eval.stage = FailureStage::kTreeStructure;
        eval.reason = e.what();
      }

      if (!eval.valid) {
        ++state.invalid;
        ++loop_invalid;
        write_invalid_log_entry(invalid_log, op_ctx, eval.stage, eval.reason, candidate_tree);
        append_debug_line(
            state,
            "[SA] INVALID candidate: op=" + op_type_to_string(op_ctx.type) +
                " stage=" + failure_stage_to_string(eval.stage) + " reason=" + eval.reason +
                " current_cost=" + std::to_string(state.current_cost) +
                " best_cost=" + std::to_string(state.best_cost),
            debug_sa);
        continue;
      }

      const double delta = eval.cost - state.current_cost;
      bool accept = false;
      if (delta <= 0.0) {
        accept = true;
      } else {
        const double accept_prob = std::exp(-delta / loop_temperature);
        accept = random_unit(rng) < accept_prob;
      }

      if (accept) {
        state.current_tree = candidate_tree;
        state.current_rotate = candidate_rotate;
        state.current_fp = eval.fp;
        state.current_cost = eval.cost;
        ++state.accepted;
        ++loop_accepted;
      } else {
        ++state.rejected;
        ++loop_rejected;
      }

      if (eval.cost < state.best_cost - kCostCmpEps) {
        state.best_tree = candidate_tree;
        state.best_rotate = candidate_rotate;
        state.best_fp = eval.fp;
        state.best_cost = eval.cost;
      }

      std::ostringstream candidate_line;
      candidate_line << "[SA] iter=" << state.total_steps << " op=" << op_type_to_string(op_ctx.type)
                     << " delta=" << delta << " status=" << (accept ? "accepted" : "rejected")
                     << " current_cost=" << state.current_cost
                     << " best_cost=" << state.best_cost;
      append_debug_line(state, candidate_line.str(), debug_sa);
    }

    if (time_limit_hit) {
      break;
    }

    const double best_delta = std::abs(best_cost_prev - state.best_cost);
    if (best_delta <= kStagnationEpsilon) {
      ++state.stagnation_count;
    } else {
      state.stagnation_count = 0;
    }

    ++state.outer_loop_count;

    std::ostringstream outer_line;
    outer_line << "[SA] outer_loop=" << state.outer_loop_count << " T=" << loop_temperature
               << " tried=" << loop_attempts << " accepted=" << loop_accepted
               << " rejected=" << loop_rejected << " invalid=" << loop_invalid
               << " current_cost=" << state.current_cost << " best_cost=" << state.best_cost
               << " best_delta=" << best_delta << " stagnation_count=" << state.stagnation_count;
    append_debug_line(state, outer_line.str(), debug_sa);

    state.temperature *= kCoolingRate;
    const double elapsed_after_update =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
    StopDecision stop_after_update = should_stop_sa(state, time_limit_seconds, elapsed_after_update);
    if (stop_after_update.should_stop) {
      stop_reason = stop_after_update.stop_reason;
      append_debug_line(state,
                        "[SA] stop_reason=" + stop_reason + " final_temperature=" +
                            std::to_string(state.temperature),
                        debug_sa);
      break;
    }
  }

  if (stop_reason.empty()) {
    stop_reason = "completed";
  }

  result.best_tree = to_public_tree(state.best_tree);
  result.best_rotate = state.best_rotate;
  result.best_fp = state.best_fp;
  result.best_cost = state.best_cost;
  result.stats.total_steps = state.total_steps;
  result.stats.accepted = state.accepted;
  result.stats.rejected = state.rejected;
  result.stats.invalid = state.invalid;
  result.stats.outer_loop_count = state.outer_loop_count;
  result.stats.stop_reason = stop_reason;
  result.stats.final_temperature = state.temperature;
  result.debug_lines = std::move(state.debug_lines);
  return result;
}
