#!/usr/bin/env python3
from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.patches import Circle


@dataclass
class BStarNode:
    node_id: int
    module_name: str
    left_id: int
    right_id: int
    left: BStarNode | None = None
    right: BStarNode | None = None


def parse_sample_block_names(path: Path) -> dict[int, str]:
    if not path.exists():
        raise ValueError(f"Sample file not found: {path}")

    def normalize(line: str) -> str:
        line = line.strip()
        if not line:
            return ""
        if line.startswith("#") or line.startswith("//"):
            return ""
        return line

    in_blocks = False
    expected_blocks: int | None = None
    names: list[str] = []
    seen_names: set[str] = set()

    with path.open("r", encoding="utf-8") as f:
        for line_no, raw in enumerate(f, start=1):
            line = normalize(raw)
            if not line:
                continue
            if ":" not in line:
                continue

            lhs, rhs = line.split(":", 1)
            lhs = lhs.strip()
            rhs = rhs.strip()

            if lhs == "Blocks":
                if expected_blocks is not None:
                    raise ValueError(f"{path}:{line_no}: duplicate Blocks header")
                try:
                    expected_blocks = int(rhs)
                except ValueError as e:
                    raise ValueError(f"{path}:{line_no}: invalid Blocks count") from e
                in_blocks = True
                continue

            if not in_blocks:
                continue

            if lhs == "Pins":
                in_blocks = False
                break

            tokens = rhs.split()
            if len(tokens) < 2:
                raise ValueError(
                    f"{path}:{line_no}: malformed block line, expected '<name> : <w> <h>'"
                )
            block_name = lhs
            if block_name in seen_names:
                raise ValueError(f"{path}:{line_no}: duplicate block name {block_name}")
            seen_names.add(block_name)
            names.append(block_name)

    if expected_blocks is None:
        raise ValueError(f"{path}: missing Blocks header")
    if len(names) != expected_blocks:
        raise ValueError(
            f"{path}: block count mismatch, declared {expected_blocks}, parsed {len(names)}"
        )

    return {i: name for i, name in enumerate(names)}


def parse_bstar_file(path: Path, id_to_name: dict[int, str]) -> tuple[dict[int, BStarNode], BStarNode]:
    if not path.exists():
        raise ValueError(f"Input file not found: {path}")

    nodes: dict[int, BStarNode] = {}
    child_ids: set[int] = set()
    parent_of: dict[int, int] = {}

    with path.open("r", encoding="utf-8") as f:
        for line_no, raw in enumerate(f, start=1):
            line = raw.strip()
            if not line:
                continue

            cols = line.split()
            if len(cols) != 8:
                raise ValueError(
                    f"{path}:{line_no}: expected 8 fields, got {len(cols)}"
                )

            try:
                node_id = int(cols[0])
                left_id = int(cols[1])
                right_id = int(cols[2])
                _x = float(cols[3])
                _y = float(cols[4])
                _w = float(cols[5])
                _h = float(cols[6])
                rotate = int(cols[7])
            except ValueError as e:
                raise ValueError(f"{path}:{line_no}: parse error: {e}") from e

            if node_id in nodes:
                raise ValueError(f"{path}:{line_no}: duplicate node_id {node_id}")
            if node_id not in id_to_name:
                raise ValueError(
                    f"{path}:{line_no}: node_id {node_id} not found in sample Blocks mapping"
                )
            if rotate not in (0, 1):
                raise ValueError(f"{path}:{line_no}: rotate must be 0/1, got {rotate}")
            if _w <= 0 or _h <= 0:
                raise ValueError(f"{path}:{line_no}: width and height must be positive")
            for cid, name in ((left_id, "left"), (right_id, "right")):
                if cid < -1:
                    raise ValueError(
                        f"{path}:{line_no}: {name}_child_id must be -1 or >= 0"
                    )

            nodes[node_id] = BStarNode(
                node_id=node_id,
                module_name=id_to_name[node_id],
                left_id=left_id,
                right_id=right_id,
            )

    if not nodes:
        raise ValueError(f"{path}: empty tree file")
    if len(nodes) != len(id_to_name):
        raise ValueError(
            f"{path}: node count ({len(nodes)}) does not match sample block count ({len(id_to_name)})"
        )

    for parent_id, node in nodes.items():
        for child_id, side in ((node.left_id, "left"), (node.right_id, "right")):
            if child_id == -1:
                continue
            if child_id not in nodes:
                raise ValueError(
                    f"{path}: node {parent_id} has missing {side} child {child_id}"
                )
            if child_id in parent_of:
                raise ValueError(
                    f"{path}: node {child_id} has multiple parents "
                    f"({parent_of[child_id]} and {parent_id})"
                )
            parent_of[child_id] = parent_id
            child_ids.add(child_id)
            if side == "left":
                node.left = nodes[child_id]
            else:
                node.right = nodes[child_id]

    root_ids = [nid for nid in nodes if nid not in child_ids]
    if len(root_ids) != 1:
        raise ValueError(f"{path}: expected exactly one root, got {len(root_ids)}")
    root = nodes[root_ids[0]]

    visiting: set[int] = set()
    visited: set[int] = set()

    def dfs_validate(node: BStarNode) -> None:
        if node.node_id in visiting:
            raise ValueError(f"{path}: cycle detected at node {node.node_id}")
        if node.node_id in visited:
            return
        visiting.add(node.node_id)
        if node.left is not None:
            dfs_validate(node.left)
        if node.right is not None:
            dfs_validate(node.right)
        visiting.remove(node.node_id)
        visited.add(node.node_id)

    dfs_validate(root)
    if len(visited) != len(nodes):
        missing = sorted(set(nodes.keys()) - visited)
        raise ValueError(f"{path}: disconnected tree, unreachable nodes: {missing}")

    return nodes, root


