"""把根目录的 icon.png 转成多尺寸 icon.ico（供编译进 exe）。

用法: python tools/make_icon.py
依赖: pip install pillow
说明: 改了 icon.png 后重新运行本脚本，再重新编译即可。
"""
import os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "icon.png")
DST = os.path.join(ROOT, "icon.ico")
SIZES = [16, 24, 32, 48, 64, 128, 256]

def main():
    img = Image.open(SRC).convert("RGBA")
    # 非正方形则居中裁成正方形
    w, h = img.size
    if w != h:
        side = min(w, h)
        img = img.crop(((w - side) // 2, (h - side) // 2,
                        (w + side) // 2, (h + side) // 2))
    img.save(DST, format="ICO", sizes=[(s, s) for s in SIZES])
    print(f"已生成 {DST}，包含尺寸: {SIZES}")

if __name__ == "__main__":
    main()
