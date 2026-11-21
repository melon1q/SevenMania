// LR2 skin selection on the settings skin page. Included by Main.cpp before SettingsRmlPage.inl.
// The command line still overrides these values for one run and is never written back.
#ifdef BMS_ENABLE_SKIN_BINDINGS
struct Lr2ScreenSlot {
    const char* element;
    const wchar_t* iniKey;
    const wchar_t* label;
    sevenmania::skin::lr2::ScreenType type;
    std::filesystem::path* file;
};
constexpr std::size_t lr2ScreenSlotCount=8;
std::array<Lr2ScreenSlot,lr2ScreenSlotCount> lr2ScreenSlots() {
    using sevenmania::skin::lr2::ScreenType;
    return {{{"lr2-select",L"Select",L"선곡",ScreenType::Select,&gLr2SelectFile},
             {"lr2-decide",L"Decide",L"곡 시작",ScreenType::Decide,&gLr2DecideFile},
             {"lr2-play7",L"Play7",L"7KEY 플레이",ScreenType::Play7Key,&gLr2PlayFile},
             {"lr2-play5",L"Play5",L"5KEY 플레이",ScreenType::Play5Key,&gLr2Play5File},
             {"lr2-play14",L"Play14",L"14KEY 플레이",ScreenType::Play14Key,&gLr2Play14File},
             {"lr2-play10",L"Play10",L"10KEY 플레이",ScreenType::Play10Key,&gLr2Play10File},
             {"lr2-play9",L"Play9",L"9KEY 플레이",ScreenType::Play9Key,&gLr2Play9File},
             {"lr2-result",L"Result",L"결과",ScreenType::Result,&gLr2ResultFile}}};
}
std::filesystem::path gLr2SkinRoot;
std::vector<sevenmania::skin::lr2::SkinEntry> gLr2Skins;
int gLr2CustomScreen{};
std::string gLr2CustomItem;

// Separators and letter case differ between a typed path, a saved one and a directory scan.
bool sameLr2SkinFile(std::filesystem::path a,std::filesystem::path b) {
    a=a.lexically_normal().make_preferred();
    b=b.lexically_normal().make_preferred();
    return _wcsicmp(a.c_str(),b.c_str())==0;
}
std::string lr2Utf8(std::wstring_view text) {
    if(text.empty())return {};
    const int size=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    std::string out(static_cast<std::size_t>(std::max(0,size)),'\0');
    if(size>0)WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),out.data(),size,nullptr,nullptr);
    return out;
}
// Same lossless "utf8:<hex>" form as the SevenMania skin path: existing INI files can be ANSI.
void writeLr2IniPath(const wchar_t* key,const std::filesystem::path& path) {
    if(gSettingsFile.empty())return;
    std::wstring encoded;
    if(!path.empty()) {
        constexpr wchar_t hex[]=L"0123456789abcdef";
        encoded=L"utf8:";
        for(unsigned char c:lr2Utf8(path.wstring())){encoded.push_back(hex[c>>4]);encoded.push_back(hex[c&15]);}
        if(encoded.size()>32000)return;
    }
    WritePrivateProfileStringW(L"LR2Skin",key,encoded.c_str(),gSettingsFile.c_str());
}
std::filesystem::path readLr2IniPath(const wchar_t* key) {
    std::wstring text(32768,L'\0');
    const auto count=GetPrivateProfileStringW(L"LR2Skin",key,L"",text.data(),static_cast<DWORD>(text.size()),gSettingsFile.c_str());
    if(count>=text.size()-1)return {};
    text.resize(count);
    if(!text.starts_with(L"utf8:"))return text;
    std::string bytes;
    const auto digit=[](wchar_t c){return c>=L'0'&&c<=L'9'?c-L'0':c>=L'a'&&c<=L'f'?c-L'a'+10:-1;};
    for(std::size_t i=5;i+1<text.size();i+=2) {
        const int high=digit(text[i]),low=digit(text[i+1]);
        if(high<0||low<0)return {};
        bytes.push_back(static_cast<char>(high*16+low));
    }
    try{return skinWide(bytes);}catch(const std::exception&){return {};}
}
void saveLr2SkinSettings() {
    if(gLr2CommandLine)return;
    for(const auto& slot:lr2ScreenSlots())writeLr2IniPath(slot.iniKey,*slot.file);
}
// A screen whose file is gone keeps using the SevenMania skin; the others are unaffected.
void loadLr2SkinSettings(const std::filesystem::path& executableDirectory=executableFolder()) {
    gLr2SkinRoot=executableDirectory/L"Theme";
    gLr2SkinsScanned=false;
    if(gLr2CommandLine||gSettingsFile.empty())return;
    for(const auto& slot:lr2ScreenSlots()) {
        const auto path=readLr2IniPath(slot.iniKey);
        std::error_code error;
        *slot.file=!path.empty()&&std::filesystem::is_regular_file(path,error)?path:std::filesystem::path{};
    }
}
// Why each assigned skin cannot be drawn, or empty. Checked once per file and choice set.
std::array<std::filesystem::path,lr2ScreenSlotCount> gLr2CheckedFile;
std::array<std::wstring,lr2ScreenSlotCount> gLr2SlotError;

