#!/usr/bin/env python3

import argparse
import math
import sys
from pathlib import Path
from typing import Dict, List, Tuple


def _is_comment_or_empty(line: str) -> bool:
    s = line.strip()
    return not s or s.startswith("#") or s.startswith("//")


def _split_once_colon(line: str, line_no: int, path: Path) -> Tuple[str, str]:
    if ":" not in line:
        raise ValueError(f"{path}:{line_no}: missing ':'")
    lhs, rhs = line.split(":", 1)
    lhs = lhs.strip()
    rhs = rhs.strip()
    if not lhs:
        raise ValueError(f"{path}:{line_no}: empty key before ':'")
    return lhs, rhs


def _parse_int(token: str, line_no: int, path: Path) -> int:
    try:
        return int(token)
    except ValueError as exc:
        raise ValueError(f"{path}:{line_no}: invalid int '{token}'") from exc


def _parse_float(token: str, line_no: int, path: Path) -> float:
    try:
        value = float(token)
    except ValueError as exc:
        raise ValueError(f"{path}:{line_no}: invalid float '{token}'") from exc
    if not math.isfinite(value):
        raise ValueError(f"{path}:{line_no}: non-finite float '{token}'")
    return value


def _fmt_num(value: float) -> str:
    if abs(value - round(value)) <= 1e-9:
        return str(int(round(value)))
    s = f"{value:.6f}".rstrip("0").rstrip(".")
    return "0" if s in ("", "-0") else s


def _longest_prefix_block(pin_name: str, block_names: List[str]) -> str:
    owner = ""
    owner_len = -1
    for bname in block_names:
        if pin_name.startswith(bname) and len(bname) > owner_len:
            owner = bname
            owner_len = len(bname)
    return owner


def parse_input(path: str):
    p = Path(path)
    if not p.is_file():
        raise FileNotFoundError(f"input file not found: {path}")

    chip_width = None
    blocks: Dict[str, Dict[str, float]] = {}
    pins: Dict[str, Dict[str, float]] = {}
    nets: List[Dict[str, object]] = []
    block_order: List[str] = []

    section = None
    expected = {"Blocks": None, "Pins": None, "Nets": None}
    seen = {"Blocks": 0, "Pins": 0, "Nets": 0}

    with p.open("r", encoding="utf-8") as f:
        for line_no, raw in enumerate(f, start=1):
            if _is_comment_or_empty(raw):
                continue
            line = raw.strip()
            lhs, rhs = _split_once_colon(line, line_no, p)

            if lhs == "chipWidth":
                chip_width = _parse_float(rhs, line_no, p)
                section = None
                continue

            if lhs in ("Blocks", "Pins", "Nets"):
                count = _parse_int(rhs, line_no, p)
                if count < 0:
                    raise ValueError(f"{p}:{line_no}: negative {lhs} count")
                expected[lhs] = count
                section = lhs
                continue

            if section is None:
                raise ValueError(f"{p}:{line_no}: data line outside section")

            tokens = rhs.split()
            if section == "Blocks":
                if len(tokens) != 2:
                    raise ValueError(f"{p}:{line_no}: block line must be '<w> <h>'")
                if lhs in blocks:
                    raise ValueError(f"{p}:{line_no}: duplicate block '{lhs}'")
                w = _parse_float(tokens[0], line_no, p)
                h = _parse_float(tokens[1], line_no, p)
                blocks[lhs] = {"w": w, "h": h}
                block_order.append(lhs)
                seen["Blocks"] += 1
            elif section == "Pins":
                if len(tokens) != 2:
                    raise ValueError(f"{p}:{line_no}: pin line must be '<dx> <dy>'")
                if lhs in pins:
                    raise ValueError(f"{p}:{line_no}: duplicate pin '{lhs}'")
                if not blocks:
                    raise ValueError(f"{p}:{line_no}: pin appears before blocks are defined")
                owner = _longest_prefix_block(lhs, list(blocks.keys()))
                if not owner:
                    raise ValueError(
                        f"{p}:{line_no}: pin '{lhs}' does not match any block prefix"
                    )
                dx = _parse_float(tokens[0], line_no, p)
                dy = _parse_float(tokens[1], line_no, p)
                pins[lhs] = {"block": owner, "dx": dx, "dy": dy}
                seen["Pins"] += 1
            elif section == "Nets":
                if len(tokens) < 2:
                    raise ValueError(f"{p}:{line_no}: net line must have at least 2 pins")
                if any(pin_name not in pins for pin_name in tokens):
                    unknown = [pin_name for pin_name in tokens if pin_name not in pins]
                    raise ValueError(
                        f"{p}:{line_no}: net '{lhs}' references unknown pin(s): "
                        + ", ".join(unknown)
                    )
                nets.append({"name": lhs, "pins": tokens[:]})
                seen["Nets"] += 1

    if chip_width is None:
        raise ValueError(f"{p}: missing chipWidth")
    for sec in ("Blocks", "Pins", "Nets"):
        if expected[sec] is None:
            raise ValueError(f"{p}: missing {sec} section")
        if expected[sec] != seen[sec]:
            raise ValueError(
                f"{p}: {sec} count mismatch (declared {expected[sec]}, parsed {seen[sec]})"
            )

    return chip_width, blocks, pins, nets, block_order


