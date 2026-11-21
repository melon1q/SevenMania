struct SkinField {
    HWND control;
    const wchar_t* section;
    std::wstring key, fallback, baseline, initial;
    bool path{}, choice{}, numeric{}, check{};
    int minimum{}, maximum{};
    bool named{};   // value is a name from launcherNoteGroups (item data 0 = automatic)
};
std::vector<SkinField> skinFields;
HWND customFps{}, hitEffectChoice{}, noteGroupChoice{};
std::vector<std::wstring> launcherNoteGroups;


std::wstring skinPathEncode(const std::wstring& path){int n=WideCharToMultiByte(CP_UTF8,0,path.data(),int(path.size()),nullptr,0,nullptr,nullptr);std::string bytes(n,0);WideCharToMultiByte(CP_UTF8,0,path.data(),int(path.size()),bytes.data(),n,nullptr,nullptr);std::wstring out=L"utf8:";for(unsigned char c:bytes){out+=L"0123456789abcdef"[c>>4];out+=L"0123456789abcdef"[c&15];}return out;}
std::wstring skinPathDecode(const std::wstring& value){if(!value.starts_with(L"utf8:"))return value;std::string bytes;for(size_t i=5;i+1<value.size();i+=2){try{bytes+=char(std::stoi(value.substr(i,2),nullptr,16));}catch(...){return {};}}int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),int(bytes.size()),nullptr,0);std::wstring out(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),int(bytes.size()),out.data(),n);return out;}

