你是一个资深 C++ 工程师。现在我的工程里已经有以下模块：

1. Parser 模块  
- API：`Problem parse_problem(const std::string& path);`

2. Orderer 模块  
- API：`std::vector<int> build_initial_ordering(const Problem& P);`

3. Init Planer 模块  
- API：`FloorplanResult build_initial_floorplan(const Problem& P, const std::vector<int>& perm);`

请你实现第四个模块：**writer**（文件名建议 `writer.h` / `writer.cc`），用于把当前 floorplan 按作业要求写成 solution 文件，供 evaluator 和后续 Python 可视化脚本使用。

⚠️ 注意：
- 只实现 writer 模块
- 不要实现 main
- 不要实现图形化脚本
- 不要修改 parser / orderer / init_planer 的已有 API
- 模块必须无副作用，除非显式调用写文件函数

---

# 1. 必须对齐的数据结构

假设工程里已有：

- `struct Problem`
- `struct Block`
- `struct FloorplanResult`

其中至少可用字段有：

## Problem
- `Problem::blocks`

## Block
- `Block::name`

## FloorplanResult
- `std::vector<double> x`
- `std::vector<double> y`
- `std::vector<int> rotate`

语义：
- `x[i], y[i]` 是 block i 左下角坐标
- `rotate[i]` 是 block i 的旋转状态（0/1）

---

# 2. 作业要求的输出格式（必须严格遵守）

输出文件中，每个 block 占一行，格式为：

`<blockName> : <x> <y> <rotate>`

例如：

`A : 0 0 0`  
`B : 50 0 1`

要求：
- `blockName` 使用 `P.blocks[i].name`
- `x` 和 `y` 输出当前 floorplan 中该 block 的左下角坐标
- `rotate` 输出 0 或 1
- 每行以换行结尾
- block 的输出顺序必须 deterministic

## 输出顺序要求
默认按 `block_id` 从小到大输出，即：
- `for i in [0, numBlocks)` 依次输出

不要按 perm 顺序输出，除非调用者显式要求；本模块默认按 block_id 顺序输出。

---

# 3. 你要实现的公共接口

请在 `writer.h` 中声明：

## 3.1 写文件接口
- `void write_solution(const Problem& P, const FloorplanResult& fp, const std::string& output_path);`

功能：
- 将 `fp` 按作业要求写入 `output_path`

## 3.2 可选：写到任意输出流
为了方便测试与调试，建议额外提供：
- `void write_solution_stream(const Problem& P, const FloorplanResult& fp, std::ostream& os);`

功能：
- 将 solution 内容写到任意 `ostream`
- `write_solution(...)` 内部可以直接调用它

---

# 4. 输出细节要求（必须实现）

## 4.1 坐标格式
由于 `FloorplanResult.x/y` 可能是 double：
- 若坐标本质上是整数（例如与最近整数相差小于 `1e-9`），则输出为整数形式，不带多余小数：
  - `50` 而不是 `50.000000`
- 若确实存在非整数，则输出为简洁的小数字符串：
  - 不要使用科学计数法
  - 使用固定精度后再去掉末尾多余的 0
  - 例如：
    - `12.5`
    - `7.25`
    - `0`

建议实现一个辅助函数：
- `std::string format_coord(double v);`

要求：
- `1e-9` 以内视为整数
- 非整数时用 `std::fixed << std::setprecision(6)` 之类的方法格式化，再去掉末尾 0 和末尾的小数点

## 4.2 rotate 格式
- 只能输出 `0` 或 `1`
- 若 `fp.rotate[i]` 不是 0/1，应抛出异常

## 4.3 行格式
每行严格输出：
- `name`
- 空格
- `:`
- 空格
- `x`
- 空格
- `y`
- 空格
- `rotate`

不要额外输出注释、header、summary、debug 行。

---

# 5. 数据一致性检查（必须做）

在写出 solution 之前，请做基本检查；若不满足则抛出 `std::runtime_error`，错误信息要清晰：

## 5.1 尺寸一致
- `fp.x.size() == P.blocks.size()`
- `fp.y.size() == P.blocks.size()`
- `fp.rotate.size() == P.blocks.size()`

## 5.2 rotate 合法
- 每个 `rotate[i]` 必须是 0 或 1

## 5.3 坐标有效
- `x[i]` 和 `y[i]` 必须是有限数（不是 NaN / inf）

不要求 writer 自己检查重叠或宽度越界；这些属于 planer / evaluator 的职责。  
writer 只做“结构一致性”和“可输出性”的检查。

---

# 6. 写文件行为要求

`write_solution(...)` 的行为：

1. 打开 `output_path`
2. 若打开失败，抛 `std::runtime_error`
3. 调用 `write_solution_stream(...)`
4. 正常关闭文件

不要自动创建目录；如果目录不存在，直接报错即可。

---

# 7. Debug / Dump 设施（可选但推荐）
为了便于测试，可以额外提供：

- `std::string solution_to_string(const Problem& P, const FloorplanResult& fp);`

要求：
- 返回完整的 solution 文本
- 内部可复用 `write_solution_stream(...)`

但这不是必须项，优先保证 `write_solution(...)` 和 `write_solution_stream(...)` 正确。

---

# 8. 实现风格要求
- 使用 C++17
- 不依赖动态库
- 代码清晰、模块化、可读
- 默认不打印 stdout/stderr
- 错误通过 `std::runtime_error` 抛出
- 优先保证格式严格、结果 deterministic

---

# 9. 你需要输出什么
只输出：
- `writer.h`
- `writer.cc`

不要输出其他文件，不要额外解释。