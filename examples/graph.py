#!/usr/bin/env python3

import time
from PythonlibVsrc import VoltageSource as vs

time.sleep(5)
k = vs(transport="ethernet", ip="192.168.1.50", port=5000)


def interpolate(points, steps_per_segment=16):
	"""Create smooth XY lists from control points for scope XY mode."""
	x_map = []
	y_map = []

	for i in range(len(points) - 1):
		x0, y0 = points[i]
		x1, y1 = points[i + 1]
		for s in range(steps_per_segment):
			t = s / float(steps_per_segment)
			x_map.append(x0 + (x1 - x0) * t)
			y_map.append(y0 + (y1 - y0) * t)

	# Keep the last point so the sequence finishes at the intended endpoint.
	x_map.append(points[-1][0])
	y_map.append(points[-1][1])
	return x_map, y_map


def fit_xy_to_box(x_values, y_values, max_abs=9.8):
	"""Center and uniformly scale XY data to fit inside [-max_abs, max_abs] on both axes."""
	x_min, x_max = min(x_values), max(x_values)
	y_min, y_max = min(y_values), max(y_values)

	x_center = 0.5 * (x_min + x_max)
	y_center = 0.5 * (y_min + y_max)

	x_half = 0.5 * (x_max - x_min)
	y_half = 0.5 * (y_max - y_min)
	max_half = max(x_half, y_half)

	if max_half == 0:
		zeros = [0.0 for _ in x_values]
		return zeros, zeros

	scale = max_abs / max_half
	x_fit = [(x - x_center) * scale for x in x_values]
	y_fit = [(y - y_center) * scale for y in y_values]
	return x_fit, y_fit


# Continuous M stroke (volts):
# bottom-left -> top-left -> center-bottom -> top-right -> bottom-right
m_points = [
	(0.8, 0.8),
	(0.8, 7.2),
	(2.8, 3.6),
	(4.8, 7.2),
	(4.8, 0.8),
]

# E to the right of M, with spacing.
# Stroke order: top bar, vertical spine, middle bar, bottom bar.
e_points = [
	(6.6, 7.2),
	(10.2, 7.2),
	(6.6, 7.2),
	(6.6, 0.8),
	(9.6, 0.8),
	(6.6, 0.8),
	(6.6, 4.0),
	(9.2, 4.0),
]

# S to the right of E, with the same spacing used between M and E.
s_points = [
	(15.6, 7.2),
	(12.0, 7.2),
	(12.0, 4.0),
	(15.6, 4.0),
	(15.6, 0.8),
	(12.0, 0.8),
]

# Next S
s2_points = [
    (21.0, 7.2),
    (17.4, 7.2),
    (17.4, 4.0),
    (21.0, 4.0),
    (21.0, 0.8),
    (17.4, 0.8),
]


# Join all letters in one XY sequence.
#word_points = m_points + [e_points[0]] + e_points + [s_points[0]] + s_points  + [s2_points[0]] + s2_points 

# Join all letters in one XY sequence.
# Join all letters in one XY sequence.
word_points = (

    # M
    m_points +

    # Retrace down right leg and move along baseline to E
    [
        (4.8, 7.2),
        (4.8, 0.8),
        (6.6, 0.8),
        (6.6, 7.2),
    ] +

    # E
    [
        (10.2, 7.2),
        (6.6, 7.2),
        (6.6, 0.8),
        (9.6, 0.8),
        (6.6, 0.8),
        (6.6, 4.0),
        (9.2, 4.0),

        # Return to bottom of E
        (6.6, 4.0),
        (6.6, 0.8),

        # Travel on baseline to S
        (12.0, 0.8),
    ] +

    # First S
    [
        (15.6, 0.8),
        (15.6, 4.0),
        (12.0, 4.0),
        (12.0, 7.2),
        (15.6, 7.2),

        # Retrace back down
        (12.0, 7.2),
        (12.0, 4.0),
        (15.6, 4.0),
        (15.6, 0.8),

        # Travel to second S
        (17.4, 0.8),
    ] +

    # Second S
    [
        (21.0, 0.8),
        (21.0, 4.0),
        (17.4, 4.0),
        (17.4, 7.2),
        (21.0, 7.2),

        # Retrace back down
        (17.4, 7.2),
        (17.4, 4.0),
        (21.0, 4.0),
        (21.0, 0.8),

        # Travel to I
        (23.8, 0.8),
    ] +

    # I
    [
        # Up the stem
        (23.8, 7.2),

        # Top bar
        (22.6, 7.2),
        (25.0, 7.2),

        # Back to center
        (23.8, 7.2),

        # Down the stem
        (23.8, 0.8),

        # Bottom bar
        (22.6, 0.8),
        (25.0, 0.8),
    ]
)
x_map, y_map = interpolate(word_points, steps_per_segment=10)

# Fit entire word inside DAC/scope range limit of +/-10 V in XY plane.
x_map, y_map = fit_xy_to_box(x_map, y_map, max_abs=9.8)


k.send_soft_sequence(1, 12, x_map)
k.send_soft_sequence(0, 12, y_map)


k.send()
