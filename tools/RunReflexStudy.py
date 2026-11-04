"""Run matched Reflex OFF/ON x 600/1000/1500-ceiling trials; no personal INI changes."""
import argparse,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--label',required=True);p.add_argument('--chart',type=Path);p.add_argument('--duration',type=int,default=8);p.add_argument('--repeats',type=int,default=1);a=p.parse_args()
if not 4<=a.duration<=40:raise SystemExit('Use 4..40 seconds per capture (bounded 65536-row buffer).')
root=Path(__file__).resolve().parents[1]
for repeat in range(a.repeats):
 for fps in ([600,1000,0] if repeat%2==0 else [0,1000,600]):
  for on in ([False,True] if repeat%2==0 else [True,False]):
   for before in ([True,False] if repeat%2==0 else [False,True]):
    label=f'{a.label}-r{repeat+1}-{fps}-'+('on' if on else 'off')+'-'+('before' if before else 'after')
    args=[sys.executable,str(root/'tools/Run-HudFrameStudy.py'),'--label',label,'--asset','original','--cases','11','--repeats','1','--duration',str(a.duration),'--fps',str(fps)]
    if a.chart:args+=['--chart',str(a.chart.resolve())]
    if on:args+=['--reflex']
    if before:args+=['--reflex-before']
    subprocess.run(args,cwd=root,check=True)
    if on:
     log=root/'build-rmlui-song-list/Validation/HudFrameStudy'/(label+'-original-11-r1.log')
     if 'reflex_active=1 driver_low_latency=1' not in log.read_text(encoding='utf-8'):
      raise SystemExit('Reflex ON was not confirmed by the driver. Do not treat this capture as an ON measurement.')
