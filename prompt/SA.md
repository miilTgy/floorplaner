# 任务目标

请在当前仓库 `floorplaner` 的现有代码基础上，实现一个基于 **B\*-tree** 的 **Simulated Annealing (SA)** 楼层规划器。  
不要重写整个项目，尽量复用已有的数据结构、解析输入、初始解生成、cost 计算、以及最终结果输出逻辑。  
本次改动目标是：在已有贪婪初始解基础上，增加一个 **SA 搜索器**，从初始 B\*-tree 出发，通过扰动不断搜索更优 floorplan。

---

# 初始化来源与禁止项（硬约束）

- SA 初态必须来自 `build_initial_bstar_result(...)` 的返回值，直接使用其中的：
  - `tree`
  - `rotate`
  - `fp`
  - `cost`
- 允许并要求改动 `main` 及相关数据结构，把上述初始状态直接传给 SA。
- 禁止任何 `floorplan -> B*-tree` 路径、建议或备选方案。

---

# 必须实现的算法能力

SA 必须支持以下三种扰动：

1. **Rotate**
   - 随机选择一个 block
   - 交换其宽高 / 旋转朝向
   - 项目已有 rotation flag，沿用现有表示

2. **Swap**
   - 随机选择两个树节点
   - **只交换 block id**
   - **不要交换整棵子树，不要交换 parent/child 指针**
   - 也就是说，树结构保持不变，仅节点上承载的模块身份互换

3. **Move**
   - 随机选择一个非根节点 `u`
   - 将 `u` 从当前 B\*-tree 中 **delete**
   - 然后将 `u` **reinsert** 到一个新的合法位置
   - **reinsert 必须支持完整的 internal position 和 external position**
   - 不能只支持空 child 槽位这种弱化版插入
   - 要显式实现“删除 + 插入”这一套 B\*-tree 结构编辑逻辑

---

# 目标实现原则

请严格遵守以下原则：

- **全量解码**
  - 每次候选解生成后，都从整棵 B\*-tree 全量生成一次 floorplan ，用 bstar_tree2fp 模块。
  - 不要做增量 contour 更新
  - 不要做局部坐标修补
  - 不要偷懒沿用旧坐标

- **全量 cost**
  - 每次候选解生成后，都重新完整计算一次 cost
  - 不要做增量 HPWL / area 更新
  - 统一定义：`numNets = P.nets.size()`，`cost = H + HPWL / numNets`，`numNets == 0` 时退化为 `H`

- **非法候选直接 reject**
  - 只要树结构非法，或者 decode 结果不满足 B\*-tree 几何合法性，或者 floorplan 非法，就立即 reject
  - 树结构合法性由 SA 层主动检查；decode / 几何 / overlap 复用 `bstar_tree2fp`
  - `bstar_tree2fp` 若报 runtime error，则视为 decode 或几何合法性检查失败
  - reject 后当前状态不变

- **记录非法候选的操作过程**
  - 每次 SA 过程中产生非法候选，都要把“导致非法的操作信息”详细记录下来
  - 包括操作类型、涉及节点、删除前后关系、插入目标、随机参数、失败原因等
  - 用于后续 debug

- **最终结果输出格式**
  - 最终 best floorplan 的输出格式，必须与当前项目里 **init fp** 的输出格式完全一致
  - 即：不要发明新的最终结果输出格式
  - 若当前项目已有 `write_result` / `dump_floorplan` / `save_fp` 之类函数，请复用

- **保留并增强 debug 输出**
  - 不要删掉原有 debug 输出
  - 在此基础上增加 SA 相关 debug 输出
  - debug 输出要足够明确，便于定位是“树非法”“decode 非法”“几何不贴邻”“overlap”“cost 计算正常但被拒绝”等
  - SA 的 debug 输出顺序一定要按照 main 中执行得顺序输出，各阶段 debug 输出不能交叠

---

# 首先要做的事情

请先阅读并理解仓库中与以下内容相关的现有代码实现过的功能：

1. block / module 数据结构
2. B\*-tree 节点与树表示
3. 初始解（greedy / init floorplan）构造逻辑
4. `bstar_tree -> floorplan` 的 decode 逻辑
5. contour 或坐标生成逻辑
6. cost 计算逻辑
7. init fp 的最终输出逻辑
8. 参数解析 / 主程序入口

不要脱离现有工程另起炉灶。

---

# 关键理论约束（必须落实到合法性检查）

下述合法性检查都在 bstar_tree2fp 模块中已完成。

