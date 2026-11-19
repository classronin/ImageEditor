"""重建自检用的 256x128 SWOP CMYK 测试图（main.cpp 的 icc 四项断言用）。

配置：C:\\Windows\\System32\\spool\\drivers\\color\\RSWOP.icm
      （Agfa : Swop Standard，K100 -> sRGB(34,33,33)，M100+Y100 -> sRGB(229,51,45)）
版面：左半 = 彩色块 CMYK(0,255,255,0)，右半 = 黑版 CMYK(0,0,0,255)。
"""
from PIL import Image, ImageCms
import os, sys

PROFILE = r"C:\Windows\System32\spool\drivers\color\RSWOP.icm"
OUT = sys.argv[1] if len(sys.argv) > 1 else r"E:\GitHub\C\ImageEditor\build_ronin\bin\Release\icc_test_cmyk.jpg"
W, H = 256, 128
COLOR_CMYK = (0, 255, 255, 0)   # (64,32) 断言 → sRGB(230,52,45) ±4
BLACK_CMYK = (0, 0, 0, 255)     # (192,96) 断言 → sRGB(34,33,33) ±4

prof_bytes = open(PROFILE, 'rb').read()
tr = ImageCms.buildTransform(ImageCms.getOpenProfile(PROFILE),
                             ImageCms.createProfile('sRGB'),
                             'CMYK', 'RGB', renderingIntent=1)
for name, v in (('color', COLOR_CMYK), ('black', BLACK_CMYK)):
    print(name, v, '->', ImageCms.applyTransform(Image.new('CMYK', (1, 1), v), tr).getpixel((0, 0)))

img = Image.new('CMYK', (W, H), COLOR_CMYK)
for y in range(H):
    for x in range(W // 2, W):
        img.putpixel((x, y), BLACK_CMYK)
img.save(OUT, format='JPEG', quality=95, subsampling=0, icc_profile=prof_bytes)
print('written', OUT, os.path.getsize(OUT))
