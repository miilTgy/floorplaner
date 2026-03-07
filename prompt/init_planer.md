你是一个资深 C++ 工程师。现在我的工程里已经有两个**无副作用**模块：

1. **Parser 模块**
   - API：`Problem parse_problem(const std::string& path);`
   - 可选：`void dump_problem(const Problem&, std::ostream&);`

2. **Orderer 模块**
   - API：`std::vector<int> build_initial_ordering(const Problem& P);`
   - 可选：`void dump_ordering_debug(std::ostream& os);`

请你实现第三个模块：**初始规划器 init_planer**（文件名建议 `init_planer.cc` / `init_planer.h`），它根据 `Problem + perm` 生成一个**合法的初始 floorplan**。  
这个模块必须：

- 动态维护 **block corner 点集**
- 针对当前待放块，临时生成 **pin align 候选点集**
- 在所有候选位置中选择一个局部最优位置
- 输出每个 block 的 `(x, y, rotate)` 初始解

⚠️ 注意：
- 不要实现 SA / 后处理局部搜索 / 输出 solution 文件 / main
- 只实现 init_planer 模块
- 这个模块必须无副作用，默认不打印
- 要与 parser / orderer 已有的数据结构对齐

---

# 1. 必须对齐的数据结构和接口

`parser.h` 中已经有：

- `struct Block`
- `struct Pin`
- `struct Net`
- `struct Problem`

orderer 提供：
- `std::vector<int> perm`

请不要修改 parser / orderer 的已有 API。

---

# 2. 你要实现的公共接口

请在 `init_planer.h` 和 `common.h` 中定义必要的数据结构，并实现以下主接口：

## 2.1 Floorplan 数据结构
建议定义：

- `struct FloorplanItem { int block_id; double x; double y; int rotate; double w_used; double h_used; };`
- `struct FloorplanResult {`
  - `std::vector<double> x;`
  - `std::vector<double> y;`
  - `std::vector<int> rotate;`
  - `double H;`
  - `double hpwl;`
  - `double cost;`
  - `std::vector<FloorplanItem> items;`
  - `};`

其中：
- `x[i], y[i]` 是 block i 左下角坐标
- `rotate[i]` 为 0/1
- `w_used, h_used` 是旋转后的实际尺寸
- `H = max_i (y[i] + h_used[i])`
- `hpwl` 是当前完整布局的总 HPWL
- `cost = H + hpwl / numNets`
- `numNets` 是 `net` 的数量

## 2.2 主函数
实现：

- `FloorplanResult build_initial_floorplan(const Problem& P, const std::vector<int>& perm);`

要求：
- 按 perm 顺序，一个块一个块地放
- 始终保持布局合法：
  - 不重叠
  - `0 <= x`
  - `0 <= x + w_used <= chipW`
  - `0 <= y`
- 对每个 block 尝试 `rotate=0/1`
- 在候选位置中选一个局部最优位置
- 返回完整初始解

---

# 3. 总体算法思路（必须按此实现）

采用 **corner-based greedy floorplanning + on-the-fly pin alignment**：

## 3.1 第一个块
对于 `perm[0]`：
- 尝试 `rotate=0/1`
- 直接放在 `(0, 0)`
- 选择局部评分更优的旋转：
  - 先更小 `H`
  - 若并列，先更小 `x`
  - 若并列，先更小 `y`
  - 若仍并列，`rotate=0` 优先
- 放下后初始化 block corner 点集

## 3.2 后续块
对于 `perm[t]` (`t > 0`)：
- 对每个旋转 `r in {0,1}`：
  - 根据当前块尺寸和 pins 生成候选点
  - 候选点由两类组成：
    1. **block corner 候选点**
    2. **pin align 候选点**
- 对所有候选 `(x, y, rotate)`：
  - 检查合法性（有没有与现有块重叠，有没有越过右边界）
  - 计算局部 cost 评分
- 选评分最优者放入 floorplan，若评分一样优先选择 y 坐标最低的
- 更新 block corner 点集
- 进入下一块

---

# 4. 关键数据结构：动态维护 block corner 点集

请在 init_planer 内部维护一个动态集合，元素类型建议为：

- `enum class CornerType { RIGHT_DOWN, LEFT_UP };`
- `struct BlockCorner {`
  - `double x;`
  - `double y;`
  - `CornerType type;`
  - `int owner_block_id;`
  - `};`

语义：
- `RIGHT_DOWN`：来源于某个已放块的**右下方向可扩展角**
- `LEFT_UP`：来源于某个已放块的**左上方向可扩展角**

对于一个已放矩形 `(x, y, w, h)`：
- 它产生两个基础 corner：
  - `RIGHT_DOWN` at `(x + w, y)`
  - `LEFT_UP` at `(x, y + h)`

## 4.1 corner 点集的维护规则
每放下一个块后：
1. 向 corner 集合中加入该块产生的两个新 corner
2. 如果本次放置是直接使用某个已有 block corner 点作为落点：
   - 从 corner 集合中删除该点