def parse_solution(path: str):
    p = Path(path)
    if not p.is_file():
        raise FileNotFoundError(f"solution file not found: {path}")

    placements: Dict[str, Dict[str, float]] = {}
    with p.open("r", encoding="utf-8") as f:
        for line_no, raw in enumerate(f, start=1):
            if _is_comment_or_empty(raw):
                continue
            line = raw.strip()
            lhs, rhs = _split_once_colon(line, line_no, p)
            tokens = rhs.split()
            if len(tokens) != 3:
                raise ValueError(f"{p}:{line_no}: solution line must be '<x> <y> <rotate>'")
            if lhs in placements:
                raise ValueError(f"{p}:{line_no}: duplicate block '{lhs}' in solution")

            x = _parse_float(tokens[0], line_no, p)
            y = _parse_float(tokens[1], line_no, p)
            rotate = _parse_int(tokens[2], line_no, p)
            if rotate not in (0, 1):
                raise ValueError(f"{p}:{line_no}: rotate must be 0 or 1")

            placements[lhs] = {"x": x, "y": y, "rotate": rotate}

    if not placements:
        raise ValueError(f"{p}: empty solution")

    return placements


def compute_rotated_size(w: float, h: float, rotate: int) -> Tuple[float, float]:
    if rotate == 0:
        return w, h
    if rotate == 1:
        return h, w
    raise ValueError(f"invalid rotate value: {rotate}")


def _compute_rotated_offset(dx: float, dy: float, rotate: int) -> Tuple[float, float]:
    if rotate == 0:
        return dx, dy
    if rotate == 1:
        return -dy, dx
    raise ValueError(f"invalid rotate value: {rotate}")


def compute_pin_position(
    block_name: str,
    pin_name: str,
    blocks: Dict[str, Dict[str, float]],
    pins: Dict[str, Dict[str, float]],
    placements: Dict[str, Dict[str, float]],
) -> Tuple[float, float]:
    if block_name not in blocks:
        raise ValueError(f"unknown block '{block_name}' for pin '{pin_name}'")
    if pin_name not in pins:
        raise ValueError(f"unknown pin '{pin_name}'")
    if block_name not in placements:
        raise ValueError(f"missing placement for block '{block_name}'")

    pin = pins[pin_name]
    if pin["block"] != block_name:
        raise ValueError(f"pin '{pin_name}' does not belong to block '{block_name}'")

    block = blocks[block_name]
    place = placements[block_name]
    w_used, h_used = compute_rotated_size(block["w"], block["h"], int(place["rotate"]))
    cx = place["x"] + w_used * 0.5
    cy = place["y"] + h_used * 0.5

    dx_rot, dy_rot = _compute_rotated_offset(pin["dx"], pin["dy"], int(place["rotate"]))

    return cx + dx_rot, cy + dy_rot


