你是一个资深 C++ 工程师。现在我已经有 Parser 模块输出的 `Problem` 数据结构（见 parser.h），请你实现 **ordering 模块**：`orderer.cc`（以及必要时的 `orderer.h`），用于根据 PPT 的 cluster growth / linear ordering 思路生成一个 block 的初始排列 `perm`。

⚠️ 本项目输入保证：**每一条 net 恰好连接 2 个 pin（也就是 2 个 block）**，不存在 3 端及以上的 net。  
注意：同一个 pin/节点可以出现在多条 net 里（例如同一个 pin 上有 net1/net2/net3），这不需要特殊处理：每条 net 仍然是一条独立的边，按条数计入统计。

⚠️ 约束：
- 不要实现 BL/skyline/SA，不要输出 solution 文件，不要写 main，不要改 parser。
- ordering 模块必须无副作用（不写文件）；默认不打印，**仅 debug 模式输出详细信息**。
- 输出必须 deterministic（不使用随机数；并列用固定规则打破）。

---

## 1) 与 parser 的数据结构对齐（必须遵守）
ordering 只依赖：
- `P.blocks[i].net_ids`：block i 参与的 net 列表（block 粒度、不重复）
- `P.nets[k].block_ids`：net k 连接的 block 列表（长度恒为 2，且已去重）

不要依赖 pin 级别信息；不要依赖字符串查找（name->id 映射）。

---

## 2) 输出接口
实现函数：
- `std::vector<int> build_initial_ordering(const Problem& P);`

返回 `perm`：
- 长度 = `P.blocks.size()`
- 元素是 block_id（0..n-1），每个 block 恰好出现一次

---

## 3) Debug 模式开关（必须实现，且不改变 main）
你不能改 main，因此 ordering 模块内部需要自己判断是否开启 debug 输出。请实现如下机制（二选一或两者都支持）：

- 运行时开关（推荐）：若环境变量 `DEBUG` 存在且值不为 "0"，则 debug=true  
  - `const bool debug = (std::getenv("DEBUG") && std::string(std::getenv("DEBUG")) != "0");`
- 编译时开关（可选）：若定义了宏 `ORDERER_DEBUG` 则 debug=true

debug 输出统一写到 `std::cerr`（避免污染正常 stdout）。

---

## 4) 算法总览（必须按此实现）
采用 “seed + cluster growth”：

### Step A：选择 seed（initial block）
选择参与 net 数最多的 block：
- 遍历 i=0..n-1：
  - `net_count(i) = P.blocks[i].net_ids.size()`
- 取 net_count 最大的 i 作为 seed

tie-break（按顺序）：
1. net_count 更大者优先
2. 若并列，面积 `w*h` 更大者优先（用 long long 计算防溢出）
3. 再并列，block_id 更小者优先

把 seed 放入 perm[0]，并设置 `used[seed]=true`。

✅ **Debug 输出要求（seed 阶段）**  
若 debug=true，在选择 seed 前后必须打印：
- 一行 header：`[ORDERER] Seed selection`
- 对每个 block i（按 i 从小到大）打印一行，包含：
  - `i`, `name`, `net_count`, `area`, 是否为当前最佳(best)
  - 示例格式（你可微调，但信息必须齐全且可读）：
    - `[ORDERER] seed-cand id=3 name=D net_count=5 area=279864 best=YES`
- 最后一行打印最终 seed：
  - `[ORDERER] chosen-seed id=... name=... net_count=... area=...`

---

### Step B：从第二个 block 开始，每一步选 Gain 最大的 block
循环直到 perm 满：
- 对每个未选 block m（used[m]==false），计算：
  - `terminating(m)`: m 的 net_ids 中，满足 `in_count[k] == 1` 的 net 数
  - `new(m)`: m 的 net_ids 中，满足 `in_count[k] == 0` 的 net 数
  - `gain(m) = terminating(m) - new(m)`

选择 gain 最大的 m 加入 perm。

tie-break（按顺序）：
1. gain 更大者优先
2. 若并列，terminating(m) 更大者优先
3. 若并列，new(m) 更小者优先
4. 再并列，block_id 更小者优先（确定性）

说明：n 不大，允许每轮对所有未选 m 做朴素遍历计算，不需要优先队列。

✅ **Debug 输出要求（每一轮加入 block）**  
若 debug=true，对于每一轮 t（从 1 开始，对应 perm 的位置 index=t）记录如下结构体：

- `struct SeedCand { int id; std::string name; int net_count; long long area; bool chosen; };`
- `struct IterCand { int id; std::string name; int term; int new_; int gain; bool chosen; };`
- `struct IterTrace { int iter; std::vector<IterCand> cands; };`
- `struct OrderingTrace { std::vector<SeedCand> seed_cands; std::vector<IterTrace> iters; std::vector<int> perm; };`

并提供一个 debug 的 API `void dump_ordering_trace(const OrderingTrace& tr, std::ostream& os);`：
   - 按可读格式输出：
     - Seed selection：逐行打印每个 SeedCand
     - 每一轮 iter：逐行打印所有 IterCand（包含 chosen 标记）
   - 不访问全局变量，不依赖环境变量

具体要求：
1) 先打印一行轮次信息：
- `[ORDERER] iter=t placed_size=<当前已放数量>`

2) 然后打印“当轮所有候选 m 的 Gain 计算结果”（必须包含所有 used[m]==false 的 m，按 m 从小到大输出），每行至少包含：
- `m id`, `name`, `terminating`, `new`, `gain`
- 以及是否为当轮最佳(best=YES/NO)
- 示例格式：
  - `[ORDERER] cand id=7 name=H term=3 new=1 gain=2 best=YES`
  - `[ORDERER] cand id=2 name=C term=1 new=4 gain=-3 best=NO`

3) 轮末再打印一行当轮选择结果：
- `[ORDERER] pick id=... name=... term=... new=... gain=...`

---

## 5) 动态维护 in_count（核心，必须正确）
`in_count[k]` 表示：net k 的两个端点里，有多少个 block 已经在 placed set S 中。
因为每条 net 都是 2-block net，`in_count[k]` 的取值只会是 0/1/2，其中 2 表示 net 已完全落在 S 内。

初始化：
- `std::vector<int> in_count(P.nets.size(), 0);`

当你把一个 block b 加入 S 时：
- 遍历 `P.blocks[b].net_ids` 的每条 net k：
  - `in_count[k] += 1;`

（因为 block.net_ids 不重复，所以每条 net 对该 block 只会 +1 一次）

---

## 6) 边界情况
- 若某个 block 的 net_ids 为空：terminating=0, new=0, gain=0；它仍必须最终被选中（靠 tie-break 的 block_id 收敛）。
- 不使用随机数，输出必须 deterministic。
- debug 输出不应影响算法逻辑与返回值。

---

## 7) 产出文件与 include
- `orderer.h`：声明 `build_initial_ordering(const Problem&)`
- `orderer.cc`：实现该函数及必要 helper

include：
- `#include "common.h"`
- 可按需 include STL（vector, string, iostream, cstdlib 等），但不要 include main，不要依赖全局变量。

---

## 你需要输出什么
只输出 `orderer.h`（如有）和 `orderer.cc` 的内容，不要解释，不要输出其他文件。