请严格按论文中的 B\*-tree 几何定义来约束合法性，而不是只检查“二叉树结构没坏”：

- 根节点对应左下角模块，坐标应为 `(0, 0)` 或与当前项目的左下角归一化逻辑一致
- **左孩子**表示：该模块位于父模块**右边且相邻贴边**
  - 几何上必须满足：`x_child = x_parent + width_parent`
  - 不是“大致在右边”就行
  - 必须是贴着右边界
- **右孩子**表示：该模块位于父模块**上方且相邻贴边，同时左边界对齐**
  - 几何上必须满足：`x_child = x_parent`
  - 且 child 必须在 parent 上方并与其接触
  - 不是“大致在上方”就行

所以一个 candidate 即使：
- 树拓扑是合法二叉树
- contour 也算出了坐标

只要 decode 出来后：
- 左孩子没有贴父节点右边
- 或右孩子没有与父节点左边界对齐
- 或右孩子虽然在上面但没贴边
- 或出现 overlap
- 或不满足当前项目接受的合法 B\*-tree placement

都必须判为 **非法候选** 并 reject。

上述合法性检查都在 bstar_tree2fp 模块中已完成。

---

# 需要新增或补强的模块

请尽量以最小侵入方式新增以下能力。函数名可以根据现有代码风格调整，但职责必须具备。

## 1. SA 状态结构

新增一个 SA 状态对象，至少包含：

- 当前 B\*-tree
- 当前 floorplan（若现有代码已有对应结构可复用）
- 当前 cost
- best B\*-tree
- best floorplan
- best cost
- 当前温度
- 当前 step / accepted / rejected / invalid 计数
- 非法候选日志缓存或输出句柄

如果现有代码已经有 tree + floorplan + cost 的组合结构，就在其上扩展。

---

## 2. 树复制能力

必须有稳定的 **deep copy / clone** 逻辑，用于：

- 从当前状态复制出 candidate tree
- 对 candidate 做 rotate / swap / move
- 如果 candidate 被拒绝，不污染当前状态

禁止在 SA 提案阶段直接原地破坏当前最佳树。

---

## 3. 节点索引与遍历辅助

为了实现 Move 和 debug，请补齐下面能力：

- 获取所有节点列表
- 获取所有非根节点列表
- 根据 block id 找节点
- 根据节点 id / index 找节点
- parent 指针或等价父关系查询
- preorder / inorder / DFS 遍历打印
- 子树序列化打印，便于 debug

---

# Move 的完整实现要求

这是本次任务最难，最容易出 bug 的部分，请认真实现。

## A. Delete 操作

请实现一个稳定的 `delete_node_from_bstar_tree(node)` 逻辑。  
删除的是某个 **非根节点** `u`，要把 `u` 从树里摘掉，然后保留剩余节点形成一棵合法 B\*-tree 结构。

必须正确处理以下情形：

1. `u` 是叶子：删除后不变
2. `u` 只有一个孩子：删除后将 `u` 的孩子接入 `u` 的父节点中原本 `u` 接入的位置
3. `u` 有两个孩子：删除后按**固定替换策略**处理，规则如下：

   - 记 `L = u.left`，`R = u.right`，`P = parent(u)`。
   - 先用 `R` 顶替 `u` 在其父节点 `P` 处的位置：
     - 如果 `u` 原本是 `P.left`，则令 `P.left = R`
     - 如果 `u` 原本是 `P.right`，则令 `P.right = R`
     - 同时更新 `R.parent = P`
   - 然后将 `L` 挂到 `R` 子树中 **最左侧可继续沿 left 走到链的末端**：
     - 从 `cur = R` 开始
     - 当 `cur.left != null` 时，令 `cur = cur.left`
     - 循环结束后，令 `cur.left = L`，并设置 `L.parent = cur`
   - 最后将 `u.left / u.right / u.parent` 清空，使 `u` 成为孤立节点，等待后续 reinsert。

   这样做的目标是：
   - 删除后剩余节点仍保持为一棵连通二叉树
   - 不丢失 `u` 的左右子树
   - 每个非根节点仍只有一个父节点
   - 不产生环

   删除完成后必须立即执行一次 `validate_tree_structure_after_delete`，至少检查：
   - 根唯一
   - 从根可达所有节点
   - 无环
   - 每个非根节点恰有一个父节点
   - 节点总数（树中节点数+ `u` ）正确

删除后要求：