def determine_pin_side(dx_rot: float, dy_rot: float, w_used: float, h_used: float) -> str:
    hw = max(w_used * 0.5, 1e-12)
    hh = max(h_used * 0.5, 1e-12)
    if abs(dx_rot / hw) >= abs(dy_rot / hh):
        return "right" if dx_rot >= 0 else "left"
    return "top" if dy_rot >= 0 else "bottom"


def check_overlaps(rects: List[Dict[str, float]]) -> List[Tuple[str, str]]:
    pairs: List[Tuple[str, str]] = []
    for i in range(len(rects)):
        a = rects[i]
        for j in range(i + 1, len(rects)):
            b = rects[j]
            overlap = (
                a["x"] < b["x"] + b["w_used"]
                and a["x"] + a["w_used"] > b["x"]
                and a["y"] < b["y"] + b["h_used"]
                and a["y"] + a["h_used"] > b["y"]
            )
            if overlap:
                pairs.append((a["name"], b["name"]))
    return pairs


def compute_height(rects: List[Dict[str, float]]) -> float:
    return 0.0 if not rects else max(r["y"] + r["h_used"] for r in rects)


def _check_boundary_violations(rects: List[Dict[str, float]], chip_width: float):
    eps = 1e-9
    violations = []
    for rect in rects:
        reasons = []
        if rect["x"] < -eps:
            reasons.append(f"x={_fmt_num(rect['x'])} < 0")
        if rect["y"] < -eps:
            reasons.append(f"y={_fmt_num(rect['y'])} < 0")
        if rect["x"] + rect["w_used"] > chip_width + eps:
            reasons.append(
                f"x+w={_fmt_num(rect['x'] + rect['w_used'])} > chipWidth={_fmt_num(chip_width)}"
            )
        if reasons:
            violations.append((rect["name"], reasons))
    return violations


def _build_rects(
    blocks: Dict[str, Dict[str, float]],
    block_order: List[str],
    placements: Dict[str, Dict[str, float]],
) -> Tuple[List[Dict[str, float]], Dict[str, Dict[str, float]]]:
    rects = []
    rect_of: Dict[str, Dict[str, float]] = {}
    for name in block_order:
        place = placements[name]
        rotate = int(place["rotate"])
        w_used, h_used = compute_rotated_size(blocks[name]["w"], blocks[name]["h"], rotate)
        rect = {
            "name": name,
            "x": float(place["x"]),
            "y": float(place["y"]),
            "rotate": rotate,
            "w_used": w_used,
            "h_used": h_used,
        }
        rects.append(rect)
        rect_of[name] = rect
    return rects, rect_of


def _build_pin_data(
    blocks: Dict[str, Dict[str, float]],
    pins: Dict[str, Dict[str, float]],
    placements: Dict[str, Dict[str, float]],
    rect_of: Dict[str, Dict[str, float]],
):
    pin_data: Dict[str, Dict[str, object]] = {}
    for pin_name, pdata in pins.items():
        block_name = str(pdata["block"])
        if block_name not in rect_of:
            raise ValueError(f"pin '{pin_name}' references unknown block '{block_name}'")

        px, py = compute_pin_position(block_name, pin_name, blocks, pins, placements)
        rotate = int(placements[block_name]["rotate"])
        w_used, h_used = compute_rotated_size(blocks[block_name]["w"], blocks[block_name]["h"], rotate)
        dx_rot, dy_rot = _compute_rotated_offset(float(pdata["dx"]), float(pdata["dy"]), rotate)
        side = determine_pin_side(dx_rot, dy_rot, w_used, h_used)

        pin_data[pin_name] = {
            "x": px,
            "y": py,
            "block": block_name,
            "side": side,
            "rect": rect_of[block_name],
        }
    return pin_data


