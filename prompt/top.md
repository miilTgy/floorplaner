你是一个资深 C++ 工程师。现在我已经有一个 **无副作用**的 Parser 模块（`Problem parse_problem(const std::string& path)`，以及可选 `dump_problem(const Problem&, std::ostream&)`），请你只实现**顶层 main 程序**和**顶层 Makefile**，用于把 parser 跑起来、方便本地测试，并为后续接 ordering/BL/SA 预留接口。

# 约束与目标
- 最终可执行文件名：`floorplan`
- 运行方式（必须匹配作业接口）：
  - `./floorplan <input.txt> <timeLimit>`
- 目前阶段：只需要验证 parser 正确，不做 ordering/BL/SA，不写 solution 文件。
- 但要提前把“未来的流水线接口”留好：`build_initial_ordering(...)`、`decode_BL(...)`、`run_SA(...)` 等可以先用 TODO/stub 占位，不要实现逻辑。
- 程序默认不打印大量内容；仅在 `--debug` 或环境变量 `DEBUG=1` 时调用 `dump_problem(...)` 打印解析结果。

# 你需要产出哪些文件
1) `src/main.cc`：实现 `int main(int argc, char** argv)`，完成：
   - 解析命令行参数：严格要求 2 个位置参数：
     - `argv[1]`: input path
     - `argv[2]`: timeLimit（double 或 int 都行，但要能解析）
   - 参数不合法时打印 usage 并返回非 0：
     - `Usage: ./floorplan <input.txt> <timeLimit> [--debug]`
   - 调用 `parse_problem(input)` 得到 `Problem P`
   - 若 `--debug` 存在，则 `dump_problem(P, std::cout)`
   - 目前阶段不生成 `<sample>_solution.txt`，但要打印一行简短信息到 stdout：
     - 例如：`Parsed OK: W=..., blocks=..., pins=..., nets=...`
   - 注意：不要在 main 里写任何 parser.out 之类文件。

2) 顶层 `Makefile`：必须做到
   - `make` 或 `make all`：构建 `floorplan`
   - `make floorplan`：同上
   - `make clean`：清理编译产物
   - `make run INPUT=samples/sample_1.txt T=1`：
     - 运行：`./floorplan $(INPUT) $(T) --debug`
   - 目录结构约定：
     - 源码在 `src/`
     - 头文件在 `include/`
     - 编译产物输出到 `build/`（对象文件）和 `bin/`（最终可执行）
   - 目标文件：自动收集 `src/*.cc`（或 `src/*.cpp`），不手写每个文件名（用通配或模式规则）
   - 编译参数：
     - `-std=c++17 -O2 -Wall -Wextra -pedantic`
   - include 路径：
     - `-Iinclude`
   - 最终可执行路径：
     - `bin/floorplan`
   - 同时提供一个便捷软链接或拷贝到项目根目录的 `./floorplan`（二选一）：
     - 例如在链接完成后执行：`ln -sf bin/floorplan floorplan`

# 重要细节
- 不要实现 ordering/BL/SA 逻辑，但请在 main.cc 留好清晰的函数调用位置（注释 + TODO），以便之后直接插入：
  - `State init = build_initial_ordering(P);`
  - `Placement pl = decode_BL(P, init);`
  - `State best = run_SA(P, init, timeLimit);`
  - `write_solution(...)`
- 目前 main 只做 parse + 基本信息输出 + 可选 dump。
- Makefile 里不要依赖动态库，不要生成额外可执行（比如 parser.out）。

# 你需要输出的内容
只输出 `src/main.cc` 和 `Makefile` 的完整内容，不要输出其他文件，不要额外解释。