// Loads the skin the way the game will, without a GPU device, and reports the first fatal problem.
std::wstring lr2SkinLoadError(const Lr2ScreenSlot& slot) {
    using namespace sevenmania::skin::lr2;
    try {
        Options options;
        options.screen=slot.type;
        options.choices=lr2SkinChoices(*slot.file);
        // One representative game state per screen: a selected song, a stage image, a cleared result.
        if(slot.type==ScreenType::Select)options.initialOptions=lr2SelectLoadOptions();
        else if(slot.type==ScreenType::Decide)options.initialOptions={{52,true},{53,false},{190,false},{191,true}};
        else if(slot.type==ScreenType::Result)options.initialOptions={{90,true},{91,false},{80,false},{81,true}};
        else options.condition=[keys=playKeys(static_cast<int>(slot.type))](int id)->std::optional<bool> {
            PlayFrame state;
            state.keys=keys;
            if(const auto enabled=state.option(std::abs(id)))return *enabled==(id>0);
            return std::nullopt;
        };
        ScreenData song;
        song.songText.fill(L"Text");
        auto result=inspectLr2Scene(*slot.file,options,&song);
        if(result.scene)return {};
        std::string reason="스킨을 읽을 수 없습니다";
        for(const auto& diagnostic:result.diagnostics)if(diagnostic.error){
            return diagnostic.source.wstring()+L":"+std::to_wstring(diagnostic.line)+L" "+skinWide(diagnostic.message);
        }
        // A missing include is the usual cause: the theme is not where its paths say it is.
        for(const auto& diagnostic:result.diagnostics)
            if(diagnostic.message.starts_with("INCLUDE skipped")) {
                const auto name=diagnostic.message.find("): ");
                if(name!=std::string::npos)reason+=" · 찾지 못한 파일: "+diagnostic.message.substr(name+3);
                break;
            }
        return skinWide(reason);
    } catch(const std::exception&) {
        return L"스킨을 읽는 중 오류가 발생했습니다";
    }
}

// The next paint or play start loads the newly chosen files.
void applyLr2SkinSettings() {
    gLr2CheckedFile={};
    gLr2RuntimeErrors={};
    if(gLr2SelectScene) {
        gLr2SelectActive=false;
        resetLr2SelectControls();
        gLr2SelectScene.reset();
        gCompositionPresenter.reset();
    }
    gLr2SelectAttempted=false;
    gLr2SelectGpuFailed=false;
    gLr2DecideScene.reset();
    if(gWindow)InvalidateRect(gWindow,nullptr,FALSE);
}
std::filesystem::path lr2ChoiceStore() {return gSettingsFile.parent_path()/L"lr2-skins.txt";}
// Header with the user's selections applied, plus the root its file patterns resolve against.
std::optional<sevenmania::skin::lr2::Document> lr2SkinHeader(const std::filesystem::path& file) {
    sevenmania::skin::lr2::Options options;
    options.headerOnly=true;
    options.choices=lr2SkinChoices(file);
    auto result=sevenmania::skin::lr2::parse(file,options);
    if(!result)return std::nullopt;
    return std::move(*result.document);
}

