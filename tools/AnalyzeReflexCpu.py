"""Offline CPU attribution with preceding-frame alignment and slow-frame neighbours."""
import argparse,csv,json,statistics,math
from collections import defaultdict
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--directory',type=Path,default=Path('build-rmlui-song-list/Validation/HudFrameStudy'));p.add_argument('--labels',default='',help='Comma-separated label prefixes; empty selects cpu-*');a=p.parse_args()
def stats(v):
 v=sorted(v)
 if not v:return {}
 q=lambda f:v[int((len(v)-1)*f)]
 return dict(n=len(v),mean=statistics.mean(v),p99=q(.99),p999=q(.999),max=v[-1])
result=[]
for mf in sorted(a.directory.glob('cpu-*.manifest.json')):
 base=str(mf)[:-len('.manifest.json')];m=json.loads(mf.read_text());meta=Path(base+'.meta.json')
 if a.labels and not any(m['label'].startswith(label) for label in a.labels.split(',')):continue
 if not meta.exists():continue
 rows=[{k:float(v) for k,v in r.items()} for r in csv.DictReader(open(base+'.frames.csv'))];ids=[i for i,r in enumerate(rows) if i>0 and m['analyze_wall_start']<=r['wall_s']<=m['analyze_wall_end']];selected=[rows[i] for i in ids];seconds=m['analyze_wall_end']-m['analyze_wall_start'];slow=sorted(ids,key=lambda i:rows[i]['frame_cpu_ms'],reverse=True)[:max(1,len(ids)//100)]
 stages=[k for k in selected[0] if k.endswith('_cpu_ms') and k!='frame_cpu_ms'];slow_intervals=sorted(ids,key=lambda i:rows[i]['interval_ms'],reverse=True)[:max(1,len(ids)//100)]
 summary=dict(label=m['label'],meta=json.loads(meta.read_text()),detail=m['detail'],on=m['reflex_requested'],interval=stats([r['interval_ms'] for r in selected]),cpu=stats([r['frame_cpu_ms'] for r in selected]),cycles=stats([r['ui_thread_cycles'] for r in selected]),cycles_slow=stats([rows[i]['ui_thread_cycles'] for i in slow]),stages={k:dict(all=stats([r[k] for r in selected]),slow=stats([rows[i][k] for i in slow])) for k in stages},frequency={str(t):dict(count=sum(r['interval_ms']>=t for r in selected),per_second=sum(r['interval_ms']>=t for r in selected)/seconds) for t in [1.667,2,5,10]},span_dropped=sum(r['span_dropped'] for r in rows),slow_alignment=dict(interval=statistics.mean(rows[i]['interval_ms'] for i in slow_intervals),previous_cpu=statistics.mean(rows[i-1]['frame_cpu_ms'] for i in slow_intervals),current_sleep=statistics.mean(rows[i]['reflex_sleep_cpu_ms'] for i in slow_intervals),current_input=statistics.mean(rows[i]['inputpump_cpu_ms'] for i in slow_intervals)),neighbours=[])
 for i in sorted(ids,key=lambda i:rows[i]['frame_cpu_ms'],reverse=True)[:5]:summary['neighbours'].append([rows[j] for j in range(max(0,i-2),min(len(rows),i+3))])
 states={k:sum(r.get(k,0) for r in selected) for k in ['paused_paints','unfocused_paints','auto_resumes','input_paints','deferred_input_paints']}
 summary['states']=states
 excluded=set()
 for i in ids:
  if any(rows[i].get(k,0)>0 for k in ['paused_paints','unfocused_paints','auto_resumes']):excluded.update(range(max(0,i-2),min(len(rows),i+3)))
 clean=[i for i in ids if i not in excluded];summary['steady_rows']=len(clean)
 if clean:
  clean_slow=sorted(clean,key=lambda i:rows[i]['frame_cpu_ms'],reverse=True)[:max(1,len(clean)//100)]
  summary['steady_interval']=stats([rows[i]['interval_ms'] for i in clean]);summary['steady_cpu']=stats([rows[i]['frame_cpu_ms'] for i in clean]);summary['steady_slow_stages']={k:statistics.mean(rows[i][k] for i in clean_slow) for k in stages};summary['steady_slow_cycles']=statistics.mean(rows[i]['ui_thread_cycles'] for i in clean_slow);summary['steady_cycles']=statistics.mean(rows[i]['ui_thread_cycles'] for i in clean);summary['steady_slow_serials']=[int(rows[i]['serial']) for i in clean_slow];summary['steady_cycle_max_serials']=[int(rows[i]['serial']) for i in sorted(clean,key=lambda i:rows[i]['frame_cpu_ms'],reverse=True)[:5]]
  if 'backdrop_copy_calls' in rows[0]:
   changed=[i for i in clean if rows[i]['backdrop_copy_calls']]
   summary['backdrop_changed']=dict(count=len(changed),cpu=stats([rows[i]['frame_cpu_ms'] for i in changed]),unchanged_cpu=stats([rows[i]['frame_cpu_ms'] for i in clean if not rows[i]['backdrop_copy_calls']]),slow_overlap=sum(bool(rows[i]['backdrop_copy_calls']) for i in clean_slow),slow_count=len(clean_slow),compared_rows=stats([rows[i]['backdrop_compared_rows'] for i in changed]))
  # Scope totals include pending input work before beginFrame; use timestamps
  # to isolate the actual CPU update, and never add inclusive nested scopes.
  spans=defaultdict(list)
  for s in csv.DictReader(open(base+'.spans.csv')):
   spans[int(s['serial'])].append(dict(stage=s['stage'],depth=int(s['depth']),start_ns=int(s['start_ns']),end_ns=int(s['end_ns']),ms=int(s['elapsed_ns'])/1e6))
  clipped={k:[] for k in stages}
  covered=[i for i in clean_slow if spans[int(rows[i]['serial'])]]
  for i in covered:
   r=rows[i];begin=r['wall_s']*1e9;end=begin+r['frame_cpu_ms']*1e6;totals=defaultdict(float)
   for s in spans[int(r['serial'])]:totals[s['stage']]+=max(0,min(end,s['end_ns'])-max(begin,s['start_ns']))/1e6
   for k in stages:clipped[k].append(totals[k])
  summary['slow_cpu_span_coverage']=dict(recorded=len(covered),selected=len(clean_slow))
  summary['slow_cpu_clipped_stages']={k:statistics.mean(v) for k,v in clipped.items() if v}
  summary['worst_paths']=[]
  for i in sorted(clean,key=lambda i:rows[i]['frame_cpu_ms'],reverse=True)[:3]:
   r=rows[i];begin=r['wall_s']*1e9;end=begin+r['frame_cpu_ms']*1e6
   summary['worst_paths'].append(dict(serial=int(r['serial']),cpu_ms=r['frame_cpu_ms'],neighbours=[{k:rows[j].get(k,0) for k in ['serial','interval_ms','frame_cpu_ms','uploadhybrid_cpu_ms','backdrop_retain_cpu_ms','backdrop_copy_cpu_ms','backdrop_compare_cpu_ms','ui_thread_cycles','paused_paints','auto_resumes']} for j in range(max(0,i-2),min(len(rows),i+3))],spans=sorted([s for s in spans[int(r['serial'])] if s['start_ns']>=begin-2 and s['end_ns']<=end+2],key=lambda s:s['start_ns'])))
  i=max(ids,key=lambda i:rows[i]['interval_ms']);summary['worst_interval']=dict(row=rows[i],previous=rows[i-1],spans=sorted(spans[int(rows[i]['serial'])],key=lambda s:s['start_ns']))
 result.append(summary)
 print(m['label'],'FPS',round(1000/summary['interval']['mean'],1),'interval P99/max',round(summary['interval']['p99'],3),round(summary['interval']['max'],3),'CPU P99/max',round(summary['cpu']['p99'],3),round(summary['cpu']['max'],3),'spans dropped',summary['span_dropped'],'freq',summary['frequency'])
 if m['detail']:
  print('slow CPU mean',round(statistics.mean(rows[i]['frame_cpu_ms'] for i in slow),3),'top inclusive',[(k,round(summary['stages'][k]['slow']['mean'],4)) for k in sorted(stages,key=lambda k:summary['stages'][k]['slow']['mean'],reverse=True)[:12]])
(a.directory/'cpu-attribution-summary.json').write_text(json.dumps(result,indent=2))
