"""Build an editable 1920x1080 reference from current native layout constants; never changes source assets."""
from pathlib import Path
import json,zipfile,zlib,struct
root=Path(__file__).resolve().parents[1]
def png():
 def c(k,d):return struct.pack('>I',len(d))+k+d+struct.pack('>I',zlib.crc32(k+d))
 return b'\x89PNG\r\n\x1a\n'+c(b'IHDR',struct.pack('>IIBBBBB',1,1,8,6,0,0,0))+c(b'IDAT',zlib.compress(b'\0\xff\xff\xff\xff'))+c(b'IEND',b'')
files={'assets/images/pixel.png':png(),'assets/fonts/Michroma-Regular.ttf':(root/'Assets/Fonts/Michroma-Regular.ttf').read_bytes()}
files['licenses/Michroma-OFL.txt']=(root/'Assets/Fonts/Licenses/Michroma-OFL.txt').read_bytes()
for name in ['hud-icon-slot.png','hud-hi-speed.png','hud-gauge-mode.png','turntable.png','whitebutton.png','blackbutton.png']:
 files['assets/images/'+name]=(root/'Assets'/name).read_bytes()
# Match KeyDeck's four concentric scratch rims, with transparent centre.
def scratch_rim():
 w=94; rows=[]
 for y in range(w):
  row=bytearray([0])
  for x in range(w):
   r=((x-47)**2+(y-47)**2)**.5
   color=(49,52,61,255) if 45<=r<=46 else (174,183,195,255) if 43<=r<45 else (193,200,210,255) if 41<=r<43 else (98,106,119,255) if 39<=r<41 else (0,0,0,0)
   row.extend(color)
  rows.append(row)
 def chunk(k,d):return struct.pack('>I',len(d))+k+d+struct.pack('>I',zlib.crc32(k+d))
 return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,w,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(rows)))+chunk(b'IEND',b'')
files['assets/images/scratch-rim.png']=scratch_rim()
resources={'scratch-rim':'assets/images/scratch-rim.png','hud-font':'assets/fonts/Michroma-Regular.ttf','pixel':'assets/images/pixel.png','slot':'assets/images/hud-icon-slot.png','disc':'assets/images/turntable.png','white-key':'assets/images/whitebutton.png','black-key':'assets/images/blackbutton.png'}
resources.update({'hi-speed-slot':'assets/images/hud-hi-speed.png','gauge-mode-slot':'assets/images/hud-gauge-mode.png'})
def image(id,x,y,w,h,res='pixel',rgb=(64,64,64),**kw):
 return dict(id=id,type='image',resource=res,x=x,y=y,width=w,height=h,tint=dict(zip(['r','g','b'],rgb)),**kw)
def text(id,text,x,y,w,h,size=32,rgb=(245,248,252),binding='',kind='text'):
 e=dict(id=id,type=kind,text=text,x=x,y=y,width=w,height=h,font='Segoe UI',fontSize=size,align='center',verticalAlign='center',tint=dict(zip(['r','g','b'],rgb)))
 if binding:e.update(binding=binding,default=text)
 return e
select=[dict(id='info-background',type='box',boxMode='title',x=0,y=0,width=1260,height=830,tint=dict(r=64,g=64,b=64)),text('genre-reference','장르 미표기',24,48,1212,60,48,(168,179,193),'song.genre'),text('song-title','곡 제목',24,108,1212,180,120,binding='song.title'),text('song-artist','작곡가',24,300,1212,60,48,(210,220,232),'song.artist'),text('bpm-label','BPM',310,360,200,64,48,(210,220,232)),text('bpm','150.0',510,360,300,64,48,(210,220,232),'song.bpm','number'),text('level','12',24,424,1212,84,64,(96,165,250),'chart.levelText')]
for i in range(5):
 select.append(image(f'slot-{i}',24+i*112,930,100,100,'hi-speed-slot' if i==0 else 'gauge-mode-slot' if i==2 else 'slot',(255,255,255)))
 if i<3:
  if i==1:select.append(text(f'slot-label-{i}',['SPEED','JUDGE','GAUGE'][i],24+i*112,950,100,30,15,(35,35,35)))
  select.append(text(f'slot-value-{i}',['1.00x','NORMAL','NORMAL'][i],24+i*112,980,100,44,16,(10,10,10),['select.speedText','select.judgementText','select.gaugeMode'][i]))
play=[text('title','곡 제목',20,20,580,58,32,binding='song.title'),text('score-label','SCORE',1470,100,400,36,24),text('score','0',1470,140,400,80,48,binding='play.score',kind='number'),dict(id='judgement-combo',type='text',text='',binding='play.judgementCombo',x=660,y=520,width=600,height=100,font='Michroma',resource='hud-font',fontSize=80,align='center',verticalAlign='center',tint=dict(r=255,g=255,b=255)),text('gauge-label','GAUGE %',1470,520,400,40,24),text('gauge','20',1470,560,400,70,44,binding='play.gauge',kind='number')]
# Native 7+1 layout at 1920x1080: 104px scratch, alternating 68/60px keys.
gear=[]
def part(element):
 element['extensions']={'nativeGearPart':True}
 gear.append(element)
