"""Offline aggregation. Kernel-wait timings exclude the message-pumping spin tail."""
import argparse,csv,json,re,statistics,os
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('label');p.add_argument('--directory',type=Path,default=Path('build-rmlui-song-list/Validation/HudFrameStudy'));a=p.parse_args()
def stats(v):
 v=sorted(v)
 if not v:return {}
 q=lambda x:v[min(len(v)-1,int((len(v)-1)*x))]
 return dict(mean=statistics.mean(v),p95=q(.95),p99=q(.99),max=v[-1],low1_fps=1000/statistics.mean(v[-max(1,len(v)//100):]) if v[-1]>0 else None)
result=[]
for f in sorted(a.directory.glob(a.label+'*.manifest.json')):
 base=str(f)[:-len('.manifest.json')];manifest=json.loads(f.read_text());meta=Path(base+'.meta.json')
 if not meta.exists():continue
 meta=json.loads(meta.read_text());lo=manifest['analyze_wall_start'];hi=manifest['analyze_wall_end']
 rows=[{k:float(v) for k,v in row.items()} for row in csv.DictReader(open(base+'.frames.csv')) if lo<=float(row['wall_s'])<=hi]
 stamps=[float(r['wall_s']) for r in csv.DictReader(open(base+'.submits.csv')) if lo<=float(r['wall_s'])<=hi]
 log=Path(base+'.log').read_text();match=re.search(r'process_cpu_ms=([0-9.]+)',log)
 cpu=100*float(match[1])/(manifest['duration']*1000) if match else None
 latency=[];status=None;lp=Path(base+'.latency.csv')
 if lp.exists():
  lines=lp.read_text().splitlines();status=int(lines[0].split(',')[1]);latency=[float(r['input_to_gpu_end_ms']) for r in csv.DictReader(lines[1:])]
 item=dict(label=manifest['label'],fps=manifest['fps'],on=manifest['reflex_requested'],before=manifest.get('reflex_before',False),frames=len(rows),interval=stats([r['interval_ms'] for r in rows]),cpu_frame=stats([r['frame_cpu_ms'] for r in rows]),submit_gap=stats([(y-x)*1000 for x,y in zip(stamps,stamps[1:])]),process_cpu_one_core_pct=cpu,process_cpu_machine_pct=cpu/os.cpu_count() if cpu is not None else None,driver_on='driver_low_latency=1' in log,sdk_latency_status=status,sdk_input_to_gpu_end_ms=stats(latency),sdk_latency_samples=len(latency),meta=meta,stages={k:stats([r[k] for r in rows]) for k in ['reflex_sleep_cpu_ms','pacer_wait_cpu_ms','game_paint_cpu_ms','composition_draw_cpu_ms','present_cpu_ms']})
 result.append(item)
for r in sorted(result,key=lambda r:(r['fps'],r['on'],not r['before'])):
 i=r['interval'];s=r['stages'];print(f"{r['fps']} {'ON' if r['on'] else 'OFF'} {'before' if r['before'] else 'after'}: FPS {1000/i['mean']:.1f} P95/P99/max {i['p95']:.3f}/{i['p99']:.3f}/{i['max']:.3f} low1 {i['low1_fps']:.1f} CPU {r['process_cpu_one_core_pct']} sleep {s['reflex_sleep_cpu_ms']['mean']:.3f} wait {s['pacer_wait_cpu_ms']['mean']:.3f} present {s['present_cpu_ms']['mean']:.3f} SDK {r['sdk_input_to_gpu_end_ms'].get('mean')} n={r['sdk_latency_samples']}")
(a.directory/(a.label+'-summary.json')).write_text(json.dumps(result,indent=2))
