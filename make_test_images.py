"""生成测试图片：造一张宽幅"风景"图，切成3张有30%重叠的照片，模拟实拍序列。"""
import math
import random
from PIL import Image, ImageDraw

random.seed(42)

W, H = 2400, 800
img = Image.new("RGB", (W, H))
d = ImageDraw.Draw(img)

# 天空渐变
for y in range(H // 2):
    t = y / (H / 2)
    d.line([(0, y), (W, y)], fill=(int(60 + 100 * t), int(120 + 80 * t), int(200 + 40 * t)))
# 地面渐变
for y in range(H // 2, H):
    t = (y - H / 2) / (H / 2)
    d.line([(0, y), (W, y)], fill=(int(80 + 60 * t), int(90 - 40 * t), int(50 + 30 * t)))

# 山
for cx, r in [(300, 260), (800, 340), (1400, 300), (1900, 380), (2300, 280)]:
    d.polygon([(cx - r, H // 2), (cx, H // 2 - r), (cx + r, H // 2)],
              fill=(70, 90, 110), outline=(50, 60, 80))
    d.polygon([(cx - r // 4, H // 2 - r + r // 4), (cx, H // 2 - r),
               (cx + r // 4, H // 2 - r + r // 4)], fill=(240, 240, 250))

# 太阳和云
d.ellipse([1800, 60, 1960, 220], fill=(255, 230, 120))
for cx, cy in [(400, 100), (1000, 160), (1600, 80)]:
    for dx, dy, rr in [(0, 0, 60), (50, 10, 45), (-50, 12, 40)]:
        d.ellipse([cx + dx - rr, cy + dy - rr // 2, cx + dx + rr, cy + dy + rr // 2],
                  fill=(245, 245, 250))

# 树（随机但固定种子，保证特征丰富）
for cx in range(60, W, 90):
    h = random.randint(60, 160)
    d.rectangle([cx - 6, H - h - 140, cx + 6, H], fill=(90, 60, 30))
    d.ellipse([cx - h // 2, H - h - 140 - h, cx + h // 2, H - h - 140 + h // 3],
              fill=(30 + random.randint(0, 40), 100 + random.randint(0, 60), 40))

# 切成 3 张，相邻重叠 30%
tile_w = 1000
overlap = 300
step = tile_w - overlap
names = ["img1.jpg", "img2.jpg", "img3.jpg"]
for i, name in enumerate(names):
    left = i * step
    tile = img.crop((left, 0, left + tile_w, H))
    tile.save(f"E:/photo_synthesiser/images/{name}", quality=95)
    print(f"{name}: 横向范围 [{left}, {left + tile_w}]")
print("测试图片生成完成")
