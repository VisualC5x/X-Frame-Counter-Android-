from PIL import Image, ImageDraw, ImageFilter, ImageFont
import math, sys
OUT = sys.argv[1] if len(sys.argv) > 1 else "../resources"
FONT = "/usr/share/fonts/opentype/inter/Inter-Bold.otf"
TIERS = [(255,45,60),(255,140,30),(255,225,40),(80,230,90),(40,220,255),(50,130,255),(170,90,255)]
BLUE=(0,170,255); CYAN=(70,235,255)
SS = 3  # supersample

def squircle_bg(S, r):
    bg = Image.new("RGBA", (S, S), (0,0,0,0)); px = Image.new("RGBA", (S, S))
    d = ImageDraw.Draw(px)
    for y in range(S):
        t = y/S; d.line([(0,y),(S,y)], fill=(int(3+6*t), int(7+16*t), int(16+40*t), 255))
    # radial glow
    glow = Image.new("RGBA", (S,S), (0,0,0,0)); gd = ImageDraw.Draw(glow)
    gd.ellipse([S*0.12, S*0.1, S*0.88, S*0.86], fill=(0,120,255,95))
    px.alpha_composite(glow.filter(ImageFilter.GaussianBlur(S*0.12)))
    m = Image.new("L", (S,S), 255)
    bg.paste(px, (0,0), m); return bg, m

def blur(S, fn, rad):
    l = Image.new("RGBA", (S,S), (0,0,0,0)); fn(ImageDraw.Draw(l)); return l.filter(ImageFilter.GaussianBlur(rad))

def ring_arcs(d, S, R, w, gap=5):
    c = S/2; box = [c-R, c-R, c+R, c+R]; seg = 360/7
    for i, col in enumerate(TIERS):
        d.arc(box, -90 + i*seg + gap/2, -90 + (i+1)*seg - gap/2, fill=col+(255,), width=w)

def logo(S=336):
    B = S*SS
    img, mask = squircle_bg(B, 0); mask = Image.new("L", (B,B), 255)   # full-bleed square: Geode draws its own frame
    # faint grid
    g = ImageDraw.Draw(img)
    for i in range(0, B, B//12):
        g.line([(i,0),(i,B)], fill=(30,80,150,40), width=SS); g.line([(0,i),(B,i)], fill=(30,80,150,40), width=SS)
    R = B*0.37; w = int(B*0.035)
    img.alpha_composite(blur(B, lambda d: ring_arcs(d, B, R, w+int(B*0.02)), B*0.02))
    ring = Image.new("RGBA", (B,B), (0,0,0,0)); ring_arcs(ImageDraw.Draw(ring), B, R, w); img.alpha_composite(ring)
    # X with gradient
    p = B*0.30; xw = int(B*0.095)
    def xs(d, col, wid): d.line([(p,p),(B-p,B-p)], fill=col, width=wid); d.line([(B-p,p),(p,B-p)], fill=col, width=wid)
    img.alpha_composite(blur(B, lambda d: xs(d, BLUE+(255,), xw+int(B*0.04)), B*0.03))
    xl = Image.new("RGBA", (B,B), (0,0,0,0)); xs(ImageDraw.Draw(xl), (255,255,255,255), xw)
    grad = Image.new("RGBA", (B,B)); gd = ImageDraw.Draw(grad)
    for y in range(B):
        t=y/B; gd.line([(0,y),(B,y)], fill=(int(70*(1-t)+0*t), int(235*(1-t)+120*t), 255, 255))
    xc = Image.new("RGBA", (B,B), (0,0,0,0)); xc.paste(grad, (0,0), xl.split()[3]); img.alpha_composite(xc)
    # inner highlight line
    hl = Image.new("RGBA", (B,B), (0,0,0,0)); ImageDraw.Draw(hl).line([(p,p),(B-p,B-p)], fill=(255,255,255,70), width=int(xw*0.25)); img.alpha_composite(hl)
    # frame label
    f = ImageFont.truetype(FONT, int(B*0.095))
    ImageDraw.Draw(img).text((B/2, B*0.93), "XFC", font=f, fill=(210,240,255,255), anchor="mm")
    # border
    out = Image.new("RGBA", (B,B), (0,0,0,0)); out.paste(img, (0,0), mask)
    flat = Image.new("RGB", (B,B), (6,16,34)); flat.paste(out, (0,0), out.split()[3])   # opaque, edge to edge
    return flat.resize((S,S), Image.LANCZOS).convert("RGBA")

def btn(S=96):
    B=S*SS; img = Image.new("RGBA", (B,B), (0,0,0,0)); d = ImageDraw.Draw(img)
    d.ellipse([SS*2,SS*2,B-SS*3,B-SS*3], fill=(4,10,24,235))
    img.alpha_composite(blur(B, lambda dd: ring_arcs(dd, B, B*0.45, int(B*0.07)), B*0.02))
    ring_arcs(ImageDraw.Draw(img), B, B*0.45, int(B*0.06), gap=8)
    p=B*0.32; w=int(B*0.1)
    def xs(dd, col, wid): dd.line([(p,p),(B-p,B-p)], fill=col, width=wid); dd.line([(B-p,p),(p,B-p)], fill=col, width=wid)
    img.alpha_composite(blur(B, lambda dd: xs(dd, BLUE+(255,), w+int(B*0.05)), B*0.03))
    xs(ImageDraw.Draw(img), CYAN+(255,), w)
    return img.resize((S,S), Image.LANCZOS)

if __name__ == "__main__":
    l = logo(); l.save(f"{OUT}/../logo.png"); l.resize((128,128), Image.LANCZOS).save(f"{OUT}/xfc-icon.png"); btn().save(f"{OUT}/xfc-btn.png"); print("ok")