- 树中所有其余节点仍然连通
- 没有环
- 每个非根节点有且仅有一个父节点
- 根唯一
- 被删除节点 `u` 被独立出来，不再挂在原树上
- `u` 自身的左右孩子关系，在 delete 完成后要么被清空，要么按你定义的“孤立节点”语义正确处理，但必须一致且清楚

注意：
- 这里的“delete”是为了后续 reinsert，不是彻底销毁模块
- delete 后原树节点数减少 1，孤立节点 `u` 保留等待插入

## B. Reinsert 操作（decision-complete 版本）

请实现 `reinsert_node_into_bstar_tree(u, position)`。  
这里的 `u` 是一个已经从树中 delete 下来、当前处于孤立状态的节点。调用 reinsert 前必须保证：

- `u.parent == null`
- `u.left == null`
- `u.right == null`

也就是说，**reinsert 不负责保留 `u` 删除前的子树关系**；delete 完成后，`u` 必须已经是一个纯孤立节点。  
reinsert 的职责只是：把这个孤立节点作为一个新的树节点插入当前树中某个合法位置。

---

### 1. position 的精确定义

请不要用模糊的“插到某个 internal / external position”。  
请把 position 明确定义为一个有限枚举，且每个枚举项都带完整参数。

建议定义为以下四类：

1. `EXTERNAL_LEFT(parent = p)`
2. `EXTERNAL_RIGHT(parent = p)`
3. `INTERNAL_LEFT(parent = p, old_child = c)`
4. `INTERNAL_RIGHT(parent = p, old_child = c)`

其含义分别固定如下。

---

### 2. 四类 position 的精确 rewiring 规则

#### A. `EXTERNAL_LEFT(parent = p)`

前置条件：

- `p` 是树中现有节点
- `p.left == null`
- `u` 不是根
- `u` 当前孤立

rewiring 规则：

- `p.left = u`
- `u.parent = p`
- `u.left = null`
- `u.right = null`

语义：

- 将 `u` 作为 `p` 的一个新的左孩子插入
- 这是 external position，因为它只在树外侧扩展，不替换任何现有边

---

#### B. `EXTERNAL_RIGHT(parent = p)`

前置条件：

- `p` 是树中现有节点
- `p.right == null`
- `u` 当前孤立

rewiring 规则：

- `p.right = u`
- `u.parent = p`
- `u.left = null`
- `u.right = null`

语义：

- 将 `u` 作为 `p` 的一个新的右孩子插入
- 这是 external position，因为它只在树外侧扩展，不替换任何现有边

---

#### C. `INTERNAL_LEFT(parent = p, old_child = c)`

前置条件：

- `p` 是树中现有节点
- `c == p.left`
- `c != null`
- `u` 当前孤立
- `c` 不在 `u` 的子树中（这里对孤立 `u` 恒成立，但仍保留此检查语义）

rewiring 规则：

- 原来边为：`p.left = c`
- 插入后变为：`p.left = u`
- `u.parent = p`
- `u.left = c`
- `u.right = null`
- `c.parent = u`

即：
- 原结构：`p ->left c`
- 新结构：`p ->left u ->left c`

语义：

- 把 `u` 插入到 `p.left` 这条边的中间
- `u` 接管 `p.left`
- 原来的 `c` 下沉成为 `u.left`

注意：

- **这里必须固定 old_child 继续挂在 `u.left`，不能让实现者自由决定挂左还是挂右**
- 这样才能让 move 空间是确定的、可复现的、不同实现一致

---

#### D. `INTERNAL_RIGHT(parent = p, old_child = c)`

前置条件：

- `p` 是树中现有节点
- `c == p.right`
- `c != null`
- `u` 当前孤立
- `c` 不在 `u` 的子树中（这里对孤立 `u` 恒成立，但仍保留此检查语义）

rewiring 规则：

- 原来边为：`p.right = c`
- 插入后变为：`p.right = u`
- `u.parent = p`
- `u.right = c`
- `u.left = null`
- `c.parent = u`

即：
- 原结构：`p ->right c`
- 新结构：`p ->right u ->right c`

语义：

- 把 `u` 插入到 `p.right` 这条边的中间
- `u` 接管 `p.right`
- 原来的 `c` 下沉成为 `u.right`

注意：

- **这里必须固定 old_child 继续挂在 `u.right`**
- 不允许实现者自己决定挂到 `u.left`

---

### 3. 为什么 internal 的 rewiring 必须写死成这样

这里不要给实现者自由发挥。  
请固定采用：

- `INTERNAL_LEFT`：旧孩子下沉到 `u.left`
- `INTERNAL_RIGHT`：旧孩子下沉到 `u.right`

