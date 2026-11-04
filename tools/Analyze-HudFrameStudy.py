"""Aggregate bounded in-memory captures after playback. CPU timings are not GPU execution."""
import csv,json,statistics,argparse
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('directory');a=p.parse_args();directory=Path(a.directory)
def stats(values):
    a=sorted(values)
    if not a:return dict(n=0)
    q=lambda p:a[min(len(a)-1,int((len(a)-1)*p))]
    return dict(n=len(a),mean=statistics.mean(a),p95=q(.95),p99=q(.99),max=a[-1])
results=[]
for manifest in sorted(directory.glob('*.manifest.json')):
    prefix=str(manifest)[:-len('.manifest.json')]
    if not Path(prefix+'.meta.json').exists():continue
    info=json.loads(manifest.read_text());meta=json.loads(Path(prefix+'.meta.json').read_text());lo,hi=info['analyze_wall_start'],info['analyze_wall_end']
    rows=[{k:float(v) for k,v in r.items()} for r in csv.DictReader(open(prefix+'.frames.csv'))]
    selected=[r for r in rows if lo<=r['wall_s']<=hi];ids={int(r['serial']) for r in selected}
    submitted=[r for r in selected if r['submits']>0]
    stamps=[float(r['wall_s']) for r in csv.DictReader(open(prefix+'.submits.csv')) if lo<=float(r['wall_s'])<=hi]
    gpu=[float(r['frame_gpu_bracket_ms']) for r in csv.DictReader(open(prefix+'.gpu.csv')) if int(r['serial']) in ids]
    item=dict(label=info['label'],manifest=info,meta=meta,interval=stats([r['interval_ms'] for r in selected]),cpu=stats([r['frame_cpu_ms'] for r in selected]),accepted_gap=stats([(b-a)*1000 for a,b in zip(stamps,stamps[1:])]),gpu=stats(gpu),stages={},counts={},hud_change_cpu=stats([r['frame_cpu_ms'] for r in selected if r['hud_text_uploads']>0]),no_hud_change_cpu=stats([r['frame_cpu_ms'] for r in selected if r['hud_text_uploads']==0]))
    for key in selected[0]:
        if key.endswith('_cpu_ms'):item['stages'][key]=dict(all=stats([r[key] for r in selected]),submitted=stats([r[key] for r in submitted]))
        elif key not in ['serial','wall_s','chart_s','interval_ms']:item['counts'][key]=sum(r[key] for r in selected)
    slow=sorted(selected,key=lambda r:r['frame_cpu_ms'],reverse=True)[:max(1,len(selected)//100)]
    item['top1pct_stages']={key:statistics.mean(r[key] for r in slow) for key in item['stages']}
    results.append(item)
output=directory/'summary.json';output.write_text(json.dumps(results,indent=2),encoding='utf-8')
print('label | update FPS | loop P95/P99/max ms | accepted P99/max ms | GPU bracket mean ms | HUD update max | score/gauge max | atlas/prepare')
for r in results:
    i=r['interval'];g=r['accepted_gap'];st=r['stages'];co=r['counts']
    print(f"{r['label']}: {1000/i['mean']:.1f} | {i['p95']:.3f}/{i['p99']:.3f}/{i['max']:.3f} | {g.get('p99',0):.3f}/{g.get('max',0):.3f} | {r['gpu'].get('mean',0):.3f} | {st['hud_update_cpu_ms']['all']['max']:.3f} | {st['score_update_cpu_ms']['all']['max']:.3f}/{st['gauge_update_cpu_ms']['all']['max']:.3f} | {co['atlas_builds']}/{co['skin_prepares']}")