def draw_floorplan(ax, chip_width: float, rects: List[Dict[str, float]], H: float, title: str):
    from matplotlib.patches import Rectangle

    rotate_color = {0: "#8ecae6", 1: "#b7e4c7"}

    max_x = max([chip_width] + [r["x"] + r["w_used"] for r in rects])
    min_x = min([0.0] + [r["x"] for r in rects])
    max_y = max([H, 1.0] + [r["y"] + r["h_used"] for r in rects])
    min_y = min([0.0] + [r["y"] for r in rects])

    span_x = max_x - min_x
    span_y = max_y - min_y
    margin_x = max(1.0, span_x * 0.03)
    margin_y = max(1.0, span_y * 0.03)

    ax.set_xlim(min_x - margin_x, max_x + margin_x)
    ax.set_ylim(min_y - margin_y, max_y + margin_y)
    ax.set_aspect("equal", adjustable="box")
    ax.grid(True, linestyle="--", linewidth=0.5, alpha=0.35)

    ax.axvline(0, color="dimgray", linewidth=1.4, zorder=0)
    ax.axvline(chip_width, color="dimgray", linewidth=1.4, zorder=0)
    ax.axhline(0, color="dimgray", linewidth=1.0, zorder=0)

    for rect in rects:
        ax.add_patch(
            Rectangle(
                (rect["x"], rect["y"]),
                rect["w_used"],
                rect["h_used"],
                facecolor=rotate_color[rect["rotate"]],
                edgecolor="none",
                linewidth=0.0,
                alpha=0.9,
                zorder=1,
            )
        )

    ax.set_xlabel("X")
    ax.set_ylabel("Y")
    ax.set_title(title)


def draw_nets(
    ax,
    nets: List[Dict[str, object]],
    pin_data: Dict[str, Dict[str, object]],
):
    for net in nets:
        pin_list = list(net["pins"])
        coords = [(pn, float(pin_data[pn]["x"]), float(pin_data[pn]["y"])) for pn in pin_list]
        if len(coords) == 2:
            _, x1, y1 = coords[0]
            _, x2, y2 = coords[1]
            ax.plot([x1, x2], [y1, y2], color="black", linewidth=0.85, alpha=1.0, zorder=2)
        elif len(coords) > 2:
            cx = sum(c[1] for c in coords) / len(coords)
            cy = sum(c[2] for c in coords) / len(coords)
            for _, px, py in coords:
                ax.plot([px, cx], [py, cy], color="black", linewidth=0.75, alpha=1.0, zorder=2)


def draw_pin_labels(ax, pin_data: Dict[str, Dict[str, object]]):
    for pin_name, p in pin_data.items():
        rect = p["rect"]
        side = str(p["side"])
        px = float(p["x"])
        py = float(p["y"])
        w_used = float(rect["w_used"])
        h_used = float(rect["h_used"])

        offset = max(min(w_used, h_used) * 0.03, 0.35)
        margin = max(min(w_used, h_used) * 0.02, 0.15)
        lx = px
        ly = py
        ha = "center"
        va = "center"

        if side == "top":
            ly = py - offset
            va = "top"
        elif side == "bottom":
            ly = py + offset
            va = "bottom"
        elif side == "left":
            lx = px + offset
            ha = "left"
        elif side == "right":
            lx = px - offset
            ha = "right"

        x_low = float(rect["x"]) + margin
        x_high = float(rect["x"]) + w_used - margin
        y_low = float(rect["y"]) + margin
        y_high = float(rect["y"]) + h_used - margin
        if x_low > x_high:
            lx = float(rect["x"]) + w_used * 0.5
        else:
            lx = min(max(lx, x_low), x_high)
        if y_low > y_high:
            ly = float(rect["y"]) + h_used * 0.5
        else:
            ly = min(max(ly, y_low), y_high)

        ax.text(
            lx,
            ly,
            pin_name,
            fontsize=6,
            color="black",
            ha=ha,
            va=va,
            zorder=5,
            clip_on=True,
        )