不要允许：
- internal-left 时把旧孩子挂到 `u.right`
- internal-right 时把旧孩子挂到 `u.left`
- 或“任选一边”

因为一旦允许自由选择，不同实现者会得到不同的 move 空间，SA 行为将不可比、不可复现，也无法稳定 debug。

---

### 4. root 相关约束

本任务默认：

- `Move` 只移动非根节点
- `reinsert` 不允许直接把 `u` 插为新根
- 根节点保持原根不变

因此 position 枚举中 **不包含 root-replacement / new-root position**。  
如果后续想扩展根级操作，请单独新增 position 类型，不要在本版里隐式支持。

---

### 5. position 枚举函数必须完整且确定

请实现 `enumerate_reinsert_positions(tree, u)`，返回当前树上 **全部合法** 的 reinsert position，且顺序确定、可 debug。

枚举规则固定为：

- 对树中每个节点 `p`：
  - 若 `p.left == null`，加入 `EXTERNAL_LEFT(p)`
  - 若 `p.right == null`，加入 `EXTERNAL_RIGHT(p)`
  - 若 `p.left != null`，加入 `INTERNAL_LEFT(p, p.left)`
  - 若 `p.right != null`，加入 `INTERNAL_RIGHT(p, p.right)`

注意：
- 不要漏掉 internal position
- 不要因为 external 已存在就跳过 internal
- internal / external 是两类不同位置，若同时合法，必须都枚举出来

---

### 6. reinsert 后必须满足的结构条件

每次 reinsert 完成后，必须立即运行 `validate_tree_structure`，至少检查：

- 根唯一且不变
- 无环
- 所有节点从根可达
- 每个非根节点恰有一个父节点
- 没有节点丢失
- 总节点数与 block 数一致
- parent/left/right 三者关系一致

任一失败，都视为本次 reinsert 非法。

---

### 7. 禁止的实现方式

请不要实现成以下任意一种：

- external / internal 只是一种“抽象标签”，但具体接线由代码临场决定
- internal-left 时旧孩子挂哪边不固定
- internal-right 时旧孩子挂哪边不固定
- reinsert 时偷偷保留 `u` 删除前的 left/right 子树
- 允许一个 position 同时重接多条边
- 允许插入后出现“同一节点两个父亲”

---

### 8. debug 输出要求

对每次 reinsert，请输出明确 debug 信息，至少包括：

- moved node `u`
- position type
- target parent `p`
- old_child（若是 internal）
- rewiring 前的局部关系
- rewiring 后的局部关系
- structure validation 是否通过

例如：

- `REINSERT u=7 pos=EXTERNAL_LEFT parent=3`
- `REINSERT u=7 pos=INTERNAL_RIGHT parent=5 old_child=9`
- `REINSERT FAILED u=7 pos=INTERNAL_LEFT parent=2 reason=cycle_detected`

---

### 9. 本版 move 空间的正式定义

本实现中，Move 的 reinsert 空间被**正式定义**为：

- 所有 `EXTERNAL_LEFT(parent = p)`，其中 `p.left == null`
- 所有 `EXTERNAL_RIGHT(parent = p)`，其中 `p.right == null`
- 所有 `INTERNAL_LEFT(parent = p, old_child = p.left)`，其中 `p.left != null`
- 所有 `INTERNAL_RIGHT(parent = p, old_child = p.right)`，其中 `p.right != null`

除此之外，**不允许实现者额外发明其他 reinsert 位置**。

这样 move 空间才是 decision-complete、确定的、可复现的。

## C. Move 候选位置枚举

不要只随机挑一个插入点后硬插。  
请先实现一个函数，能够在当前 delete 之后的树上，**枚举所有合法的 reinsert position**：

- 所有 external positions
- 所有 internal positions

然后 Move 扰动时可以：
- 随机从完整位置集合中采样一个
- 或先采样位置类型，再在该类型里采样

但总之，**位置空间必须完整**，不能漏掉 internal position。

## D. Move 的边界约束

必须避免以下情况：

- 把节点插回自己的子树内部，形成环
- 把孤立节点重复接入多个父节点
- 插入后根消失或出现多个根
- 插入后原树某些节点丢失
- 插入后节点总数不对
- 插入后 parent 指针和 child 指针不一致

在 reinsert 前后都要做结构检查。

---

# Swap 的实现要求

Swap 只做以下事情：

