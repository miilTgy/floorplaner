#include "writer.h"

#include <cmath>
#include <iomanip>

namespace {

constexpr double kIntEps = 1e-9;

void validate_solution_input(const Problem &P, const FloorplanResult &fp) {
  const size_t n = P.blocks.size();
  if (fp.x.size() != n || fp.y.size() != n || fp.rotate.size() != n) {
    throw std::runtime_error("writer: floorplan vector sizes do not match block count");
  }

  for (size_t i = 0; i < n; ++i) {
    if (fp.rotate[i] != 0 && fp.rotate[i] != 1) {
      throw std::runtime_error("writer: invalid rotate value at block_id=" +
                               std::to_string(i));
    }
    if (!std::isfinite(fp.x[i]) || !std::isfinite(fp.y[i])) {
      throw std::runtime_error("writer: non-finite coordinate at block_id=" +
                               std::to_string(i));
    }
  }
}

std::string format_coord(double v) {
  const double rounded = std::round(v);
  if (std::abs(v - rounded) <= kIntEps) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(0) << rounded;
    std::string s = oss.str();
    return (s == "-0") ? "0" : s;
  }

  std::ostringstream oss;
  oss << std::fixed << std::setprecision(6) << v;
  std::string s = oss.str();

  while (!s.empty() && s.back() == '0') {
    s.pop_back();
  }
  if (!s.empty() && s.back() == '.') {
    s.pop_back();
  }
  if (s.empty() || s == "-0") {
    return "0";
  }
  return s;
}

}  // namespace

void write_solution_stream(const Problem &P, const FloorplanResult &fp, std::ostream &os) {
  validate_solution_input(P, fp);

  const size_t n = P.blocks.size();
  for (size_t i = 0; i < n; ++i) {
    os << P.blocks[i].name << " : " << format_coord(fp.x[i]) << " " << format_coord(fp.y[i])
       << " " << fp.rotate[i] << "\n";
  }
}

void write_solution(const Problem &P, const FloorplanResult &fp,
                    const std::string &output_path) {
  std::ofstream fout(output_path.c_str());
  if (!fout.is_open()) {
    throw std::runtime_error("writer: failed to open output file: " + output_path);
  }

  write_solution_stream(P, fp, fout);

  if (!fout) {
    throw std::runtime_error("writer: failed to write output file: " + output_path);
  }
}

std::string solution_to_string(const Problem &P, const FloorplanResult &fp) {
  std::ostringstream oss;
  write_solution_stream(P, fp, oss);
  return oss.str();
}
