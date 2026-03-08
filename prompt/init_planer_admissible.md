````markdown
你是一个资深 C++17 工程师。现在我的工程中已经存在并且**不要修改其公共 API** 的模块有：

- `parser.h / parser.cc`
- `orderer.h / orderer2.cc`
- `writer.h / writer.cc`
- `common.h`
- `init_planer.h`

请你**新增并实现一个新的源文件**：

- `init_planer_admissible.cc`

目标是：在**不修改任何既有头文件公共接口**的前提下，实现与当前 `build_initial_floorplan(const Problem&, const std::vector<int>&)` **相同 API** 的一个新版本初始规划器，使其生成的布局满足：

1. **始终合法**
   - 不重叠
   - `x >= 0`
   - `y >= 0`
   - `x + w_used <= chipWidth`
2. **最终是全局 admissible floorplan**
   - 对最终布局中的任意已放块，都不能在保持合法的前提下继续**整体向下**移动
   - 也不能继续**整体向左**移动
3. **按给定顺序逐块放置**
   - 顺序来自 `orderer` 的 `perm`
4. **每放一个块，都要在候选位置中寻找 full cost 最小的位置**
   - cost 必须按照作业 PDF 中的目标函数计算：
   \[
   \text{cost} = H + \frac{HPWL}{\text{numNets}}
   \]
   其中：
   - \(H = \max_i (y_i + h_i)\)
   - `HPWL` 是当前完整布局下全部 nets 的总半周长线长
   - `numNets = P.nets.size()`
   - 若 `numNets == 0`，则 cost 退化为 `H`

⚠️ 重要约束：
- **不要修改** `main.cc`、`parser.*`、`orderer.*`、`writer.*`、`common.h`、`init_planer.h` 的公共接口
- `main.cc` 仍然会调用：
  - `FloorplanResult build_initial_floorplan(const Problem&, const std::vector<int>& perm);`
  - `dump_init_planer_debug(std::ostream&)`
  - `clear_init_planer_debug()`
- 因此 `init_planer_admissible.cc` 必须提供与现有 `init_planer.cc` **完全兼容的外部符号**
- 只输出 `init_planer_admissible.cc` 的完整源码
- 不要输出解释，不要输出其他文件

---

# 1. 必须对齐的现有数据结构与接口

现有 `common.h` 中已经定义：

- `struct Block`
- `struct Pin`
- `struct Net`
- `struct Problem`
- `struct FloorplanItem`
- `struct FloorplanResult`

现有 `init_planer.h` 中已经声明：

- `FloorplanResult build_initial_floorplan(const Problem &P, const std::vector<int> &perm);`
- `void dump_init_planer_debug(std::ostream &os);`
- `void clear_init_planer_debug();`

请直接包含 `init_planer.h`，并严格实现这三个接口，保持签名不变。

---

# 2. 必须兼容的其他模块行为

## 2.1 `parser`
- `Problem` 的 block/pin/net 数据已经被正确解析
- `Pin.dx, Pin.dy` 是**相对 block 中心**的偏移
- 若块旋转 90°，pin 偏移也必须同步旋转

## 2.2 `orderer`
- `perm` 是一个长度等于 `P.blocks.size()` 的 block id 排列
- 你必须严格按 `perm` 顺序逐块放置，不要重排

## 2.3 `writer`
- 最终结果写出时依赖：
  - `fp.x[i]`
  - `fp.y[i]`
  - `fp.rotate[i]`
- 因此这些字段必须完整、合法、与 `P.blocks` 一一对应
- `rotate` 只能是 `0` 或 `1`

---

# 3. 算法总目标

实现一个**contour-based admissible initial placer**，替代旧的 corner / mixed-corner / pin-align 方案。

你必须实现的整体思想是：

