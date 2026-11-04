"""Real player/message/audio/paint comparison. Writes only isolated diagnostic outputs."""
import argparse,configparser,hashlib,json,os,subprocess,zipfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def package(source,output,asset,freeze_info=False):
    if asset=='original':output.write_bytes(Path(source).read_bytes());return
    with zipfile.ZipFile(source) as z: files={n:z.read(n) for n in z.namelist()}
    with zipfile.ZipFile(root/'tests/fixtures/skins/hud-images.semaskin') as z:glyphs={n:z.read(n) for n in z.namelist()}
    screen=json.loads(files['screens/play.json']);manifest=json.loads(files['manifest.json'])
    for e in screen['elements']:
        if e.get('binding')!='play.judgementCombo':continue
        e.update(x=660,y=520,width=600,height=100,fontSize=80,opacity=1)
        e['hud']=dict(judgementMode='font',comboMode='font',judgementWidth=320,judgementHeight=80,digitWidth=48,digitHeight=80,spacing=0,gap=16,displayMs=850)
        if asset=='png':
            names=['pgreat','great','good','bad','poor','mine'];e['hud']['judgements']={n:'probe-'+n for n in names};e['hud']['digits']=['probe-'+str(n) for n in range(10)]
            for n in names+[str(n) for n in range(10)]:
                path='assets/images/probe/'+n+'.png';files[path]=glyphs['assets/images/'+n+'.png'];manifest['resourcePaths']['probe-'+n]=path
    if freeze_info:
        for e in screen['elements']:
            if e.get('binding') in ['play.score','play.gauge']:e['text']='000000' if e.pop('binding')=='play.score' else '20'
    files['screens/play.json']=json.dumps(screen,ensure_ascii=False).encode();files['manifest.json']=json.dumps(manifest,ensure_ascii=False).encode()
    with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED) as z:
        for name,data in files.items():z.writestr(name,data)
p=argparse.ArgumentParser();p.add_argument('--label',required=True);p.add_argument('--legacy-video-frames',action='store_true');p.add_argument('--bga',choices=['original','off','still','switch'],default='original');p.add_argument('--chart',type=Path);p.add_argument('--focus-probe',action='store_true');p.add_argument('--reflex-before',action='store_true');p.add_argument('--reflex',action='store_true');p.add_argument('--legacy',action='store_true');p.add_argument('--verify',action='store_true');p.add_argument('--freeze-info',action='store_true');p.add_argument('--asset',choices=['png','fallback','original'],default='png');p.add_argument('--repeats',type=int,default=2);p.add_argument('--duration',type=int,default=12);p.add_argument('--boundary',action='store_true');p.add_argument('--cases',default='00,11,10,01');p.add_argument('--fps',type=int,default=0);p.add_argument('--output',default='build-rmlui-song-list/Validation/HudFrameStudy');p.add_argument('--exe',default='build-rmlui-song-list/HudProbe/Release/BmsHudFrameProfile.exe');a=p.parse_args()
if a.focus_probe and (not a.reflex or a.reflex_before or a.verify or a.legacy):p.error('--focus-probe requires --reflex and cannot be combined with legacy/verify modes')
out=root/a.output;out.mkdir(parents=True,exist_ok=True);(out/'EmptyLibrary').mkdir(exist_ok=True);exe=(root/a.exe).resolve()
chart=a.chart.resolve() if a.chart else next((root/'build/Release/Songs').rglob('__btb_cso_7another.bme'));source=root/'build-rmlui-song-list/Release/Assets/Skins/DefaultReference.semaskin';skin=out/(a.label+'-'+a.asset+'.semaskin');package(source,skin,a.asset,a.freeze_info)
for repeat in range(a.repeats):
    cases=a.cases.split(',');cases=cases if repeat%2==0 else cases[::-1]
    for mode in cases:
        label=f'{a.label}-{a.asset}-{mode}-r{repeat+1}';prefix=out/label
        if Path(str(prefix)+'.frames.csv').exists():raise RuntimeError(f'Preserve existing results: {prefix}')
        cfg=out/(label+'.ini');cfg.write_text(f'[Library]\nLastFolder={out/"EmptyLibrary"}\n[Display]\nFPS={a.fps}\nSpeedSteps=24\nScreenMode=1\nResolutionWidth=1920\nResolutionHeight=1080\nDirectComposition=1\n[System]\nShowUsage=0\nCpuLimitPercent=100\nMemoryLimitPercent=100\nGpuLimitPercent=100\n[Gameplay]\nAccuracyLimit=0\n',encoding='utf-16')
        env=os.environ.copy();env['BMS_PLAYER_CONFIG_PATH']=str(cfg);env.pop('BMS_PROBE_LEGACY_VIDEO_FRAMES',None);env['BMS_PROBE_BGA_CASE']=a.bga
        if a.legacy_video_frames:env['BMS_PROBE_LEGACY_VIDEO_FRAMES']='1'
        sources=['src/AsyncVideoBga.h','src/LowLatency.h','src/FramePacer.h','src/ReflexBackend.h','src/Main.cpp','src/HudFrameProbe.h','src/CompositionPresenter.h','src/skin/SkinRenderer.cpp','src/skin/SkinHudImages.h','src/skin/SkinApplication.cpp','tests/HudFrameProfile.cpp']
        manifest=dict(label=label,asset=a.asset,modes=mode,detail=not a.boundary,start=30,duration=a.duration,analyze_wall_start=2,analyze_wall_end=a.duration-1,exe_sha256=sha(exe),skin_sha256=sha(skin),original_skin_sha256=sha(source),chart_sha256=sha(chart),config_sha256=sha(cfg),source_sha256={s:sha(root/s) for s in sources},bga_case=a.bga,legacy_video_frames=a.legacy_video_frames,focus_probe=a.focus_probe,reflex_before=a.reflex_before,reflex_requested=a.reflex,force_legacy_icons=a.legacy,freeze_info=a.freeze_info,fps=a.fps,direct_composition=True,muted_audio=True,resource_overlay=False)
        Path(str(prefix)+'.manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
        with Path(str(prefix)+'.log').open('w',encoding='utf-8') as log:
            result=subprocess.run([str(exe),str(chart),str(skin),str(prefix),mode[0],mode[1],str(a.duration),'0' if a.boundary else '1',str(a.fps)]+(['reflex-focus'] if a.focus_probe else ['reflex-before' if a.reflex else 'off-before'] if a.reflex_before else ['reflex'] if a.reflex else ['verify'] if a.verify else ['legacy'] if a.legacy else []),cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        print(label,'exit',result.returncode,flush=True)
        if result.returncode:raise RuntimeError(f'Profile failed: {prefix}')