bool validSkinNumber(const std::wstring& value, int minimum, int maximum) {
    try {
        size_t used{};
        const int number = std::stoi(value, &used);
        return used == value.size() && number >= minimum && number <= maximum;
    } catch (const std::exception&) { return false; }
}
std::wstring skinFieldValue(const SkinField& field) {
    if (field.check) return SendMessageW(field.control, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0";
    if (field.named) {
        const int index = selected(field.control);
        const auto item = index < 0 ? 0 : static_cast<size_t>(SendMessageW(field.control, CB_GETITEMDATA, index, 0));
        return item > 0 && item <= launcherNoteGroups.size() ? skinSettingEncode(launcherNoteGroups[item - 1]) : std::wstring();
    }
    if (field.choice) {
        const int index = selected(field.control);
        return index < 0 ? field.fallback : std::to_wstring(SendMessageW(field.control, CB_GETITEMDATA, index, 0));
    }
    wchar_t text[32768]{};
    GetWindowTextW(field.control,text,32768);
    return field.path && *text ? skinPathEncode(text) : std::wstring(text);
}
void addSkinChoice(HWND control, int value, const std::wstring& text) {
    const auto index = SendMessageW(control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    if (index >= 0) SendMessageW(control, CB_SETITEMDATA, index, value);
}
void selectSkinChoice(HWND control, const std::wstring& value, const std::wstring& fallback) {
    int fallbackIndex = 0;
    for (int i = 0, count = int(SendMessageW(control, CB_GETCOUNT, 0, 0)); i < count; ++i) {
        const auto item = std::to_wstring(SendMessageW(control, CB_GETITEMDATA, i, 0));
        if (item == value) { select(control, i); return; }
        if (item == fallback) fallbackIndex = i;
    }
    select(control, fallbackIndex);
}
std::filesystem::path launcherSkinFile() {
    wchar_t path[32768]{};
    GetWindowTextW(GetDlgItem(window,60), path, 32768);
    auto skin = std::filesystem::path(path);
    if (skin.empty()) {
        const auto skins = base / L"Assets/Skins";
        skin = skins / L"DefaultSkin/DefaultReference.semaskin";
        std::error_code error;
        if (!std::filesystem::is_regular_file(skin,error)) skin = skins / L"DefaultReference.semaskin";
    }
    return skin;
}
// Lists Notes/<name>-NoteGroup.png of the selected skin folder; the label is the name before "-NoteGroup".
void refreshLauncherNoteGroups(const std::wstring* wanted = nullptr) {
    std::wstring name;
    if (wanted) name = *wanted;
    else {
        const auto old = selected(noteGroupChoice);
        const auto item = old < 0 ? 0 : static_cast<size_t>(SendMessageW(noteGroupChoice, CB_GETITEMDATA, old, 0));
        name = old < 0 ? skinSettingDecode(sevenmania::iniText(config,L"Skin",L"NoteGroup"))
            : item > 0 && item <= launcherNoteGroups.size() ? launcherNoteGroups[item - 1] : std::wstring();
    }
    launcherNoteGroups.clear();
    for (auto& entry : skinNoteGroupFiles(launcherSkinFile().parent_path())) launcherNoteGroups.push_back(entry.first);
    SendMessageW(noteGroupChoice,CB_RESETCONTENT,0,0);
    addSkinChoice(noteGroupChoice,0,L"자동 / 기본");
    int chosen = 0;
    for (size_t i = 0; i < launcherNoteGroups.size(); ++i) {
        addSkinChoice(noteGroupChoice,int(i + 1),launcherNoteGroups[i]);
        if (_wcsicmp(launcherNoteGroups[i].c_str(), name.c_str()) == 0) chosen = int(i + 1);
    }
    select(noteGroupChoice, chosen);
}
void refreshLauncherHitEffects() {
    refreshLauncherNoteGroups();
    const auto old = selected(hitEffectChoice);
    const auto value = old < 0 ? sevenmania::iniText(config,L"Skin",L"HitEffect") :
        std::to_wstring(SendMessageW(hitEffectChoice, CB_GETITEMDATA, old, 0));
    wchar_t path[32768]{};
    GetWindowTextW(GetDlgItem(window,60), path, 32768);
    auto skin = std::filesystem::path(path);
    if (skin.empty()) {
        const auto skins = base / L"Assets/Skins";
        skin = skins / L"DefaultSkin/DefaultReference.semaskin";
        std::error_code error;
        if (!std::filesystem::is_regular_file(skin,error)) skin = skins / L"DefaultReference.semaskin";
    }
    SendMessageW(hitEffectChoice,CB_RESETCONTENT,0,0);
    addSkinChoice(hitEffectChoice,0,L"자동 / 기본");
    ULONG_PTR token{};
    Gdiplus::GdiplusStartupInput startup;
    if (Gdiplus::GdiplusStartup(&token,&startup,nullptr) == Gdiplus::Ok) {
        for (const auto& [number,file] : skinHitEffectFiles(skin.parent_path())) {
            TurntableImage image;
            const int frames = skinHitEffectFrames(file);
            if (frames >= 1 && frames <= 32 && loadRasterPngFile(file,image) && image.width()%frames==0)
                addSkinChoice(hitEffectChoice,number,std::to_wstring(number)+L".png");
        }
        Gdiplus::GdiplusShutdown(token);
    }
    selectSkinChoice(hitEffectChoice,value,L"0");
}
void reloadSkinFields() {
    for (auto& field : skinFields) {
        field.baseline = sevenmania::iniText(config,field.section,field.key.c_str());
        const auto& value = field.baseline.empty() ? field.fallback : field.baseline;
        if (field.control == hitEffectChoice) {
            refreshLauncherHitEffects();
            selectSkinChoice(field.control,value,field.fallback);
        } else if (field.named) {
            const auto name = skinSettingDecode(value);
            refreshLauncherNoteGroups(&name);
        } else if (field.choice) selectSkinChoice(field.control,value,field.fallback);
        else if (field.check) SendMessageW(field.control,BM_SETCHECK,value==L"0"?BST_UNCHECKED:BST_CHECKED,0);
        else SetWindowTextW(field.control,(field.path ? skinPathDecode(value) : value).c_str());
        field.initial = skinFieldValue(field);
    }
}
void createSkinSettings() {
    const auto caption = [](const wchar_t* title,int x,int y,int width=175) {
        control(L"STATIC",title,x,y+4,width,24,0,2);
    };
    const auto edit = [&](const wchar_t* title,int x,int y,int width,int id,const wchar_t* section,
        const wchar_t* key,const wchar_t* fallback,int minimum,int maximum,bool numeric=true,bool path=false) {
        caption(title,x,y);
        HWND input=control(L"EDIT",L"",x+175,y,width,28,id,2,WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP|(numeric?0:ES_READONLY));
        skinFields.push_back({input,section,key,fallback,{},{},path,false,numeric,false,minimum,maximum});
    };
    const auto combo = [&](const wchar_t* title,int x,int y,int width,int id,const wchar_t* section,
        const wchar_t* key,const wchar_t* fallback,std::initializer_list<std::pair<int,const wchar_t*>> options) {
        caption(title,x,y);
        HWND input=control(L"COMBOBOX",L"",x+175,y,width,180,id,2,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
        for(auto [value,label]:options)addSkinChoice(input,value,label);
        skinFields.push_back({input,section,key,fallback,{},{},false,true});
        return input;
    };
    edit(L"스킨 패키지",35,57,430,60,L"Skin",L"PackagePath",L"",0,0,false,true);
    control(L"BUTTON",L"선택...",650,57,96,28,61,2,WS_TABSTOP);
    control(L"BUTTON",L"기본 스킨",757,57,108,28,62,2,WS_TABSTOP);
    combo(L"hybrid 박스 배경",35,96,430,64,L"Browse",L"Background",L"2",
        {{0,L"단색"},{1,L"곡 BGA / 동영상"},{2,L"곡 타이틀 이미지"},{3,L"사용자 이미지"}});
    edit(L"사용자 이미지",35,135,430,67,L"Browse",L"CustomBackground",L"",0,0,false);
    control(L"BUTTON",L"이미지 선택...",650,135,215,28,68,2,WS_TABSTOP);
    edit(L"배경 블러 (0~100)",35,174,220,65,L"Browse",L"Blur",L"20",0,100);
    edit(L"배경 불투명도 (0~100)",470,174,220,66,L"Browse",L"Opacity",L"10",0,100);
    const std::initializer_list<std::pair<int,const wchar_t*>> modes{{-1,L"스킨 설정"},{0,L"폰트"},{1,L"이미지"}};
    combo(L"판정 표시",35,213,220,70,L"Skin",L"JudgementMode",L"1",modes);
    combo(L"콤보 표시",470,213,220,71,L"Skin",L"ComboMode",L"1",modes);
    edit(L"판정·콤보 세로 위치",35,252,220,63,L"Gameplay",L"JudgementTextYOffset",L"0",-256,256);
    hitEffectChoice=combo(L"히트 이펙트",470,252,220,72,L"Skin",L"HitEffect",L"0",{});
    caption(L"노트 이미지",35,291);
    noteGroupChoice=control(L"COMBOBOX",L"",210,291,220,180,79,2,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);
    skinFields.push_back({noteGroupChoice,L"Skin",L"NoteGroup",L"",{},{},false,false,false,false,0,0,true});
    combo(L"기어 위치",470,291,103,74,L"Display",L"GearPosition",L"1",{{0,L"왼쪽"},{1,L"가운데"},{2,L"오른쪽"}});
    auto mirror=control(L"BUTTON",L"UI 좌우 반전",753,291,112,28,77,2,BS_AUTOCHECKBOX|WS_TABSTOP);
    skinFields.push_back({mirror,L"Skin",L"MirrorGearUi",L"0",{},{},false,false,false,true});
    edit(L"레인 투명도 (0~100)",35,330,220,75,L"Display",L"LaneTransparency",L"34",0,100);
    edit(L"구분선 불투명도",470,330,220,76,L"Display",L"SeparatorOpacity",L"100",0,100);
    caption(L"구분선 표시",35,369);
    for(int i=0;i<7;++i) {
        const auto title=i==0?L"S|1":std::to_wstring(i)+L"|"+std::to_wstring(i+1);
        auto input=control(L"BUTTON",title.c_str(),210+i*88,369,83,28,90+i,2,BS_AUTOCHECKBOX|WS_TABSTOP);
        skinFields.push_back({input,L"Display",L"Separator"+std::to_wstring(i),i==0?L"1":L"0",{},{},false,false,false,true});
    }
    for(int lane=0;lane<8;++lane) {
        const int x=35+(lane%4)*207, y=408+(lane/4)*36;
        const auto title=lane==0?L"스크래치":std::to_wstring(lane)+L"번";
        caption(title.c_str(),x,y,84);
        auto input=control(L"COMBOBOX",L"",x+88,y,110,120,100+lane,2,CBS_DROPDOWNLIST|WS_TABSTOP);
        addSkinChoice(input,lane==0?3:7,lane==0?L"초록":L"흰색");
        addSkinChoice(input,lane==0?4:2,lane==0?L"빨강":L"파랑");
        skinFields.push_back({input,L"NoteColors",L"Lane"+std::to_wstring(lane),lane==0?L"4":lane%2?L"7":L"2",{},{},false,true});
    }
    reloadSkinFields();
}
void pickSkinAsset(bool skin){
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))return;
    COMDLG_FILTERSPEC filter{skin?L"SevenMania 스킨":L"이미지",skin?L"*.semaskin":L"*.png;*.jpg;*.jpeg;*.bmp"};dialog->SetFileTypes(1,&filter);dialog->SetOptions(FOS_FORCEFILESYSTEM|FOS_FILEMUSTEXIST|FOS_NOCHANGEDIR);
    if(dialog->Show(window)!=S_OK)return;Microsoft::WRL::ComPtr<IShellItem> item;PWSTR value{};if(FAILED(dialog->GetResult(&item))||FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&value)))return;
    const std::filesystem::path path(value);CoTaskMemFree(value);
    if(skin){auto loaded=sevenmania::skin::SkinLoader{}.load(path);if(!loaded){message(L"스킨을 읽을 수 없습니다. 기존 선택을 유지합니다.");return;}SetWindowTextW(GetDlgItem(window,60),path.c_str());refreshLauncherHitEffects();}
    else {sevenmania::SettingsGate gate(config);if(!gate.acquire()){message(L"게임을 종료한 뒤 이미지를 바꾸세요.");return;}ULONG_PTR token{};Gdiplus::GdiplusStartupInput startup;Gdiplus::GdiplusStartup(&token,&startup,nullptr);std::wstring name,error;const bool copied=copyUserBackgroundImage(path,base/L"Assets",name,error);Gdiplus::GdiplusShutdown(token);if(!copied){message(error);return;}SetWindowTextW(GetDlgItem(window,67),name.c_str());select(GetDlgItem(window,64),3);}
}
