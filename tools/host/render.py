import sys, glob, os
from PIL import Image, ImageDraw, ImageFont
def c565(v):
    v=int(v); return (((v>>11)&31)*255//31, ((v>>5)&63)*255//63, (v&31)*255//31)
font1 = ImageFont.load_default()
try:
    fontm = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 9)
    fontm2 = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 18)
except Exception:
    fontm = fontm2 = font1
S=2
for path in sorted(glob.glob('snaps/*.log')):
    im = Image.new('RGB',(320,240)); d = ImageDraw.Draw(im)
    for line in open(path, encoding='latin-1'):
        p=line.rstrip('\n').split(' ')
        k=p[0]
        try:
            if k=='clear': d.rectangle([0,0,320,240], fill=c565(p[1]))
            elif k=='fr': x,y,w,h,c=map(int,p[1:6]); 
            if k=='fr' and w>0 and h>0: d.rectangle([x,y,x+w-1,y+h-1], fill=c565(c))
            elif k=='dr': x,y,w,h,c=map(int,p[1:6]); d.rectangle([x,y,x+w-1,y+h-1], outline=c565(c))
            elif k=='fc': x,y,r,c=map(int,p[1:5]); d.ellipse([x-r,y-r,x+r,y+r], fill=c565(c))
            elif k=='dc': x,y,r,c=map(int,p[1:5]); d.ellipse([x-r,y-r,x+r,y+r], outline=c565(c))
            elif k=='dl': a,b,e,f,c=map(int,p[1:6]); d.line([a,b,e,f], fill=c565(c))
            elif k=='dp': x,y,c=map(int,p[1:4]); d.point((x,y), fill=c565(c))
            elif k=='ft': a=list(map(int,p[1:8])); d.polygon([(a[0],a[1]),(a[2],a[3]),(a[4],a[5])], fill=c565(a[6]))
            elif k=='tx':
                x,y,sz,c=map(int,p[1:5]); txt=' '.join(p[5:])
                d.text((x,y-1), txt, fill=c565(c), font=fontm2 if sz==2 else fontm)
        except Exception as ex: pass
    im=im.resize((320*S,240*S), Image.NEAREST)
    im.save(path.replace('.log','.png'))
# contact sheet
ims=[Image.open(p) for p in sorted(glob.glob('snaps/*.png')) if 'sheet' not in p]
W,H=ims[0].size; cols=3; rows=(len(ims)+cols-1)//cols
sheet=Image.new('RGB',(W*cols+10*(cols-1), H*rows+10*(rows-1)),(40,40,40))
for i,im in enumerate(ims): sheet.paste(im,((i%cols)*(W+10),(i//cols)*(H+10)))
sheet.save('snaps/sheet.png'); print(sheet.size)