3. 如果本次放置使用的是某个 pin-align 点：
   - 该 pin-align 点一定对应某个“基准 block corner”
   - 从 corner 集合中删除这个“对应的基准 block corner”
4. 可选清理：
   - 若某个 corner 已明显落入某个已放块内部，可删除
   - 但这不是必须，优先保证正确性

⚠️ pin-align 点**不全局维护**
- pin-align 点只在当前待放块、当前旋转下临时生成
- 用完即丢弃

---

# 5. 候选点生成规则（必须严格实现）

对当前待放块 b、当前旋转 r，设旋转后尺寸为 `(w, h)`。

## 5.1 block corner 候选点
对于当前动态维护的每个 `BlockCorner c`：

- 如果 `c.type == RIGHT_DOWN`
  - 直接加入一个 block-corner 候选点：`(c.x, c.y)`
- 如果 `c.type == LEFT_UP`
  - 直接加入一个 block-corner 候选点：`(c.x, c.y)`

这里的“block-corner 候选点”表示：
- 新块左下角直接放在这个点上

每个候选点要记录来源：
- 它来自哪个 corner
- 它是不是 pin-align 点
- 如果是 pin-align 点，它对应哪个基准 corner

建议定义：

- `struct CandidatePoint {`
  - `double x;`
  - `double y;`
  - `int rotate;`
  - `bool is_pin_align;`
  - `int base_corner_index;`
  - `CornerType base_corner_type;`
  - `};`

---

## 5.2 pin align 候选点
pin-align 点从每个 block corner 出发，**只在一个方向上搜索**：

### 对于 RIGHT_DOWN corner
- 只搜索 **x 方向** pin align
- 不搜索 y 方向 pin align

即：
- 若基准 corner 是 `(xc, yc)`
- 固定 `y = yc`
- 通过当前块 pins 与已放块相关 pins 的对齐，生成若干 `x_align`
- 候选点是 `(x_align, yc)`

### 对于 LEFT_UP corner
- 只搜索 **y 方向** pin align
- 不搜索 x 方向 pin align

即：
- 若基准 corner 是 `(xc, yc)`
- 固定 `x = xc`
- 通过当前块 pins 与已放块相关 pins 的对齐，生成若干 `y_align`
- 候选点是 `(xc, y_align)`

⚠️ 禁止：
- RIGHT_DOWN corner 搜 y
- LEFT_UP corner 搜 x

原因：
- 避免把块无节制地抬高或推得过右
- 这部分自由度留给后续局部搜索/改进器处理

---

# 6. pin align 点怎么计算（必须写清楚）

## 6.1 只对“与已放块相连的 nets”生成对齐点
对于当前块 b 的每个 pin p：
- 遍历 `Pin p` 所属的 nets
- 对每条 net k：
  - 找 net 中其他 pin q
  - 若 q 所属的 block 已经放置，则它是一个有效参考 pin
  - 只有这种情况下，才生成 pin align 候选

## 6.2 旋转后的 pin 绝对坐标公式
请统一采用“相对块中心偏移”的表示。

设当前块左下角为 `(x, y)`，旋转后尺寸为 `(w, h)`。

块中心：
- `cx = x + w / 2`
- `cy = y + h / 2`

原始 pin 偏移为 `(dx, dy)`（parser 中给的是相对块中心）

旋转规则：
- `rotate = 0`：
  - `dx_rot = dx`
  - `dy_rot = dy`
- `rotate = 1`（逆时针 90°）：
  - `dx_rot = -dy`
  - `dy_rot = dx`

于是 pin 绝对坐标：
- `X_pin = cx + dx_rot`
- `Y_pin = cy + dy_rot`

## 6.3 从已放参考 pin 反推出左下角坐标
### 对于 RIGHT_DOWN corner 的 x-align
固定 `y = yc`，要求当前块某 pin p 与已放 pin q **x 对齐**：

\[
X_p = X_q
\]

由于：
\[
X_p = x + \frac{w}{2} + dx_{rot}
\]

解得：
\[
x = X_q - \frac{w}{2} - dx_{rot}
\]

若该 x 合法（后续完整检查），则形成候选点：
- `(x, yc)`

### 对于 LEFT_UP corner 的 y-align
固定 `x = xc`，要求当前块某 pin p 与已放 pin q **y 对齐**：

\[
Y_p = Y_q
\]

由于：
\[
Y_p = y + \frac{h}{2} + dy_{rot}
\]

解得：
\[
y = Y_q - \frac{h}{2} - dy_{rot}
\]

若该 y 合法（后续完整检查），则形成候选点：
- `(xc, y)`

---

# 7. 候选点合法性检查（必须实现）

对任意候选 `(x, y, rotate)`：

## 7.1 边界约束
约束左下右三边，必须满足：
- `x >= 0`
- `x + w <= chipW`
- `y >= 0`

## 7.2 非重叠约束
与所有已放块逐个检查矩形是否重叠。

设当前候选矩形为：
- `[x, x+w) × [y, y+h)`

已放块 i 为：
- `[xi, xi+wi) × [yi, yi+hi)`