- 选两个不同节点 `a`, `b`
- 交换它们承载的 `block_id`
- rotate 是 block 属性
- **不要交换 parent/left/right**
- **不要交换子树**

Swap 后直接走全量 decode + 全量合法性检查 + 全量 cost。

---

# Rotate 的实现要求

Rotate 逻辑保持最简单：

- 选一个节点
- 切换其朝向
- 所有模块都可旋转
- 旋转后直接走全量 decode + 全量合法性检查 + 全量 cost

---

# 候选解合法性检查：必须分层实现

请不要只做一个大而糊的 `is_valid()`。  
请拆成下面几层，每层都要有明确错误信息，便于 debug。

## 1. 树结构合法性检查 `validate_tree_structure`

至少检查：

- 根存在且唯一
- 根的 parent 为 null
- 所有其余节点都有且仅有一个 parent
- 无环
- 从根可达所有节点
- 节点数量与 block 数量一致
- 没有重复节点引用
- 没有 child 指针与 parent 指针不一致
- 左右孩子若存在，不能指向自己
- 同一 child 不能同时是两个父的孩子

失败时返回：
- false
- 明确 reason
- 相关节点信息

## 2. decode 过程合法性检查 `validate_decode_success`

以下已经在 bstar_tree2fp 中实现

`bstar_tree_to_floorplan` 若失败，必须返回失败原因，而不是静默给出错误坐标。

至少检查：

- 每个 block 都成功放置
- 坐标数值有效（非 NaN / 非 inf）
- 宽高有效（> 0）
- floorplan 边界有效
- contour 更新过程没有异常

## 3. 几何关系合法性检查 `validate_bstar_geometric_constraints`

这是最关键的一层。  
decode 完成后，必须对树中每条父子边逐条检查：

### 若 `child == parent.left`
必须满足：
- `x_child == x_parent + width_parent`
- child 与 parent 在 y 方向上应存在接触区间，且 child 真实贴在 parent 右侧形成相邻关系
- 不能只是 x 对了但完全悬空或隔着缝

### 若 `child == parent.right`
必须满足：
- `x_child == x_parent`
- `y_child >= y_parent + something`，并且必须与 parent 上边接触，不允许留缝
- 本质上是“上方贴邻且左边界对齐”
- 不能只是左边界对齐但飘在空中

请根据你项目中的坐标体系精确定义“贴边接触”，不要写成模糊的启发式。

如果当前 contour 解码逻辑只保证“放到当前 contour 高度”，那你必须额外检查这个结果是否真的满足 B\*-tree 定义；如果不满足，就直接判非法，而不是默认接受。

## 4. overlap 检查 `validate_no_overlap`

必须检查任意两个 block 不重叠。  
边界接触是允许的，面积交叠不允许。

## 5. admissible / compactness 说明

本项目中，**不把“模块已经完全不能继续向左移动 / 向下移动”作为 SA 的额外硬约束**。

也就是说：
- SA 不需要单独实现一个“还能不能继续左移 / 下移”的 admissible 检查器
- SA 不需要额外证明某个 floorplan 达到了论文之外的全局 compactness

在当前仓库语境下，candidate 是否可接受，以以下条件为准：
- `validate_tree_structure` 通过
- `bstar_tree_to_floorplan(...)` 能成功 decode
- decode 后满足父子几何关系约束
- 无 overlap
- floorplan 边界合法、cost 可正常计算

若满足上述条件，就视为当前项目接受的合法 B\*-tree placement。

因此：
- 不再额外要求“所有模块都不能继续左移 / 下移”
- 不单独增加一个与当前 decoder 语义脱节的 admissible 失败分支

以上所需的 decode / 几何 / overlap / 边界检查，已经在 `bstar_tree2fp` 中实现

---

# 合法性检查总入口

请实现一个统一入口，例如 `evaluate_candidate_tree(tree)`，流程必须是：

1. `validate_tree_structure`
2. 全量 bstar_tree2fp
3. 看看 bstar_tree2fp有没报 runtime error
7. 全量计算 cost
8. 返回：
   - 是否合法
   - floorplan
   - cost
   - 失败原因（若非法）

任何一步失败，都算非法候选。

---

# 非法候选日志：必须详细

请新增一个非法候选日志机制，例如输出到：
- `stdout`
- `invalid_sa_moves_log.txt`
都可以，但至少要有稳定落盘或稳定打印方案。

每次非法候选至少记录：

