#include "parser.h"
#include "orderer.h"
#include "init_planer.h"
#include "sa.h"
#include "writer.h"

namespace {

enum class RunMode {
  kInit,
  kSa,
};

struct CliOptions {
  std::string input_path;
  double time_limit_seconds = 0.0;
  bool debug = false;
  RunMode mode = RunMode::kSa;
};

void print_usage() {
  std::cerr << "Usage: ./floorplan <input.txt> <timeLimit> [--mode <init|sa>] [--debug]\n";
}

bool parse_time_limit(const std::string &s, double &out) {
  try {
    size_t pos = 0;
    out = std::stod(s, &pos);
    if (pos != s.size()) {
      return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

RunMode parse_mode_value(const std::string &value) {
  if (value == "init") {
    return RunMode::kInit;
  }
  if (value == "sa") {
    return RunMode::kSa;
  }
  throw std::runtime_error("main: invalid value for --mode: " + value);
}

CliOptions parse_cli(int argc, char **argv) {
  if (argc < 3) {
    throw std::runtime_error("main: missing required arguments");
  }

  CliOptions options;
  options.input_path = argv[1];
  if (!parse_time_limit(argv[2], options.time_limit_seconds)) {
    throw std::runtime_error("main: invalid timeLimit");
  }

  for (int i = 3; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--debug") {
      options.debug = true;
      continue;
    }
    if (arg == "--mode") {
      if (i + 1 >= argc) {
        throw std::runtime_error("main: missing value for --mode");
      }
      ++i;
      options.mode = parse_mode_value(argv[i]);
      continue;
    }
    throw std::runtime_error("main: unknown option: " + arg);
  }

  return options;
}

std::string derive_input_stem(const std::string &input_path) {
  const size_t slash = input_path.find_last_of("/\\");
  const std::string base =
      (slash == std::string::npos) ? input_path : input_path.substr(slash + 1);
  if (base.empty()) {
    throw std::runtime_error("main: invalid input path");
  }

  const size_t dot = base.find_last_of('.');
  return (dot == std::string::npos || dot == 0) ? base : base.substr(0, dot);
}

std::string derive_solution_path(const std::string &input_path) {
  return derive_input_stem(input_path) + "_solution.txt";
}

std::string derive_invalid_log_path(const std::string &input_stem) {
  return "invalid_sa_moves_" + input_stem + ".log";
}

void dump_ordering_names(const Problem &P, const std::vector<int> &perm) {
  std::cout << "Ordering (by block name):";
  for (int id : perm) {
    std::cout << " " << P.blocks[static_cast<size_t>(id)].name;
  }
  std::cout << "\n";
}

std::string run_mode_to_string(RunMode mode) {
  return (mode == RunMode::kInit) ? "init" : "sa";
}

int best_tree_root_block_id(const BStarTree &tree) {
  return (tree.root == nullptr) ? -1 : tree.root->block_id;
}

}  // namespace

int main(int argc, char **argv) {
  CliOptions options;
  try {
    options = parse_cli(argc, argv);
  } catch (const std::exception &e) {
    print_usage();
    std::cerr << e.what() << "\n";
    return 1;
  }

  try {
    Problem P = parse_problem(options.input_path);

    if (options.debug) {
      dump_problem(P, std::cout);
      setenv("DEBUG", "1", 1);
    }

    const std::string input_stem = derive_input_stem(options.input_path);
    setenv("INIT_FP_BSTAR_INPUT_STEM", input_stem.c_str(), 1);
    std::vector<int> perm = build_initial_ordering(P);
    InitBStarResult init = build_initial_bstar_result(P, perm);
    const std::string output_path = derive_solution_path(options.input_path);
    const std::string tree_dump_path = derive_tree_dump_filename();

    SAResult sa_result;
    const BStarTree *final_tree = &init.tree;
    const std::vector<int> *final_rotate = &init.rotate;
    FloorplanResult final_fp = init.fp;
    bool ran_sa = false;
    if (options.mode == RunMode::kSa) {
      sa_result =
          run_sa(P, init, options.time_limit_seconds, options.debug, derive_invalid_log_path(input_stem));
      final_tree = &sa_result.best_tree;
      final_rotate = &sa_result.best_rotate;
      final_fp = sa_result.best_fp;
      ran_sa = true;
    }

    dump_bstar_tree_text(*final_tree, final_fp, *final_rotate, tree_dump_path);
    write_solution(P, final_fp, output_path);

    if (options.debug) {
      std::cout << "[BSTAR_TREE] tree_dump=" << tree_dump_path << "\n";
      dump_ordering_trace();
      dump_ordering_names(P, perm);
      dump_init_planer_debug(std::cout);
      std::cout << "Initial floorplan: H=" << init.fp.H << ", hpwl=" << init.fp.hpwl
                << ", cost=" << init.fp.cost << "\n";
    }
    if (ran_sa && options.debug) {
      for (const std::string &line : sa_result.debug_lines) {
        std::cout << line << "\n";
      }
    }

    if (!ran_sa) {
      std::cout << "Parsed OK: W=" << P.chipW << ", blocks=" << P.blocks.size()
                << ", pins=" << P.pins.size() << ", nets=" << P.nets.size()
                << ", ordering_len=" << perm.size() << ", H=" << final_fp.H
                << ", hpwl=" << final_fp.hpwl << ", cost=" << final_fp.cost
                << ", solution=" << output_path << "\n";
      if (options.debug) {
        std::cout << "Solution written to: " << output_path << "\n";
      }
    } else {
      std::cout << "Parsed OK: mode=" << run_mode_to_string(options.mode) << ", W=" << P.chipW
                << ", blocks=" << P.blocks.size() << ", pins=" << P.pins.size()
                << ", nets=" << P.nets.size() << ", ordering_len=" << perm.size()
                << ", H=" << final_fp.H << ", hpwl=" << final_fp.hpwl
                << ", cost=" << final_fp.cost
                << ", stop_reason=" << sa_result.stats.stop_reason
                << ", solution=" << output_path << "\n";
      std::cout << "SA stats: steps=" << sa_result.stats.total_steps
                << ", accepted=" << sa_result.stats.accepted
                << ", rejected=" << sa_result.stats.rejected
                << ", invalid=" << sa_result.stats.invalid
                << ", outer_loops=" << sa_result.stats.outer_loop_count
                << ", final_temperature=" << sa_result.stats.final_temperature
                << ", invalid_log=" << sa_result.invalid_log_path << "\n";

      const int root_block_id = best_tree_root_block_id(sa_result.best_tree);
      std::cout << "Best tree: nodes=" << sa_result.best_tree.nodes.size()
                << ", root_block_id=" << root_block_id;
      if (root_block_id >= 0 && static_cast<size_t>(root_block_id) < P.blocks.size()) {
        std::cout << ", root_block_name=" << P.blocks[static_cast<size_t>(root_block_id)].name;
      }
      std::cout << "\n";
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }

  return 0;
}
