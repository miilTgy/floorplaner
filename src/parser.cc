#include "parser.h"

namespace {

std::string trim(const std::string &s) {
  size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
    ++start;
  }
  if (start == s.size()) {
    return std::string();
  }
  size_t end = s.size() - 1;
  while (end > start && std::isspace(static_cast<unsigned char>(s[end]))) {
    --end;
  }
  return s.substr(start, end - start + 1);
}

bool starts_with(const std::string &s, const std::string &prefix) {
  if (prefix.size() > s.size()) {
    return false;
  }
  for (size_t i = 0; i < prefix.size(); ++i) {
    if (s[i] != prefix[i]) {
      return false;
    }
  }
  return true;
}

std::vector<std::string> tokenize_ws(const std::string &s) {
  std::vector<std::string> out;
  std::istringstream iss(s);
  std::string tok;
  while (iss >> tok) {
    out.push_back(tok);
  }
  return out;
}

bool split_once(const std::string &line, char delim, std::string &lhs, std::string &rhs) {
  size_t pos = line.find(delim);
  if (pos == std::string::npos) {
    return false;
  }
  lhs = line.substr(0, pos);
  rhs = line.substr(pos + 1);
  return true;
}

int parse_int_token(const std::string &s, int line_no, const std::string &line_text) {
  try {
    size_t pos = 0;
    int v = std::stoi(s, &pos);
    if (pos != s.size()) {
      throw std::runtime_error("invalid int token");
    }
    return v;
  } catch (...) {
    throw std::runtime_error("line " + std::to_string(line_no) + ": " + line_text);
  }
}

double parse_double_token(const std::string &s, int line_no, const std::string &line_text) {
  try {
    size_t pos = 0;
    double v = std::stod(s, &pos);
    if (pos != s.size()) {
      throw std::runtime_error("invalid double token");
    }
    return v;
  } catch (...) {
    throw std::runtime_error("line " + std::to_string(line_no) + ": " + line_text);
  }
}

[[noreturn]] void parse_error(int line_no, const std::string &line_text, const std::string &msg) {
  throw std::runtime_error("line " + std::to_string(line_no) + ": " + line_text + " | " + msg);
}

int longest_prefix_block(const std::string &pin_name,
                         const std::unordered_map<std::string, int> &block_id_of) {
  int best_id = -1;
  size_t best_len = 0;
  for (const auto &kv : block_id_of) {
    const std::string &bname = kv.first;
    if (starts_with(pin_name, bname)) {
      if (bname.size() > best_len) {
        best_len = bname.size();
        best_id = kv.second;
      }
    }
  }
  return best_id;
}

}  // namespace

