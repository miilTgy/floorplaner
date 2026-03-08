# init_fp_bstar.md

你是一个资深 C++17 工程师。现在我的工程中已经存在并且默认不要修改公共 API 的文件有：

- `common.h`
- `init_planer.h`
- `bstar_tree2fp.h`
- `bstar_tree.h`
- `parser.h / parser.cc`
- `orderer.h / orderer2.cc`
- `writer.h / writer.cc`

请阅读这些文件中的定义，尤其是：

- `Problem`
- `FloorplanItem`
- `FloorplanResult`
- `BStarNode`
- `BStarTree`
- `bstar_tree2fp(...)` 的接口
- `init_planer.h` 中已有声明（并按本提示词新增结果结构/新入口）

然后实现以下改动：

- 在 `init_planer.h` 新增结果结构与新入口声明
- 在 `init_fp_bstar.cc` 实现相关逻辑

要求：
- 除 `init_planer.h` 外，不修改任何已有头文件的公共接口
- 允许在 `init_planer.h` 中新增结果结构与新入口 API
- `init_fp_bstar.cc` 中实现 `init_planer.h` 声明的全部相关函数
- 内部可以定义任意私有 helper / 私有结构

---

## 1. 目标

实现一个基于 **horizontal B\*-tree** 的初始 floorplan 构造器。

思路：

1. 按 `perm` 顺序处理 block
2. `perm[0]` 对应的 block 作为初始根节点
3. 对于后续每个 block：
   - 枚举当前树中所有**可插入位置**
   - 尝试把该 block 插入到这些位置
   - 对每个候选树调用 `bstar_tree2fp(...)`
   - 得到候选 `FloorplanResult`
   - 计算 cost
   - 选择 cost 最小的候选
4. 新入口返回最优初始结果对象（包含 `tree/rotate/fp/cost`），并保留旧入口返回 `FloorplanResult` 兼容调用方

---

## 2. 必须实现的对外类型与函数

你必须在 `init_planer.h` 中新增如下公共类型与新入口，并在 `init_fp_bstar.cc` 实现对应函数：

```cpp
struct InitBStarResult {
    BStarTree tree;
    std::vector<int> rotate;
    FloorplanResult fp;
    double cost;
};

InitBStarResult build_initial_bstar_result(const Problem& P, const std::vector<int>& perm);
FloorplanResult build_initial_floorplan(const Problem& P, const std::vector<int>& perm);
void dump_init_planer_debug(std::ostream& os);
void clear_init_planer_debug();
```

说明：

- 现有 `build_initial_floorplan(...)` / debug API 必须保留并可正常工作
- 兼容行为必须明确：`build_initial_floorplan(...)` 作为 wrapper，内部调用 `build_initial_bstar_result(...)` 并返回 `result.fp`
- 允许并建议修改 `main.cc`，使 SA 主链路直接使用 `build_initial_bstar_result(...)`
- 最终输出至少包含 `init_planer.h` 与 `init_fp_bstar.cc`

---

## 2.1 禁止项（硬约束）

- 禁止任何 `floorplan -> B*-tree` 路径、建议或备选方案。
- SA 初始化必须使用 `build_initial_bstar_result(...)` 返回的 `tree` 与 `rotate`。
- 若实现中缺少树状态，应视为流程设计错误，而不是允许补救路径。

---

## 3. B\*-tree 构造规则

必须使用 **horizontal B\*-tree**：

- 左孩子表示放在父节点右边
- 右孩子表示与父节点左边界对齐、放在其“上方链”中
- 具体摆放坐标由 `bstar_tree2fp(...)` 解码决定

这里 `init_fp_bstar` 的职责不是自己算几何坐标，而是：

- 维护树结构
- 调用 `bstar_tree2fp(...)`
- 基于返回的 floorplan 比较 cost

---

## 4. 插入策略

### 4.1 初始根节点

对 `perm[0]`：

- 至少尝试 `rotate = 0 / 1`
- 用两者中 cost 更小的作为初始树
- 初始树只有一个节点

### 4.2 后续节点插入

对每个 `perm[k]`（`k >= 1`）：

枚举：

- `rotate = 0 / 1`
- 当前树中每个已有节点 `u`
- 若 `u.left == nullptr`，可将新节点插为 `u.left`
- 若 `u.right == nullptr`，可将新节点插为 `u.right`

也就是说，**第一版只枚举空 left / 空 right 位置**，不要实现更复杂的 reattach / edge insertion / subtree rotation。

### 4.3 候选评估

对每个候选插入方案：

1. 复制当前树
2. 插入新节点
3. 构造对应的 `rotate` 向量
4. 调用 `bstar_tree2fp(...)`
5. 若解码失败、抛异常、floorplan 非法（包含宽度超限等解码器硬约束），则该候选直接丢弃
6. 对合法候选计算 cost
7. 记录当前最优候选

