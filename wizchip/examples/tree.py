#!/usr/bin/env python3

import math

from PythonlibVsrc import VoltageSource as vs


SOFT_SEQUENCE_MAX_POINTS = 1024
SHAPE_MAX_POINTS = 499


def add_line(points, x0, y0, x1, y1, n):
    for i in range(n):
        t = i / float(n)
        points.append((x0 + (x1 - x0) * t, y0 + (y1 - y0) * t))


def add_arc(points, cx, cy, rx, ry, a0_deg, a1_deg, n):
    for i in range(n):
        t = i / float(n)
        a = math.radians(a0_deg + (a1_deg - a0_deg) * t)
        points.append((cx + rx * math.cos(a), cy + ry * math.sin(a)))


def add_quadratic(points, x0, y0, cx, cy, x1, y1, n):
    for i in range(n):
        t = i / float(n)
        u = 1.0 - t
        x = (u * u * x0) + (2.0 * u * t * cx) + (t * t * x1)
        y = (u * u * y0) + (2.0 * u * t * cy) + (t * t * y1)
        points.append((x, y))


def downsample_points(points, max_points):
    if len(points) <= max_points:
        return points

    if max_points < 2:
        raise ValueError("max_points must be at least 2")

    step = (len(points) - 1) / float(max_points - 1)
    return [points[int(round(i * step))] for i in range(max_points)]


def fit_xy_to_box(x_values, y_values, max_abs=9.8):
    x_min, x_max = min(x_values), max(x_values)
    y_min, y_max = min(y_values), max(y_values)

    x_center = 0.5 * (x_min + x_max)
    y_center = 0.5 * (y_min + y_max)

    x_half = 0.5 * (x_max - x_min)
    y_half = 0.5 * (y_max - y_min)
    max_half = max(x_half, y_half)
    if max_half == 0.0:
        return [0.0] * len(x_values), [0.0] * len(y_values)

    scale = max_abs / max_half
    return [((x - x_center) * scale) for x in x_values], [((y - y_center) * scale) for y in y_values]


def build_simple_tree_outline():
    points = []

   # ---------- Large left canopy ----------
    add_quadratic(points,
                  -1.5, 0.8,
                  -4.8, 2.0,
                  -6.0, 4.0,
                  22)
    
    add_quadratic(points,
                  -6.0, 4.0,
                  -5.8, 6.0,
                  -4.3, 6.4,
                  22)
    
    add_quadratic(points,
                  -4.3, 6.4,
                  -3.2, 7.4,
                  -1.8, 6.6,
                  20)
    
    add_quadratic(points,
                  -1.8, 6.6,
                  -0.8, 7.8,
                  0.5, 7.1,
                  20)
    
    # ---------- Highest crown ----------
    add_quadratic(points,
                  0.5, 7.1,
                  2.0, 8.3,
                  3.3, 7.2,
                  22)
    
    # ---------- Right canopy ----------
    add_quadratic(points,
                  3.3, 7.2,
                  4.8, 7.9,
                  5.8, 6.6,
                  20)
    
    add_quadratic(points,
                  5.8, 6.6,
                  7.2, 6.0,
                  7.0, 4.0,
                  22)
    
    add_quadratic(points,
                  7.0, 4.0,
                  6.2, 2.2,
                  1.5, 0.8,
                  24)
    
    # ---------- Right trunk ----------
    add_quadratic(points,
                  1.4, 0.8,
                  1.5, -0.8,
                  1.1, -3.0,
                  22)

    # Right root
    add_quadratic(points,
                  1.1, -3.0,
                  2.1, -3.7,
                  0.8, -4.2,
                  16)

    # Bottom
    add_quadratic(points,
                  0.8, -4.2,
                  0.0, -4.5,
                  -0.8, -4.2,
                  16)

    # Left root
    add_quadratic(points,
                  -0.8, -4.2,
                  -2.1, -3.7,
                  -1.1, -3.0,
                  16)

    # ---------- Left trunk ----------
    add_quadratic(points,
                  -1.1, -3.0,
                  -1.5, -0.8,
                  -1.4, 0.8,
                  22)

    return points


def main():
    k = vs(transport="ethernet", ip="192.168.1.52", port=5000)

    shape_points = build_simple_tree_outline()
    shape_points = downsample_points(shape_points, SHAPE_MAX_POINTS)

    if len(shape_points) >= 500:
        raise ValueError(f"Shape point count {len(shape_points)} must be below 500")

    if len(shape_points) > SOFT_SEQUENCE_MAX_POINTS:
        raise ValueError(
            f"Shape point count {len(shape_points)} exceeds soft-sequence max {SOFT_SEQUENCE_MAX_POINTS}"
        )

    x_map = [p[0] for p in shape_points]
    y_map = [p[1] for p in shape_points]
    x_map, y_map = fit_xy_to_box(x_map, y_map, max_abs=9.8)

    k.send_soft_sequence(23, 10, x_map)
    k.send_soft_sequence(0, 10, y_map)
    k.send()

    print(
        f"Sent simple tree outline with {len(x_map)} points "
        f"(shape cap {SHAPE_MAX_POINTS}, firmware max {SOFT_SEQUENCE_MAX_POINTS})."
    )


if __name__ == "__main__":
    main()
