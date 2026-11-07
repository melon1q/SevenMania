#include "skin/SkinEntrance.h"
#include "editor/EditorDocument.h"
int overlayEntranceRegression(const std::filesystem::path& output){
 try{
  require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)),"COM startup");
  std::filesystem::create_directories(output);
  HDC dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=64;info.bmiHeader.biHeight=-64;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* pixels{};auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);auto old=SelectObject(dc,bitmap);
  for(int preset=1;preset<9;++preset){
   sevenmania::editor::Document doc;doc.fresh();doc.manifest.object[L"resolution"].object[L"width"]=sevenmania::editor::num(64);doc.manifest.object[L"resolution"].object[L"height"]=sevenmania::editor::num(64);
   for(auto name:{"songinformation","settingssummary"}){std::string json="{\"screen\":\""+std::string(name)+"\",\"elements\":[{\"id\":\"panel\",\"type\":\"box\",\"x\":8,\"y\":8,\"width\":48,\"height\":48,\"extensions\":{\"entrance\":{\"preset\":\""+std::string(entrancePresets[preset])+"\",\"duration\":0.04}}}]}";doc.screens[name]=detail::json(Bytes(json.begin(),json.end()),name,{});}
   const auto path=output/(L"overlay-"+std::to_wstring(preset)+L".semaskin");doc.save(path);SkinApplication app;require(!app.load(path,64,64),"overlay fixture load");
   SkinDataProvider select,overlay;select.beginFrame(ScreenKind::Select);select.endFrame();
   for(auto kind:{ScreenKind::SongInformation,ScreenKind::SettingsSummary}){
    overlay.beginFrame(kind);overlay.endFrame();
    for(int frame=0;frame<12;++frame){PatBlt(dc,0,0,64,64,BLACKNESS);require(app.drawDc(dc,64,64,select.snapshot()),"select draw");require(app.drawDc(dc,64,64,overlay.snapshot()),"overlay draw");Sleep(10);}
    require(GetPixel(dc,32,32)==RGB(255,255,255),"animated overlay never became visible: screen.enter restarted during composition");
    app.enterScreen(kind);PatBlt(dc,0,0,64,64,BLACKNESS);require(app.drawDc(dc,64,64,select.snapshot()),"reopen select");require(app.drawDc(dc,64,64,overlay.snapshot()),"reopen overlay");require(GetPixel(dc,32,32)!=RGB(255,255,255),"overlay did not restart on reopen");Sleep(65);PatBlt(dc,0,0,64,64,BLACKNESS);require(app.drawDc(dc,64,64,select.snapshot())&&app.drawDc(dc,64,64,overlay.snapshot()),"reopen composition");require(GetPixel(dc,32,32)==RGB(255,255,255),"reopened overlay did not finish");
   }
  }
  SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);CoUninitialize();std::cout<<"PASS: all 8 presets on both overlay screens, select/overlay composition across 192 frames and 16 reopen cycles\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