若某一轮所有候选都非法，则抛出清晰的 `std::runtime_error`

---

## 5. cost 定义

统一口径：候选比较与最终返回都使用同一 cost 定义：

- `numNets = P.nets.size()`
- `cost = fp.H + fp.hpwl / numNets`
- 当 `numNets == 0` 时退化为 `cost = fp.H`

芯片宽度合法性由 `bstar_tree2fp(...)` 作为硬约束负责；planner 通过捕获解码异常过滤非法候选。

不要在这一版强制加入面积加权或 SA。

---

## 6. 对 `bstar_tree2fp(...)` 的调用要求

你必须复用已有的 `bstar_tree2fp(...)` 模块，而不是在 `init_fp_bstar.cc` 中重复实现摆放逻辑。

也就是说：

- `init_fp_bstar` 只负责“生成候选树”
- `bstar_tree2fp(...)` 负责“树 -> floorplan”
- `init_fp_bstar` 再根据 floorplan 的 `cost` 比较优劣

---

## 7. 建议的内部数据结构

你可以在 `.cc` 中定义私有结构，例如：

```cpp
struct CandidatePlacement {
    BStarTree tree;
    std::vector<int> rotate;
    FloorplanResult fp;
    double cost = std::numeric_limits<double>::infinity();
    bool valid = false;
};
```

还可以定义例如：

```cpp
struct TreeCursor {
    int parent_block_id;
    bool as_left;
};
```

或者其他你认为更方便的结构。

要求：

- 内部表示清晰
- 不要改 `BStarNode / BStarTree` 的公共定义

---

## 8. 树操作要求

请实现清晰、稳健的树复制与插入 helper，例如：

- 复制整棵 `BStarTree`
- 根据 block_id 找到对应节点
- 给某个节点补一个 left child
- 给某个节点补一个 right child

建议至少有这些语义的 helper：

```cpp
BStarTree make_single_node_tree(int block_id);
BStarTree clone_tree(const BStarTree& src);
std::vector<BStarNode*> collect_nodes_preorder(BStarTree& tree);
void attach_left(BStarNode* parent, BStarNode* child);
void attach_right(BStarNode* parent, BStarNode* child);
double eval_cost_from_tree(const Problem& P, const BStarTree& tree, const std::vector<int>& rotate, FloorplanResult& out_fp);
```

如果已有 `BStarTree.nodes` 是稳定存储，请在其基础上实现，避免悬空指针。

---

## 9. 旋转处理要求

必须支持旋转枚举：

- 对每个待插入 block，尝试 `rotate = 0` 和 `rotate = 1`
- 当前已插入 block 的旋转状态沿用当前最优树中的状态
- 候选解码时将完整 `rotate` 向量传给 `bstar_tree2fp(...)`

要求：

- `rotate.size() == P.blocks.size()`
- 未插入 block 的 `rotate` 可以临时设为 `0`
- 更稳妥的做法是只让 `bstar_tree2fp(...)` 使用树中实际出现的 block
- 若某个 block 尚未插入，不应影响已插入节点的解码

## 9.1 芯片宽度合法性检查

芯片宽度检查由 `bstar_tree2fp(...)` 统一负责（硬约束）：

- 解码器内部检查 `layout_width <= P.chipW`
- 若超宽，`bstar_tree2fp(...)` 抛异常
- `init_fp_bstar` 通过异常感知并丢弃该候选

`init_fp_bstar` 不需要再单独实现一套本地宽度判定逻辑。

---

## 10. 非法候选处理

以下情况的候选必须直接丢弃，不参与最优比较：

- `bstar_tree2fp(...)` 抛异常（包含宽度超限等硬约束失败）
- 返回的 `FloorplanResult` 中 block 数不一致
- 出现重叠
- `H` 非法（NaN / inf / 负数）
- 输出为空
- 插入后树结构异常（例如重复 block）
- `cost` 非法（NaN / inf）

如果一轮所有候选都非法：

- 输出 debug 信息
- 抛出异常，说明第几个 `perm[k]` 插入失败

---

## 11. Debug 输出要求

保留 `init_planer.h` 风格的 debug 接口：

```cpp
void dump_init_planer_debug(std::ostream& os);
void clear_init_planer_debug();
```

请在 `.cc` 内部维护一个静态 debug 文本缓冲区，记录关键过程，便于排查。

建议记录：

- `perm`
- 每一步正在插入的 `block_id`
- 当前树中可插入位置数量
- 每个候选：
  - `parent_block_id`
  - `as_left / as_right`
  - `rotate`
  - `H`
  - `hpwl`
  - `cost`
  - 是否解码成功
- 最终选择的插入位置
- 最终树节点数
- 最终 `H`
- 最终 `hpwl`
- 最终 `cost`

输出格式要求简单清楚，纯文本即可。

### 11.1 额外要求：将当前 B\*-tree 输出到文本文件

