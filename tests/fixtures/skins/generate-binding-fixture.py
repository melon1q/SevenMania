from pathlib import Path
import json,zipfile
manifest=dict(format='SevenMania.Skin',formatVersion=1,id='test.binding',name='Binding validation',author='SevenMania',skinVersion='1.0.0',description='Minimal data binding test, no design skin.',resolution=dict(width=640,height=360),supportedScreens=['select','play','result'],resourcePaths={})
def text(id,y,binding='',kind='text',literal='',**kw):
 e=dict(id=id,type=kind,x=20,y=y,width=600,height=40,fontSize=28,text=literal)
 if binding:e['binding']=binding
 e.update(kw);return e
screens={
 'select':[text('fixed',0,literal='SevenMania / 한글 / 日本語'),text('title',45,'song.title',default='No song'),text('artist',90,'song.artist'),text('bpm',135,'song.bpm','number',default='--',numberFormat=dict(decimals=1)),text('level',180,'chart.level','number',default='--',numberFormat=dict(decimals=1)),text('level-raw',225,'chart.levelText'),text('unknown',270,'unknown.value',default='Unknown binding')],
 'play':[text('title',0,'song.title'),text('score',45,'play.score','number',numberFormat=dict(grouping=True,minDigits=7)),text('combo',90,'play.combo','number'),text('gauge',135,'play.gauge','number',numberFormat=dict(decimals=2)),text('judgement',180,'play.judgement',default='--'),text('paused',225,'play.paused'),text('bpm',270,'song.bpm','number',numberFormat=dict(decimals=1))],
 'result':[text('title',0,'song.title'),text('score',45,'result.score','number',numberFormat=dict(grouping=True)),text('rank',90,'result.rank'),text('clear',135,'result.clearType'),text('max-combo',180,'result.maxCombo','number'),text('cleared',225,'result.cleared'),text('wrong-screen',270,'play.score','number',default='--'),text('wrong-type',315,'result.rank','number',default='--')]
}
p=Path(__file__).with_name('data-binding.semaskin')
with zipfile.ZipFile(p,'w',zipfile.ZIP_DEFLATED) as z:
 for name,data in [('manifest.json',manifest)]+[(f'screens/{s}.json',dict(screen=s,elements=e)) for s,e in screens.items()]:
  info=zipfile.ZipInfo(name,date_time=(2026,10,28,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=0o100644<<16;info.create_system=3
  z.writestr(info,json.dumps(data,ensure_ascii=False).encode('utf-8'))
