#include "parser.h"
#include "orderer.h"

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

    std::cout << "Parsed OK: W=" << P.chipW << ", blocks=" << P.blocks.size()
    << ", pins=" << P.pins.size() << ", nets=" << P.nets.size()
    << "\n";
    
    std::vector<int> perm = build_initial_ordering(P);

    if (debug) {
      dump_ordering_trace();
      std::cout << "Ordering (by block name):";
      for (int id : perm) {
        std::cout << " " << P.blocks[static_cast<size_t>(id)].name;
      }
      std::cout << "\n";
    }

    // TODO: Placement pl = decode_BL(P, perm, ...);
    // TODO: State best = run_SA(P, init_state, time_limit);
    // TODO: write_solution(...)
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }

  return 0;
}