除了 `dump_init_planer_debug(std::ostream& os)` 之外，还需要在 `init_fp_bstar.cc` 内部提供一个私有 helper，
将“当前最优树”输出到文本文件，便于后续可视化和排查。

输出格式参考 `bstar.md`，每行一个节点，字段顺序固定为：

    node_id left_child_id right_child_id x y w h rotate

说明：

- `node_id`：当前节点对应的 `block_id`
- `left_child_id`：左孩子的 `block_id`，若为空则输出 `-1`
- `right_child_id`：右孩子的 `block_id`，若为空则输出 `-1`
- `x / y`：该 block 在当前 floorplan 中的位置
- `w / h`：该 block 在当前旋转状态下的实际尺寸，即 `w_used / h_used`
- `rotate`：该 block 的旋转状态，取 `0` 或 `1`

要求：

1. 输出顺序使用 **DFS preorder**：
   - 当前节点
   - 左子树
   - 右子树

2. 输出必须 deterministic：
   - 相同输入下，生成的树文本必须一致

3. 该文本文件用于后续 Python 可视化脚本读取，因此格式必须简洁、稳定、无额外注释

4. 建议提供私有 helper，例如：

```cpp
void dump_bstar_tree_text(
    const BStarTree& tree,
    const FloorplanResult& fp,
    const std::vector<int>& rotate,
    const std::string& filename);
```

5. 建议在 `build_initial_floorplan(...)` 成功完成后，将最终最优树输出到一个文本文件中，例如：
   - `init_fp_bstar_tree.txt`
   或者其他清晰的默认文件名

6. 若某节点在树中存在，但在 `FloorplanResult` 中查不到对应位置/尺寸信息，应抛出异常，而不是输出错误内容

---

## 12. 实现细节建议

### 12.1 根节点初始化

第一步建议同时尝试：

- 根节点 `rotate=0`
- 根节点 `rotate=1`

分别调用 `bstar_tree2fp(...)`，选 cost 更小者。

### 12.2 候选遍历顺序

为了 deterministic，建议：

- 按当前树的 preorder 顺序收集已有节点
- 对每个节点先试 left，再试 right
- 对旋转先试 0，再试 1

### 12.3 tie-break

若多个合法候选 `cost` 完全相同，则按以下顺序打破平局：

1. preorder 中更早出现的 parent
2. 先 left 再 right
3. 先 `rotate=0` 再 `rotate=1`

这样保证结果稳定。

---

## 13. 最终返回结果要求

`build_initial_bstar_result(...)` 最终返回：

- `InitBStarResult`，其中包含：
  - `tree`：当前最优完整 B\*-tree
  - `rotate`：与 `P.blocks` 对齐的旋转向量
  - `fp`：`tree` 解码得到的 `FloorplanResult`
  - `cost`：与 `fp.cost` 一致

`build_initial_floorplan(...)` 兼容返回：

- `build_initial_bstar_result(...).fp`

要求：

- `result.fp.items.size() == P.blocks.size()`
- 所有 block 都已放置
- `result.cost == result.fp.cost`
- `fp.cost` 必须等于：
  ```cpp
  fp.H + fp.hpwl / numNets
  ```
  其中 `numNets = P.nets.size()`，在 `numNets == 0` 时退化为 `fp.H`
- 返回前必须确保 `fp.cost` 已正确填写

---

## 14. 代码风格要求

- 使用 C++17
- 输出完整可编译代码，不要伪代码
- 除 `init_planer.h` 的新增声明外，不修改其他公共头文件
- 正确性优先于性能
- 第一版允许对每个候选都完整复制树并重新解码，不必做增量优化
- 注释简洁明确，重点解释：
  - 候选插入位置如何枚举
  - 为什么第一版只枚举空 left / 空 right
  - cost 使用 `H + HPWL / numNets`
  - 非法候选如何过滤

---

## 15. 最终要求

请直接输出完整可编译的：

- `init_planer.h`
- `init_fp_bstar.cc`

要求它能够：

- 按 `perm` 顺序增量构造 B*-tree
- 每一步枚举所有空 left / 空 right 插入位置
- 对每个候选调用 `bstar_tree2fp(...)`
- 仅接受 `bstar_tree2fp(...)` 成功解码的候选（宽度约束由解码器保证）
- 在合法候选中按 `numNets = P.nets.size()` 与 `cost = fp.H + fp.hpwl / numNets`（`numNets==0` 时为 `fp.H`）选择最优插入
- 新入口返回最终合法且满足芯片宽度约束的 `InitBStarResult`
- 旧入口 `build_initial_floorplan(...)` 兼容返回 `FloorplanResult`
- 禁止出现任何 `floorplan -> B*-tree` 路径
- 支持 `dump_init_planer_debug(...)` 和 `clear_init_planer_debug()`
- 在成功构造后将最终最优树输出到文本文件
