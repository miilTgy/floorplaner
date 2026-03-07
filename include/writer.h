#pragma once

#include "common.h"

void write_solution(const Problem &P, const FloorplanResult &fp,
                    const std::string &output_path);
void write_solution_stream(const Problem &P, const FloorplanResult &fp, std::ostream &os);
std::string solution_to_string(const Problem &P, const FloorplanResult &fp);
