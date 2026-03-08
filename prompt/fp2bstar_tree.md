你是一个资深 C++17 工程师。现在我的工程中已有以下模块和数据结构（不要修改它们）：

- `common.h` 中定义：
  - `struct Block`
  - `struct Pin`
  - `struct Net`
  - `struct Problem`
  - `struct FloorplanItem`
  - `struct FloorplanResult`
- `init_planer_admissible.cc`

请在工作目录及其所有子目录中搜寻这些数据结构，阅读它们的定义。

---

## 1. 模块目标

新增模块 `fp2bstar_tree.cc` / `fp2bstar_tree.h`，提供功能：

1. **输入**：
   - `const Problem &P`
   - `const FloorplanResult &fp`（已生成 admissible floorplan）
2. **输出**：
   - 对应的 **水平 B*-tree** 数据结构
3. **要求**：
   - 每个模块在 B*-tree 中节点包含：
     - `block_id`
     - 左子节点指针（可能为空）
     - 右子节点指针（可能为空）
   - 根节点 = bottom-left 模块；即在 admissible placement 中位于整体左下角的模块。若实现中需要从坐标选取，可按 `y` 最小、并列再按 `x` 最小确定。
   - `left child` = 在水平 B*-tree 定义中，对应 **右侧且相邻** 的模块
   - `right child` = 在水平 B*-tree 定义中，对应 **上方且相邻、x 相同，并满足论文中的上界约束** 的模块
   - 必须按论文定义构造 induced horizontal B*-tree，不得把“在右边/在上面”误写成“右侧相邻/上方相邻”的弱化版本
   - 保证 DFS 构造顺序
   - 不要删改我原有的 debug 输出逻辑，只能在必要时补充更详细的 debug 信息

---

## 2. 数据结构设计

### 2.1 树节点

    struct BStarNode {
        int block_id;
        BStarNode *left = nullptr;   // 左子节点：右侧且相邻的模块
        BStarNode *right = nullptr;  // 右子节点：上方且相邻、x 相同的模块
    };

### 2.2 树

    struct BStarTree {
        BStarNode *root = nullptr;
        std::vector<BStarNode> nodes; // 保存所有节点，保证节点内存稳定
    };

`nodes` 用于存储节点对象，DFS 构造时 `root` 指向其中一个元素。

---

## 3. 算法概述

### 3.1 选择根节点

- 在 `fp.items` 中找到：
  - `y` 最小的模块
  - 若并列，`x` 最小的模块
  - 若仍并列，可按 `block_id` 最小保证 deterministic
- 将其作为树根 `root`

### 3.2 寻找左子节点（左子树）

- 定义：模块 `B` 是模块 `A` 的左子节点，当且仅当：

  1. `B` 位于 `A` 的右侧且与 `A` 相邻（abut），即：
     - `x_B == x_A + w_A`
  2. `B` 与 `A` 在竖直方向上有实际接触区间：
     - `max(y_A, y_B) < min(y_A + h_A, y_B + h_B)`
  3. `B` 尚未被访问

- 在所有满足上述条件且尚未访问的模块中，选择其中 **`y` 最小（bottom-most）** 的那个作为左子节点
- 若 `y` 相同，再按 `x` 升序；若仍相同，再按 `block_id` 升序，保证 deterministic
- 左子节点只选一个
- 递归对左子节点构造其子树

### 3.3 寻找右子节点（右子树）

