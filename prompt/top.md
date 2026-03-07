你是一个资深 C++ 工程师。现在我的工程里已经有两个**无副作用**模块：

1) Parser 模块  
- API：`Problem parse_problem(const std::string& path);`  
- 可选：`void dump_problem(const Problem&, std::ostream&);`  
- 数据结构在 `parser.h`（或等价头文件）里定义：`Problem/Block/Net/Pin`

2) Orderer 模块（cluster growth / linear ordering）  
- API：`std::vector<int> build_initial_ordering(const Problem& P);`  
- 只依赖：
  - `P.blocks[i].net_ids`
  - `P.nets[k].block_ids`
- 同样**无副作用**（不写文件，不默认打印）

请你只实现**顶层 main 程序**和**顶层 Makefile**，把 parser + orderer 串起来跑通，并为后续接 BL/SA 预留接口。

---

# 约束与目标
- 最终可执行文件名：`floorplan`
- 运行方式（必须匹配作业接口）：
  - `./floorplan <input.txt> <timeLimit> [--debug]`
- 目前阶段只完成：
  - 解析输入（parser）
  - 构建初始 ordering（orderer）
  - 打印一行简短 summary
- 本阶段**不实现**：
  - BL/skyline 解码
  - SA
  - 生成 `<sample>_solution.txt`
- 程序默认安静；仅在 `--debug` 或环境变量 `DEBUG=1` 时输出详细信息：
  - 调用 `dump_problem(P, std::cout)`（如果存在）
  - 输出 ordering 的结果（block 名序列）

---

# 你需要产出哪些文件
## 1) `src/main.cc`
实现 `int main(int argc, char** argv)`，要求：

### CLI 参数解析
- 必须支持：
  - 位置参数 1：`input.txt`
  - 位置参数 2：`timeLimit`（double 或 int 都行，但必须能解析）
  - 可选参数：`--debug`
- 参数不合法时打印 usage 并返回非 0：
  - `Usage: ./floorplan <input.txt> <timeLimit> [--debug]`

### 主流程（本阶段）
1. `Problem P = parse_problem(input_path);`
2. `std::vector<int> perm = build_initial_ordering(P);`
3. 若 debug 开启：
   - 如果项目里有 `dump_problem`：调用 `dump_problem(P, std::cout);`
   - 打印 ordering（按 block 名）：
     - 例如：
       - `Ordering (by block name): A B D E C`
     - 要用 `P.blocks[id].name` 从 id 转名字
   - 可额外打印 seed（perm[0]）的信息（可选）
4. 无论是否 debug，都打印一行简短 summary（便于脚本检查）：
   - `Parsed OK: W=..., blocks=..., pins=..., nets=..., ordering_len=...`

### 重要限制
- main 不得生成任何额外文件（不要写 parser.out）
- 本阶段不生成 solution 文件

### 为后续预留接口（只留 TODO/stub，不实现）
在 main.cc 中用清晰注释留好位置（不要 include 未实现文件导致编译失败）：
- `// TODO: Placement pl = decode_BL(P, perm, ...);`
- `// TODO: State best = run_SA(P, init_state, timeLimit);`
- `// TODO: write_solution(...);`

---

## 2) 顶层 `Makefile`
必须做到：

### 目标
- `make` 或 `make all`：构建 `floorplan`
- `make floorplan`：同上
- `make clean`：清理编译产物
- `make run INPUT=samples/sample_1.txt T=1`：
  - 运行：`./floorplan $(INPUT) $(T) --debug`

### 目录与构建约定
- 源码：`src/`
- 头文件：`include/`
- 编译产物：
  - 对象文件：`build/`
  - 最终可执行：`bin/floorplan`
- 自动收集 `src/*.cc`（或 `src/*.cpp`），不要手写文件列表（用 wildcard + pattern rules）
- 编译参数：
  - `-std=c++17 -O2 -Wall -Wextra -pedantic`
- include 路径：
  - `-Iinclude`
- 链接完成后在项目根目录放一个便捷入口（任选一种）：
  - `ln -sf bin/floorplan floorplan`

### 关键点
- Makefile 不要依赖动态库
- 不要生成额外可执行（比如 parser.out）

---