void syncLr2SkinSettings(const std::wstring& status=L"") {
    if(!gRmlSettingsPage.window())return;
    using Options=std::vector<std::pair<std::string,std::string>>;
    if(!gLr2SkinsScanned) {
        gLr2SkinsScanned=true;
        gLr2Skins=gLr2SkinRoot.empty()?std::vector<sevenmania::skin::lr2::SkinEntry>{}:sevenmania::skin::lr2::findSkins(gLr2SkinRoot);
    }
    const bool allowed=!gLr2CommandLine&&!gPlaying&&!gShowingResult&&!gLoading;
    gRmlSettingsPage.setText("lr2-root",lr2Utf8(gLr2SkinRoot.wstring()));
    for(const char* id:{"lr2-clear","lr2-rescan"})gRmlSettingsPage.setEnabled(id,allowed);
    const auto slots=lr2ScreenSlots();
    Options assigned;
    for(std::size_t index=0;index<slots.size();++index) {
        const auto& slot=slots[index];
        Options options{{"none","사용 안 함 (SevenMania 스킨)"}};
        std::string selected="none";
        for(std::size_t i=0;i<gLr2Skins.size();++i) {
            const auto& skin=gLr2Skins[i];
            if(skin.header.type!=static_cast<int>(slot.type))continue;
            std::error_code error;
            const auto relative=std::filesystem::relative(skin.file,gLr2SkinRoot,error);
            options.emplace_back(std::to_string(i),skin.header.name+" · "+lr2Utf8((error?skin.file:relative).wstring()));
            if(sameLr2SkinFile(skin.file,*slot.file))selected=std::to_string(i);
        }
        if(!slot.file->empty()&&selected=="none") {
            options.emplace_back("current",lr2Utf8(slot.file->filename().wstring())+" (목록 밖)");
            selected="current";
        }
        gRmlSettingsPage.setOptions(slot.element,options);
        gRmlSettingsPage.setValue(slot.element,selected);
        gRmlSettingsPage.setEnabled(slot.element,allowed);
        if(!slot.file->empty())assigned.emplace_back(std::to_string(index),lr2Utf8(slot.label));
        if(slot.file->empty())gLr2SlotError[index].clear();
        else if(gLr2CheckedFile[index].empty()||!sameLr2SkinFile(gLr2CheckedFile[index],*slot.file)) {
            gLr2CheckedFile[index]=*slot.file;
            gLr2SlotError[index]=lr2SkinLoadError(slot);
        }
    }
    std::wstring failures;
    for(std::size_t index=0;index<slots.size();++index) {
        if(!gLr2SlotError[index].empty())failures+=std::wstring(L"  [")+slots[index].label+L" 사전 검사] "+gLr2SlotError[index];
        if(const auto& runtime=gLr2RuntimeErrors[static_cast<std::size_t>(slots[index].type)];!runtime.empty())
            failures+=std::wstring(L"  [")+slots[index].label+L" 실행 중] "+runtime;
    }
    // Options of one assigned skin at a time: screen, then item, then its value.
    Options items,values;
    std::string value="none";
    if(assigned.empty())assigned.emplace_back("none","(지정된 LR2 스킨 없음)");
    else {
        if(std::none_of(assigned.begin(),assigned.end(),[](const auto& entry){return entry.first==std::to_string(gLr2CustomScreen);}))
            gLr2CustomScreen=std::stoi(assigned.front().first);
        const auto& file=*slots[static_cast<std::size_t>(gLr2CustomScreen)].file;
        if(const auto document=lr2SkinHeader(file)) {
            const auto& header=document->header;
            for(std::size_t i=0;i<header.options.size();++i)
                if(!header.options[i].choices.empty())items.emplace_back("o"+std::to_string(i),"옵션 · "+header.options[i].name);
            for(std::size_t i=0;i<header.files.size();++i)items.emplace_back("f"+std::to_string(i),"파일 · "+header.files[i].name);
            if(std::none_of(items.begin(),items.end(),[](const auto& entry){return entry.first==gLr2CustomItem;}))
                gLr2CustomItem=items.empty()?std::string{}:items.front().first;
            if(!gLr2CustomItem.empty()) {
                const auto index=static_cast<std::size_t>(std::stoul(gLr2CustomItem.substr(1)));
                if(gLr2CustomItem.front()=='o') {
                    const auto& option=header.options[index];
                    for(std::size_t i=0;i<option.choices.size();++i)values.emplace_back(std::to_string(i),option.choices[i]);
                    value=std::to_string(option.selected);
                } else {
                    const auto& entry=header.files[index];
                    const auto stems=sevenmania::skin::lr2::listWildcardChoices(document->root,file,entry.pattern);
                    for(std::size_t i=0;i<stems.size();++i) {
                        values.emplace_back(std::to_string(i),stems[i]);
                        // The selection wins over the default; with neither present the first file is what loads.
                        if(stems[i]==entry.selected||(value=="none"&&stems[i]==entry.defaultName))value=std::to_string(i);
                    }
                    if(value=="none"&&!stems.empty())value="0";
                }
            }
        }
    }
    if(items.empty())items.emplace_back("none","(바꿀 항목 없음)");
    if(values.empty())values.emplace_back("none","(선택지 없음)");
    gRmlSettingsPage.setOptions("lr2-custom-screen",assigned);
    gRmlSettingsPage.setValue("lr2-custom-screen",assigned.front().first=="none"?"none":std::to_string(gLr2CustomScreen));
    gRmlSettingsPage.setOptions("lr2-custom-item",items);
    gRmlSettingsPage.setValue("lr2-custom-item",items.front().first=="none"?"none":gLr2CustomItem);
    gRmlSettingsPage.setOptions("lr2-custom-value",values);
    gRmlSettingsPage.setValue("lr2-custom-value",value);
    gRmlSettingsPage.setEnabled("lr2-custom-value",allowed&&value!="none");
    const std::wstring state=!status.empty()?status:gLr2CommandLine?L"명령줄 인자로 지정한 LR2 스킨을 사용 중입니다. 이번 실행에서는 여기서 바꿀 수 없습니다.":
        !gCompositionEnabled?L"LR2 스킨은 GPU 렌더링이 켜져 있어야 표시됩니다.":
        !failures.empty()?L"불러올 수 없는 스킨이 있습니다. 해당 화면은 SevenMania 스킨으로 표시됩니다."+failures:
        gLr2Skins.empty()?L"실행 파일 옆 Theme 폴더에 LR2 스킨 폴더를 넣어 주세요.":
        L"스킨 "+std::to_wstring(gLr2Skins.size())+L"개를 찾았습니다.";
    gRmlSettingsPage.setText("lr2-status",lr2Utf8(state));
}

