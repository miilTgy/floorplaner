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
   - **只交换 block id / block reference / rotation state**
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
  - 上述非法情况不需要主动判定，bstar_tree2fp 模块会主动报 runtime error 。
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
- 不是 admissible placement

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

1. `u` 是叶子
2. `u` 只有一个孩子
3. `u` 有两个孩子

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

## B. Reinsert 操作

请实现 `reinsert_node_into_bstar_tree(node, position)`，其中 position 不是简单的“某个空 left/right child”。

必须支持：

### 1）External position
即插入到树外侧扩展位置。  
典型表现是新节点成为某个节点的新孩子，并把原有关系适当下挂。  
你需要根据当前仓库的树表示，明确定义 external position 的枚举方式与插入规则，并确保不会破坏树结构。

### 2）Internal position
即插入到树内部位置。  
这意味着不是只有“当前 child 为 null”时才能插入，而是允许把一个新节点插到某条现有父子关系之间：

- 原来 `p -> c`
- 插入后变成 `p -> u -> c`

同时必须根据 B\*-tree 的 left/right child 语义，明确：
- 插入的是 left edge 还是 right edge
- 原 child 如何挂到新节点上
- 另一侧 child 如何保持不变
- 不允许因为插入导致子树丢失

请显式枚举并实现 internal / external position 的表示方法，例如：
- “把 u 插到 p 的 left edge 上”
- “把 u 插到 p 的 right edge 上”
- “把 u 插到 p.left 这条边中间”
- “把 u 插到 p.right 这条边中间”
具体命名可调整，但语义要清楚、可枚举、可 debug 输出。

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
- 若 rotation 状态是跟随 block 的，也一并交换
- 若 rotation 状态是节点状态，也请明确并保持语义一致
- **不要交换 parent/left/right**
- **不要交换子树**

Swap 后直接走全量 decode + 全量合法性检查 + 全量 cost。

---

# Rotate 的实现要求

Rotate 逻辑保持最简单：

- 选一个节点
- 切换其朝向
- 如果模块不可旋转，跳过或不把它纳入 rotate 候选
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

## 5. admissible / compactness 检查 `validate_admissible_floorplan`

至少应检查：
- 不存在还能继续向左移动的模块
- 不存在还能继续向下移动的模块

如果现有项目的 contour decode 理论上保证了某种紧致性，也仍建议实现一个显式检查函数；若实现成本太高，可至少检查：
- 所有模块的放置都来自 contour 支撑
- 没有明显悬空
- 没有可见空隙违反 B\*-tree 邻接定义

如果完全严格的 admissible 检查难以优雅实现，请务必至少把“父子贴邻关系 + 无 overlap + decode 成功”检查做严。

以上已经在 bstar_tree2fp 中实现

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
  - admissible_failed
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

---

# SA 参数

请把 SA 参数做成可配置，尽量复用项目现有参数风格，例如命令行参数或 config。

至少支持：

- `initial_temperature`
- `cooling_rate`
- `min_temperature`
- `moves_per_temperature`
- `max_steps` 或 `max_outer_loops`
- `rotate_prob`
- `move_prob`
- `swap_prob`
- `random_seed`
- `debug_sa`

若仓库已有参数系统，就接入现有系统。  
若没有，就加最小必要参数。

默认建议可先给一组保守参数，但不要写死在逻辑里。

---

# 扰动采样策略

默认扰动采样建议：

- Rotate: 0.3
- Move: 0.4
- Swap: 0.3

Move 是最重要的扰动，比例可以更高。  
但请把概率参数化。

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
- 也可以做成开关：
  - `--mode init`
  - `--mode sa`
  - 或 `--sa` 之类
- 但不要破坏现有 init 的可用性
- 禁止任何 `floorplan -> B*-tree` 路径

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
8. 若需要，补充最小必要的命令行参数

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