- 定义：模块 `B` 是模块 `A` 的右子节点，当且仅当：

  1. `B` 位于 `A` 的上方且与 `A` 相邻（abut），即：
     - `y_B == y_A + h_A`
  2. `B` 与 `A` 的 `x` 坐标相同：
     - `x_B == x_A`
  3. `B` 尚未被访问
  4. 先用严格几何条件寻找 `A` 的左侧相邻模块 `L``：
     - `x_L + w_L == x_A`
     - `L` 与 `A` 在竖直方向上有非空重叠区间
  5. 若左侧相邻模块候选数 `== 1`，则该唯一模块就是 `L`，并且 `B` 还必须满足论文中的右子约束：
     - `y_B < y_L + h_L`
  6. 若左侧相邻模块候选数 `== 0`，则说明 `A` 不存在左侧相邻模块，此时第 5 条约束不适用
  7. 若左侧相邻模块候选数 `> 1`，不要按任意一个 `L` 计算；这通常意味着几何判定过松、`eps` 过大或输入不满足论文假设。
     - 必须输出详细 debug 信息
     - 优先收紧判定条件而不是随意选一个 `L`
     - 若必须提供工程 fallback，则使用最保守约束：
       - `y_B < min_i (y_Li + h_Li)`

- 右子节点只选一个
- 若存在多个右子候选，按 `y` 升序 → `x` 升序 → `block_id` 升序选择，保证 deterministic
- 递归对右子节点构造其子树

### 3.4 DFS 构造

- 对每个节点，严格按论文中的 DFS 顺序构造：
  1. 先构造当前节点
  2. 先找左子节点 → 递归构造左子树
  3. 再找右子节点 → 递归构造右子树
- 遍历顺序必须是：
  - current
  - left subtree
  - right subtree
- 不允许交换左右子树构造顺序

---

## 4. 重要语义要求

### 4.1 不允许错误弱化论文定义

必须避免以下错误：

- 错误写法 1：把左子节点写成“任意在右边的块”
  - 错误示例：`x_B >= x_A + w_A`
  - 正确要求：必须是 **右侧且相邻**，即 `x_B == x_A + w_A`，并且有竖直接触区间

- 错误写法 2：把右子节点写成“任意在上面的块”
  - 错误示例：`y_B >= y_A + h_A`
  - 正确要求：必须是 **上方且相邻**，即 `y_B == y_A + h_A`，同时 `x_B == x_A`
  - 还必须补上论文要求的上界约束：若左侧相邻模块存在，则 `y_B < top(left_adjacent_of_A)`

- 错误写法 3：把“唯一性”理解成“每个模块右边/上边只有一个邻居”
  - 不要这样表述
  - 正确表述应为：对于 admissible placement，按论文定义可诱导出唯一的 horizontal B*-tree；除根节点外，每个节点都有唯一父节点

### 4.2 父子关系解释

- 若 `node(B)` 是 `node(A)` 的左孩子，则 `B` 必须是 `A` 的右侧相邻模块
- 若 `node(B)` 是 `node(A)` 的右孩子，则 `B` 必须是 `A` 的上方相邻模块，且 `x_B == x_A`
- 根节点对应整个 placement 的 bottom-left 模块

---

## 5. 实现细节

### 5.1 标记已访问模块

- 使用 `std::vector<bool> visited(P.blocks.size(), false)`
- 每次把模块加入 B*-tree 时标记为已访问
- 若某个模块已经访问过，则不能再次作为其他节点的子节点

### 5.2 几何判断必须可靠

- 坐标比较请考虑浮点误差，使用统一的 `eps`
- 判断“相邻”时，必须是“边界相等 + 接触区间非空”
- 判断接触区间时，不要把仅端点接触误判为重叠
- 右子节点不要仅靠“同列 + y 命中”做哈希查找；必须在几何上验证：
  - 上方相邻
  - `x` 相同
  - 左侧相邻模块上界约束（若存在）

### 5.3 候选模块排序规则

- 左子节点候选：
  - 按 `y` 升序 → `x` 升序 → `block_id` 升序
- 右子节点候选：
  - 按 `y` 升序 → `x` 升序 → `block_id` 升序
- 确保 deterministic，避免不同遍历顺序生成不同树

### 5.4 复杂度要求

- 目标复杂度：尽量接近线性或 `O(n log n)`
- 允许为保证正确性使用额外索引结构
- 但**正确性优先于强行 O(n)**；不得为了追求表面 O(n) 而删掉论文要求的几何约束

---

## 6. 必须保留的 debug 输出要求

保留我原有的 debug 输出逻辑，不要删除，不要改格式语义；如果你需要额外诊断，请在此基础上新增而不是替换。

### 6.1 输出文件格式

- 输出文件格式必须简单、可读，方便 Python 解析
- 每行表示一个节点，包含：
  1. 节点 ID（或 `block_id`）
  2. 左子节点 ID（如果为空，用 `-1`）
  3. 右子节点 ID（如果为空，用 `-1`）
  4. block 的坐标 `(x, y)` 和尺寸 `(w_used, h_used)`，旋转 `rotate`

示例：

    0 1 2 x y w h rotate

- 保证每行对应一个节点
- 顺序使用 DFS（先根、左子、右子）

### 6.2 建议新增的诊断输出

如果树构造失败或不能覆盖所有 block，请额外输出：

- `root_id`
- 每个节点查找到的 `left_id` / `right_id`
- 每次 child lookup 被过滤掉的原因：
  - 不是右侧相邻
  - 不是上方相邻
  - `x` 不相同
  - 无竖直接触区间
  - 违反左侧相邻模块上界约束
  - 已访问
- 最终未访问的 `block_id` 列表

这些新增 debug 不能破坏原有输出接口。

---

## 7. 需要实现的函数建议

你可以自行拆分函数，但至少建议包含如下语义：

- `int find_bottom_left_module(...)`
- `int find_left_child(...)`
- `int find_right_child(...)`
- `int find_left_adjacent_module(...)`  // 用于右子节点上界约束
- `bool is_right_adjacent(...)`
- `bool is_above_adjacent_same_x(...)`
- `BStarNode* build_subtree(...)`

---

## 8. 递归构造 DFS 模板

1. 找根节点
2. `build_subtree(root_id)`
3. 在 `build_subtree(cur)` 中：
   - 若 `cur` 已访问，直接返回空或报错
   - 标记 `cur` 已访问
   - 找左子节点
   - 递归构造左子树
   - 找右子节点
   - 递归构造右子树
   - 返回当前节点指针
4. 构造结束后，检查所有 block 是否均被访问
5. 若未覆盖所有 block，报错并输出详细 debug 信息

---

## 9. 数据稳定性和接口兼容

- 保证 `BStarTree.nodes` 保存所有节点，避免野指针
- 返回 `BStarTree.root` 作为树入口
- 模块接口仅包含 `floorplan_to_bstar_tree`
- 不依赖其他模块内部私有实现细节，只使用允许访问的数据结构
- 不修改已有模块的 public 接口

---

## 10. 结果输出要求

最终生成的 `fp2bstar_tree.cc/.h` 必须满足：

- B*-tree 结构完整
- 根节点为 bottom-left 模块
- 左子节点对应右侧且相邻的模块
- 右子节点对应上方且相邻、`x` 相同，并满足左侧相邻模块上界约束的模块
- 严格遵循 DFS：先左子树，再右子树
- 对 admissible placement 能构造出与论文定义一致的 horizontal B*-tree
- 节点内存固定在 `BStarTree` 内部，避免外部野指针

---

## 11. 代码风格要求

- 使用 C++17
- 代码可编译、可直接集成
- 不写伪代码，要给出完整实现
- 必要处写清楚注释，尤其是：
  - 左子节点几何定义
  - 右子节点几何定义
  - 右子节点的论文额外约束
  - 为什么不能把“在右边/在上面”当作“相邻”
- 对异常情况给出清晰的 `std::runtime_error` 报错信息

请直接输出完整可编译的 `fp2bstar_tree.h` 与 `fp2bstar_tree.cc` 代码。