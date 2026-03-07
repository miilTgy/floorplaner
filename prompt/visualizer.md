````markdown id="a1p4pz"
你是一个资深 Python 工程师。请实现一个 **visualizer** 模块，文件名为 `visualizer.py`。这个脚本用于读取作业的 input 文件和 solution 文件，并将当前 floorplan 可视化，帮助我检查：

- block 是否有重叠
- block 是否超出 chipWidth
- rotate 是否生效
- 整体高度 H
- floorplan 是否看起来合理
- **每一条 net 的连线是否符合预期**
- **每个 pin / 端口的位置与名称是否正确**

⚠️ 注意：
- 只实现一个 Python 脚本
- 不要实现 C++ 代码
- 不要修改输入输出文件内容
- 脚本应尽量独立、可直接运行
- 使用 Python 3
- 优先使用标准库 + `matplotlib`
- 如果需要数据处理，可使用 `math`、`argparse`、`pathlib`、`re`
- 不要依赖 pandas / seaborn / plotly

---

# 1. 输入文件格式（必须支持）

## 1.1 input 文件格式
作业 input 文件中至少包含这些 section：

- `chipWidth : <chipWidth>`
- `Blocks : <numBlocks>`
  - 每行：`<blockName> : <w> <h>`
- `Pins : <numPins>`
  - 每行：`<pinName> : <dx> <dy>`
- `Nets : <numNets>`
  - 每行：`<netName> : <pin1> <pin2> ...`

脚本必须正确解析：
- `chipWidth`
- blocks 的 `name, w, h`
- pins 的 `name, dx, dy`
- nets 的 pin 列表

⚠️ 当前作业样例里 net 通常是 2-pin，但脚本应尽量支持一般情况。  
本版可视化时，**若某条 net 恰好有 2 个 pin，则直接画一条 pin-to-pin 直线**。  
若未来遇到 >2 pin 的 net，可以选择：
- 不画，或者
- 用星形连接到 bbox 中心 / 几何中心  
但当前优先支持 2-pin nets。

---

## 1.2 solution 文件格式
solution 文件中每个 block 一行：

`<blockName> : <x> <y> <rotate>`

例如：
- `A : 0 0 0`
- `B : 50 0 1`

语义：
- `(x, y)` 是 block 左下角坐标
- `rotate` 为 0/1
- 若 `rotate=0`，尺寸是 `(w,h)`
- 若 `rotate=1`，尺寸是 `(h,w)`

---

# 2. 脚本功能要求

## 2.1 命令行接口
脚本支持命令行运行：