若同时满足：
- `x < xi + wi`
- `x + w > xi`
- `y < yi + hi`
- `y + h > yi`

则重叠，候选非法。

## 7.3 首块特判
若当前还没有已放块，则 `(0,0)` 总是合法（前提是旋转后宽度不超过 chipW）

---

# 8. 局部评分函数（必须实现）

这是一个 **initial planer**，不是最终优化器。  
请实现一个轻量级、局部的 cost-aware 评分：

\[
score = 2 \times (1 - \alpha) \cdot \Delta H + 2 \times \alpha \cdot \Delta \left( \frac{HPWL}{numNets} \right)
\]

其中：
- `ΔH = H_after - H_before`
- `Δ(HPWL/numNets)` = 当前候选相对于当前 floorplan 的平均 HPWL 增量
- 初始建议 `alpha = 0.5`
- 将 alpha 写成模块内常量，后续可调

## 8.1 ΔH 的计算
当前 floorplan 高度：
- `H_before`

若把当前块放在 `(x, y)`，旋转后高度为 `h`：
- `H_after = max(H_before, y + h)`
- `ΔH = H_after - H_before`

## 8.2 ΔHPWL 的计算
只对**当前块相关的 nets**计算增量。

建议简化做法：
- 对当前块 b 的每条 net k：
  - 取 net k 的所有 pins
  - 若已有 pin 已放，当前候选将影响该 net 的 bbox
  - 计算“放当前块前”和“放当前块后”的该 net HPWL
  - 做差，累加到 `ΔHPWL`

这样虽然不是最极致高效，但块数不大时可接受，且逻辑清晰。

### HPWL 计算方式
对某条 net：
- 收集当前已放 pins 的绝对坐标
- 如果当前候选块也包含该 net 的 pin，则把候选位置下的这些 pin 坐标也加入
- 求：
  - `xmin, xmax, ymin, ymax`
- `HPWL = (xmax - xmin) + (ymax - ymin)`

## 8.3 评分 tie-break（必须 deterministic）
若 score 并列，按以下顺序：
1. `ΔH` 更小者优先
2. `ΔHPWL` 更小者优先
3. `x` 更小者优先
4. `y` 更小者优先
5. `rotate=0` 优先

---

# 9. 当前块放下后如何更新状态

当选定最优候选 `(x, y, rotate)` 后：

1. 写入：
   - `result.x[block_id] = x`
   - `result.y[block_id] = y`
   - `result.rotate[block_id] = rotate`

2. 加入 `result.items`
3. 更新 `current_H`
4. 更新当前已放标记
5. 更新 corner 点集：
   - 删除本次使用的基准 corner
   - 加入当前块产生的两个新 corner：
     - `(x + w, y)` as RIGHT_DOWN
     - `(x, y + h)` as LEFT_UP

---

# 10. 初始 floorplan 的完整流程（必须按此实现）

对于 `perm` 中每个 block b，按顺序：

1. 对 `rotate = 0/1` 分别处理
2. 生成 block-corner 候选点
3. 基于每个 corner 按规则生成 pin-align 候选点
4. 合并候选点集合
5. 对每个候选点：
   - 检查合法性
   - 计算局部 score
6. 选最优候选放下
7. 更新 corner 点集与 floorplan 状态
8. 继续下一个 block

若某块在某旋转下没有任何合法候选，则该旋转无效。  
若两个旋转都无合法候选，这理论上不应发生；若发生，请抛出 `std::runtime_error`，并附带 block_id / block_name 信息。

---

# 11. Debug 输出：提供单独 dump 函数，不在主算法内部打印

请新增可选 debug 设施，但不要污染主算法 API。

## 11.1 新增函数
在 `init_planer.h` 中新增：

- `void dump_init_planer_debug(std::ostream& os);`
- `void clear_init_planer_debug();`（可选但推荐）

## 11.2 build_initial_floorplan 内部记录的 debug 信息
仅当环境变量 `DEBUG` 存在且不为 "0" 时记录。

记录内容：
1. 每个 block 的放置轮次
2. 当前 block、当前旋转
3. 所有 block-corner 候选点
4. 所有 pin-align 候选点（标注它来自哪个基准 corner、哪对 pins）
5. 所有合法候选点的：
   - `(x, y, rotate)`
   - `ΔH`
   - `ΔHPWL`
   - `score`
   - 是否最终被选中

## 11.3 dump 输出格式
可读即可，但必须包含：
- block 轮次 header
- 所有候选点列表
- 被选中的候选点
- 当前放置后高度 `H`

---

# 12. 实现风格要求
- 使用 C++17
- 不依赖动态库
- 不修改 parser / orderer 的 API
- 不在算法内部写 stdout/stderr（debug 信息通过 dump 函数输出）
- 代码应清晰、模块化、可读
- 优先正确性与确定性，性能优化放在次要位置

---

# 13. 你需要输出什么
只输出：
- `init_planer.h`
- `init_planer.cc`

不要输出其他文件，不要额外解释。