# 你需要输出的内容
只输出 `src/main.cc` 和 `Makefile` 的完整内容，不要输出其他文件，不要额外解释。

---

# 在保持以上所有原始要求不变的前提下，新增以下补充要求（加入 planer 模块）

现在工程里已经新增第三个**无副作用**模块：

3) Planer 模块（initial floorplan planner）  
- API：`FloorplanResult build_initial_floorplan(const Problem& P, const std::vector<int>& perm);`
- 可选：`void dump_init_planer_debug(std::ostream& os);`
- 该模块负责：
  - 根据 `Problem + perm` 构建一个合法的初始 floorplan
  - 输出每个 block 的 `(x, y, rotate)`
  - 返回 `H / hpwl / cost`
- `FloorplanResult` 至少包含：
  - `std::vector<double> x`
  - `std::vector<double> y`
  - `std::vector<int> rotate`
  - `double H`
  - `double hpwl`
  - `double cost`

⚠️ 请注意：
- 以上原始提示词的内容不要删除或改写，只是在实现 main 和 Makefile 时，把 planer 串接进去
- 当前阶段仍然**不实现 SA**
- 仍然**不写 `<sample>_solution.txt`**
- 只是把 `parser + orderer + planer` 串起来，让程序能输出一个合法初始 floorplan 的 summary 和 debug 信息

---

## 对 `src/main.cc` 的新增要求（在原有要求基础上追加）

### 主流程（更新后）
1. `Problem P = parse_problem(input_path);`
2. `std::vector<int> perm = build_initial_ordering(P);`
3. `FloorplanResult fp = build_initial_floorplan(P, perm);`
4. 若 debug 开启：
   - 如果项目里有 `dump_problem`：调用 `dump_problem(P, std::cout);`
   - 打印 ordering（按 block 名）：
     - `Ordering (by block name): ...`
   - 如果项目里有 `dump_ordering_debug`：调用它（可选）
   - 如果项目里有 `dump_init_planer_debug`：调用它（可选）
   - 额外打印初始 floorplan 的简短信息，例如：
     - `Initial floorplan: H=..., hpwl=..., cost=...`
5. 无论是否 debug，都打印一行简短 summary（便于脚本检查）：
   - `Parsed OK: W=..., blocks=..., pins=..., nets=..., ordering_len=..., H=..., hpwl=..., cost=...`

### 重要限制（补充）
- main 仍然不得生成任何额外文件
- main 仍然不生成最终 solution 文件
- 但 main 现在必须真实调用 planer，得到一个完整初始 floorplan

### 为后续预留接口（更新后）
请把原有 TODO 改成与 planer 一致的风格，只留注释，不要 include 未实现文件导致编译失败。例如：
- `// TODO: Improve fp with local search / SA within timeLimit`
- `// TODO: Convert improved floorplan to final solution output`
- `// TODO: write_solution(...);`

---

## 对 `Makefile` 的新增要求（在原有要求基础上追加）

- `make` / `make all` / `make floorplan` 必须把 parser + orderer + planer + main 一起编译链接成最终 `floorplan`
- `make run INPUT=samples/sample_1.txt T=1` 仍然运行：
  - `./floorplan $(INPUT) $(T) --debug`
- 不需要新增其他 target 名称
- 不要把 planer 做成独立可执行文件
- 仍然不要生成任何额外测试可执行文件（比如 parser.out / orderer.out / planer.out）

---

## 你实现 main 时必须满足的兼容性要求

- 如果 `dump_problem` 存在，就调用
- 如果 `dump_ordering_debug` 存在，就调用
- 如果 `dump_init_planer_debug` 存在，就调用
- 如果某些 dump 函数在头文件中没有声明，不要强行调用导致编译错误；请以当前工程里真实存在的声明为准
- 也就是说：main 的 include 和调用要与当前工程的真实头文件保持一致，不能假设不存在的符号

---

## 你现在最终要做的事情
在**不改变上面原始提示词文字内容**的前提下，实现一个更新后的：

- `src/main.cc`
- `Makefile`

使其能够：
- 读取输入
- 调 parser
- 调 orderer
- 调 planer
- 打印 summary
- 在 debug 下打印 problem / ordering / planer 的 debug 信息
- 为后续 SA 和写最终 solution 文件预留 TODO 注释