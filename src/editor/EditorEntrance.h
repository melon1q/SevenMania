// Shared preset catalog; saves declarative data only, no scripts.
void chooseEntrance(HWND owner){
 HMENU menu=CreatePopupMenu(),one=CreatePopupMenu(),scene=CreatePopupMenu();
 for(int i=0;i<9;++i){auto label=title(sevenmania::skin::entrancePresets[i]);AppendMenuW(one,MF_STRING,100+i,label.c_str());AppendMenuW(scene,MF_STRING,200+i,label.c_str());}
 AppendMenuW(menu,MF_POPUP|(selected<0?MF_GRAYED:0),reinterpret_cast<UINT_PTR>(one),L"선택 요소 애니메이션");
 AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(scene),L"현재 장면 애니메이션");
 RECT button{};GetWindowRect(GetDlgItem(owner,37),&button);const int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,button.left,button.bottom,0,owner,nullptr);DestroyMenu(menu);if(!command)return;
 const bool sceneScope=command>=200&&command<209;const int preset=command%100;checkpoint();auto previous=doc;
 try{
  auto applyPreset=[&](J& e){auto& ext=e.object[L"extensions"];ext.type=T::Object;if(preset==0){ext.object.erase(L"entrance");return;}const double duration=ext[L"entrance"][L"duration"].num(.6),delay=ext[L"entrance"][L"delay"].num(0);J entry;entry.type=T::Object;entry.object={{L"preset",str(title(sevenmania::skin::entrancePresets[preset]))},{L"duration",num(duration)},{L"delay",num(delay)}};ext.object[L"entrance"]=std::move(entry);};
  if(sceneScope){for(auto& e:doc.elements())if(e[L"id"].str()!=L"play-bga-viewport")applyPreset(e);}else if(selected>=0&&selected<int(doc.elements().size()))applyPreset(doc.elements()[selected]);
  prepare();refresh();
 }catch(...){doc=previous;undo.pop_back();throw;}
}
