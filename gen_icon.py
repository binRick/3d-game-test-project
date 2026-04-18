#!/usr/bin/env python3
# Generates a 512x512 iron fist PNG icon
import struct, zlib

def png(w, h, pixels):
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(c[4:]) & 0xffffffff)
    hdr = chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    rows = b''.join(b'\x00' + bytes(pixels[y*w*3:(y+1)*w*3]) for y in range(h))
    idat = chunk(b'IDAT', zlib.compress(rows, 9))
    return b'\x89PNG\r\n\x1a\n' + hdr + idat + chunk(b'IEND', b'')

S = 512
pix = [0] * (S * S * 3)

def setpx(x, y, r, g, b):
    if 0 <= x < S and 0 <= y < S:
        i = (y * S + x) * 3
        pix[i], pix[i+1], pix[i+2] = r, g, b

def circle(cx, cy, r, r2, g2, b2, r1=None, g1=None, b1=None):
    if r1 is None: r1,g1,b1 = r2,g2,b2
    for dy in range(-r, r+1):
        for dx in range(-r, r+1):
            d = (dx*dx + dy*dy) ** 0.5
            if d <= r:
                t = 1 - d/r
                R = int(r1 + (r2-r1)*t)
                G = int(g1 + (g2-g1)*t)
                B = int(b1 + (b2-b1)*t)
                setpx(cx+dx, cy+dy, R, G, B)

def rect(x0, y0, x1, y1, r, g, b):
    for y in range(y0, y1):
        for x in range(x0, x1):
            setpx(x, y, r, g, b)

# Background - dark red circle
circle(S//2, S//2, S//2-4, 160, 0, 0, 220, 0, 0)
circle(S//2, S//2, S//2-4, 100, 0, 0, 100, 0, 0)  # darken edge

# Fist body (main palm block) - skin tone
rect(140, 230, 370, 390, 220, 175, 120)
rect(145, 235, 365, 385, 230, 185, 130)

# Four knuckles across the top
knuckle_x = [155, 210, 265, 320]
for kx in knuckle_x:
    circle(kx+25, 218, 38, 230, 185, 130, 200, 155, 100)
    circle(kx+25, 218, 30, 240, 195, 140, 210, 165, 110)
    # knuckle highlight
    circle(kx+20, 205, 10, 255, 230, 190)

# Thumb (left side, going slightly down)
rect(85, 260, 155, 330, 220, 175, 120)
rect(90, 265, 150, 325, 230, 185, 130)
circle(105, 275, 28, 230, 185, 130, 200, 155, 100)

# Finger dividers (dark lines between fingers)
for fx in [197, 252, 307]:
    rect(fx, 195, fx+8, 310, 100, 65, 40)

# Wrist (bottom of fist)
rect(155, 375, 355, 430, 200, 155, 100)
rect(160, 380, 350, 425, 190, 145, 90)

# Shadow under fist
for y in range(385, 440):
    for x in range(145, 370):
        alpha = (y - 385) / 55
        dx = abs(x - 257)
        if dx < 110 * (1-alpha*0.5):
            setpx(x, y, int(60*(1-alpha)), 0, 0)

# Highlight on top knuckles
for kx in knuckle_x:
    circle(kx+25, 200, 12, 255, 240, 210)

# Border ring
for dy in range(-S//2+4, S//2-3):
    for dx in range(-S//2+4, S//2-3):
        d = (dx*dx + dy*dy) ** 0.5
        if S//2-20 < d < S//2-4:
            t = (d - (S//2-20)) / 16
            setpx(S//2+dx, S//2+dy, int(255*(1-t)+180*t), 0, 0)

data = png(S, S, pix)
with open('/tmp/ironfist.png', 'wb') as f:
    f.write(data)
print("Icon generated: /tmp/ironfist.png")