- SA iteration / temperature / step
- 操作类型：Rotate / Swap / Move
- 若是 Rotate：node id, block id, 旋转前后尺寸/方向
- 若是 Swap：node a / node b, block_a / block_b
- 若是 Move：
  - 被移动节点 `u`
  - delete 前父节点
  - delete 前左右孩子
  - reinsert position 类型（internal / external）
  - reinsert 目标父节点 / 边
  - 插入前相关边关系
- 失败阶段：
  - tree_structure_failed
  - decode_failed
  - geometric_constraint_failed
  - overlap_failed
- 明确失败原因字符串
- 若可能，附一份简短树结构摘要

注意：
- 日志必须是真正能用来复现 bug 的
- 不要只打印 “invalid move”

---

# SA 主循环要求

请实现标准 SA 主循环，至少包含：

1. 用当前 greedy/init 解作为初始状态
   - 初态来源必须为：
     - `InitBStarResult init = build_initial_bstar_result(P, perm);`
     - `current.tree = init.tree`
     - `current.rotate = init.rotate`
     - `current.fp = init.fp`
     - `current.cost = init.cost`
2. 初始状态先完整 decode + cost，确认它是合法起点
3. 对每个温度层执行若干次扰动
4. 每次扰动流程：
   - 从当前状态 clone candidate
   - 应用 Rotate / Move / Swap 之一
   - 全量合法性检查
   - 若非法：reject + 记录 invalid log
   - 若合法：计算 `delta = cand_cost - cur_cost`
   - 若 `delta <= 0` 则接受
   - 否则按 `exp(-delta / T)` 概率接受
5. 若 cand 优于 best，则更新 best
6. 温度逐步下降直到终止

## SA 终止条件（decision-complete）

请将 SA 的终止条件定义为：**以下任一条件满足，即立即停止 SA**。

### 终止条件

1. **达到时间上限**
   - 若设置了 `time_limit_seconds > 0`
   - 且从 SA 开始到当前的累计运行时间 `elapsed_seconds >= time_limit_seconds`
   - 则立即停止

2. **温度低于最小阈值**
   - 若当前温度 `T < min_temperature`
   - 则立即停止

3. **达到最大外层温度轮数**
   - 若已完成的外层温度轮数 `outer_loop_count >= max_outer_loops`
   - 则立即停止

4. **best cost 长时间停滞不变**
   - 设最近一次 outer loop 结束后的最优代价为 `best_cost_current`
   - 设进入该 outer loop 之前的最优代价为 `best_cost_prev`
   - 若：
     - `abs(best_cost_prev - best_cost_current) <= stagnation_epsilon`
   - 则认为这一轮 outer loop 没有实质改善

   维护一个连续停滞计数器 `stagnation_count`：
   - 若本轮满足停滞条件，则 `stagnation_count += 1`
   - 否则 `stagnation_count = 0`

   当：
   - `stagnation_count >= stagnation_outer_loops`
   时，立即停止 SA

---

## 参数定义

请统一使用以下参数名，不要混用别名：

- `time_limit_seconds`
- `initial_temperature`
- `cooling_rate`
- `min_temperature`
- `max_outer_loops`
- `moves_per_temperature`
- `rotate_prob`
- `move_prob`
- `swap_prob`
- `random_seed`
- `stagnation_epsilon`
- `stagnation_outer_loops`
- `debug_sa`

在当前工程实现中，这些参数的落点统一如下：

- CLI 仅保留：
  - `INPUT`
  - `T`
  - `--mode <init|sa>`
  - `--debug`
- 其中：
  - `time_limit_seconds` 由位置参数 `T` 提供
  - `debug_sa` 由 `--debug` 控制
- 其余 SA 参数不走 CLI，也不走额外 config 文件，而是固定写在 `src/sa.cc` 顶部的文件作用域常量中
- 这些固定常量必须集中声明，不要散落在多个函数内部
- 每个常量后面都必须带行尾注释，解释该参数控制的行为

其中：

### `stagnation_epsilon`
用于定义“best cost 基本不变”的阈值。  
只有当相邻两轮 outer loop 的 `best cost` 改变量满足：

- `abs(best_cost_prev - best_cost_current) <= stagnation_epsilon`

才认为这一轮属于“停滞”。

默认建议：
- 若 cost 是浮点数，可设为 `1e-9`、`1e-8` 或与当前 cost 尺度相适配的小阈值
- 不要直接用 `==` 判断浮点 cost 不变

### `stagnation_outer_loops`
表示允许连续多少个 outer loop 处于“停滞”状态。  
当连续停滞轮数达到该值时，终止 SA。

