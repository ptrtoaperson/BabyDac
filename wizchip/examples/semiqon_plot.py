#!/usr/bin/env python3

from PythonlibVsrc import VoltageSource as vs

k = vs(transport="ethernet", ip="192.168.1.52", port=5000)


BOTTOM = 0.8
MID = 4.0
TOP = 7.2


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


def glyph_s(w):
	return [
		(0.0, BOTTOM),
		(w, BOTTOM),
		(w, MID),
		(0.0, MID),
		(0.0, TOP),
		(w, TOP),
		(0.0, TOP),
		(0.0, MID),
		(w, MID),
		(w, BOTTOM),
	]


def glyph_e(w):
	return [
		(0.0, BOTTOM),
		(0.0, TOP),
		(w, TOP),
		(0.0, TOP),
		(0.0, MID),
		(w, MID),
		(0.0, MID),
		(0.0, BOTTOM),
		(w, BOTTOM),
	]


def glyph_m(w):
	return [
		(0.0, BOTTOM),
		(0.0, TOP),
		(0.5 * w, MID),
		(w, TOP),
		(w, BOTTOM),
	]


def glyph_i(w):
	c = 0.5 * w
	return [
		(0.0, BOTTOM),
		(w, BOTTOM),
		(c, BOTTOM),
		(c, TOP),
		(0.0, TOP),
		(w, TOP),
		(c, TOP),
		(c, BOTTOM),
		(w, BOTTOM),
	]


def glyph_q(w, h_scale=1.0):
	top_q = BOTTOM + (TOP - BOTTOM) * h_scale
	mid_q = BOTTOM + (MID - BOTTOM) * h_scale
	tail_up = 1.2 * h_scale
	tail_down = 0.2 * h_scale
	return [
		(0.0, BOTTOM),
		(0.0, top_q),
		(w, top_q),
		(w, BOTTOM),
		(0.0, BOTTOM),
		(0.65 * w, BOTTOM + tail_up),
		(w + 0.9, BOTTOM - tail_down),
		(w + 1.2, BOTTOM),
	]


def glyph_o(w):
	return [
		(0.0, BOTTOM),
		(0.0, TOP),
		(w, TOP),
		(w, BOTTOM),
		(0.0, BOTTOM),
		(w, BOTTOM),
	]


def glyph_n(w):
	return [
		(0.0, BOTTOM),
		(0.0, TOP),
		(w, BOTTOM),
		(w, TOP),
		(w, BOTTOM),
	]


def translate(points, x_off):
	return [(x + x_off, y) for x, y in points]


def build_word_points():
	width = 3.6
	narrow = 1.6
	q_width = width * 1.5
	q_height_scale = 1.5
	spacing = 1.8

	letters = [
		(glyph_s(width), width),
		(glyph_e(width), width),
		(glyph_m(width), width),
		(glyph_i(narrow), narrow),
		(glyph_q(q_width, q_height_scale), q_width + 1.2),  # account for Q tail
		(glyph_o(width), width),
		(glyph_n(width), width),
	]

	word_points = []
	cursor_x = 0.8

	for pts, glyph_width in letters:
		shifted = translate(pts, cursor_x)
		if not word_points:
			word_points.extend(shifted)
		else:
			# Move along baseline between letters to keep a controlled connector.
			word_points.append((cursor_x, BOTTOM))
			word_points.extend(shifted)
		cursor_x += glyph_width + spacing

	return word_points


word_points = build_word_points()
x_map, y_map = interpolate(word_points, steps_per_segment=8)

# Fit entire word inside DAC/scope range limit of +/-10 V in XY plane.
x_map, y_map = fit_xy_to_box(x_map, y_map, max_abs=9.8)

k.send_soft_sequence(23, 12, x_map)
k.send_soft_sequence(0, 12, y_map)
k.send()

