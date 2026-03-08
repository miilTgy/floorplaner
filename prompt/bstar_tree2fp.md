你是一个资深 C++17 工程师。现在我的工程中已经存在并且不要修改公共 API 的文件有：

- `common.h`
- `fp2bstar_tree.h`

请你阅读它们的定义，并新增实现一个模块：

- `bstar_tree.h` 中定义 B*-tree 的数据结构，请不要使用 `fp2bstar_tree.h` 中的数据结构。
- `bstar_tree2fp.h` 中定义本模块的 API
- `bstar_tree2fp.cc`

---

## 1. 目标

实现一个 **horizontal B*-tree -> floorplan** 的解码器。

输入：

- `const Problem &P`
- `const BStarTree &tree`
- 旋转信息 `std::vector<int> rotate`，长度等于 `P.blocks.size()`

输出：

- `FloorplanResult`

输出的 `FloorplanResult` / `FloorplanItem` 字段必须严格对齐 `common.h` 中已有定义，请阅读那个文件：
- `x`
- `y`
- `rotate`
- `H`
- `hpwl`
- `cost`
- `items`
- `items[i].block_id`
- `items[i].x`
- `items[i].y`
- `items[i].rotate`
- `items[i].w_used`
- `items[i].h_used` :contentReference[oaicite:2]{index=2}

---

## 2. B*-tree 语义

必须按 **horizontal B*-tree** 语义解码：

- 若 `child` 是 `parent.left`：
  - child 表示 parent 的左孩子
  - 几何含义是：**child 放在 parent 的右边**
  - 即：
    - `x_child = x_parent + w_parent_used`
    - 且 child 与 parent 在竖直方向必须存在**非空接触区间**（重叠长度 `> eps`）

- 若 `child` 是 `parent.right`：
  - child 表示 parent 的右孩子
  - 几何含义是：**child 与 parent 左边界对齐**
  - 即：
    - `x_child = x_parent`
    - `y_child = y_parent + h_parent_used`（允许 `eps` 误差）

若任一父子不满足上述几何关系，则此次解码非法，必须抛出 `std::runtime_error`。

注意：
- 这里的 `w_parent_used / h_parent_used` 必须使用 **旋转后的实际尺寸**
- `BStarNode` 定义以 `block_id / left / right` 为准，不要改动它的公共结构 :contentReference[oaicite:3]{index=3}

---

## 3. 合法 floorplan 的要求

你生成的 floorplan 必须满足：

1. **所有 block 都被放置**
2. **block 之间不能重叠**
3. block 坐标满足：
   - `x >= 0`
   - `y >= 0`
4. `FloorplanResult.items.size() == P.blocks.size()`
5. `FloorplanResult.x / y / rotate` 的长度都等于 `P.blocks.size()`
6. `FloorplanResult.items` 中每个 block 只出现一次
7. `H` 为最终布局最大上边界
8. 布局宽度满足芯片上限：
   - `layout_width = max_i (x_i + w_used_i)`
   - `layout_width <= P.chipW`（含 `eps`）
9. `hpwl` 正确按 net 计算
10. `cost` 必须与 init 流程统一：
   - `numNets = P.nets.size()`
   - `cost = H + hpwl / numNets`
   - 当 `numNets == 0` 时退化为 `cost = H`

如果树非法（例如 root 为空、block_id 越界、同一 block 出现多次、存在环、节点数不覆盖全部 block），要抛出清晰的 `std::runtime_error`。

---

## 4. 解码核心要求

### 4.1 必须使用 contour / skyline 思想
不能只根据父子关系直接给定 `y`。  
正确做法是：

- 先由 B*-tree 决定每个节点的 `x`
- 再根据当前 contour 计算该 block 在 `[x, x + w_used)` 区间上的**最低合法 y**
- 将该 block 放到该 y
- 更新 contour

也就是说：

- left child 决定“向右放”
- right child 决定“左边界对齐”
- **真正的 y 由 contour 决定**
- contour 只能用于求候选 `y`，不是 B*-tree 几何合法性的最终判据
- child 落位后，必须立刻检查它和 parent 是否满足 B*-tree 几何关系
- 若不满足，必须立即抛 `std::runtime_error`
- 不能只因为“不重叠”就接受

### 4.2 根节点
- root 放在 `(0, 0)`

### 4.3 旋转尺寸
对于 block `b`：

- 若 `rotate == 0`
  - `w_used = P.blocks[b].w`
  - `h_used = P.blocks[b].h`