// Returns true when the element belongs to this section, handled or not.
bool handleLr2SkinSetting(const std::string& id,const std::string& value) {
    if(!id.starts_with("lr2-"))return false;
    if(id=="lr2-custom-screen") {
        try{gLr2CustomScreen=std::clamp(std::stoi(value),0,static_cast<int>(lr2ScreenSlotCount)-1);}catch(const std::exception&){}
        gLr2CustomItem.clear();
        syncLr2SkinSettings();
        return true;
    }
    if(id=="lr2-custom-item"){gLr2CustomItem=value=="none"?std::string{}:value;syncLr2SkinSettings();return true;}
    if(gLr2CommandLine){syncLr2SkinSettings();return true;}
    if(gPlaying||gShowingResult||gLoading){syncLr2SkinSettings(L"곡 선택 화면에서 변경하세요.");return true;}
    if(id=="lr2-rescan"){gLr2SkinsScanned=false;syncLr2SkinSettings();return true;}
    const auto slots=lr2ScreenSlots();
    if(id=="lr2-clear") {
        for(const auto& slot:slots)slot.file->clear();
        saveLr2SkinSettings();
        applyLr2SkinSettings();
        syncLr2SkinSettings(L"LR2 스킨을 사용하지 않습니다.");
        return true;
    }
    for(const auto& slot:slots) {
        if(id!=slot.element)continue;
        if(value=="none")slot.file->clear();
        else if(value!="current") {
            std::size_t index=gLr2Skins.size();
            try{index=std::stoul(value);}catch(const std::exception&){}
            if(index>=gLr2Skins.size()||gLr2Skins[index].header.type!=static_cast<int>(slot.type)){syncLr2SkinSettings(L"지원하지 않는 설정 값입니다.");return true;}
            *slot.file=gLr2Skins[index].file;
        }
        saveLr2SkinSettings();
        applyLr2SkinSettings();
        syncLr2SkinSettings();
        return true;
    }
    if(id=="lr2-custom-value"&&value!="none"&&!gLr2CustomItem.empty()&&gLr2CustomScreen>=0&&gLr2CustomScreen<static_cast<int>(lr2ScreenSlotCount)) {
        const auto& file=*slots[static_cast<std::size_t>(gLr2CustomScreen)].file;
        const auto document=file.empty()?std::nullopt:lr2SkinHeader(file);
        bool saved=false;
        try {
            const auto item=static_cast<std::size_t>(std::stoul(gLr2CustomItem.substr(1))),choice=static_cast<std::size_t>(std::stoul(value));
            auto choices=lr2SkinChoices(file);
            if(document&&gLr2CustomItem.front()=='o'&&item<document->header.options.size()&&choice<document->header.options[item].choices.size()) {
                choices.options[document->header.options[item].name]=document->header.options[item].choices[choice];
                saved=sevenmania::skin::lr2::saveSkinChoices(lr2ChoiceStore(),file,choices);
            } else if(document&&gLr2CustomItem.front()=='f'&&item<document->header.files.size()) {
                const auto stems=sevenmania::skin::lr2::listWildcardChoices(document->root,file,document->header.files[item].pattern);
                if(choice<stems.size()) {
                    choices.files[document->header.files[item].name]=stems[choice];
                    saved=sevenmania::skin::lr2::saveSkinChoices(lr2ChoiceStore(),file,choices);
                }
            }
        } catch(const std::exception&) {
        }
        if(saved)applyLr2SkinSettings();
        syncLr2SkinSettings(saved?L"":L"스킨 옵션을 저장하지 못했습니다.");
    }
    return true;
}
#else
void loadLr2SkinSettings() {}
void syncLr2SkinSettings(const std::wstring& = L"") {}
bool handleLr2SkinSetting(const std::string&,const std::string&) {return false;}
#endif
