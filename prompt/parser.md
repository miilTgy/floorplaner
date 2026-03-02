你是一个资深 C++ 工程师。请为一个 floorplanning 作业实现 **Parser 模块**，只负责读取输入 `.txt` 并构建内存数据结构，不负责排序、放置或输出。

## 目标
实现 `Problem parse_problem(const std::string& path)`，读取作业输入文件，解析：
- `chipWidth`
- `Blocks : <numBlocks>` 段（每行：`<blockName> : <w> <h>`）
- `Pins : <numPins>` 段（每行：`<pinName> : <dx> <dy>`，相对 block 中心）
- `Nets : <numNets>` 段（每行：`<netName> : <pinName0> <pinName1> ...`）

并构建 `Problem`，要求后续 ordering/BL/SA 能直接使用（尽量不用字符串遍历）。

输入文件格式上文已指出，您可以阅读输入文件的例子：
@samples/sample_1.txt
@samples/sample_2.txt
@samples/sample_3.txt
@samples/sample_4.txt
@samples/sample_5.txt

## 输入格式细节（必须遵守）
- 行可能有多余空格；冒号 `:` 是关键分隔符。
- 四个 section：`chipWidth`、`Blocks`、`Pins`、`Nets`，顺序如样例所示（但 parser 不要依赖固定行号）。
- `pinName` 必须以其所属 `blockName` 为前缀（例如 block `A` 的 pin 可能叫 `A1` `A_out` 等）。Parser 需要据此推断 `pin.block_id`。
- `Nets` 行中同一个 block 可能出现多个 pin；请保留 `net.pin_ids` 全量，但同时维护 `net.block_ids`（对 block 去重）供 ordering 使用。

## 数值类型要求（务必严格）
- `block w/h`、`chipW`：用 `int`
- `pin dx/dy`：**用 `double`**（输入可能包含 0.5 这类小数）
- `Nets` 中 pin 列表长度不定（2 个或更多）

## 数据结构设计（请按此实现）
`parse_problem` 在 parser.cc 内部创建并返回 Problem；main.cc 只负责调用与后续处理。
main.cc 调 `parse_problem(path)` 得到 `Problem`
用整数 ID 统一引用，构建 name->id 映射，避免后续模块反复查字符串。

- `struct Block { std::string name; int w,h; std::vector<int> pin_ids; std::vector<int> net_ids; };`
- `struct Pin { std::string name; int block_id; double dx,dy; std::vector<int> net_ids; };`
- `struct Net { std::string name; std::vector<int> pin_ids; std::vector<int> block_ids; };`
- `struct Problem { int chipW; std::vector<Block> blocks; std::vector<Pin> pins; std::vector<Net> nets; std::unordered_map<std::string,int> block_id_of, pin_id_of, net_id_of; };`

## Parser 必须完成的“反向索引填充”
解析完后请补齐这些字段（ordering 会用）：
1) `Block.pin_ids`：每个 block 拥有哪些 pin  
2) `Pin.net_ids`：每个 pin 属于哪些 net（通常一个，但不要假设）  
3) `Block.net_ids`：每个 block 参与哪些 net（从 nets 推出来）  
4) `Net.block_ids`：去重后的 blocks 列表（同 block 多 pin 只保留一次）

## 分段识别（必须鲁棒）
不要写死“第几行是什么”。请按以下策略：
- 逐行读取，先 `trim`
- 空行跳过
- 注释行跳过：以 `#` 或 `//` 开头
- 若行包含 `:`：
  - 先用 `split_once(line, ':')` 分成 `lhs` 与 `rhs`，并 `trim(lhs/rhs)`
  - 用 `lhs` 的前缀/关键字判定是 header 还是数据行
- Header 识别规则：
  - `lhs == "chipWidth"` → 解析 rhs 为 `int chipW`
  - `lhs == "Blocks"` → 解析 rhs 为 `numBlocks`，然后进入 Blocks section
  - `lhs == "Pins"` → 解析 rhs 为 `numPins`，进入 Pins section
  - `lhs == "Nets"` → 解析 rhs 为 `numNets`，进入 Nets section

## 数据行解析（请按 section 区分）
### Blocks section 数据行
格式：`<blockName> : <w> <h>`
- `blockName` 在冒号左侧
- `w h` 在右侧，用 `int` 解析
- 构建 `block_id_of[blockName] = id`，并 push 到 `blocks`

### Pins section 数据行
格式：`<pinName> : <dx> <dy>`
- `dx dy` 用 `double` 解析
- 必须从 `pinName` 推断所属 block：
  - 在 `block_id_of` 的所有 key 中做前缀匹配，选 **最长匹配** 的 blockName 作为归属
  - 若没有任何匹配 → `throw runtime_error`（带行号与原始行内容）
- 写入 `pins`、`pin_id_of`，并把该 pin id push 到对应 `Block.pin_ids`

### Nets section 数据行
格式：`<netName> : <pin0> <pin1> ...`
- rhs token 数 >= 2，否则报错
- 逐个 pinName 查 `pin_id_of`，不存在则报错
- `net.pin_ids` 保存全部 pin id（不去重）
- `net.block_ids` 保存去重后的 block id（用 `unordered_set<int>` 去重）
- 同时补齐反向索引：
  - 对每个 pin：`Pin.net_ids.push_back(net_id)`
  - 对每个 block（去重后）：`Block.net_ids.push_back(net_id)`

## 完整性校验（必须做）
- 检查实际读到的 blocks/pins/nets 数量是否等于 header 声明的 num*
- 若不等，报错并指明哪个 section 数量不匹配

## 错误处理与信息（务必清晰）
遇到以下情况直接 `throw std::runtime_error`：
- 缺少 chipWidth 或缺少任一 section header
- section 数据行格式不对（缺冒号、缺字段、无法解析数值）
- pin 前缀无法匹配任何 block
- net 引用了不存在的 pin
- section 计数不匹配
错误信息必须包含：`line_no` + `line_text`（便于调试）

## 输出的代码要求
输出 Parser 模块相关内容（头文件+实现文件，或一个 cpp 文件也行），包含：
- `Problem/Block/Pin/Net` 结构体
- `parse_problem`
- 必要的辅助函数（trim、split_once、tokenize、longestPrefixMatch 等）

## 调试输出（避免副作用）
- Parser 不得在 `parse_problem` 内部打印 stdout，也不得写任何文件。
- 如需调试，请额外提供 `void dump_problem(const Problem&, std::ostream&)`，由 main.cc 在需要时调用。

## 工程要求
parser 的 .cc 源代码只有一个 parser.cc。本项目有一个总的头文件 common.h 请再里面 include 所有必要的库和写必要的宏，然后在每一个单独的宏里面 include 它。

不要实现 ordering / BL / SA / 输出文件。