- 若 `rotate == 1`
  - `w_used = P.blocks[b].h`
  - `h_used = P.blocks[b].w`

所有几何判断和 contour 更新都必须使用 `w_used / h_used`。

### 4.4 芯片宽度硬约束

在完成放置并组装 `fp.items` 后，必须计算：

- `layout_width = max_i (items[i].x + items[i].w_used)`

并检查：

- `layout_width <= P.chipW`（含 `eps`）

若超宽，必须直接抛出 `std::runtime_error`，错误信息建议包含 `layout_width` 与 `chipW`，用于上层候选过滤与 debug。

---

## 5. 推荐实现方式

### 5.1 内部辅助结构
可以在 `.cc` 中定义私有辅助结构，例如：

- `PlacedBlock`
- `ContourSegment`
- `DecodeState`

### 5.2 推荐 helper
你可以自由拆分，但建议至少有这些语义：

- `void validate_tree(const Problem&, const BStarTree&)`
- `void collect_tree_nodes(...)`
- `std::pair<double,double> used_size(const Problem&, int block_id, int rotate)`
- `double query_contour_y(double xL, double xR)`
- `void update_contour(double xL, double xR, double newTop)`
- `void place_subtree(const BStarNode* node, double x, ...)`
- `void validate_left_child_geometry(...)`
- `void validate_right_child_geometry(...)`
- `double compute_hpwl(const Problem&, const FloorplanResult&)`

### 5.3 遍历顺序
建议使用 DFS：

1. 放置当前节点
2. 递归放置 left child
3. 递归放置 right child

注意：
- left child 的目标 `x = parent.x + parent.w_used`
- right child 的目标 `x = parent.x`

---

## 6. HPWL 计算要求

对每个 net：

- 遍历其所有 pin
- 由 block 位置 + pin 在 block 上的偏移得到 pin 的全局坐标
- 若 block 旋转，需要正确处理 pin 偏移的旋转
- 计算：
  - `hpwl_net = (max_x - min_x) + (max_y - min_y)`
- 总 `hpwl` 为所有 net 的和

如果你不确定 pin 旋转后的偏移约定，请在代码中写清楚假设，并保持实现自洽、可复用。

---

## 7. 输出组装要求

最终返回的 `FloorplanResult` 需要完整填写：

- `fp.x[block_id]`
- `fp.y[block_id]`
- `fp.rotate[block_id]`
- `fp.items.push_back(FloorplanItem{...})`
- `fp.H`
- `fp.hpwl`
- `fp.cost`

其中：

- `items` 建议按 `block_id` 升序输出，保证 deterministic
- `H = max_i (y_i + h_used_i)`
- `numNets = P.nets.size()`
- `fp.cost = fp.H + fp.hpwl / numNets`，若 `numNets == 0` 则 `fp.cost = fp.H`

---

## 8. Debug 与健壮性

请额外提供一个调试输出函数，例如：

```cpp
void dump_bstar_tree2fp_debug(const FloorplanResult &fp, const std::string &filename);
```

输出每个 block 的：

* `block_id`
* `x`
* `y`
* `rotate`
* `w_used`
* `h_used`

便于我排查重叠或 contour 错误。

此外请在以下情况抛异常：

* `tree.root == nullptr`
* block 数与 tree 覆盖数不一致
* 同一 block 被访问多次
* 存在未访问 block
* 放置后出现重叠
* 布局宽度超过芯片上限（`layout_width > P.chipW`）
* left child 未贴 parent 右边界
* right child 未贴 parent 顶边
* floorplan 虽不重叠但不满足 B*-tree 几何定义
* `x/y` 为负
* `items` 与 `x/y/rotate` 不一致

---

## 9. 代码风格要求

* 使用 C++17
* 输出完整可编译代码，不要伪代码
* 注释简洁但清楚
* 正确性优先于过度优化
* 不修改已有公共头文件
* 只新增：

  * `bstar_tree2fp.h`
  * `bstar_tree2fp.cc`

---

## 10. 最终要求

请直接输出完整可编译的：

* `bstar_tree2fp.h`
* `bstar_tree2fp.cc`

要求该模块能够：

* 根据 `BStarTree` 解码出合法、不重叠的 floorplan
* 保证输出满足芯片宽度上限（`layout_width <= P.chipW`）
* 保证输出满足 B*-tree 父子几何定义（left 接触、right 贴顶）
* 输出格式严格对齐 `common.h`
* 为后续 `init_fp_bstar` 和 SA 提供稳定的评估基础