left=682; judge=720; width=556
part(image('gear-panel',left,judge+4,width,1080-judge-4,rgb=(0,0,0)))
lanes=[104,68,60,68,60,68,60,68]
x=left
for lane,w in enumerate(lanes):
 part(image(f'gear-lane-{lane}',x,0,w,judge,rgb=(16,16,16) if lane==0 or lane%2==0 else (23,23,23)))
 gear[-1]['extensions']['editorOnly']=True
 if lane:
  part(image(f'gear-separator-{lane}',x,0,1,judge,rgb=(255,255,255)))
  gear[-1]['extensions']['editorOnly']=True
 part(text(f'gear-key-guide-{lane}',['SHIFT','S','D','F','SPACE','J','K','L'][lane],x,judge-30,w,24,16))
 gear[-1]['extensions']['editorOnly']=True
 if lane:
  part(image(f'gear-key-{lane}',x+(4 if lane%2 else 7),judge+11,w-(8 if lane%2 else 14),63,'white-key' if lane%2 else 'black-key',(255,255,255)))
 x+=w
part(image('gear-judgement-line',left,judge,width,3,rgb=(248,250,252)))
part(image('gear-scratch-rim',left+5,judge-1,94,94,'scratch-rim',(255,255,255)))
part(image('gear-scratch-disc',left+12,judge+6,80,80,'disc',(255,255,255)))
gaugeX=left+2;gaugeY=judge+121;gaugeW=width-4
part(image('gear-gauge-frame',gaugeX,gaugeY,gaugeW,28,rgb=(157,178,193)))
part(image('gear-gauge-background',gaugeX+1,gaugeY+1,gaugeW-2,26,rgb=(25,32,42)))
part(image('gear-gauge-interior',gaugeX+2,gaugeY+2,gaugeW-4,24,rgb=(43,54,65)))
part(image('gear-gauge-fill-reference',gaugeX+2,gaugeY+2,(gaugeW-4)*.2,24,rgb=(255,255,255)))
iconX=left+(width-(52*4+7*3))/2
for i in range(4):
 part(image(f'gear-icon-{i}',iconX+i*59,gaugeY+36,52,52,'hi-speed-slot' if i==0 else 'gauge-mode-slot' if i==1 else 'slot',(255,255,255)))
part(text('gear-speed-reference','1.00x',iconX,gaugeY+48,52,40,16,(24,28,33)))
part(text('gear-gauge-mode','NORMAL',iconX+59,gaugeY+48,52,40,13,(24,28,33),binding='play.gaugeMode'))
gear[-1]['default']='NORMAL'
part(text('gear-judgement-mode','NORMAL',iconX+118,gaugeY+48,52,40,13,(24,28,33),binding='play.judgementProfile'))
part(text('gear-judgement-label','JUDGE',iconX+118,gaugeY+37,52,12,7,(24,28,33)))
play=gear+play
# Same normalized slot layout as SelectionSlotLayout.h; retain selection area scale.
models={e['id']:e for e in play};targets={e['id']:e for e in select}
sx=targets['slot-0']['width']/models['gear-icon-0']['width'];sy=targets['slot-0']['height']/models['gear-icon-0']['height']
baseX,baseY=models['gear-icon-0']['x'],models['gear-icon-0']['y'];originX,originY=targets['slot-0']['x'],targets['slot-0']['y']
for slot,semantic in enumerate([0,2,1]):
 pairs=[(f'slot-{semantic}',f'gear-icon-{slot}'),(f'slot-value-{semantic}',['gear-speed-reference','gear-gauge-mode','gear-judgement-mode'][slot])]
 if slot==2:pairs.append((f'slot-label-{semantic}','gear-judgement-label'))
 for target,source in pairs:
  out=targets[target];model=models[source];out.update(x=originX+(model['x']-baseX)*sx,y=originY+(model['y']-baseY)*sy,width=model['width']*sx,height=model['height']*sy)
  for key in ['resource','tint','font','align','verticalAlign']:
   if key in model:out[key]=model[key]
  if 'fontSize' in model:out['fontSize']=round(model['fontSize']*min(sx,sy))

result=[image('result-reference-background',0,0,1920,1080,rgb=(10,14,21)),text('title','RESULT',32,44,1856,60,38,binding='song.title'),text('level','12',32,105,1856,40,28,(162,213,236),'chart.levelText'),text('artist','작곡가',32,145,1856,33,24,(218,228,240),'song.artist'),text('rank','AA',0,425,1920,130,112,(255,214,20),'result.rank'),text('score','882460',0,566,1920,69,52,binding='result.score',kind='number'),text('clear','CLEAR',0,638,1920,46,32,(74,222,128),'result.clearType')]
manifest=dict(format='SevenMania.Skin',formatVersion=1,id='sevenmania.default-reference',name='기본 외형 참고 템플릿',author='SevenMania',skinVersion='1.0.0',description='Editable reference from native layout. Not a complete conversion of native UI. Speed/judge/gauge labels use reference values; native lists, notes and interactions remain engine-owned.',resolution=dict(width=1920,height=1080),supportedScreens=['select','play','result'],resourcePaths=resources)
files['manifest.json']=json.dumps(manifest,ensure_ascii=False).encode()
for name,elements in [('select',select),('play',play),('result',result)]:
 for i,e in enumerate(elements):e['z']=i
 files['screens/'+name+'.json']=json.dumps(dict(screen=name,elements=elements),ensure_ascii=False).encode()
with zipfile.ZipFile(root/'Assets/Skins/DefaultReference.semaskin','w',zipfile.ZIP_STORED) as z:
 for name,data in files.items():
  info=zipfile.ZipInfo(name,(2026,10,28,0,0,0));info.external_attr=0o100644<<16;z.writestr(info,data)
# Only used when intentionally regenerating the entire native reference layout.
from UpdateDefaultHudImages import update
update(root/'Assets/Skins/DefaultReference.semaskin', root/'Assets/Skins/DefaultHudImages')