def compute_subtree_spans(root: BStarNode) -> dict[int, int]:
    spans: dict[int, int] = {}

    def dfs(node: BStarNode) -> int:
        left_span = dfs(node.left) if node.left is not None else 0
        right_span = dfs(node.right) if node.right is not None else 0

        if node.left is None and node.right is None:
            span = 1
        elif node.left is not None and node.right is not None:
            span = left_span + 1 + right_span
        elif node.left is not None:
            span = left_span + 1
        else:
            span = 1 + right_span

        spans[node.node_id] = span
        return span

    dfs(root)
    return spans


def assign_tree_positions(
    root: BStarNode, spans: dict[int, int], level_gap: float = 1.5
) -> dict[int, tuple[float, float]]:
    pos: dict[int, tuple[float, float]] = {}

    def dfs(node: BStarNode, x_left: float, depth: int) -> None:
        left_span = spans[node.left.node_id] if node.left is not None else 0

        if node.left is not None and node.right is not None:
            node_x = x_left + left_span + 0.5
        elif node.left is not None:
            node_x = x_left + left_span + 0.5
        else:
            node_x = x_left + 0.5

        node_y = -depth * level_gap
        pos[node.node_id] = (node_x, node_y)

        if node.left is not None:
            dfs(node.left, x_left, depth + 1)
        if node.right is not None:
            right_start = x_left + left_span + 1.0
            dfs(node.right, right_start, depth + 1)

    dfs(root, 0.0, 0)
    return pos


def draw_tree(nodes: dict[int, BStarNode], root: BStarNode, title: str):
    spans = compute_subtree_spans(root)
    pos = assign_tree_positions(root, spans)

    fig, ax = plt.subplots(figsize=(11, 7))
    node_radius = 0.28

    visited: set[int] = set()

    def draw_edges(node: BStarNode) -> None:
        if node.node_id in visited:
            return
        visited.add(node.node_id)
        px, py = pos[node.node_id]

        if node.left is not None:
            cx, cy = pos[node.left.node_id]
            ax.plot([px, cx], [py, cy], color="blue", linewidth=1.6, zorder=1)
            draw_edges(node.left)

        if node.right is not None:
            cx, cy = pos[node.right.node_id]
            ax.plot([px, cx], [py, cy], color="red", linewidth=1.6, zorder=1)
            draw_edges(node.right)

    draw_edges(root)

    for node_id in sorted(nodes.keys()):
        x, y = pos[node_id]
        circle = Circle(
            (x, y),
            radius=node_radius,
            facecolor="#d9d9d9",
            edgecolor="black",
            linewidth=1.1,
            zorder=2,
        )
        ax.add_patch(circle)
        ax.text(
            x,
            y,
            nodes[node_id].module_name,
            ha="center",
            va="center",
            fontsize=9,
            color="black",
            zorder=3,
        )

    xs = [xy[0] for xy in pos.values()]
    ys = [xy[1] for xy in pos.values()]
    x_min, x_max = min(xs), max(xs)
    y_min, y_max = min(ys), max(ys)
    margin_x = max(1.0, (x_max - x_min) * 0.08)
    margin_y = max(1.0, (y_max - y_min) * 0.12)
    ax.set_xlim(x_min - margin_x, x_max + margin_x)
    ax.set_ylim(y_min - margin_y, y_max + margin_y)

    ax.set_title(title)
    ax.set_aspect("equal", adjustable="box")
    ax.axis("off")
    plt.tight_layout()
    return fig


def main() -> int:
    parser = argparse.ArgumentParser(description="Visualize B*-tree as a tree-structure graph")
    parser.add_argument("--bstar", required=True, help="Input *_bstar.txt")
    parser.add_argument("--sample", required=True, help="Input sample file, e.g. samples/sample_4.txt")
    parser.add_argument("--output", help="Output PNG path")
    parser.add_argument("--show", action="store_true", help="Display figure window")
    args = parser.parse_args()

    if not args.output and not args.show:
        raise ValueError("must provide --output or --show")
    if args.output and args.show:
        raise ValueError("--output and --show are mutually exclusive")

    bstar_path = Path(args.bstar)
    sample_path = Path(args.sample)
    id_to_name = parse_sample_block_names(sample_path)
    nodes, root = parse_bstar_file(bstar_path, id_to_name)
    fig = draw_tree(nodes, root, title=f"{bstar_path.name} | B*-tree")

    if args.output:
        fig.savefig(args.output, dpi=160, bbox_inches="tight")
    else:
        plt.show()

    plt.close(fig)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        raise SystemExit(1)
