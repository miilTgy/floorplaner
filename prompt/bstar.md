你是一个资深 Python 工程师。现在我要修改 `bstar.py` 脚本，将 B*-tree 可视化时**节点显示为模块名称（字母）**，而不是 node_id/编号。请按照以下要求生成提示词：

---

## 1. 输入文件格式

- B*-tree 文件仍然是：
```

node_id left_child_id right_child_id x y w h rotate

```
- 模块名称需要从对应的 **floorplan样本文件**中读取：
  - 例如 `samples/sample_4.txt`
  - 每行包含模块信息，至少包括模块 ID 和名称（例如 `A`, `B`, …）
- 脚本需要将 node_id 映射到模块名称

---

## 2. 输出目标

- 树结构图显示节点为 **模块名称**（例如 `A`, `B`, …），而不是编号
- 保留原来的 DFS 树布局
- 左/右子节点用边区分颜色（蓝色/红色）
- 保存为 PNG，可选显示

---

## 3. Python 模块和库

- 使用 `matplotlib` + `networkx` 或 `matplotlib` 纯绘图
- 使用 `argparse` 接收：
  - `--bstar`: B*-tree 文件
  - `--sample`: 对应 floorplan 样本文件
  - `--output`: 输出 PNG 路径
  - `--show`: 可选，显示图像窗口

---

## 4. 数据结构

- 用字典映射 node_id → module_name
- B*-tree 节点类：
```python
class BStarNode:
    def __init__(self, node_id, module_name):
        self.node_id = node_id
        self.module_name = module_name
        self.left = None
        self.right = None
````

---

## 5. 构建树

1. 读取 floorplan 样本文件，生成：

   * `id_to_name = {node_id: 'A', …}`
2. 读取 B*-tree 文件，创建节点对象
3. 用 left_child_id / right_child_id 连接左右子节点
4. 将 node_id 映射为 module_name
5. 找 root：node_id 未出现在任何子节点中

---

## 6. 可视化方法

* DFS 遍历树
* 节点显示模块名称：

```python
plt.text(x, y, module_name, ha='center', va='center')
```

* 画圆圈表示节点
* 左子节点边蓝色，右子节点边红色
* 树层级布局保持清晰

---

## 7. 输出要求

* PNG 文件中：

  * 节点为模块名称（字母）
  * 左/右子关系可直观看出
* DFS 遍历顺序 deterministic
* 可直接通过命令行：

```bash
python bstar.py --bstar sample_4_bstar.txt --sample samples/sample_4.txt --output sample_4_bstar_tree.png --show
```

> 这样可视化图将清楚显示 B*-tree 树结构，并用模块名称标注节点。

```
```
