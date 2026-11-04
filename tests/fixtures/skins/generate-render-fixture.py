"""Regenerate the tiny deterministic renderer fixture; Python stdlib only, not a game dependency."""
from pathlib import Path
import struct,zlib,json,zipfile

def png(w,h,raw):
 def chunk(k,d):return struct.pack('>I',len(d))+k+d+struct.pack('>I',zlib.crc32(k+d))
 return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
white=png(4,2,(b'\x00'+b'\xff\xff\xff\xff'*4)*2)
alpha=png(4,2,(b'\x00'+bytes([255,255,255,128])*4)*2)
strip=png(4,2,(b'\x00'+bytes([255,255,255,255,255,0,0,255,0,255,0,255,0,0,255,255]))*2)
manifest=dict(format='SevenMania.Skin',formatVersion=1,id='test.render-layers',name='Renderer validation',author='SevenMania',skinVersion='1.0.0',description='Simple loader/render test shapes, not a design skin.',resolution=dict(width=1920,height=1080),supportedScreens=['result'],resourcePaths={'white':'assets/images/white.png','alias':'assets/images/white.png','strip':'assets/images/strip.png','alpha':'assets/images/alpha.png'})
def element(id,x,y,w,h,color=None,**kw):
 e=dict(id=id,type='image',resource='white',x=x*6,y=y*6,width=w*6,height=h*6)
 if color:e['tint']=dict(zip(['r','g','b'],color))
 e.update(kw);return e
els=[element('red',20,20,100,100,(255,0,0)),element('green',60,60,100,60,(0,255,0),z=1),element('blue',80,80,40,20,(0,0,255),z=1),element('half',180,20,40,40,opacity=.5),element('rotate',200,100,60,20,(0,0,255),rotation=90),element('crop',20,140,40,20,resource='strip',source=dict(x=3,y=0,width=1,height=2)),element('clip',100,140,80,20,(255,255,0),clip=dict(x=110*6,y=140*6,width=20*6,height=20*6)),element('hidden',0,0,320,180,visible=False,z=9),element('alias',260,20,20,20,(0,0,255),resource='alias'),element('alpha-tint',260,60,20,20,tint=dict(r=255,g=255,b=255,a=128))]
els.append(element('image-alpha',40,40,20,20,resource='alpha',z=2))
els=els[1:]+els[:1] # deliberately unsorted z; equal z keeps declaration order
layout=dict(screen='result',elements=els)
p=Path(__file__).with_name('render-layers.semaskin')
with zipfile.ZipFile(p,'w',zipfile.ZIP_DEFLATED) as z:
 for name,data in [('manifest.json',json.dumps(manifest)),('screens/result.json',json.dumps(layout)),('assets/images/white.png',white),('assets/images/strip.png',strip),('assets/images/alpha.png',alpha)]:
  info=zipfile.ZipInfo(name,date_time=(2026,10,28,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=0o100644<<16
  z.writestr(info,data)
