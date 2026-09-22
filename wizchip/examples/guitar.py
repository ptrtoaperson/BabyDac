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


def build_guitar_outline():
    points = []

    # One continuous contour: neck + headstock + body.
    add_quadratic(points, 0.95, 1.55, 2.85, 1.85, 4.7, 1.7, 30)      # neck top
    add_quadratic(points, 4.7, 1.7, 5.35, 2.05, 5.9, 1.65, 14)       # headstock top shoulder
    add_quadratic(points, 5.9, 1.65, 6.15, 1.1, 5.92, 0.65, 12)      # rounded tip
    add_quadratic(points, 5.92, 0.65, 5.35, 0.25, 4.65, 0.45, 14)    # headstock bottom shoulder
    add_quadratic(points, 4.65, 0.45, 2.75, 0.2, 0.85, 0.0, 30)      # neck bottom
    add_quadratic(points, 0.85, 0.0, 0.2, -0.25, 0.0, -0.95, 16)     # into waist

    add_quadratic(points, 0.0, -0.95, 1.4, -3.5, -0.6, -4.6, 34)     # lower bout right
    add_quadratic(points, -0.6, -4.6, -3.8, -5.0, -5.0, -2.4, 38)    # lower bout left
    add_quadratic(points, -5.0, -2.4, -5.8, -0.6, -4.4, 0.8, 28)     # upper bout left
    add_quadratic(points, -4.4, 0.8, -2.8, 2.4, -0.8, 2.15, 28)      # shoulder left to top
    add_quadratic(points, -0.8, 2.15, 0.1, 2.0, 0.95, 1.55, 24)      # close at neck heel

    # Move along neck center to start strings near the nut.
    add_line(points, 0.95, 1.55, 4.95, 1.1, 18)

    # Snake strings to avoid repeated bridge->nut jumps.
    string_bridge_x = [-0.45, -0.27, -0.09, 0.09, 0.27, 0.45]
    string_nut_x = [4.90, 4.98, 5.06, 5.14, 5.22, 5.30]
    for i in range(6):
        if i % 2 == 0:
            add_line(points, string_nut_x[i], 1.08, string_bridge_x[i], -2.15, 18)
        else:
            add_line(points, string_bridge_x[i], -2.15, string_nut_x[i], 1.08, 18)

    # End with a single bridge stroke.
    add_line(points, string_nut_x[-1], 1.08, 0.0, -2.15, 18)
    add_arc(points, 0.0, -2.45, 0.8, 0.23, 0.0, 360.0, 28)

    return points


def main():
    k = vs(transport="ethernet", ip="192.168.1.52", port=5000)

    shape_points = build_guitar_outline()
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

    # XY mode: one channel drives X, another drives Y.
    k.send_soft_sequence(23, 12, x_map)
    k.send_soft_sequence(0, 12, y_map)
    k.send()

    print(
        f"Sent guitar outline with {len(x_map)} points "
        f"(shape cap {SHAPE_MAX_POINTS}, firmware max {SOFT_SEQUENCE_MAX_POINTS})."
    )


if __name__ == "__main__":
    main()