## 3.1 核心思路
- 维护当前已放块集合的 **horizontal contour / skyline**
- 对每个待放块、每个旋转状态，枚举少量候选 `x`
- 对每个候选 `x`：
  1. 先根据当前 contour 计算最低可行 `y`
  2. 然后仅对“当前新块”执行 **局部 down-left fixed-point compaction**，直到该块对当前已放块集合稳定
  3. 在“不移动任何旧块”的前提下，把该块加入当前布局形成假设布局
  4. 在该假设布局上计算**完整 full cost**- 从所有合法候选中选择 full cost 最小者
- 将其真正写入当前布局
- 更新 contour
- 继续下一块

---

# 4. 不再使用的旧机制

本文件**不要实现也不要保留**以下旧机制：

- block corner 点集
- mixed corner
- pin align 候选点
- 基于 `ΔH + αΔHPWL` 的局部启发式评分

本文件要完全改成：

- contour / skyline 驱动的候选生成
- 以**完整 cost**
  \[
  H + \frac{HPWL}{numNets}
  \]
  作为候选评价标准

---

# 5. 内部数据结构建议

你可以在 `.cc` 内部自定义私有结构，建议至少包含：

## 5.1 Contour 段
```cpp
struct Segment {
  double x_l;
  double x_r;
  double h;
};
````

语义：

* 在横区间 `[x_l, x_r)` 上，当前 skyline 高度恒为 `h`

初始 contour：

```cpp
[0, chipW) -> h = 0
```

## 5.2 内部已放块状态

建议私有维护：

```cpp
struct PlacedRect {
  int block_id;
  double x;
  double y;
  int rotate;
  double w;
  double h;
};
```

## 5.3 候选结构

建议：

```cpp
struct Candidate {
  double x;
  double y;
  int rotate;
  double w;
  double h;
  double H_after;
  double hpwl_after;
  double cost_after;
};
```

---

# 6. 几何与 pin 坐标规则（必须正确实现）

## 6.1 旋转后的块尺寸

* `rotate == 0`:

  * `w_used = block.w`
  * `h_used = block.h`
* `rotate == 1`:

  * `w_used = block.h`
  * `h_used = block.w`

## 6.2 pin 旋转规则

若原始相对中心偏移为 `(dx, dy)`，则：

* `rotate == 0`:

  * `(dx_rot, dy_rot) = (dx, dy)`
* `rotate == 1`:

  * `(dx_rot, dy_rot) = (-dy, dx)`

## 6.3 pin 绝对坐标

若块左下角为 `(x, y)`，旋转后的实际尺寸为 `(w_used, h_used)`，则块中心为：
\[
(cx, cy) = (x + w_used/2,\; y + h_used/2)
\]

对应 pin 绝对坐标：
\[
(X_p, Y_p) = (cx + dx_{rot},\; cy + dy_{rot})
\]

---

# 7. contour / skyline 的维护（必须实现）

## 7.1 初始 contour

只有一段：

```cpp
[0, chipW) -> 0
```

## 7.2 由 contour 求某个候选 x 的最低 y

设候选块宽度为 `w`，候选左边界为 `x`，定义：

* `xr = x + w`

则最低落点：
\[
y = \max \{ seg.h \mid [seg.x_l, seg.x_r) \cap [x, xr) \neq \varnothing \}
\]

若没有相交段，则 `y = 0`

## 7.3 更新 contour

当一个块最终放在 `(x, y, w, h)` 后：

* 它覆盖区间 `[x, x+w)`
* 其顶高度是 `y + h`

因此要把 contour 中 `[x, x+w)` 对应的部分更新为高度 `y+h`

实现步骤：

1. 在 `x` 和 `x+w` 处切分 segment
2. 覆盖中间区间为新高度
3. 合并相邻且等高的 segment
4. 保证 contour 始终有序、互不重叠、覆盖 `[0, chipW)`

---

# 8. 候选 x 的生成（必须实现）

对于当前待放块、当前旋转后的宽度 `w`，候选 `x` 必须来自以下集合并去重：

1. `0`
2. 当前 contour 中每个 segment 的左端点 `x_l`
3. 所有已放块的左边界 `x`
4. 所有已放块的右边界 `x + w_used`

然后过滤掉不可能的位置：

* 仅保留满足 `x >= 0` 且 `x + w <= chipWidth` 的 `x`

所有候选 `x` 要排序、去重、deterministic。

---

# 9. 单块局部固定点 compaction（必须实现）

对每个初始候选 `(x, rotate)`：

## 9.1 先由 contour 下压

先求最低 `y`

## 9.2 再做局部 down-left fixed-point compaction

不能只做一次“下压 + 左压”。
必须反复执行直到稳定：

1. 在当前 `x` 下，计算最低合法 `y`
2. 在当前 `y` 下，求使该块仍合法的最小 `x`
3. 若 `(x, y)` 变化，继续循环；否则停止

### 9.2.1 downward compaction

给定当前 `x` 和块宽度 `w`，最低 `y` 可以由当前已放布局几何关系求得，不允许与任意已放块重叠。

### 9.2.2 left compaction

给定当前 `y` 和块尺寸 `(w, h)`，求最小合法 `x_left`，满足：

* `x_left >= 0`
* `x_left + w <= chipWidth`
* 与所有已放块不重叠
* 且保持当前 `y` 不变

不要做连续优化器；直接用 deterministic 的离散边界法即可。
建议把左移候选 `x_left` 枚举为：
- `0`
- 所有已放块的右边界
然后取其中最小合法者。
左移后必须再次执行 downward compaction，因此整体要做 `down -> left -> down -> ...` 直到 `(x, y)` 不再变化。

---

# 11. 合法性检查（必须实现）

对任意完整布局候选，必须检查：

## 11.1 边界

* `x >= 0`
* `y >= 0`
* `x + w <= chipW`

## 11.2 非重叠

矩形采用半开区间：

* 当前块 `[x, x+w) × [y, y+h)`
* 已放块 `[xi, xi+wi) × [yi, yi+hi)`

若同时满足：

* `x < xi + wi`
* `x + w > xi`
* `y < yi + hi`
* `y + h > yi`

则重叠，非法。

---

# 12. 候选 cost 的计算（必须使用 full cost，不是增量近似）

对每个完整布局候选，都要计算：

## 12.1 当前完整高度

\[
H = \max_i (y_i + h_i)
\]

## 12.2 当前完整 HPWL

对 `P.nets` 中每条 net：

* 收集该 net 所有 pin 的绝对坐标
* 取：

  * `xmin, xmax, ymin, ymax`
* 则该 net 的 HPWL 为：
\[
(xmax - xmin) + (ymax - ymin)
\]

总 HPWL 为所有 net 的和。

## 12.3 当前完整 cost

若 `P.nets.size() > 0`：
\[
cost = H + \frac{HPWL}{|nets|}
\]
否则：
\[
cost = H
\]

### 重要要求

每次“选择当前块放在哪里”时，必须比较的是：
- 把当前块真正加进去、且只对该块完成局部 fixed-point compaction 后
- 在“旧块位置保持不变”的当前布局下得到的 full cost

不要使用：

* `ΔH`
* `ΔHPWL`
* `alpha`
* 线性近似打分

---

# 13. 候选比较规则（必须 deterministic）

若两个候选 `cost_after` 相同（允许 `1e-9` 级容差），按以下顺序比较：

1. 更小的 `full cost`
2. 更小的 `hpwl_after`
3. 更小的 `y`
4. 更小的 `x`
5. `rotate = 0` 优先
6. 更小的 `block_id`

首块同样使用 deterministic tie-break：

1. 更小 full cost
2. 更小 `H`
3. 更小 `y`
4. 更小 `x`
5. `rotate = 0` 优先

---

# 14. 首块处理

对于 `perm[0]`：

* 枚举 `rotate = 0/1`
* 只能从 `(0,0)` 开始
* 若旋转后宽度超出 `chipWidth`，则该旋转无效
* 计算该单块布局的 full cost
* 选最优
* 结果应显然是 admissible
* 初始化 contour

---

# 15. 每一轮放置流程（必须按此实现）

对 `perm[t]` 中的 block，`t > 0`：

1. 枚举 `rotate = 0/1`
2. 根据旋转后宽度生成候选 `x` 集合
3. 对每个候选 `x`：
   * 先求 contour 下的最低 `y`
   * 构造该块的初始候选位置
   * 仅对该块做局部 `down-left` fixed-point compaction，直到稳定
   * 将该块加入当前已放块集合，形成假设布局（旧块不移动）
   * 检查合法
   * 计算该完整布局的 `H / HPWL / cost`
4. 在所有合法候选中选 full cost 最优者
5. 将其写入真实状态：

   * `result.x[block_id]`
   * `result.y[block_id]`
   * `result.rotate[block_id]`
   * `result.items`
6. 重建或更新 contour
7. 进入下一块

若某块两个旋转都没有任何合法候选，抛出：

```cpp
std::runtime_error("init_planer_admissible: no legal admissible candidate for block ...");
```

异常信息中要包含：

* `block_id`
* `block.name`

---

# 16. contour 的维护策略建议

为了保证正确性和简洁性，不要求做复杂增量修补。
你可以采用以下务实做法：

## 16.1 真正接受某个候选后
不要依赖旧 contour 做局部 patch。

而是直接根据**当前所有已放块**重新构建 contour：
- 收集所有关键 `x`：
  - `0`
  - `chipWidth`
  - 所有块的左边界
  - 所有块的右边界
- 排序后形成若干基本区间 `[xs[i], xs[i+1])`
- 对每个区间中点 `xm`，求该横坐标下所有覆盖该点的块的最大顶边高度
- 得到新的 `Segment`
- 最后合并等高相邻段

评估候选时可以基于“当前已放旧块”构建 contour，并用它生成与下压当前新块；不要为了评估候选去对整张布局做全局 sweep。

---

# 17. debug 接口要求（API 不变，但内容可升级）

你必须实现：

* `void clear_init_planer_debug();`
* `void dump_init_planer_debug(std::ostream &os);`

## 17.1 记录条件

仅当环境变量 `DEBUG` 存在且不为 `"0"` 时记录调试信息。

## 17.2 调试信息至少应包含

对每个放置轮次：

* 当前 block id / block name
* 当前旋转
* 枚举到的候选 `x`
* 由 contour 求得的初始 `(x, y)`
* 当前新块局部 fixed-point compaction 后位置
* 对应 `H_after`
* `hpwl_after`
* `cost_after`
* 是否被选中

## 17.3 dump 输出要求

输出应可读、deterministic，不要求与旧版 debug 格式完全一致，但必须清楚标明：

* 轮次
* block
* 候选列表
* chosen 候选
* 当前完整布局 `H / hpwl / cost`

---

# 18. 数值与确定性要求

* 使用 `double`
* 统一使用小容差，例如：

  * `constexpr double kEps = 1e-9;`
* 所有排序、比较、去重必须 deterministic
* 不能依赖未定义顺序的容器遍历结果作为最终 tie-break
* 最终输出布局必须适合 `writer.cc` 直接写出

---

# 19. 代码组织要求

* 使用 C++17
* 不依赖第三方动态库
* 仅实现 `init_planer_admissible.cc`
* 可以在 `.cc` 中定义任意私有辅助函数/结构
* 代码必须自包含、清晰、模块化、可编译
* 头文件仍使用现有 `init_planer.h`
* 本文件中最终导出的公共函数只有：

  * `build_initial_floorplan`
  * `dump_init_planer_debug`
  * `clear_init_planer_debug`

---

# 20. 你最终必须输出什么

只输出完整的：

* `init_planer_admissible.cc`

不要输出解释，不要输出伪代码，不要输出其他文件。

```
