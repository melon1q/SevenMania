// Included inside the launcher namespace after its common control helpers.
struct SkinField {HWND control;const wchar_t* section;const wchar_t* key;std::wstring baseline;bool path{};bool choice{};};
std::vector<SkinField> skinFields;HWND customFps;
std::wstring skinPathEncode(const std::wstring& path){int n=WideCharToMultiByte(CP_UTF8,0,path.data(),int(path.size()),nullptr,0,nullptr,nullptr);std::string bytes(n,0);WideCharToMultiByte(CP_UTF8,0,path.data(),int(path.size()),bytes.data(),n,nullptr,nullptr);std::wstring out=L"utf8:";for(unsigned char c:bytes){out+=L"0123456789abcdef"[c>>4];out+=L"0123456789abcdef"[c&15];}return out;}
std::wstring skinPathDecode(const std::wstring& value){if(!value.starts_with(L"utf8:"))return value;std::string bytes;for(size_t i=5;i+1<value.size();i+=2){try{bytes+=char(std::stoi(value.substr(i,2),nullptr,16));}catch(...){return {};}}int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),int(bytes.size()),nullptr,0);std::wstring out(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),int(bytes.size()),out.data(),n);return out;}
void skinEdit(const wchar_t* title,int y,int id,const wchar_t* section,const wchar_t* key,const wchar_t* fallback,bool path=false){label(title,y,2);auto value=sevenmania::iniText(config,section,key);auto shown=value.empty()?std::wstring(fallback):path?skinPathDecode(value):value;auto c=control(L"EDIT",shown.c_str(),255,y,580,28,id,2,WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP);const bool choice=std::wstring(key)==L"Background";
    if(choice){DestroyWindow(c);c=control(L"COMBOBOX",L"",255,y,580,180,id,2,CBS_DROPDOWNLIST|WS_TABSTOP);for(auto option:{L"단색",L"곡 BGA / 동영상",L"곡 타이틀 이미지",L"사용자 이미지"})add(c,option);int index=2;try{index=std::stoi(shown);}catch(...){}select(c,std::clamp(index,0,3));}
    skinFields.push_back({c,section,key,value,path,choice});}
void createSkinSettings(){
    skinEdit(L"스킨 패키지",65,60,L"Skin",L"PackagePath",L"",true);
    control(L"BUTTON",L"스킨 선택...",255,100,190,30,61,2,WS_TABSTOP);control(L"BUTTON",L"기본 스킨",460,100,190,30,62,2,WS_TABSTOP);
    skinEdit(L"판정 세로 위치 (px)",145,63,L"Gameplay",L"JudgementTextYOffset",L"0");
    skinEdit(L"hybrid 박스 배경",190,64,L"Browse",L"Background",L"2");
    skinEdit(L"배경 블러 (0~100)",235,65,L"Browse",L"Blur",L"20");
    skinEdit(L"배경 불투명도 (0~100)",280,66,L"Browse",L"Opacity",L"10");
    skinEdit(L"사용자 이미지",325,67,L"Browse",L"CustomBackground",L"");
    control(L"BUTTON",L"사용자 이미지 선택...",255,365,300,32,68,2,WS_TABSTOP);
    control(L"STATIC",L"판정·콤보는 이미지 모드입니다. 없는 이미지는 기존 폰트로 안전하게 대체합니다.\nboxMode가 hybrid인 박스에만 이 배경 종류를 적용합니다. 변경은 적용 또는 게임 시작 때 저장합니다.",35,410,830,60,0,2);
}
void pickSkinAsset(bool skin){
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return;
    COMDLG_FILTERSPEC filter{skin?L"SevenMania 스킨":L"이미지",skin?L"*.semaskin":L"*.png;*.jpg;*.jpeg;*.bmp"};dialog->SetFileTypes(1,&filter);dialog->SetOptions(FOS_FORCEFILESYSTEM|FOS_FILEMUSTEXIST|FOS_NOCHANGEDIR);
    if(dialog->Show(window)!=S_OK)return;Microsoft::WRL::ComPtr<IShellItem> item;PWSTR value{};if(FAILED(dialog->GetResult(&item))||FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&value)))return;
    const std::filesystem::path path(value);CoTaskMemFree(value);
    if(skin){auto loaded=sevenmania::skin::SkinLoader{}.load(path);if(!loaded){message(L"스킨을 읽을 수 없습니다. 기존 선택을 유지합니다.");return;}SetWindowTextW(GetDlgItem(window,60),path.c_str());}
    else {sevenmania::SettingsGate gate(config);if(!gate.acquire()){message(L"게임을 종료한 뒤 이미지를 바꾸세요.");return;}ULONG_PTR token{};Gdiplus::GdiplusStartupInput startup;Gdiplus::GdiplusStartup(&token,&startup,nullptr);std::wstring name,error;const bool copied=copyUserBackgroundImage(path,base/L"Assets",name,error);Gdiplus::GdiplusShutdown(token);if(!copied){message(error);return;}SetWindowTextW(GetDlgItem(window,67),name.c_str());select(GetDlgItem(window,64),3);}
}