默认建议：
- 例如 `10`、`20`、`30`
- 具体值作为 `src/sa.cc` 顶部常量集中维护，不要散落在逻辑里

---

## 外层循环结束时的停滞判定逻辑

请在每个 outer loop（即一个固定温度层）结束时执行一次：

1. 记录该轮结束时的 `best_cost_current`
2. 与该轮开始前保存的 `best_cost_prev` 比较
3. 若改变量 `<= stagnation_epsilon`，则记为“本轮停滞”
4. 更新 `stagnation_count`
5. 若 `stagnation_count >= stagnation_outer_loops`，则停止 SA
6. 否则进入降温后的下一轮

注意：
- 停滞判定基于 **best cost**
- 不是基于 current cost
- 也不是基于 acceptance ratio

---

## debug 输出要求

在 `debug_sa = true` 时，每个 outer loop 结束后至少输出：

- `outer_loop_count`
- 当前温度 `T`
- 本轮开始前的 `best_cost_prev`
- 本轮结束后的 `best_cost_current`
- 改变量 `abs(best_cost_prev - best_cost_current)`
- 当前 `stagnation_count`
- 是否触发终止条件
- 若触发，输出具体原因：
  - `stop_reason = time_limit`
  - `stop_reason = min_temperature`
  - `stop_reason = max_outer_loops`
  - `stop_reason = stagnation`

---

## 实现要求

请把 SA 终止逻辑实现成一个明确函数，例如：

- `should_stop_sa(state, time_limit_seconds, elapsed_seconds)`

它必须返回：
- `bool should_stop`
- `string stop_reason`

并且按如下优先方式实现：
- 依次检查各终止条件
- 只要任一条件满足，就返回 true
- `stop_reason` 必须是稳定、可 debug 的固定字符串

不要把终止逻辑散落在多个位置，也不要让不同调用点各自决定停机条件。

---

# SA 参数

这里使用的参数名必须与前文“参数定义”一节完全一致，不要再引入别名。  
但当前工程中不把这些参数做成一长串 CLI 选项，而是按以下方式实现：

- CLI 仅支持：
  - `INPUT`
  - `T`
  - `--mode <init|sa>`
  - `--debug`
- `time_limit_seconds` 来自位置参数 `T`
- `debug_sa` 由 `--debug` 控制
- 其余 SA 参数固定写在 `src/sa.cc` 顶部常量中
- 不要额外引入命令行参数、环境变量或 config 文件来覆盖这些固定 SA 参数

固定参数至少包括：

- `time_limit_seconds`
- `initial_temperature`
- `cooling_rate`
- `min_temperature`
- `max_outer_loops`
- `moves_per_temperature`
- `rotate_prob`
- `move_prob`
- `swap_prob`
- `random_seed`
- `stagnation_epsilon`
- `stagnation_outer_loops`
- `debug_sa`

其中：

- `time_limit_seconds` 和 `debug_sa` 属于运行期入口参数
- 其余项属于 `src/sa.cc` 顶部固定常量

默认建议可先给一组保守参数，但要集中写在 `src/sa.cc` 顶部，并给每个参数加注释解释。

---

# 扰动采样策略

默认扰动采样建议：

- Rotate: 0.3
- Move: 0.4
- Swap: 0.3

Move 是最重要的扰动，比例可以更高。  
但请把概率集中写成 `src/sa.cc` 顶部常量，不要散落在采样逻辑里。

---

# debug 输出要求

请保留原有 debug 输出，并增加以下 SA 过程输出。  
在 `debug_sa=true` 时至少打印：

## 初始化阶段
- 初始 tree 摘要
- 初始 floorplan 摘要
- 初始 cost
- 初始合法性检查结果

## 每个温度层
- 当前温度
- 本层尝试次数
- 接受数 / 拒绝数 / 非法数
- 当前 cost
- best cost

## 每次候选
打印一行简洁摘要，例如：
- iter
- op type
- delta
- accepted / rejected / invalid
- current cost
- best cost

## 对非法候选
除了写日志，控制台也应打印简短摘要：
- `INVALID candidate: op=Move reason=geometric_constraint_failed details=...`

## 对 Move
建议额外打印：
- delete 的节点
- reinsert position 类型
- 目标位置
- 插入前后父子关系变化摘要

---

# 最终结果输出要求

SA 结束后：

