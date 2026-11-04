"""Replace the retired HUD font in .semaskin packages, preserving authored layouts."""
import argparse,json,zipfile,os
from pathlib import Path

def legacy(value):
 return 'bankgothic' in ''.join(c.lower() for c in value if c.isalnum())
def migrate(path):
 root=Path(__file__).resolve().parents[1]
 with zipfile.ZipFile(path) as archive: files={i.filename:archive.read(i) for i in archive.infolist() if not i.is_dir()}
 manifest=json.loads(files['manifest.json']);resources=manifest['resourcePaths'];removed=set();changed=False
 for id,r in resources.items():
  p=r if isinstance(r,str) else r['path']
  if legacy(p):removed.add(id)
 screens={name:json.loads(data) for name,data in files.items() if name.startswith('screens/') and name.endswith('.json')}
 for screen in screens.values():
  for e in screen['elements']:
   if legacy(e.get('font','')):
    e['font']='Michroma';changed=True
    if e.get('resource'):removed.add(e['resource'])
 for id in removed:
  r=resources.pop(id,None)
  if r:files.pop(r if isinstance(r,str) else r['path'],None)
  changed=True
 for screen in screens.values():
  for e in screen['elements']:
   if e.get('resource') in removed:e['resource']='bundled-michroma';e['font']='Michroma'
 for name in list(files):
  if legacy(name):del files[name];changed=True
 if not changed:return False
 for screen in screens.values():
  for e in screen['elements']:
   if e.get('font')=='Michroma':e['resource']='bundled-michroma'
 resources['bundled-michroma']='assets/fonts/Michroma-Regular.ttf'
 files['assets/fonts/Michroma-Regular.ttf']=(root/'Assets/Fonts/Michroma-Regular.ttf').read_bytes()
 files['licenses/Michroma-OFL.txt']=(root/'Assets/Fonts/Licenses/Michroma-OFL.txt').read_bytes()
 files['manifest.json']=json.dumps(manifest,ensure_ascii=False,separators=(',',':')).encode()
 for name,screen in screens.items():files[name]=json.dumps(screen,ensure_ascii=False,separators=(',',':')).encode()
 temporary=path.with_name(path.name+'.font-migration.tmp')
 try:
  with zipfile.ZipFile(temporary,'w',zipfile.ZIP_STORED) as archive:
   for name,data in files.items():archive.writestr(name,data)
  os.replace(temporary,path)
 finally:
  if temporary.exists():temporary.unlink()
 return True
if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('packages',nargs='+',type=Path)
 for path in parser.parse_args().packages:print(f'{path}: '+('migrated' if migrate(path) else 'unchanged'))
