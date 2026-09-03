#!/usr/bin/env python3
"""生成项目所需图标的简易 PNG（程序化绘制，避免外部素材依赖）"""
import struct, zlib, os

def make_png(size, draw):
    """draw(x, y, size) -> (r,g,b,a) 或 None(透明)"""
    rows = []
    for y in range(size):
        row = bytearray([0])  # filter type 0
        for x in range(size):
            c = draw(x, y, size)
            if c is None:
                row += bytes((0, 0, 0, 0))
            else:
                row += bytes(c)
        rows.append(bytes(row))
    raw = b"".join(rows)

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(raw, 9))
            + chunk(b"IEND", b""))

BLUE   = (52, 116, 224, 255)
RED    = (224, 82, 82, 255)
GREEN  = (76, 175, 80, 255)
GREY   = (110, 122, 138, 255)
WHITE  = (255, 255, 255, 255)

def circle(cx, cy, r, x, y):
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r

def connect_icon(x, y, s):
    # 插头样式：两个圆点 + 连线
    if circle(s*0.25, s*0.35, s*0.13, x, y): return GREEN
    if circle(s*0.75, s*0.65, s*0.13, x, y): return GREEN
    # 斜线连接两个圆点
    if abs((x - s*0.25) - (y - s*0.35) * (s*0.5/s*0.3)) < s*0.06 and \
       s*0.25 <= x <= s*0.75:
        return GREEN
    return None

def disconnect_icon(x, y, s):
    if circle(s*0.25, s*0.35, s*0.13, x, y): return RED
    if circle(s*0.75, s*0.65, s*0.13, x, y): return RED
    # 断开的斜线（中间缺口）
    if s*0.25 <= x <= s*0.44 or s*0.56 <= x <= s*0.75:
        if abs((x - s*0.25) - (y - s*0.35) * (s*0.5/s*0.3)) < s*0.06:
            return RED
    return None

def open_icon(x, y, s):
    # 文件夹样式
    if s*0.15 <= x <= s*0.85 and s*0.35 <= y <= s*0.75:
        return BLUE
    if s*0.15 <= x <= s*0.45 and s*0.25 <= y <= s*0.35:
        return BLUE
    if s*0.20 <= x <= s*0.80 and s*0.40 <= y <= s*0.70:
        return WHITE
    return None

def send_icon(x, y, s):
    # 纸飞机样式
    if x + y < s * 0.9 and x > s*0.1 and y > s*0.1:
        return BLUE
    if abs(x - y) < s*0.08 and s*0.2 < x < s*0.8:
        return WHITE
    return None

def settings_icon(x, y, s):
    # 齿轮简化：圆环 + 齿
    r = ((x - s/2)**2 + (y - s/2)**2) ** 0.5
    if s*0.28 <= r <= s*0.40: return GREY
    if circle(s/2, s/2, s*0.14, x, y): return None
    import math
    for k in range(8):
        a = k * math.pi / 4
        gx, gy = s/2 + s*0.34*math.cos(a), s/2 + s*0.34*math.sin(a)
        if circle(gx, gy, s*0.09, x, y): return GREY
    return None

icons = {
    "connect.png":    connect_icon,
    "disconnect.png": disconnect_icon,
    "open.png":       open_icon,
    "send.png":       send_icon,
    "settings.png":   settings_icon,
}

if __name__ == "__main__":
    out = os.path.join(os.path.dirname(__file__), "icons")
    os.makedirs(out, exist_ok=True)
    for name, fn in icons.items():
        with open(os.path.join(out, name), "wb") as f:
            f.write(make_png(64, fn))
        print("生成", name)