1. 输出 best cost
2. 输出 best tree 的简要摘要
3. 输出 best floorplan
4. **最终结果文件的格式必须与 init fp 的输出格式完全一致**
5. 如果当前项目已经有“写 init floorplan 结果”的函数，请直接复用这套函数来写 best SA floorplan
6. 不要新增一个与 init fp 不兼容的新格式
7. `solution` 文件只能通过现有 writer 输出；`best cost` / `best tree` / SA 统计信息 / invalid summary 只能输出到控制台或单独日志文件，不能混进 `solution` 文件
8. 若项目同时输出 B\*-tree dump 文件，则该文件必须与当前 mode 的最终 `solution` 对应；在 `sa` 模式下它对应最终 best solution，而不是 init 解

此外建议：
- 同时输出一份 SA 统计信息，例如总步数、accepted/rejected/invalid 数量
- 但这部分可以是额外日志，不能破坏最终 floorplan 主输出格式

---

# 尽量少改但要改对的地方

请遵守以下工程要求：

- 优先复用现有 `bstar_tree_to_floorplan`
- 如果现有 decode 存在“树结构合法但几何不符合论文定义仍被接受”的问题，请在 decode 后增加严格合法性检查，而不是忽略
- 不要删除现有 greedy/init 逻辑
- 新增 SA 入口时，允许：
  - 先跑 greedy/init 通过 `build_initial_bstar_result(...)` 拿到初始树状态
  - 再进入 SA
- CLI 入口仅保留：
  - `INPUT`
  - `T`
  - `--mode init`
  - `--mode sa`
  - `--debug`
- 但不要破坏现有 init 的可用性
- 禁止任何 `floorplan -> B*-tree` 路径
- 除 `T` 和 `--debug` 外，其余 SA 参数固定写在 `src/sa.cc` 顶部常量里，并且每个常量都要带注释解释

保持公开 BStarTree 不变，新增一个 SA 内部专用 EditableBStar，用稳定整数索引保存 block_id/left/right/parent/root。候选操作都在这个内部树上做，评估前再导出成 BStarTree 调现有 decoder。这样能避开 raw pointer 在 delete/reinsert 时的失效问题。

---

# 代码实现风格要求

- 代码要清晰，函数职责明确
- 不要把所有逻辑塞进一个超长函数
- 对每种失败原因都给出明确字符串
- 对树编辑逻辑要写注释，尤其是 delete / reinsert internal / external
- 对合法性检查要写注释，明确每条检查对应的 B\*-tree 论文约束
- 对 debug 输出要可读，不要只打印裸指针地址

---

# 你最终需要交付的内容

请直接修改仓库代码并确保可运行。  
我希望你完成的是“代码修改”，不是只写设计文档。

至少应包含：

1. SA 主循环实现
2. Rotate / Move / Swap 扰动实现
3. 完整 internal / external reinsert 支持
4. 全量合法性检查链路
5. 非法候选日志
6. debug 输出
7. 与 init fp 一致格式的最终 best floorplan 输出
8. 仅保留最小必要命令行参数：`INPUT`、`T`、`--mode <init|sa>`、`--debug`

---

# 最后提醒：最容易出错的点

请重点防止以下问题：

1. Move 删除后树断裂或丢子树
2. Reinsert 时把节点插回自己子树中形成环
3. Swap 错把整个节点位置交换了，而不是只 swap block id
4. decode 结果虽然有坐标，但不满足：
   - 左孩子贴父右侧
   - 右孩子在父上方且左边界对齐
5. 非法候选没有记录完整上下文，导致 debug 无法复现
6. 最终输出格式与 init fp 不一致

---

# 实现优先级建议

如果你需要分步骤改代码，请按下面顺序完成，并确保每一步都能编译运行：

1. 加入 tree clone + SA 状态结构
2. 加入 Rotate / Swap
3. 加入树结构合法性检查
4. 加入 Move 的 delete
5. 加入完整 internal / external reinsert
6. 接入全量 decode + cost
7. 接入非法候选日志
8. 接入 SA 主循环
9. 接入最终 best 输出

---

# 完成后的自检要求

请在代码里或最终说明里确保以下自检结论成立：

- 初始 greedy 解能作为合法 SA 初态
- Rotate 能运行
- Swap 能运行，且只交换 block id
- Move 能覆盖 internal / external position
- 非法候选会被 reject
- 非法原因会被记录
- SA 结束后 best 结果能按 init fp 格式输出

如果发现现有 `bstar_tree_to_floorplan` 本身就会产生不符合 B\*-tree 定义的解，请优先修正或在 SA 评估阶段严格拦截，不要把这种非法解放进 SA 的 current/best 状态。
