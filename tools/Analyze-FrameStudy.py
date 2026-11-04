"""Analyze buffered, real-player traces; standard library only, no guessed metrics."""
import csv, json, math, statistics, pathlib, argparse, collections, hashlib, re

def percentile(values, p):
    a = sorted(values)
    if not a: return 0
    x = (len(a)-1)*p; i = int(x); f = x-i
    return a[i]*(1-f)+a[min(i+1,len(a)-1)]*f

def stats(a):
    if not a: return {}
    a=sorted(a);mean=statistics.fmean(a)
    def q(p):
        x=(len(a)-1)*p;i=int(x);f=x-i
        return a[i]*(1-f)+a[min(i+1,len(a)-1)]*f
    return dict(mean=mean,p95=q(.95),p99=q(.99),p999=q(.999),
                stdev=math.sqrt(math.fsum((x-mean)**2 for x in a)/len(a)),maximum=a[-1])

def correlation(a,b):
    if not a or statistics.pstdev(a)==0 or statistics.pstdev(b)==0: return None
    ma,mb=statistics.mean(a),statistics.mean(b)
    return sum((x-ma)*(y-mb) for x,y in zip(a,b))/math.sqrt(sum((x-ma)**2 for x in a)*sum((x-mb)**2 for x in b))

def analyze(path):
    manifest=json.loads(path.with_suffix('.manifest.json').read_text(encoding='utf-8-sig'))
    rows=[{k:float(v) for k,v in r.items()} for r in csv.DictReader(path.open())]
    rows=[r for r in rows if manifest['analyzeStart']<=r['playhead_s']<=manifest['analyzeEnd'] and r['interval_ms']>0]
    intervals=[r['interval_ms'] for r in rows]; durations=[r['frame_ms'] for r in rows]
    result=dict(label=manifest['label'],case=manifest['case'],repeat=manifest['repeat'],sourceCommit=manifest['sourceCommit'],
                frames=len(rows),interval_ms=stats(intervals),processing_ms=stats(durations))
    result['fps']=1000/statistics.mean(intervals)
    result['low1_fps']=1000/statistics.mean(sorted(intervals,reverse=True)[:max(1,math.ceil(len(rows)*.01))])
    scopes=[k for k in rows[0] if k.endswith('_ms') and k not in ('interval_ms','frame_ms')]
    result['scopes']={k:dict(**stats([r[k] for r in rows]),
        nonzero=stats([r[k] for r in rows if r[k]>0]),total_ms=sum(r[k] for r in rows)) for k in scopes}
    result['boundary_only']=bool(manifest.get('boundaryOnly',False))
    result['text_miss_cost_ms']=stats([r['text_ms'] for r in rows if r['text_misses']>0])
    result['text_hit_cost_ms']=stats([r['text_ms'] for r in rows if r['text_misses']==0])
    result['associations']={}
    for k in ('key_down','key_up','judgements','text_misses','cpp_allocations','visible_notes','bga_rebuilt'):
        yes=[r['interval_ms'] for r in rows if r[k]>0]; no=[r['interval_ms'] for r in rows if r[k]==0]
        result['associations'][k]=dict(pearson_interval=correlation(intervals,[r[k] for r in rows]),
            pearson_processing=correlation(durations,[r[k] for r in rows]),positive_count=len(yes),positive=stats(yes),zero=stats(no))
    # Work performed after begin() largely lengthens the *next* begin interval.
    result['previous_frame_associations']={k:correlation(intervals[1:],[r[k] for r in rows[:-1]])
        for k in ('text_misses','cpp_allocations','bga_rebuilt','visible_notes')}
    result['spikes']={}
    for name,threshold in [('over_2ms',2),('over_twice_median',2*statistics.median(intervals))]:
        spikes=[r for r in rows if r['interval_ms']>threshold]; clusters=[]
        for r in rows:
            if r['interval_ms']>threshold:
                if not clusters or not clusters[-1]['open']: clusters.append(dict(open=True,frames=0,duration_ms=0,start=r['wall_s']))
                clusters[-1]['frames']+=1;clusters[-1]['duration_ms']+=r['interval_ms']
            elif clusters: clusters[-1]['open']=False
        deltas=[1000*(b['wall_s']-a['wall_s']) for a,b in zip(spikes,spikes[1:])]
        phase_total=collections.Counter(int(r['wall_s']*1000)%24 for r in rows)
        phase_spikes=collections.Counter(int(r['wall_s']*1000)%24 for r in spikes)
        result['spikes'][name]=dict(threshold_ms=threshold,frames=len(spikes),clusters=len(clusters),
            total_duration_ms=sum(r['interval_ms'] for r in spikes),max_cluster_ms=max([c['duration_ms'] for c in clusters] or [0]),
            excess_duration_ms=sum(r['interval_ms']-threshold for r in spikes),
            interarrival_ms=stats(deltas),interarrival_5ms_bins=dict(collections.Counter(int(d//5)*5 for d in deltas)),
            phase24_counts=dict(phase_spikes),phase24_rates={i:phase_spikes[i]/n for i,n in phase_total.items()})
    worst=sorted(range(len(rows)),key=lambda i:rows[i]['interval_ms'],reverse=True)[:12]
    result['worst_frames']=[dict(current=rows[i],previous=rows[i-1] if i else {}) for i in worst]
    events=list(csv.DictReader(pathlib.Path(str(path)+'.events.csv').open()))
    intended=collections.defaultdict(collections.deque); delays=[]; queue_delays=[]; actual=0; matched=0
    for e in events:
        kind=int(e['kind']); lane=int(e['value'])
        if kind in (10,11): intended[(kind-9,lane)].append((float(e['chart_s']),float(e['wall_s'])))
        elif kind in (1,2):
            actual+=1
            if intended[(kind,lane)]:
                chart,wall=intended[(kind,lane)].popleft();matched+=1
                if manifest['analyzeStart']<=chart<=manifest['analyzeEnd']:
                    delays.append(1000*(float(e['chart_s'])-chart))
                    queue_delays.append(1000*(float(e['wall_s'])-wall))
    result['software_dispatch_ms']=stats(delays)
    result['post_to_handler_ms']=stats(queue_delays)
    lower=rows[0]['wall_s']-rows[0]['frame_ms']/1000;upper=rows[-1]['wall_s']
    presented=[float(e['wall_s']) for e in events if int(e['kind'])==4 and lower<=float(e['wall_s'])<=upper]
    result['present_submission_ms']=stats([1000*(b-a) for a,b in zip(presented,presented[1:])])
    result['accepted_present_fps']=(len(presented)-1)/(presented[-1]-presented[0]) if len(presented)>1 else None
    result['events']=dict(actual=actual,unmatched=sum(map(len,intended.values())),matched=matched,analyzed_matches=len(delays),
        unmatched_analyzed=sum(1 for q in intended.values() for chart,wall in q if manifest['analyzeStart']<=chart<=manifest['analyzeEnd']),
        judgement_histogram=dict(collections.Counter(e['value'] for e in events if int(e['kind'])==3)))
    result['run_log']=path.with_suffix('.log').read_text(encoding='utf-8-sig',errors='replace')
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('directory');p.add_argument('--output',default='summary.json');p.add_argument('--csv');args=p.parse_args()
    results=[analyze(x) for x in sorted(pathlib.Path(args.directory).glob('*.csv')) if not x.name.endswith('.events.csv')]
    pathlib.Path(args.output).write_text(json.dumps(results,indent=2,ensure_ascii=False),encoding='utf8')
    if args.csv:
        exported=[]
        for r in results:
            name=f"{r['label']}-{r['case']}-r{r['repeat']}"
            root=pathlib.Path(args.directory);m=json.loads((root/(name+'.manifest.json')).read_text(encoding='utf-8-sig'))
            values=dict(re.findall(r'(\w+)=([\d.]+)',r['run_log']))
            row={k:r[k] for k in ('label','case','repeat','sourceCommit','frames','fps','low1_fps')}
            row.update({'interval_'+k:v for k,v in r['interval_ms'].items()})
            row.update({'processing_'+k:v for k,v in r['processing_ms'].items()})
            row.update({'spikes2_'+k:r['spikes']['over_2ms'][k] for k in ('frames','clusters','total_duration_ms','excess_duration_ms','max_cluster_ms')})
            row.update({k+'_p99':r['scopes'][k]['p99'] for k in ('text_ms','deck_ms','notes_ms','present_ms','bga_ms','sync_ms')})
            row.update({k+'_max':r['scopes'][k]['maximum'] for k in ('input_ms','judge_logic_ms','trigger_lock_ms','audio_lock_ms')})
            row.update({'text_miss_'+k:r['text_miss_cost_ms'].get(k,'') for k in ('mean','p99','p999','maximum')})
            if r['boundary_only']:
                for k in list(row):
                    if k.endswith('_ms_p99') or k.endswith('_ms_max') or k.startswith('text_miss_'):row[k]=''
            row.update({'queue_'+k:v for k,v in r['post_to_handler_ms'].items()})
            for k in ('mean','p95','p99','p999','stdev','maximum'):row.setdefault('queue_'+k,'')
            row['accepted_present_fps']=r['accepted_present_fps']
            row.update({'accepted_present_'+k:r['present_submission_ms'].get(k,'') for k in ('mean','p95','p99','p999','stdev','maximum')})
            row.update({k:values.get(k,'') for k in ('peak_working_set_MiB','submitted_frames','gpu_busy_ms','gpu_samples','audio_empty_padding_events','audio_render_failures','trace_dropped','replay_events','score','reflex_active')})
            row['input_unmatched_analyzed']='' if r['boundary_only'] else r['events']['unmatched_analyzed']
            row.update({k:m.get(k,'') for k in ('exeSha256','chartSha256','configSha256','boundaryOnly')})
            row['csvSha256']=hashlib.sha256((root/(name+'.csv')).read_bytes()).hexdigest()
            exported.append(row)
        with pathlib.Path(args.csv).open('w',newline='',encoding='utf8') as f:
            writer=csv.DictWriter(f,fieldnames=list(exported[0]));writer.writeheader();writer.writerows(exported)
    for r in results:
        s=r['interval_ms'];print(f"{r['label']:10} {r['case']:22} r{r['repeat']} FPS {r['fps']:7.1f} low {r['low1_fps']:6.1f} P99 {s['p99']:6.3f} P999 {s['p999']:6.3f} std {s['stdev']:5.3f}")
        top=sorted(r['scopes'].items(),key=lambda kv:kv[1]['p99'],reverse=True)[:6]
        print('  '+', '.join(f'{k} p99={v["p99"]:.3f}' for k,v in top))
