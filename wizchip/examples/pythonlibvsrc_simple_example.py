#!/usr/bin/env python3

import time
import numpy as np
from PIL import Image, ImageDraw, ImageFont

from PythonlibVsrc import VoltageSource as vs

k = vs(transport="ethernet", ip="192.168.1.50", port=5000)

pts = np.zeros(31)
pts[0] = 8.0
pts[29] = 8.0
pts[30] = 0
for i in range(1,15):
    pts[i] = 8.0 - (i * 0.5)
    pts[29-i] = 8.0 - (i * 0.5)






#k.send()

for i in np.linspace(0,1.63,30):
    pts = np.append(pts, np.arctan(i)*8)     
   
pts_e = np.append(pts, 0)





for  i in np.linspace(-6,6,50):
    #pts = np.append(pts, 3.17+(np.tanh(i)*4))
    pts = np.append(pts, 1/(1+np.exp(-i))*8)
pts = np.append(pts  , 0)

for  i in np.linspace(-6,6,50):
    #pts = np.append(pts, 3.17+(np.tanh(i)*4))
    pts = np.append(pts, 1/(1+np.exp(-i))*8)

pts = np.append(pts, np.zeros(5))

k.send_soft_sequence(0, 50, pts)

pts_i =np.array([0])
for i in range(1, 40):
    if i % 2 == 0:
        pts_i = np.append(pts_i, 8)
    else:
        pts_i = np.append(pts_i, 0)

pts_i = np.append(pts_i, np.zeros(5))

k.send_soft_sequence(0,10,pts_i)

k.send()
