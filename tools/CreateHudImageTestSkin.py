"""Generate a small loader/render fixture; never overwrite the default skin."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import io,json,zipfile
root=Path(__file__).resolve().parents[1]
files={};resources={}
font=ImageFont.truetype(str(root/'Assets/Fonts/Oxanium-Bold.ttf'),60)
colors=['#ffcc32','#4ade80','#38bdf8','#fb923c','#f87171','#ff4040']
names=['pgreat','great','good','bad','poor','mine']
for i,name in enumerate(names+[str(n) for n in range(10)]):
    image=Image.new('RGBA',(320,96) if i<6 else (64,96));draw=ImageDraw.Draw(image)
    draw.text((image.width/2,image.height/2),name.upper(),anchor='mm',font=font,fill=colors[i] if i<6 else '#ffffff',stroke_width=2,stroke_fill='#304060')
    data=io.BytesIO();image.save(data,format='PNG');path='assets/images/'+name+'.png';files[path]=data.getvalue();resources['hud-'+name]=path
manifest=dict(format='SevenMania.Skin',formatVersion=1,id='test.hud.images',name='HUD image validation',author='SevenMania',skinVersion='1.0.0',description='Minimal test fixture, not a design skin',resolution=dict(width=960,height=540),supportedScreens=['play'],resourcePaths=resources)
style=dict(judgementMode='image',comboMode='image',judgements={name:'hud-'+name for name in names},digits=['hud-'+str(i) for i in range(10)],judgementWidth=250,judgementHeight=75,digitWidth=38,digitHeight=75,spacing=2,gap=12,displayMs=850)
element=dict(id='image-hud',type='text',binding='play.judgementCombo',x=180,y=300,width=600,height=100,font='Michroma',fontSize=80,align='center',verticalAlign='center',hud=style)
files['manifest.json']=json.dumps(manifest).encode();files['screens/play.json']=json.dumps(dict(screen='play',elements=[element])).encode()
# The generated font images retain the font's license in the package.
files['licenses/Oxanium-OFL.txt']=(root/'Assets/Fonts/Licenses/Oxanium-OFL.txt').read_bytes()
output=root/'tests/fixtures/skins/hud-images.semaskin'
with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED) as z:
    for name,data in files.items():z.writestr(name,data)
print(output)