Problem parse_problem(const std::string &path) {
  std::ifstream fin(path.c_str());
  if (!fin.is_open()) {
    throw std::runtime_error("line 0: <EOF> | failed to open file");
  }

  Problem p;
  p.chipW = 0;

  enum Section { NONE, BLOCKS, PINS, NETS };
  Section section = NONE;

  bool has_chip = false;
  bool has_blocks = false;
  bool has_pins = false;
  bool has_nets = false;

  int num_blocks = -1;
  int num_pins = -1;
  int num_nets = -1;

  int line_no = 0;
  std::string raw;
  while (std::getline(fin, raw)) {
    ++line_no;
    std::string line = trim(raw);
    if (line.empty()) {
      continue;
    }
    if (starts_with(line, "#") || starts_with(line, "//")) {
      continue;
    }

    std::string lhs, rhs;
    if (!split_once(line, ':', lhs, rhs)) {
      parse_error(line_no, raw, "missing ':'");
    }
    lhs = trim(lhs);
    rhs = trim(rhs);

    if (lhs == "chipWidth") {
      if (rhs.empty()) {
        parse_error(line_no, raw, "missing chipWidth value");
      }
      p.chipW = parse_int_token(rhs, line_no, raw);
      has_chip = true;
      section = NONE;
      continue;
    }
    if (lhs == "Blocks") {
      if (rhs.empty()) {
        parse_error(line_no, raw, "missing Blocks count");
      }
      num_blocks = parse_int_token(rhs, line_no, raw);
      if (num_blocks < 0) {
        parse_error(line_no, raw, "negative Blocks count");
      }
      p.blocks.reserve(static_cast<size_t>(num_blocks));
      has_blocks = true;
      section = BLOCKS;
      continue;
    }
    if (lhs == "Pins") {
      if (rhs.empty()) {
        parse_error(line_no, raw, "missing Pins count");
      }
      num_pins = parse_int_token(rhs, line_no, raw);
      if (num_pins < 0) {
        parse_error(line_no, raw, "negative Pins count");
      }
      p.pins.reserve(static_cast<size_t>(num_pins));
      has_pins = true;
      section = PINS;
      continue;
    }
    if (lhs == "Nets") {
      if (rhs.empty()) {
        parse_error(line_no, raw, "missing Nets count");
      }
      num_nets = parse_int_token(rhs, line_no, raw);
      if (num_nets < 0) {
        parse_error(line_no, raw, "negative Nets count");
      }
      p.nets.reserve(static_cast<size_t>(num_nets));
      has_nets = true;
      section = NETS;
      continue;
    }

    if (section == NONE) {
      parse_error(line_no, raw, "data line before any section header");
    }

    if (section == BLOCKS) {
      if (lhs.empty()) {
        parse_error(line_no, raw, "missing block name");
      }
      std::vector<std::string> toks = tokenize_ws(rhs);
      if (toks.size() != 2) {
        parse_error(line_no, raw, "block needs <w> <h>");
      }
      Block b;
      b.name = lhs;
      b.w = parse_int_token(toks[0], line_no, raw);
      b.h = parse_int_token(toks[1], line_no, raw);
      int id = static_cast<int>(p.blocks.size());
      p.block_id_of[b.name] = id;
      p.blocks.push_back(b);
      continue;
    }

    if (section == PINS) {
      if (lhs.empty()) {
        parse_error(line_no, raw, "missing pin name");
      }
      std::vector<std::string> toks = tokenize_ws(rhs);
      if (toks.size() != 2) {
        parse_error(line_no, raw, "pin needs <dx> <dy>");
      }
      int block_id = longest_prefix_block(lhs, p.block_id_of);
      if (block_id < 0) {
        parse_error(line_no, raw, "pin name does not match any block prefix");
      }
      Pin pin;
      pin.name = lhs;
      pin.block_id = block_id;
      pin.dx = parse_double_token(toks[0], line_no, raw);
      pin.dy = parse_double_token(toks[1], line_no, raw);
      int id = static_cast<int>(p.pins.size());
      p.pin_id_of[pin.name] = id;
      p.pins.push_back(pin);
      p.blocks[static_cast<size_t>(block_id)].pin_ids.push_back(id);
      continue;
    }

    if (section == NETS) {
      if (lhs.empty()) {
        parse_error(line_no, raw, "missing net name");
      }
      std::vector<std::string> toks = tokenize_ws(rhs);
      if (toks.size() < 2) {
        parse_error(line_no, raw, "net needs at least 2 pins");
      }
      Net net;
      net.name = lhs;
      std::unordered_set<int> block_seen;
      std::unordered_set<int> pin_seen_for_net;
      for (const std::string &pin_name : toks) {
        auto it = p.pin_id_of.find(pin_name);
        if (it == p.pin_id_of.end()) {
          parse_error(line_no, raw, "net references unknown pin");
        }
        int pin_id = it->second;
        net.pin_ids.push_back(pin_id);
        int block_id = p.pins[static_cast<size_t>(pin_id)].block_id;
        if (block_seen.insert(block_id).second) {
          net.block_ids.push_back(block_id);
        }
        if (pin_seen_for_net.insert(pin_id).second) {
          p.pins[static_cast<size_t>(pin_id)].net_ids.push_back(
              static_cast<int>(p.nets.size()));
        }
      }
      int net_id = static_cast<int>(p.nets.size());
      p.net_id_of[net.name] = net_id;
      p.nets.push_back(net);
      for (int block_id : p.nets[static_cast<size_t>(net_id)].block_ids) {
        p.blocks[static_cast<size_t>(block_id)].net_ids.push_back(net_id);
      }
      continue;
    }
  }

  if (!has_chip || !has_blocks || !has_pins || !has_nets) {
    throw std::runtime_error("line 0: <EOF> | missing required headers");
  }

  if (num_blocks >= 0 && static_cast<int>(p.blocks.size()) != num_blocks) {
    throw std::runtime_error("line 0: <EOF> | Blocks count mismatch");
  }
  if (num_pins >= 0 && static_cast<int>(p.pins.size()) != num_pins) {
    throw std::runtime_error("line 0: <EOF> | Pins count mismatch");
  }
  if (num_nets >= 0 && static_cast<int>(p.nets.size()) != num_nets) {
    throw std::runtime_error("line 0: <EOF> | Nets count mismatch");
  }

  return p;
}

void dump_problem(const Problem &p, std::ostream &os) {
  os << "Blocks:\n";
  for (const Block &b : p.blocks) {
    os << "Block " << b.name << ":";
    for (int pid : b.pin_ids) {
      os << " " << p.pins[static_cast<size_t>(pid)].name;
    }
    os << "\n";
  }
  os << "Nets:\n";
  for (const Net &n : p.nets) {
    os << "Net " << n.name << ":";
    for (int pid : n.pin_ids) {
      os << " " << p.pins[static_cast<size_t>(pid)].name;
    }
    os << "\n";
  }
}
