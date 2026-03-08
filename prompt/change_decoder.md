请对当前 `bstar_tree2fp.md` 做最小修改，只补足“B*-tree 几何合法性校验”，不要重写全文，不要改无关部分。

必须改这几点：

1. 在 B*-tree 语义里补充硬约束：
- left child 不仅要求 `x_child = x_parent + w_parent_used`，还必须与 parent 在竖直方向有非空接触区间
- right child 不仅要求 `x_child = x_parent`，还必须满足 `y_child = y_parent + h_parent_used`
- 若不满足，则此次解码非法

2. 在 contour 解码要求里明确：
- contour 只能用于求候选 `y`
- child 落位后，必须立刻检查它和 parent 是否满足 B*-tree 几何关系
- 若不满足，立即抛 `std::runtime_error`
- 不能只因为“不重叠”就接受

3. 在 helper 建议里补两个函数语义：
- `validate_left_child_geometry(...)`
- `validate_right_child_geometry(...)`

4. 在异常/健壮性要求里新增：
- left child 未贴 parent 右边界时抛异常
- right child 未贴 parent 顶边时抛异常
- floorplan 虽不重叠但不满足 B*-tree 几何定义时抛异常

5. 不要改总体结构
6. 不要新增 SA、cost、fp2bstar_tree 相关内容
7. 直接修改 `bstar_tree2fp.md` 文件