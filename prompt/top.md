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