#include "parser.h"
#include "orderer.h"
#include "init_planer.h"

namespace {

void print_usage() {
  std::cerr << "Usage: ./floorplan <input.txt> <timeLimit> [--debug]\n";
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

bool debug_enabled(int argc, char **argv) {
  if (argc == 4) {
    return std::string(argv[3]) == "--debug";
  }
  const char *env = std::getenv("DEBUG");
  return env && std::string(env) == "1";
}

}  // namespace

int main(int argc, char **argv) {
  if (argc != 3 && argc != 4) {
    print_usage();
    return 1;
  }

  const std::string input_path = argv[1];
  double time_limit = 0.0;
  if (!parse_time_limit(argv[2], time_limit)) {
    print_usage();
    return 1;
  }

  if (argc == 4 && std::string(argv[3]) != "--debug") {
    print_usage();
    return 1;
  }

  const bool debug = debug_enabled(argc, argv);
  (void)time_limit;

  try {
    Problem P = parse_problem(input_path);

    if (debug) {
      dump_problem(P, std::cout);
      setenv("DEBUG", "1", 1);
    }

    std::vector<int> perm = build_initial_ordering(P);
    FloorplanResult fp = build_initial_floorplan(P, perm);

    std::cout << "Parsed OK: W=" << P.chipW << ", blocks=" << P.blocks.size()
              << ", pins=" << P.pins.size() << ", nets=" << P.nets.size()
              << ", ordering_len=" << perm.size() << ", H=" << fp.H
              << ", hpwl=" << fp.hpwl << ", cost=" << fp.cost << "\n";

    if (debug) {
      dump_ordering_trace();
      std::cout << "Ordering (by block name):";
      for (int id : perm) {
        std::cout << " " << P.blocks[static_cast<size_t>(id)].name;
      }
      std::cout << "\n";

      dump_init_planer_debug(std::cout);
      std::cout << "Initial floorplan: H=" << fp.H << ", hpwl=" << fp.hpwl
                << ", cost=" << fp.cost << "\n";
    }

    // TODO: Improve fp with local search / SA within timeLimit
    // TODO: Convert improved floorplan to final solution output
    // TODO: write_solution(...)
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }

  return 0;
}