```bash
python visualizer.py input.txt solution.txt
````

也支持可选参数：

* `--save output.png`

  * 保存图片到文件，而不是只弹窗
* `--dpi 150`

  * 可选图像分辨率
* `--title "sample_1"`

  * 自定义图标题
* `--show-pins`

  * 在图中画出 pin 的位置
* `--pin-labels`

  * 若开启，则显示 pin / 端口名（默认建议开启）

如果参数不合法，打印 usage 并退出。

---

# 3. 解析要求

## 3.1 解析 input.txt

请实现：

* `parse_input(path)`

返回至少包括：

* `chip_width`
* `blocks` 字典：

  * key = blockName
  * value = `{ "w": ..., "h": ... }`
* `pins` 字典：

  * key = pinName
  * value = `{ "block": blockName, "dx": ..., "dy": ... }`
* `nets` 列表：

  * 每个元素形如 `{ "name": ..., "pins": [pin1, pin2, ...] }`

要求：

* 支持空行
* 支持多余空格
* 支持按 section 解析
* blockName / pinName / netName 要按字符串原样保存
* pinName 与 blockName 的归属关系可通过“pin 名前缀匹配 block 名”确定，优先最长前缀匹配

## 3.2 解析 solution.txt

请实现：

* `parse_solution(path)`

返回：

* `placements` 字典：

  * key = blockName
  * value = `{ "x": ..., "y": ..., "rotate": ... }`

要求：

* 支持整数和浮点坐标
* `rotate` 必须解析为 int
* 若某个 blockName 在 input 中不存在，应报错
* 若 input 中的 block 在 solution 中缺失，应报错
* 若 solution 中有额外 block，应报错

---

# 4. 几何计算要求

## 4.1 旋转后的尺寸

对于每个 block：

* 若 `rotate == 0`：

  * `w_used = w`
  * `h_used = h`
* 若 `rotate == 1`：

  * `w_used = h`
  * `h_used = w`

## 4.2 floorplan 总高度

计算：

* `H = max(y + h_used)`

## 4.3 旋转后的 pin 绝对坐标

脚本必须正确计算 pin 的绝对坐标，用于画连线和端口名。

设 block 左下角为 `(x, y)`，旋转后尺寸为 `(w_used, h_used)`。

块中心：

* `cx = x + w_used / 2`
* `cy = y + h_used / 2`

原始 pin 偏移 `(dx, dy)` 来自 input，且是**相对于 block 中心**的偏移。

旋转规则：

* 若 `rotate == 0`：

  * `dx_rot = dx`
  * `dy_rot = dy`
* 若 `rotate == 1`（逆时针 90°）：

  * `dx_rot = -dy`
  * `dy_rot = dx`

则 pin 绝对坐标为：

* `X_pin = cx + dx_rot`
* `Y_pin = cy + dy_rot`

请实现：

* `compute_pin_position(block_name, pin_name, blocks, pins, placements)`

## 4.4 pin 所在边的判定（用于放端口名）

为了把 pin / 端口名放在模块内部且靠近端口，请根据 pin 相对块中心的旋转后偏移 `(dx_rot, dy_rot)` 判断 pin 更靠近哪一条边：

* 若 `abs(dx_rot / (w_used/2)) >= abs(dy_rot / (h_used/2))`

  * 说明更靠近左右边：

    * `dx_rot > 0` → 右边
    * `dx_rot < 0` → 左边
* 否则更靠近上下边：

  * `dy_rot > 0` → 上边
  * `dy_rot < 0` → 下边

若恰好在角附近，按主导方向处理即可，不需要过度复杂。

---

# 5. 检测要求（终端检查即可）

## 5.1 宽度越界检测

每个 block 都要检查：

* `x >= 0`
* `y >= 0`
* `x + w_used <= chipWidth`

若违反：

* 在终端打印 warning

## 5.2 重叠检测

对每对 block 做矩形重叠检测：

矩形 A 与 B 重叠，当且仅当：

* `Ax < Bx + Bw`
* `Ax + Aw > Bx`
* `Ay < By + Bh`
* `Ay + Ah > By`

如果重叠：

* 在终端打印所有重叠对

⚠️ 但是：

* **不要在图中显示 overlaps 统计框**
* **不要在图中显示 boundary violations 统计框**
* 图中也不要额外写 overlap count / boundary violation count 文字

终端可以打印这些信息，但图上不要显示。

---

# 6. 可视化要求（必须使用 matplotlib）

## 6.1 绘制顺序要求（非常重要）

为了避免 block 把 net 挡住，请按以下顺序绘制：

1. 先画所有 block 填充矩形
2. 再画所有 net 连线
3. 再画 block 边框
4. 再画 pin 点（如果开启）
5. 最后画 block name 和 pin / 端口名

也就是说：

* **net 连线必须在 block 填充层之上**
* **block 填充不能盖住 net**
* 文本层应在最上面，保证可读性

请使用 matplotlib 的 `zorder` 明确控制绘制层级。

## 6.2 画布

* x 轴范围至少覆盖 `[0, chipWidth]`
* y 轴范围至少覆盖 `[0, H]`
* 保持坐标比例正确（`ax.set_aspect('equal')`）
* 显示网格（浅色即可）

## 6.3 绘制芯片宽度边界

由于 chip 只有固定宽度，没有固定高度：

* 画出左边界 `x=0`
* 画出右边界 `x=chipWidth`
* 可选画底边 `y=0`

## 6.4 绘制每个 block

每个 block 画成矩形：

* 左下角 `(x, y)`
* 宽高 `(w_used, h_used)`

样式要求：

* **必须用不同颜色区分 rotate**

  * `rotate=0`：一种固定颜色（例如浅蓝）
  * `rotate=1`：另一种固定颜色（例如浅绿）
* 默认黑色边框
* **必须在每个 block 的中心写 block name**

  * 文本放在矩形中心
  * 水平、垂直居中
  * 字体大小适中，尽量保证可读性
  * 即使不额外传参数，也默认显示 block name

## 6.5 绘制 pin

若传入 `--show-pins`：

* 在每个 pin 的绝对坐标处画一个小 marker（例如黑色小圆点）
* marker 尺寸不要过大，避免喧宾夺主

## 6.6 绘制 pin / 端口名（本次新增的核心要求）

若传入 `--pin-labels`（建议默认开启）：

* 必须显示每个 pin / 端口名
* **端口名只能出现在该模块内部，且靠近该端口的位置**
* 绝对不能把端口名画到模块外面

具体规则：

* 如果 pin 在 **上边**

  * 端口名放在 pin 的**下面**（即模块内部方向）
* 如果 pin 在 **下边**

  * 端口名放在 pin 的**上面**
* 如果 pin 在 **左边**

  * 端口名放在 pin 的**右边**
* 如果 pin 在 **右边**

  * 端口名放在 pin 的**左边**

请通过一个较小的偏移量把文字放在模块内部，例如：

* `offset = min(w_used, h_used) * 0.03` 或一个合理小常数
* 文本必须仍然保持在矩形内部
* 水平/垂直对齐方式要和边匹配：

  * 上边：`va='top'`
  * 下边：`va='bottom'`
  * 左边：`ha='left'`
  * 右边：`ha='right'`

如果 pin 非常靠近角，仍按前述“主导边”处理即可，不需要为角点单独分太复杂分支。

## 6.7 绘制 net 连线（本次新增的核心要求）

脚本必须显示每一条连线。

### 当前主要目标：2-pin net

若某条 net 恰好有两个 pin：

* 计算两个 pin 的绝对坐标
* 直接画一条从一个 pin 到另一个 pin 的直线

### 线条样式要求（必须严格遵守）

* **net 必须用纯黑色显示**
* **透明度 alpha = 1.0**（完全不透明）
* 线宽适中，不要太粗
* 由于要避免 block 遮住 net，必须确保 net 的 zorder 高于 block 填充

### 若 net 不是 2-pin

可选策略（二选一即可）：

1. 暂时跳过不画
2. 计算这些 pin 的几何中心，画“每个 pin 到中心”的星形线

但当前重点是：**所有 2-pin net 都必须正确显示为 pin-to-pin 直线**。

## 6.8 标题

图标题至少包含：

* input 文件名
* solution 文件名（可选）
* `chipWidth`
* `H`
* block 数量

例如：

* `sample_1 | chipWidth=100 | H=80 | blocks=10`

⚠️ 不要在图中额外显示 overlaps 和 boundary violations 的文本框。

---

# 7. 终端输出要求

脚本运行后，在终端打印：

* `chipWidth`
* `numBlocks`
* `numPins`
* `numNets`
* `H`
* 是否存在越界
* 是否存在 overlap
* 若有重叠，列出重叠 block 对
* 若有 solution / input 不一致，直接报错退出

例如：

```text id="71c8y8"
chipWidth = 100
numBlocks = 10
numPins = 20
numNets = 14
H = 83
boundary_violations = 0
overlaps = 2
overlap pairs:
  - B vs D
  - F vs H
```

---

# 8. 保存与显示要求

## 8.1 保存图片

若传入 `--save output.png`：

* 用 `plt.savefig(...)`

## 8.2 弹窗显示

若未传 `--save`：

* 调用 `plt.show()`

---

# 9. 实现风格要求

* 使用 Python 3
* 代码清晰、模块化
* 至少拆成这些函数：

  * `parse_input`
  * `parse_solution`
  * `compute_rotated_size`
  * `compute_pin_position`
  * `determine_pin_side`
  * `check_overlaps`
  * `compute_height`
  * `draw_floorplan`
  * `draw_nets`
  * `draw_pin_labels`
  * `main`
* 错误信息清晰
* 不要写得过度花哨
* 优先保证正确性和易用性

---

# 10. 你需要输出什么

只输出完整的 `visualizer.py` 源码，不要输出额外解释。

# 11. 同步更新 Makefile 中的相关内容

同步更新 Makefile 中关于 visualize 的内容
去除过时的参数，加入新的参数（如有）