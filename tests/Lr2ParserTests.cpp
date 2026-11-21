#include "skin/lr2/Lr2Parser.h"
#include "skin/lr2/Lr2ImageModel.h"
#include "skin/lr2/Lr2SkinConfig.h"
#include "skin/lr2/Lr2SkinLibrary.h"
#include <windows.h>
#include <cwctype>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <tuple>

using namespace sevenmania::skin::lr2;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static std::string ms932(const std::wstring& s){
    BOOL fallback=FALSE;int n=WideCharToMultiByte(932,WC_NO_BEST_FIT_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,&fallback);
    check(n>0&&!fallback,"cannot encode fixture");std::string out(static_cast<std::size_t>(n),'\0');
    check(WideCharToMultiByte(932,WC_NO_BEST_FIT_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,&fallback)>0&&!fallback,"cannot encode fixture");return out;
}
static void write(const std::filesystem::path& path,const std::wstring& text){
    std::filesystem::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);const auto bytes=ms932(text);out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));check(bool(out),"fixture write failed");
}
static const Command& find(const Document& doc,const std::string& name){for(const auto& c:doc.commands)if(c.name==name)return c;throw std::runtime_error("command missing: "+name);}
int wmain(int argc,wchar_t** argv){
 try{
    if(argc==3&&std::wstring(argv[1])==L"--real"){
        const auto result=parse(argv[2]);if(!result){for(const auto& d:result.diagnostics)std::cerr<<d.message<<" line "<<d.line<<"\n";return 1;}
        std::size_t active=0;for(const auto& c:result.document->commands)if(c.enabled)++active;
        std::cout<<"7KEY: "<<result.document->header.width<<"x"<<result.document->header.height<<", "<<result.document->commands.size()<<" commands, "<<active<<" enabled, "<<result.diagnostics.size()<<" warnings\n";return 0;
    }
    if(argc==3&&std::wstring(argv[1])==L"--real-all"){
        for(int side:{900,901})for(int scratch:{905,906})for(int size:{910,911,912}){
            Options o;o.initialOptions={{side,true},{scratch,true},{size,true}};const auto result=parse(argv[2],o);
            if(!result){for(const auto& d:result.diagnostics)if(d.error)std::cerr<<d.message<<" line "<<d.line<<"\n";return 1;}
            bool included=false;for(const auto& c:result.document->commands)if(c.source!=std::filesystem::canonical(argv[2]))included=true;
            check(included,"selected branch did not include its CSV");
        }
        std::cout<<"7KEY: all 12 side/scratch/size combinations parsed\n";return 0;
    }
    check(argc==2,"expected test directory");const auto root=std::filesystem::path(argv[1])/L"lr2-phase1";
    const auto main=root/L"LR2files/Theme/Test/play.lr2skin";
    write(main,L"#INFORMATION,0,日本語,作者,\n#RESOLUTION,2\n#CUSTOMOPTION,配置,900,左,,右,,\n#CUSTOMFILE,画像,*.png,default\n#CUSTOMOFFSET,位置,42,1,0,1,0,1,0\n#IF,900\n#INCLUDE,.\\LR2files\\Theme\\Test\\parts\\first.csv\n#ELSE\n#SRC_BAD,0\n#ENDIF\n#SETOPTION,901,1\n#IF,901,!900\n#SRC_FALSE\n#ELSEIF,901,900\n#SRC_SELECTED,alpha,,\n#ELSE\n#SRC_OTHER\n#ENDIF\n#UNKNOWN_FUTURE,1,,\n");
    write(main.parent_path()/L"parts/first.csv",L"#IF,900\n#SRC_IMAGE,1,2,,,\n#DST_IMAGE,0,0,1,2,\n#ENDIF\n");
    auto r=parse(main);check(bool(r),"valid skin rejected");const auto& d=*r.document;
    check(d.header.type==0&&d.header.name=="日本語"&&d.header.author=="作者","MS932 header");
    check(d.header.width==1920&&d.header.height==1080,"FHD resolution");
    check(d.header.options.size()==1&&d.header.options[0].choices.size()==2&&d.header.options[0].choices[1]=="右","custom option fields");
    check(d.header.files.size()==1&&d.header.offsets.size()==1&&!d.header.offsets[0].enabled[1],"custom file/offset");
    const auto& img=find(d,"SRC_IMAGE");check(img.enabled&&img.line==2&&img.source.filename()==L"first.csv"&&img.fields.size()==6&&img.fields[5].empty(),"included conditional command or trailing empty field");
    auto model=imageModel(d);check(model&&model->sources.size()==1&&model->sources[0].destinations.size()==1,"image source/destination model");
    ImageSource sample;sample.values={0,0,0,0,100,40,2,1,1000,0};
    std::array<int,19> a{},b{};a[3]=100;a[4]=50;a[6]=255;b=a;b[0]=1000;b[1]=100;sample.destinations={a,b};
    auto frame=sampleImage(sample,100,40,500,1280,960,640,480);
    check(frame&&frame->x==100&&frame->y==0&&frame->sourceX==50&&frame->sourceWidth==50&&frame->alpha==1,"image interpolation, coordinates, sprite frame");
    check(!sampleImage(sample,99,40,500,1280,960,640,480),"invalid source region accepted");
    check(find(d,"SRC_SELECTED").enabled&&!find(d,"SRC_FALSE").enabled&&!find(d,"SRC_BAD").enabled,"conditional branch");
    check(find(d,"UNKNOWN_FUTURE").fields.size()==4&&!r.diagnostics.empty(),"unsupported command must be retained and diagnosed");
    auto alternateSkin=root/L"alternate.lr2skin";write(alternateSkin,L"#INFORMATION,0,T,A\n#CUSTOMOPTION,Side,900,LEFT,RIGHT\n#IF,900\n#SRC_LEFT\n#ELSE\n#SRC_RIGHT\n#ENDIF\n#IF,!900\n#SRC_NEGATIVE_UNKNOWN\n#ENDIF\n");
    Options chooseRight;chooseRight.initialOptions[901]=true;auto alternate=parse(alternateSkin,chooseRight);
    check(alternate&&!find(*alternate.document,"SRC_LEFT").enabled&&find(*alternate.document,"SRC_RIGHT").enabled&&!find(*alternate.document,"SRC_NEGATIVE_UNKNOWN").enabled,"explicit option selection or unknown negative condition");
    for(const auto& [index,w,h]:{std::tuple{0,640u,480u},std::tuple{1,1280u,720u},std::tuple{2,1920u,1080u},std::tuple{3,3840u,2160u}}){
        auto p=root/L"resolution.lr2skin";write(p,L"#INFORMATION,0,Test,Author\n#RESOLUTION,"+std::to_wstring(index)+L"\n");auto t=parse(p);check(t&&t.document->header.width==w&&t.document->header.height==h,"resolution variant");
    }
    auto skin=root/L"invalid.lr2skin";write(skin,L"#INFORMATION,1,Other,Author\n");check(!parse(skin),"non-7KEY accepted");
    Options decide;decide.screen=ScreenType::Decide;
    Options result;result.screen=ScreenType::Result;
    write(skin,L"#INFORMATION,6,Decide,Author\n");
    check(bool(parse(skin,decide))&&!parse(skin)&&!parse(skin,result),"Decide type validation");
    write(skin,L"#INFORMATION,7,Result,Author\n");
    check(bool(parse(skin,result))&&!parse(skin,decide),"Result type validation");
    write(skin,L"#INFORMATION,+0,T,A\n#RESOLUTION,+1\n");check(bool(parse(skin)),"explicit positive integer rejected");
    write(skin,L"#INFORMATION,0,T,A\r#RESOLUTION,1\r#SRC_CR,1\r");auto cr=parse(skin);check(cr&&cr.document->header.width==1280&&find(*cr.document,"SRC_CR").line==3,"CR-only line handling");
    write(skin,L"#INFORMATION,0,T,A\n#RESOLUTION,9\n");check(!parse(skin),"invalid resolution accepted");
    const auto resolutionBody=root/L"resolution-body.csv";
    write(skin,L"#INFORMATION,0,T,A\n#INCLUDE,resolution-body.csv\n");
    write(resolutionBody,L"#RESOLUTION,1\n#DST_IMAGE,0,0,0,0,1920,1080\n");
    auto includedResolution=parse(skin);
    check(includedResolution&&includedResolution.document->header.width==1280,"included resolution overrides canvas inference");
    write(resolutionBody,L"#INCLUDE,missing-effect.csv\n#DST_IMAGE,0,0,0,0,1920,1080\n"
        L"#IF,999\n#DST_IMAGE,0,0,0,0,1280,720\n#ENDIF\n#DST_IMAGE,0,0,-1,0,3840,2160\n");
    auto inferredResolution=parse(skin);
    check(inferredResolution&&inferredResolution.document->header.width==1920&&inferredResolution.document->header.height==1080,
        "an unambiguous full-canvas image in an included CSV supplies the missing resolution");
    check(std::any_of(inferredResolution.diagnostics.begin(),inferredResolution.diagnostics.end(),[](const auto& diagnostic){
        return !diagnostic.error&&diagnostic.message.find("missing-effect.csv")!=std::string::npos;
    }),"missing nested effect include is reported without aborting the core scene");
    write(resolutionBody,L"#DST_IMAGE,0,0,0,0,1920,1080\n#DST_IMAGE,0,0,0,0,1280,720\n");
    inferredResolution=parse(skin);
    check(inferredResolution&&inferredResolution.document->header.width==640,"ambiguous image sizes preserve the SD default");
    // IF nests: an inner block only narrows the outer one, and each ENDIF closes its own block.
    write(skin,L"#INFORMATION,0,T,A\n#SETOPTION,900,1\n#IF,900\n#SRC_OUTER\n#IF,999\n#SRC_INNER\n#ELSE\n#SRC_INNER_ELSE\n#ENDIF\n#SRC_AFTER\n#ENDIF\n#SRC_TAIL\n"
        L"#IF,999\n#SRC_SKIPPED\n#IF,900\n#SRC_SKIPPED_INNER\n#ELSE\n#SRC_SKIPPED_ELSE\n#ENDIF\n#SRC_SKIPPED_AFTER\n#ELSE\n#SRC_OUTER_ELSE\n#ENDIF\n");
    auto nested=parse(skin);
    const auto on=[](const Result& r,const char* name){return find(*r.document,name).enabled;};
    check(nested&&on(nested,"SRC_OUTER")&&!on(nested,"SRC_INNER")&&on(nested,"SRC_INNER_ELSE")&&on(nested,"SRC_AFTER")&&on(nested,"SRC_TAIL"),
        "nested IF inside a true block");
    check(!on(nested,"SRC_SKIPPED")&&!on(nested,"SRC_SKIPPED_INNER")&&!on(nested,"SRC_SKIPPED_ELSE")&&!on(nested,"SRC_SKIPPED_AFTER")&&
        on(nested,"SRC_OUTER_ELSE"),"nested IF inside a false block stays skipped, including its ELSE");
    const auto warned=[](const Result& r,const char* text){return std::any_of(r.diagnostics.begin(),r.diagnostics.end(),
        [&](const auto& d){return !d.error&&d.message.find(text)!=std::string::npos;});};
    check(!warned(nested,"ENDIF without")&&!warned(nested,"Unclosed IF"),"balanced nesting has no control warnings");
    // An INCLUDE shares the condition state: its ENDIF may close the including file's IF.
    write(root/L"shared/close.csv",L"#SRC_INSIDE\n#ENDIF\n");
    write(root/L"shared/open.csv",L"#IF,999\n");
    write(skin,L"#INFORMATION,0,T,A\n#SETOPTION,900,1\n#IF,900\n#INCLUDE,shared/close.csv\n#SRC_CLOSED\n#INCLUDE,shared/open.csv\n#SRC_OPENED\n#ENDIF\n#SRC_END\n");
    auto shared=parse(skin);
    check(shared&&on(shared,"SRC_INSIDE")&&on(shared,"SRC_CLOSED")&&!on(shared,"SRC_OPENED")&&on(shared,"SRC_END")&&
        !warned(shared,"ENDIF without")&&!warned(shared,"Unclosed IF"),"INCLUDE shares the IF state with the including file");
    write(skin,L"#INFORMATION,0,T,A\n#IF,no-number\n#SRC_BAD_ID\n#ELSE\n#SRC_FALLBACK\n#ENDIF\n#ELSE\n#SRC_STRAY_ELSE\n#ENDIF\n#SETOPTION,x\n#IF,999\n#SRC_OPEN\n");
    auto tolerant=parse(skin);
    check(tolerant&&!find(*tolerant.document,"SRC_BAD_ID").enabled&&find(*tolerant.document,"SRC_FALLBACK").enabled&&
        find(*tolerant.document,"SRC_STRAY_ELSE").enabled&&!find(*tolerant.document,"SRC_OPEN").enabled,"unbalanced control words must not reject the skin");
    check(warned(tolerant,"Invalid condition ID")&&warned(tolerant,"ELSE without")&&warned(tolerant,"Invalid SETOPTION")&&warned(tolerant,"Unclosed IF"),
        "tolerated control errors are diagnosed");
    write(skin,L"#INFORMATION,0,T,A\n#INCLUDE,不存在.csv\n");check(!parse(skin),"missing include accepted");
    Options preview;preview.previewIncludes=true;
    auto missing=parse(skin,preview);
    check(missing&&!missing.diagnostics.empty()&&!missing.diagnostics.front().error,"preview missing include diagnostic");
    write(root/L"wildcard/b.csv",L"#SRC_SECOND\n");
    write(root/L"wildcard/a.csv",L"#SRC_FIRST\n");
    write(skin,L"#INFORMATION,0,T,A\n#INCLUDE,wildcard/*.csv\n");
    auto wildcard=parse(skin,preview);
    check(wildcard&&find(*wildcard.document,"SRC_FIRST").enabled,"preview wildcard order");
    auto strictWildcard=parse(skin);
    check(strictWildcard&&find(*strictWildcard.document,"SRC_FIRST").enabled&&warned(strictWildcard,"INCLUDE wildcard selected: a.csv"),
        "wildcard include outside preview mode");
    write(skin,L"#INFORMATION,0,T,A\n#INCLUDE,wildcard\\B*.CSV\n");
    auto prefixed=parse(skin);check(prefixed&&find(*prefixed.document,"SRC_SECOND").enabled,"wildcard filename prefix and case");
    write(skin,L"#INFORMATION,0,T,A\n#CUSTOMFILE,Parts,.\\wildcard\\*.csv,b\n#INCLUDE,wildcard/*.csv\n");
    auto chosen=parse(skin);check(chosen&&find(*chosen.document,"SRC_SECOND").enabled,"CUSTOMFILE default selects the wildcard include");
    write(skin,L"#INFORMATION,0,T,A\n#CUSTOMFILE,Parts,wildcard/*.csv,absent\n#INCLUDE,wildcard/*.csv\n");
    auto fallback=parse(skin);check(fallback&&find(*fallback.document,"SRC_FIRST").enabled,"missing CUSTOMFILE default falls back to the first match");
    // "pattern|a|b" leaves out names containing a or b.
    write(skin,L"#INFORMATION,0,T,A\n#INCLUDE,wildcard/*.csv|A|\n");
    auto excluded=parse(skin);check(excluded&&find(*excluded.document,"SRC_SECOND").enabled,"wildcard exclusion skips a.csv");
    write(skin,L"#INFORMATION,0,T,A\n#INCLUDE,wildcard/*.csv|a|b\n");check(!parse(skin),"wildcard exclusion of every match is a missing include");
    check(listWildcardChoices(root,skin,"wildcard/*.csv|b")==std::vector<std::string>{"a"},"wildcard choices honour exclusions");
    write(skin,L"#INFORMATION,0,T,A\n#CUSTOMFILE,Parts,wildcard/*.csv|a,b\n#INCLUDE,wildcard/*.csv|a\n");
    auto excludedChoice=parse(skin);check(excludedChoice&&find(*excludedChoice.document,"SRC_SECOND").enabled,"CUSTOMFILE pattern with exclusions matches the include");
    write(skin,L"#INFORMATION,0,T,A\n#FONT,18,3,3,MS Gothic\n#LOADSTART,2200\n#LOADEND,2800\n#PLAYSTART,1000\n#SCRATCHSIDE,1,0\n#SKIP,250\n");
    auto sceneWords=parse(skin);
    check(sceneWords&&!warned(sceneWords,"FONT")&&!warned(sceneWords,"LOADSTART")&&!warned(sceneWords,"LOADEND")&&
        !warned(sceneWords,"PLAYSTART")&&!warned(sceneWords,"SCRATCHSIDE"),"play scene words and FONT are read without warnings");
    check(warned(sceneWords,"preserved for later phases: SKIP"),"SKIP outside the decide screen stays reported");
    write(skin,L"#INFORMATION,0,T,A\n#INCLUDE,*/a.csv\n");check(!parse(skin,preview),"directory wildcard accepted");
    write(skin,L"#INFORMATION,0,T,A\n#INCLUDE,wildcard/*.txt\n");
    check(!parse(skin)&&bool(parse(skin,preview)),"unmatched wildcard is a missing include");
    write(skin,L"#INFORMATION,0,T,A\n#INCLUDE,../*.csv\n");check(!parse(skin,preview),"wildcard escaped the root");
    write(skin,L"#INFORMATION,0,T,A\n#INCLUDE,../absent.csv\n");
    check(!parse(skin,preview),"preview tolerated an out-of-root missing include");
    write(skin,L"#INFORMATION,0,T,A\n#INCLUDE,日本語.csv\n");write(root/L"日本語.csv",L"#SRC_IMAGE,日本語,,\n");
    auto japanese=parse(skin);check(japanese&&find(*japanese.document,"SRC_IMAGE").fields[1]=="日本語","MS932 include path/content");
    write(root.parent_path()/L"outside.csv",L"#SRC_IMAGE,1\n");
    write(skin,L"#INFORMATION,0,T,A\n#INCLUDE,../outside.csv\n");check(!parse(skin),"existing path escape accepted");
    write(skin,L"#INFORMATION,0,T,A\n#INCLUDE,C:\\outside.csv\n");check(!parse(skin),"absolute path accepted");
    write(skin,L"#INFORMATION,0,T,A\n#INCLUDE,cycle.csv\n");write(root/L"cycle.csv",L"#INCLUDE,invalid.lr2skin\n");check(!parse(skin),"include cycle accepted");
    write(skin,L"#INFORMATION,0,T,A\n#INCLUDE,cycle.csv\n");write(root/L"cycle.csv",L"#INCLUDE,third.csv\n");write(root/L"third.csv",L"#SRC_IMAGE,1\n");Options limited;limited.limits.includeDepth=2;check(!parse(skin,limited),"include depth not enforced");
    limited={};limited.limits.fileBytes=4;check(!parse(skin,limited),"file size not enforced");limited={};limited.limits.commands=1;check(!parse(skin,limited),"command limit not enforced");
    {std::ofstream bad(skin,std::ios::binary);bad<<"#INFORMATION,0,T,A\n";bad.put(static_cast<char>(0x81));}check(!parse(skin),"invalid MS932 accepted");
    write(skin,L"#INFORMATION,0,T,A\n#IF,123\n#SRC_DYNAMIC\n#ENDIF\n");Options dynamic;dynamic.condition=[](int id)->std::optional<bool>{return id==123?std::optional<bool>{true}:std::nullopt;};auto t=parse(skin,dynamic);check(t&&find(*t.document,"SRC_DYNAMIC").enabled,"condition provider");
    // User selections: a named option choice, a CUSTOMFILE stem and CUSTOMOFFSET values.
    write(skin,L"#INFORMATION,0,T,A\n#CUSTOMOPTION,配置,900,左,,右\n#CUSTOMFILE,Parts,wildcard/*.csv,a\n#CUSTOMOFFSET,Pos,42,1,1,0,0,0,1\n"
        L"#IF,900\n#SRC_LEFT\n#ENDIF\n#IF,901\n#SRC_RIGHT\n#ENDIF\n#IF,!900\n#SRC_NOT_LEFT\n#ENDIF\n#INCLUDE,wildcard/*.csv\n"
        L"#SRC_IMAGE,0,0,0,0,1,1,1,1,0,0\n#DST_IMAGE,0,0,100,100,10,10,0,255,255,255,255,0,0,0,0,0,0,0,0,0,42,77\n"
        L"#DST_IMAGE,0,1000,200,100,10,10,0,255,255,255,255\n");
    Options chosenOptions;
    chosenOptions.choices.options["配置"]="右";
    chosenOptions.choices.files["Parts"]="b";
    chosenOptions.choices.offsets["Pos"]={10,20,30,40,50,-55};
    chosenOptions.initialOptions[900]=true; // A named user choice outranks the host's value for the same group.
    auto custom=parse(skin,chosenOptions);
    check(custom&&custom.document->header.options[0].selected==1&&find(*custom.document,"SRC_RIGHT").enabled&&
        !find(*custom.document,"SRC_LEFT").enabled&&find(*custom.document,"SRC_NOT_LEFT").enabled,"named CUSTOMOPTION choice");
    check(find(*custom.document,"SRC_SECOND").enabled&&custom.document->header.files[0].selected=="b","CUSTOMFILE selection");
    check(custom.document->offsetValues.at(42)==std::array<int,6>{10,20,0,0,0,-55},"CUSTOMOFFSET keeps only enabled components");
    std::vector<Diagnostic> modelDiagnostics;
    auto shifted=imageModel(*custom.document,&modelDiagnostics);
    check(shifted&&shifted->sources.size()==1&&shifted->sources[0].destinations.size()==2,"offset fixture model");
    const auto& moved=shifted->sources[0].destinations;
    check(moved[0][1]==110&&moved[0][2]==120&&moved[0][3]==10&&moved[0][6]==200&&moved[1][1]==210&&moved[1][2]==120&&moved[1][6]==200,
        "object offset applies to every keyframe");
    check(std::any_of(modelDiagnostics.begin(),modelDiagnostics.end(),[](const auto& d){return d.message=="Unsupported destination offset: 77";}),
        "undeclared offset ID diagnostic");
    Options staleChoices;
    staleChoices.choices.options["配置"]="削除済み";
    staleChoices.choices.files["Parts"]="absent";
    auto stale=parse(skin,staleChoices);
    check(stale&&stale.document->header.options[0].selected==0&&find(*stale.document,"SRC_LEFT").enabled&&
        find(*stale.document,"SRC_FIRST").enabled&&stale.document->offsetValues.at(42)==std::array<int,6>{},
        "stale selections fall back to the skin defaults");
    auto unshifted=imageModel(*stale.document);
    check(unshifted&&unshifted->sources[0].destinations[0][1]==100,"declared offset without a value leaves geometry unchanged");

    const auto store=root/L"lr2-skins.txt";
    std::filesystem::remove(store);
    check(loadSkinChoices(store,skin).options.empty(),"missing choice store");
    SkinChoices saved=chosenOptions.choices;
    saved.options["bad\tname"]="x";
    check(saveSkinChoices(store,skin,saved),"save skin choices");
    SkinChoices other;other.files["背景"]="夜空";
    check(saveSkinChoices(store,main,other),"save a second skin");
    auto upperCase=skin.wstring();for(auto& ch:upperCase)ch=static_cast<wchar_t>(towupper(ch));
    const auto loaded=loadSkinChoices(store,upperCase);
    check(loaded.options==chosenOptions.choices.options&&loaded.files==chosenOptions.choices.files&&
        loaded.offsets==chosenOptions.choices.offsets,"choices round-trip by case-insensitive skin path without unsafe names");
    check(loadSkinChoices(store,main).files.at("背景")=="夜空"&&loadSkinChoices(store,main).options.empty(),"choices are kept per skin");
    check(saveSkinChoices(store,skin,{})&&loadSkinChoices(store,skin).files.empty()&&
        loadSkinChoices(store,main).files.size()==1,"clearing one skin keeps the others");
    {std::ofstream junk(store,std::ios::binary|std::ios::app);junk<<"["<<std::string(config_detail::skinKey(skin))<<"]\noffset\tPos\t1,2,3\noffset\tPos2\t1,2,3,4,5,6,7\nnonsense\noption\tA\tB\n";}
    const auto partial=loadSkinChoices(store,skin);
    check(partial.offsets.empty()&&partial.options.at("A")=="B","malformed rows are skipped");
    // Settings model: header-only reads of any screen type, CUSTOMFILE candidates and a folder scan.
    const auto library=root/L"library";
    std::filesystem::remove_all(library);
    write(library/L"LR2files/Theme/A/select.lr2skin",L"#INFORMATION,5,Beta Select,Author\n#CUSTOMOPTION,Mode,900,One,Two\n#INCLUDE,missing.csv\n");
    write(library/L"LR2files/Theme/A/play9.lr2skin",L"#INFORMATION,4,Alpha Nine,Author\n");
    write(library/L"LR2files/Theme/B/deep/play.LR2SKIN",L"#INFORMATION,0,Alpha Play,Author\n#CUSTOMFILE,Frame,LR2files\\Theme\\B\\frame\\f*.png,one\n");
    write(library/L"LR2files/Theme/B/broken.lr2skin",L"#RESOLUTION,9\n");
    write(library/L"LR2files/Theme/B/notes.txt",L"#INFORMATION,0,Not a skin,Author\n");
    for(const auto* name:{L"fTwo.png",L"fone.PNG",L"other.png",L"f.txt"})write(library/L"LR2files/Theme/B/frame"/name,L"x");
    Options headerOptions;headerOptions.headerOnly=true;headerOptions.choices.options["Mode"]="Two";
    const auto selectFile=library/L"LR2files/Theme/A/select.lr2skin";
    auto headerOnly=parse(selectFile,headerOptions);
    check(headerOnly&&headerOnly.document->header.type==5&&headerOnly.document->commands.empty()&&
        headerOnly.document->header.options[0].selected==1&&!parse(selectFile),"header-only read ignores the screen type and the body");
    const auto skins=findSkins(library);
    check(skins.size()==3&&skins[0].header.name=="Alpha Nine"&&skins[0].header.type==4&&skins[1].header.name=="Alpha Play"&&
        skins[2].header.name=="Beta Select"&&skins[1].header.files.size()==1,"skin scan lists every readable header in name order");
    SkinSearchLimits shallow;shallow.depth=3;
    check(findSkins(library,shallow).size()==2&&findSkins(library/L"absent").empty(),"skin scan depth limit and missing folder");
    const auto playFile=skins[1].file;
    const auto stems=listWildcardChoices(library,playFile,skins[1].header.files[0].pattern);
    check(stems==std::vector<std::string>{"one","Two"},"CUSTOMFILE candidates are stems in case-insensitive order");
    check(listWildcardChoices(library,playFile,"LR2files\\Theme\\B\\absent\\*.png").empty()&&
        listWildcardChoices(library,playFile,"..\\..\\..\\..\\*.png").empty()&&listWildcardChoices(library,playFile,"plain.png").empty(),
        "unusable CUSTOMFILE patterns list nothing");
    // A theme folder copied out of an LR2 install still resolves its "LR2files\Theme\<name>\" paths.
    const auto detached=root/L"detached/MyTheme";
    std::filesystem::remove_all(root/L"detached");
    write(detached/L"Select/select.lr2skin",L"#INFORMATION,5,Detached,Author\n#CUSTOMFILE,Frame,LR2files\\Theme\\LR2\\Select\\frame\\*.png,one\n"
        L"#INCLUDE,LR2files\\Theme\\LR2\\Select\\parts\\select.csv\n#INCLUDE,.\\LR2files\\Theme\\LR2\\Shared\\common.csv\n#INCLUDE,LR2files\\WallPaper\\wall.csv\n");
    write(detached/L"Select/parts/select.csv",L"#SRC_DETACHED_SELECT\n");
    write(detached/L"Shared/common.csv",L"#SRC_DETACHED_SHARED\n");
    write(detached/L"Select/frame/one.png",L"x");
    write(root/L"detached/outside.csv",L"#SRC_OUTSIDE\n");
    Options detachedOptions;detachedOptions.screen=ScreenType::Select;detachedOptions.previewIncludes=true;
    auto loose=parse(detached/L"Select/select.lr2skin",detachedOptions);
    check(loose&&loose.document->root==std::filesystem::canonical(detached)&&find(*loose.document,"SRC_DETACHED_SELECT").enabled&&
        find(*loose.document,"SRC_DETACHED_SHARED").enabled,"detached theme folder resolves LR2files\\Theme paths");
    check(std::any_of(loose.diagnostics.begin(),loose.diagnostics.end(),[](const auto& d){return d.message.find("INCLUDE skipped")!=std::string::npos;}),
        "paths outside the theme, such as WallPaper, stay missing");
    check(listWildcardChoices(loose.document->root,detached/L"Select/select.lr2skin",loose.document->header.files[0].pattern)==std::vector<std::string>{"one"},
        "detached theme CUSTOMFILE candidates");
    write(detached/L"Select/escape.lr2skin",L"#INFORMATION,5,Escape,Author\n#INCLUDE,LR2files\\Theme\\LR2\\Select\\parts\\select.csv\n#INCLUDE,LR2files\\Theme\\LR2\\..\\outside.csv\n");
    check(!parse(detached/L"Select/escape.lr2skin",detachedOptions),"detached theme root must still contain every include");
    std::cout<<"LR2 Phase 1 parser tests passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
