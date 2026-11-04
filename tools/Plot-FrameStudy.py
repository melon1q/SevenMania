"""Render the measured study summary; run after all profiling processes stop."""
import argparse, csv, json, os, pathlib, statistics

p=argparse.ArgumentParser();p.add_argument('summary');p.add_argument('traces');p.add_argument('output');args=p.parse_args()
root=pathlib.Path(args.traces).parent
os.environ.setdefault('MPLCONFIGDIR',str((root/'matplotlib-cache').resolve()))
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

data=json.loads(pathlib.Path(args.summary).read_text(encoding='utf8'))
labels=['cleanbase','outline','surfaces','deck','hud','combined','final']
names=['Baseline','E1: identity\npass removal','E2: GDI\nreuse','E3: deck\ncache','E4: HUD\nraster','E1+E2\n(CPU-tail candidate)','E1+E2+E3*']
groups={k:[r for r in data if r['label']==k and r['case']=='creamy-gpu-replay' and r['repeat'] in (1,2,3)] for k in labels}
fig,axes=plt.subplots(2,1,figsize=(12,8),layout='constrained',gridspec_kw={'height_ratios':[1,1.1]})
x=np.arange(len(labels));colors=['#4071ab','#d18b32']
for offset,(field,title) in enumerate([('p99','P99'),('p999','P99.9')]):
    values=[statistics.median(r['interval_ms'][field] for r in groups[k]) for k in labels]
    axes[0].bar(x+(offset-.5)*.32,values,.32,label=title,color=colors[offset])
    for i,k in enumerate(labels):
        axes[0].scatter([x[i]+(offset-.5)*.32]*len(groups[k]),[r['interval_ms'][field] for r in groups[k]],s=12,c='black',alpha=.6,zorder=3)
axes[0].set_xticks(x,names);axes[0].set_ylabel('Update start interval (ms)');axes[0].set_ylim(bottom=0)
axes[0].set_title('Creamy / standard study settings / unlimited (1500 ceiling): median of 3 trials')
axes[0].legend();axes[0].grid(axis='y',alpha=.2)
for label,color,title in [('cleanbase','#4071ab','Baseline'),('combined','#d18b32','CPU-tail candidate E1+E2')]:
    # Use the middle P99 trial, never the best-looking excerpt.
    run=sorted(groups[label],key=lambda r:r['interval_ms']['p99'])[1]
    file=pathlib.Path(args.traces)/f"{label}-creamy-gpu-replay-r{run['repeat']}.csv"
    rows=[r for r in csv.DictReader(file.open()) if 32<=float(r['playhead_s'])<=33]
    axes[1].plot([float(r['playhead_s']) for r in rows],[float(r['interval_ms']) for r in rows],color=color,lw=.7,alpha=.8,label=f"{title} (trial {run['repeat']})")
axes[1].set_xlabel('Chart time (s), one-second excerpt');axes[1].set_ylabel('Update start interval (ms)')
axes[1].set_title('Original measured frames; input delivery and color phases are not time-locked')
axes[1].legend();axes[1].grid(alpha=.2)
fig.suptitle('Frame stability study — measured data, not physical scanout',fontsize=16)
fig.supxlabel('* All-combined entered a different submission regime and regressed on CPU; rejected.\nE1 had one 74.95 ms outlier; all maxima and spike durations remain in the report.',fontsize=9)
fig.savefig(args.output,dpi=150)
plt.close(fig)