def _draw_block_borders(ax, rects: List[Dict[str, float]]):
    from matplotlib.patches import Rectangle

    for rect in rects:
        ax.add_patch(
            Rectangle(
                (rect["x"], rect["y"]),
                rect["w_used"],
                rect["h_used"],
                facecolor="none",
                edgecolor="black",
                linewidth=1.1,
                zorder=3,
            )
        )


def _draw_block_names(ax, rects: List[Dict[str, float]]):
    for rect in rects:
        ax.text(
            rect["x"] + rect["w_used"] * 0.5,
            rect["y"] + rect["h_used"] * 0.5,
            rect["name"],
            ha="center",
            va="center",
            fontsize=8,
            color="black",
            zorder=5,
        )


def _draw_pin_points(ax, pin_data: Dict[str, Dict[str, object]]):
    xs = [float(v["x"]) for v in pin_data.values()]
    ys = [float(v["y"]) for v in pin_data.values()]
    ax.scatter(xs, ys, s=10, c="black", alpha=1.0, zorder=4)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Visualize floorplan and net wiring from input and solution files."
    )
    parser.add_argument("input_path", help="Path to input txt")
    parser.add_argument("solution_path", help="Path to solution txt")
    parser.add_argument("--save", default="", help="Save figure to file")
    parser.add_argument("--dpi", type=int, default=150, help="Image DPI")
    parser.add_argument("--title", default="", help="Custom title")
    parser.add_argument("--show-pins", action="store_true", help="Draw pin markers")
    parser.add_argument("--pin-labels", action="store_true", help="Draw pin labels")
    args = parser.parse_args()

    if args.dpi <= 0:
        parser.error("--dpi must be > 0")

    try:
        chip_width, blocks, pins, nets, block_order = parse_input(args.input_path)
        placements = parse_solution(args.solution_path)

        unknown = sorted(name for name in placements if name not in blocks)
        if unknown:
            raise ValueError("solution contains unknown block(s): " + ", ".join(unknown))

        missing = [name for name in block_order if name not in placements]
        if missing:
            raise ValueError("solution missing block(s): " + ", ".join(missing))

        rects, rect_of = _build_rects(blocks, block_order, placements)
        pin_data = _build_pin_data(blocks, pins, placements, rect_of)
        H = compute_height(rects)
        boundary_violations = _check_boundary_violations(rects, chip_width)
        overlap_pairs = check_overlaps(rects)

        print(f"chipWidth = {_fmt_num(chip_width)}")
        print(f"numBlocks = {len(block_order)}")
        print(f"numPins = {len(pins)}")
        print(f"numNets = {len(nets)}")
        print(f"H = {_fmt_num(H)}")
        print(f"boundary_violations = {len(boundary_violations)}")
        print(f"overlaps = {len(overlap_pairs)}")

        if boundary_violations:
            print("boundary warning details:")
            for name, reasons in boundary_violations:
                print(f"  - {name}: {', '.join(reasons)}")

        if overlap_pairs:
            print("overlap pairs:")
            for b1, b2 in overlap_pairs:
                print(f"  - {b1} vs {b2}")

        title = args.title
        if not title:
            title = (
                f"{Path(args.input_path).name} | {Path(args.solution_path).name} | "
                f"chipWidth={_fmt_num(chip_width)} | H={_fmt_num(H)} | blocks={len(block_order)}"
            )

        try:
            import matplotlib.pyplot as plt
        except ImportError as exc:
            raise RuntimeError("matplotlib is required. Please install matplotlib.") from exc

        fig, ax = plt.subplots(figsize=(12, 7))
        draw_floorplan(ax, chip_width, rects, H, title)
        draw_nets(
            ax=ax,
            nets=nets,
            pin_data=pin_data,
        )
        _draw_block_borders(ax, rects)
        if args.show_pins or args.pin_labels:
            _draw_pin_points(ax, pin_data)
        _draw_block_names(ax, rects)
        if args.pin_labels:
            draw_pin_labels(ax, pin_data)

        if args.save:
            plt.savefig(args.save, dpi=args.dpi, bbox_inches="tight")
        else:
            plt.show()
        plt.close(fig)
        return 0
    except Exception as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
