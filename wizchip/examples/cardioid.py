#!/usr/bin/env python3

import numpy as np

from PythonlibVsrc import VoltageSource as vs

k = vs(transport="ethernet", ip="192.168.1.52", port=5000)

t = np.linspace(0.0, 2.0 * np.pi, 400, endpoint=False)

# Classic parametric heart curve (upright, cusp at bottom).
scale = 0.55
x = scale * (16.0 * np.sin(t) ** 3)
y = scale * (
	13.0 * np.cos(t)
	- 5.0 * np.cos(2.0 * t)
	- 2.0 * np.cos(3.0 * t)
	- np.cos(4.0 * t)
)

if not np.isfinite(x).all() or not np.isfinite(y).all():
	raise ValueError("x/y contains NaN or inf; cannot send soft sequence")

k.send_soft_sequence(0,20,y)
k.send_soft_sequence(23,20,x)


k.send()
