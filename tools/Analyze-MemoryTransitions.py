"""Summarize the real-player transition probe. All sizes are bytes in raw CSV."""
import argparse,csv,json,statistics
from pathlib import Path
METRICS=('private_bytes','working_set_bytes','peak_working_set_bytes','handles','gdi','user','threads','audio_bytes','video_readers')
def summarize(rows):
    result={'count':len(rows)}
    for metric in METRICS:
        values=[int(r[metric]) for r in rows]
        x=[float(r['operation']) for r in rows]
        meanx=statistics.mean(x);meany=statistics.mean(values)
        denominator=sum((v-meanx)**2 for v in x)
        result[metric]={'first':values[0],'last':values[-1],'min':min(values),'max':max(values),'median':statistics.median(values),'delta':values[-1]-values[0],
                        'slope_per_operation':sum((a-meanx)*(b-meany) for a,b in zip(x,values))/denominator if denominator else None}
    return result

def main():
    parser=argparse.ArgumentParser();parser.add_argument('directory',type=Path);args=parser.parse_args()
    with (args.directory/'samples.csv').open(encoding='utf-8-sig',newline='') as f:rows=list(csv.DictReader(f))
    if not rows:raise SystemExit('No samples')
    result={'rows':len(rows),'duration_seconds':float(rows[-1]['seconds']),'rounds':{},'checkpoints':[]}
    for row in rows:
        if row['phase'] in ('startup','warm_exit','baseline','round_idle','quiescent','after_shutdown','failure'):
            result['checkpoints'].append({k:(float(v) if k=='seconds' else int(v) if k not in ('phase',) else v) for k,v in row.items()})
    for roundno in sorted({int(r['round']) for r in rows if int(r['round'])>0}):
        group=[r for r in rows if int(r['round'])==roundno]
        phases={}
        for phase in ('song','variant','enter','exit'):
            selected=[r for r in group if r['phase']==phase]
            if selected:phases[phase]=summarize(selected)
        exits=[r for r in group if r['phase']=='exit']
        if exits:
            phases['exit_last10']=summarize(exits[-10:])
            phases['exit_by_chart']={chart:summarize([r for r in exits if r['chart']==chart]) for chart in sorted({r['chart'] for r in exits})}
        result['rounds'][str(roundno)]=phases
    result['counts']={phase:sum(r['phase']==phase for r in rows) for phase in ('song','variant','enter','exit','failure')}
    result['maxima']={metric:max(int(r[metric]) for r in rows) for metric in METRICS}
    (args.directory/'summary.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps({'counts':result['counts'],'checkpoints':result['checkpoints'],'maxima':result['maxima']},indent=2))
if __name__=='__main__':main()
