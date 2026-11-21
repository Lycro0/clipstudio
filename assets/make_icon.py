# Draws the ClipStudio app icon. Usage: python make_icon.py <output dir>
# Writes icon-1024.png, icon-256.png and ClipStudio.ico. Needs Pillow.
# Draws the ClipStudio icon: white rounded square with a glowing black play triangle.
import sys
from PIL import Image, ImageDraw, ImageFilter

out = sys.argv[1]
S = 1024
img = Image.new("RGBA", (S, S), (0, 0, 0, 0))

# White rounded square with a subtle top-to-bottom grey gradient.
bg = Image.new("RGBA", (S, S))
grad = ImageDraw.Draw(bg)
for y in range(S):
    v = int(255 - 22 * (y / S))
    grad.line([(0, y), (S, y)], fill=(v, v, v, 255))
mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle([40, 40, S - 40, S - 40], radius=220, fill=255)
img.paste(bg, (0, 0), mask)

# Play triangle (optically centered: nudged right).
cx, cy, h = S * 0.53, S * 0.5, S * 0.46
tri = [(cx - h * 0.42, cy - h / 2), (cx - h * 0.42, cy + h / 2), (cx + h * 0.5, cy)]

# Glow: blurred dark copies of the triangle, clipped to the square.
for radius, alpha in ((110, 200), (60, 210), (28, 220), (12, 230)):
    g = Image.new("L", (S, S), 0)
    ImageDraw.Draw(g).polygon(tri, fill=alpha)
    g = g.filter(ImageFilter.GaussianBlur(radius))
    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    glow.putalpha(Image.composite(g, Image.new("L", (S, S), 0), mask))
    img = Image.alpha_composite(img, glow)

# Crisp black triangle with rounded corners (stroke join).
tri_layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(tri_layer)
d.polygon(tri, fill=(8, 8, 8, 255))
d.line(tri + [tri[0]], fill=(8, 8, 8, 255), width=40, joint="curve")
for p in tri:
    d.ellipse([p[0] - 20, p[1] - 20, p[0] + 20, p[1] + 20], fill=(8, 8, 8, 255))
img = Image.alpha_composite(img, tri_layer)

img.save(f"{out}/icon-1024.png")
img.resize((256, 256), Image.LANCZOS).save(f"{out}/icon-256.png")
img.save(f"{out}/ClipStudio.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print("ok")
