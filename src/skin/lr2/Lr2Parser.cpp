#include "Lr2ImageModel.h"
#include <windows.h>
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace sevenmania::skin::lr2 {
namespace {
struct Failure : std::runtime_error { using std::runtime_error::runtime_error; };
struct MissingAsset : Failure { using Failure::Failure; };
constexpr std::array<std::pair<unsigned,unsigned>,4> canvasSizes{{{640,480},{1280,720},{1920,1080},{3840,2160}}};
std::string utf8(std::wstring_view value) {
    if(value.empty())return {};
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    if(!n)throw Failure("Invalid Unicode string");
    std::string out(static_cast<std::size_t>(n),'\0');
    if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),n,nullptr,nullptr))throw Failure("UTF-8 conversion failed");
    return out;
}
std::wstring wide(std::string_view value) {
    if(value.empty())return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
    if(!n)throw Failure("Invalid UTF-8 path");
    std::wstring out(static_cast<std::size_t>(n),L'\0');
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),n))throw Failure("Path conversion failed");
    return out;
}
std::string decode(std::string_view bytes) {
    if(bytes.empty())return {};
    if(bytes.find('\0')!=bytes.npos)throw Failure("NUL in LR2 input");
    int n=MultiByteToWideChar(932,MB_ERR_INVALID_CHARS,bytes.data(),static_cast<int>(bytes.size()),nullptr,0);
    if(!n)throw Failure("Invalid MS932 input");
    std::wstring w(static_cast<std::size_t>(n),L'\0');
    if(!MultiByteToWideChar(932,MB_ERR_INVALID_CHARS,bytes.data(),static_cast<int>(bytes.size()),w.data(),n))throw Failure("MS932 conversion failed");
    return utf8(w);
}
std::string upper(std::string value) {for(char& c:value)if(c>='a'&&c<='z')c=static_cast<char>(c-'a'+'A');return value;}
int number(std::string_view s) {
    if(!s.empty()&&s.front()=='+')s.remove_prefix(1);
    int value{};const auto [end,ec]=std::from_chars(s.data(),s.data()+s.size(),value);
    if(s.empty()||ec!=std::errc{}||end!=s.data()+s.size())throw Failure("Invalid or out-of-range integer");
    return value;
}
std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> fields;std::size_t start=0;
    for(;;){const auto at=line.find(',',start);fields.push_back(line.substr(start,at-start));if(at==line.npos)break;start=at+1;}
    return fields;
}
bool inside(const std::filesystem::path& root,const std::filesystem::path& path) {
    auto r=root.begin(),p=path.begin();
    for(;r!=root.end();++r,++p)if(p==path.end()||*r!=*p)return false;
    return true;
}
bool componentEqual(const std::filesystem::path& p,const wchar_t* name){return _wcsicmp(p.c_str(),name)==0;}
std::filesystem::path canonicalAssetPath(const std::filesystem::path& root,
                                         const std::filesystem::path& declaringFile,
                                         const std::string& value,bool directory=false) {
    auto name=wide(value);
    std::replace(name.begin(),name.end(),L'\\',L'/');
    if(name.empty()||name.find_first_of(L":*?")!=name.npos||name.starts_with(L'/'))
        throw Failure("Expected a relative literal path");
    const auto relative=std::filesystem::path(name).lexically_normal();
    auto it=relative.begin();
    auto candidate=declaringFile.parent_path()/relative;
    if(it!=relative.end()&&componentEqual(*it,L"LR2files")) {
        std::error_code probe;
        const bool installed=componentEqual(root.filename(),L"LR2files");
        auto next=std::next(it);
        if(!installed&&!std::filesystem::is_directory(root/L"LR2files",probe)&&
           next!=relative.end()&&componentEqual(*next,L"Theme")&&std::next(next)!=relative.end()) {
            // A theme copied out of its LR2 install: the root is the theme folder
            // itself, standing in for "LR2files\Theme\<name>".
            candidate=root;
            it=std::next(next);
        } else candidate=installed?root:root/L"LR2files";
        for(++it;it!=relative.end();++it)candidate/=*it;
    }
    std::error_code ec;
    const auto canonical=std::filesystem::canonical(candidate,ec);
    if(ec==std::errc::no_such_file_or_directory) {
        const auto parent=std::filesystem::weakly_canonical(candidate,ec);
        if(ec||root.empty()||!inside(root,parent))throw Failure("Path is outside permitted root");
        throw MissingAsset("File or directory is missing");
    }
    if(ec)throw Failure("File or directory is inaccessible");
    if(root.empty()||!inside(root,canonical))throw Failure("Path is outside permitted root");
    if(!(directory?std::filesystem::is_directory(canonical,ec):std::filesystem::is_regular_file(canonical,ec)))
        throw Failure(directory?"Expected a directory":"Expected a regular file");
    return canonical;
}
struct WildcardParts { std::string directoryName; std::wstring prefix, suffix; std::vector<std::wstring> excluded; };
// "pattern|a|b": the text before the first '|' is the pattern; each later non-empty part
// excludes the files whose name contains it (case-insensitive).
std::string wildcardPattern(const std::string& value) {return value.substr(0,value.find('|'));}
WildcardParts wildcardParts(const std::string& full) {
    const auto value=wildcardPattern(full);
    const auto star=value.find('*');
    const auto separator=value.find_last_of("/\\");
    if(star==value.npos||value.find('*',star+1)!=value.npos||(separator!=value.npos&&star<separator))
        throw Failure("Unsupported wildcard pattern");
    WildcardParts parts;
    parts.directoryName=separator==value.npos?std::string{}:value.substr(0,separator+1);
    const auto filename=wide(value.substr(parts.directoryName.size()));
    const auto at=filename.find(L'*');
    parts.prefix=filename.substr(0,at);
    parts.suffix=filename.substr(at+1);
    for(auto bar=full.find('|');bar!=full.npos;) {
        const auto next=full.find('|',bar+1);
        auto part=wide(full.substr(bar+1,next==full.npos?full.npos:next-bar-1));
        if(!part.empty()) {
            for(auto& ch:part)ch=static_cast<wchar_t>(towlower(ch));
            parts.excluded.push_back(std::move(part));
        }
        bar=next;
    }
    return parts;
}
// Matching regular files in case-insensitive name order.
std::vector<std::wstring> wildcardMatches(const std::filesystem::path& root,const std::filesystem::path& declaringFile,
                                          const WildcardParts& parts) {
    const auto directory=canonicalAssetPath(root,declaringFile,parts.directoryName.empty()?".":parts.directoryName,true);
    std::vector<std::wstring> matches;
    std::size_t entries=0;
    std::error_code ec;
    for(const auto& entry:std::filesystem::directory_iterator(directory,ec)) {
        if(++entries>Limits{}.commands)throw Failure("Wildcard entry limit exceeded");
        const auto name=entry.path().filename().wstring();
        if(name.size()<parts.prefix.size()+parts.suffix.size()||_wcsnicmp(name.c_str(),parts.prefix.c_str(),parts.prefix.size())||
           _wcsicmp(name.c_str()+name.size()-parts.suffix.size(),parts.suffix.c_str())||!entry.is_regular_file(ec))continue;
        auto lower=name;
        for(auto& ch:lower)ch=static_cast<wchar_t>(towlower(ch));
        if(std::any_of(parts.excluded.begin(),parts.excluded.end(),
            [&](const std::wstring& part){return lower.find(part)!=lower.npos;}))continue;
        matches.push_back(name);
    }
    std::sort(matches.begin(),matches.end(),[](const std::wstring& a,const std::wstring& b){return _wcsicmp(a.c_str(),b.c_str())<0;});
    return matches;
}
std::filesystem::path wildcardAssetPath(const std::filesystem::path& root,
                                        const std::filesystem::path& declaringFile,
                                        const std::string& full,const std::vector<CustomFile>& files) {
    const auto value=wildcardPattern(full);
    const auto star=value.find('*');
    if(star==value.npos)return canonicalAssetPath(root,declaringFile,value);
    const auto parts=wildcardParts(full);
    const auto normalized=[](std::string text) {
        std::replace(text.begin(),text.end(),'\\','/');
        return std::filesystem::path(wide(text)).lexically_normal();
    };
    const auto pattern=normalized(value);
    for(const auto& file:files) {
        // A CUSTOMFILE with the same pattern names the file: the user's selection, then the skin's default.
        std::filesystem::path candidate;
        try{candidate=normalized(wildcardPattern(file.pattern));}catch(const Failure&){continue;}
        if(_wcsicmp(pattern.c_str(),candidate.c_str())!=0)continue;
        for(const auto* stem:{&file.selected,&file.defaultName}) {
            if(stem->empty()||stem->find_first_of("/\\:*?|")!=stem->npos)continue;
            auto chosen=value;
            chosen.replace(star,1,*stem);
            try{return canonicalAssetPath(root,declaringFile,chosen);}catch(const MissingAsset&){}
        }
        break;
    }
    // LR2 picks a matching file at random. The first name in case-insensitive order keeps loads repeatable.
    const auto matches=wildcardMatches(root,declaringFile,parts);
    if(matches.empty())throw MissingAsset("Wildcard has no matching file");
    return canonicalAssetPath(root,declaringFile,parts.directoryName+utf8(matches.front()));
}
// For a skin with no LR2files ancestor, finds the folder its "LR2files\Theme\<name>\..."
// paths are relative to: the nearest ancestor under which one of those paths exists.
std::filesystem::path detachedThemeFolder(const std::filesystem::path& skin,const std::vector<Command>& lines) {
    constexpr std::string_view prefix="LR2FILES/THEME/";
    for(const auto& command:lines) {
        for(std::size_t i=1;i<command.fields.size();++i) {
            auto text=command.fields[i];
            std::replace(text.begin(),text.end(),'\\','/');
            if(text.starts_with("./"))text.erase(0,2);
            if(text.size()<=prefix.size()||upper(text.substr(0,prefix.size()))!=prefix)continue;
            const auto name=text.find('/',prefix.size());
            if(name==text.npos)continue;
            auto rest=text.substr(name+1);
            // A wildcard names a file, so only its directory can be probed.
            const bool pattern=rest.find('*')!=rest.npos;
            if(pattern) {
                const auto slash=rest.find_last_of('/');
                if(slash==rest.npos)continue;
                rest.resize(slash);
            }
            if(rest.empty()||rest.find_first_of("*?:")!=rest.npos)continue;
            std::filesystem::path relative;
            try{relative=std::filesystem::path(wide(rest)).lexically_normal();}catch(const Failure&){continue;}
            if(relative.empty()||*relative.begin()==L"..")continue;
            auto folder=skin.parent_path();
            for(int depth=0;depth<4&&!folder.empty();++depth,folder=folder.parent_path()) {
                std::error_code ec;
                if(pattern?std::filesystem::is_directory(folder/relative,ec):std::filesystem::is_regular_file(folder/relative,ec))return folder;
                if(folder==folder.root_path())break;
            }
        }
    }
    return {};
}
// Scene words the loader reads in PreviewScene::load: play loading and start timing,
// the play scratch side, the decide screen's skip time and the select / play input lock.
bool sceneWord(int type,const std::string& name) {
    if(isPlayScreen(type))return name=="LOADSTART"||name=="LOADEND"||name=="PLAYSTART"||name=="SCRATCHSIDE"||name=="STARTINPUT";
    if(type==static_cast<int>(ScreenType::Select))return name=="STARTINPUT";
    return type==static_cast<int>(ScreenType::Decide)&&name=="SKIP";
}
class Parser {
    Options options_;Document document_;std::filesystem::path root_;
    std::map<int,bool> values_;std::set<int> unknown_;std::set<std::string> unknownWords_;
    std::vector<std::filesystem::path> stack_;std::size_t bytes_{};
    struct Frame { bool matched{}, skip{}, sawElse{}; };
    struct Branch {
        std::vector<Frame> open;
        bool skip() const noexcept {return !open.empty()&&open.back().skip;}
    };
    // Shared by the main file and every INCLUDE, as in LR2: an included CSV sees the
    // enclosing IF and may close or open blocks for the file that included it.
    Branch branch_;
    bool collectWarnings_{};
    void fail(const std::filesystem::path& file,std::size_t line,const std::string& message) {
        document_.diagnostics.push_back({file,line,message,true});throw Failure(message);
    }
    void warning(const Command& c,const std::string& message){document_.diagnostics.push_back({c.source,c.line,message,false});}
    bool resolutionDeclared_{};
    void readResolution(const Command& c) {
        try {
            if(c.fields.size()<2)throw Failure("RESOLUTION needs index");
            const int index=number(c.fields[1]);
            if(index<0||static_cast<std::size_t>(index)>=canvasSizes.size())throw Failure("Unsupported resolution index");
            const auto [width,height]=canvasSizes[index];
            document_.header.width=width;
            document_.header.height=height;
            resolutionDeclared_=true;
        } catch(const Failure& e){fail(c.source,c.line,e.what());}
    }
    void inferResolution() {
        if(resolutionDeclared_)return;
        const Command* evidence=nullptr;
        std::pair<unsigned,unsigned> inferred{};
        for(const auto& command:document_.commands) {
            if(!command.enabled||command.name!="DST_IMAGE"||command.fields.size()<7)continue;
            std::array<int,4> rect{};
            bool valid=true;
            for(std::size_t i=0;i<rect.size();++i) {
                const auto& field=command.fields[i+3];
                const auto [end,error]=std::from_chars(field.data(),field.data()+field.size(),rect[i]);
                valid=valid&&error==std::errc{}&&end==field.data()+field.size();
            }
            if(!valid||rect[0]!=0||rect[1]!=0||rect[2]<=0||rect[3]<=0)continue;
            const auto size=std::pair{unsigned(rect[2]),unsigned(rect[3])};
            if(std::find(canvasSizes.begin(),canvasSizes.end(),size)==canvasSizes.end())continue;
            if(evidence&&inferred!=size)return;
            evidence=&command;
            inferred=size;
        }
        if(evidence&&inferred!=canvasSizes.front()) {
            document_.header.width=inferred.first;
            document_.header.height=inferred.second;
            warning(*evidence,"Inferred LR2 canvas: "+std::to_string(inferred.first)+"x"+std::to_string(inferred.second));
        }
    }
    std::vector<Command> read(const std::filesystem::path& path) {
        std::error_code ec;const auto size=std::filesystem::file_size(path,ec);
        if(ec||size>static_cast<std::uintmax_t>(std::numeric_limits<int>::max())||size>options_.limits.fileBytes||size>options_.limits.totalBytes||bytes_>options_.limits.totalBytes-size)fail(path,0,"Cannot read file or input size limit exceeded");
        std::ifstream in(path,std::ios::binary);if(!in)fail(path,0,"Cannot open file");
        std::string bytes(static_cast<std::size_t>(size),'\0');
        if(size&&!in.read(bytes.data(),static_cast<std::streamsize>(size)))fail(path,0,"Cannot read complete file");
        bytes_+=static_cast<std::size_t>(size);
        std::string text;try{text=decode(bytes);}catch(const Failure& e){fail(path,0,e.what());}
        std::vector<Command> lines;std::size_t start=0,line=1;
        while(start<text.size()) {
            auto end=text.find_first_of("\r\n",start);if(end==text.npos)end=text.size();
            auto row=text.substr(start,end-start);
            if(!row.empty()&&row.front()=='#'){
                if(lines.size()>=options_.limits.commands)fail(path,line,"Command count limit exceeded");
                if(row.size()>65536||std::count(row.begin(),row.end(),',')>4095)fail(path,line,"Command length or field count limit exceeded");
                auto fields=split(row);lines.push_back({path,line,upper(fields.front().substr(1)),std::move(fields),false});
            }
            start=end+1;if(end<text.size()&&text[end]=='\r'&&start<text.size()&&text[start]=='\n')++start;++line;
        }
        return lines;
    }
    bool evaluate(const Command& c) {
        for(std::size_t i=1;i<c.fields.size();++i){
            if(c.fields[i].empty())continue;
            std::string filtered;for(char ch:c.fields[i]){if(ch=='!')filtered+='-';else if((ch>='0'&&ch<='9')||ch=='-')filtered+=ch;}
            // LR2 treats an unreadable condition as false instead of rejecting the skin.
            int id=std::numeric_limits<int>::min();try{id=number(filtered);}catch(const Failure&){}
            if(id==std::numeric_limits<int>::min()){if(collectWarnings_)warning(c,"Invalid condition ID treated as false");return false;}
            const auto found=values_.find(id<0?-id:id);
            bool enabled=false;
            if(found!=values_.end())enabled=id<0?!found->second:found->second;
            // 900-999 belong to the skin (#CUSTOMOPTION, #SETOPTION). One that is never set is false like any
            // unknown ID (also when negated), but it is the skin's own and not a gap in the player: no warning.
            else if((id<0?-id:id)>=900&&(id<0?-id:id)<=999)enabled=false;
            else if(options_.condition){auto v=options_.condition(id);enabled=v.value_or(false);if(!v && collectWarnings_ && unknown_.insert(id).second)warning(c,"Unknown condition ID: "+std::to_string(id));}
            else if(collectWarnings_ && unknown_.insert(id).second)warning(c,"Unknown condition ID: "+std::to_string(id));
            if(!enabled)return false;
        }
        return true;
    }
    // IF blocks nest. A block inside a skipped branch is skipped whole and does not evaluate its
    // conditions. Unbalanced control words adjust the state with a warning instead of stopping the load.
    void control(const Command& c,Branch& branch) {
        const auto note=[&](const char* message){if(collectWarnings_)warning(c,message);};
        auto& open=branch.open;
        const auto outerSkip=[&]{return open.size()>1&&open[open.size()-2].skip;};
        const auto take=[&](Frame& frame) {
            frame.matched=evaluate(c);frame.skip=!frame.matched;
        };
        if(c.name=="IF"){
            const bool parentSkip=branch.skip();
            open.push_back({});
            if(parentSkip){open.back().matched=true;open.back().skip=true;}
            else take(open.back());
        } else if(c.name=="ELSEIF") {
            if(open.empty()||open.back().sawElse){note("ELSEIF without an open IF");if(open.empty())open.push_back({});}
            auto& frame=open.back();
            if(outerSkip()||frame.matched)frame.skip=true;else take(frame);
        } else if(c.name=="ELSE") {
            if(open.empty()||open.back().sawElse){note("ELSE without an open IF");if(open.empty())open.push_back({});}
            auto& frame=open.back();
            frame.sawElse=true;frame.skip=outerSkip()||frame.matched;
        } else if(c.name=="ENDIF") {
            if(open.empty())note("ENDIF without an open IF");else open.pop_back();
        } else if(c.name=="SETOPTION"&&!branch.skip()) {
            try {
                if(c.fields.size()<3)throw Failure("needs ID and value");
                values_[number(c.fields[1])]=number(c.fields[2])>=1;
            } catch(const Failure&){note("Invalid SETOPTION ignored");}
        }
    }
    void header(const Command& c) {
        auto& h=document_.header;const auto& f=c.fields;
        try {
            if(c.name=="INFORMATION") {if(f.size()<4)throw Failure("INFORMATION needs type, name and author");h.type=number(f[1]);h.name=f[2];h.author=f[3];}
            else if(c.name=="CUSTOMOPTION") {if(f.size()<3)throw Failure("CUSTOMOPTION needs name and ID");CustomOption o{f[1],number(f[2]),{}};for(std::size_t i=3;i<f.size();++i)if(!f[i].empty())o.choices.push_back(f[i]);if(o.firstId<0||o.choices.size()>static_cast<std::size_t>(std::numeric_limits<int>::max()-o.firstId))throw Failure("CUSTOMOPTION ID range invalid");h.options.push_back(std::move(o));}
            else if(c.name=="CUSTOMFILE") {if(f.size()<3)throw Failure("CUSTOMFILE needs name and pattern");h.files.push_back({f[1],f[2],f.size()>3?f[3]:std::string{}});}
            else if(c.name=="CUSTOMOFFSET") {if(f.size()<3)throw Failure("CUSTOMOFFSET needs name and ID");CustomOffset o;o.name=f[1];o.id=number(f[2]);for(std::size_t i=0;i<6&&i+3<f.size();++i)o.enabled[i]=number(f[i+3])>0;h.offsets.push_back(std::move(o));}
        } catch(const Failure& e){fail(c.source,c.line,e.what());}
    }
    std::filesystem::path includePath(const Command& c) {
        if(c.fields.size()<2||c.fields[1].empty())fail(c.source,c.line,"INCLUDE needs a path");
        const auto& name=c.fields[1];
        try {
            if(name.find_first_of("*|")==name.npos)return resolveAssetPath(root_,c.source,name);
            if(wildcardPattern(name).find('*')==name.npos)return wildcardAssetPath(root_,c.source,name,document_.header.files);
            const auto path=wildcardAssetPath(root_,c.source,name,document_.header.files);
            warning(c,"INCLUDE wildcard selected: "+utf8(path.filename().wstring()));
            return path;
        }
        catch(const MissingAsset& e) {
            if(!options_.previewIncludes&&stack_.size()==1)
                fail(c.source,c.line,std::string("INCLUDE: ")+e.what()+": "+name);
            warning(c,std::string("INCLUDE skipped (")+e.what()+"): "+name);
            return {};
        }
        catch(const Failure& e){fail(c.source,c.line,std::string("INCLUDE: ")+e.what());}
    }
    void body(const std::vector<Command>& lines,const std::filesystem::path& file) {
        // INCLUDE enters only from an active branch and shares its condition state.
        auto& branch=branch_;
        for(auto c:lines){
            const bool isControl=c.name=="IF"||c.name=="ELSEIF"||c.name=="ELSE"||c.name=="ENDIF";
            const std::string_view name=c.name;
            const bool isLayer=(name.starts_with("SRC_")||name.starts_with("DST_"))&&
                layerKind(name.substr(4),true,true,document_.header.type==static_cast<int>(ScreenType::Select)).has_value();
            // Screen timing the host uses: result and decide #STARTINPUT/#SCENETIME/#FADEOUT, select #FADEOUT.
            const int screenType=document_.header.type;
            const bool resultTiming=((screenType==static_cast<int>(ScreenType::Result)||screenType==static_cast<int>(ScreenType::Decide))&&
                (name=="STARTINPUT"||name=="SCENETIME"||name=="FADEOUT"))||(screenType==static_cast<int>(ScreenType::Select)&&name=="FADEOUT");
            control(c,branch);c.enabled=isControl||!branch.skip();
            if(document_.commands.size()>=options_.limits.commands)fail(c.source,c.line,"Command count limit exceeded");
            document_.commands.push_back(c);
            if(c.enabled&&c.name=="RESOLUTION")readResolution(c);
            if(c.name=="INCLUDE"&&c.enabled){
                auto path=includePath(c);
                if(path.empty())continue;
                if(stack_.size()>=options_.limits.includeDepth)fail(c.source,c.line,"INCLUDE depth limit exceeded");
                if(std::any_of(stack_.begin(),stack_.end(),[&](const auto& p){return _wcsicmp(p.c_str(),path.c_str())==0;}))fail(c.source,c.line,"INCLUDE cycle detected");
                stack_.push_back(path);auto nested=read(path);body(nested,path);stack_.pop_back();
            } else if(c.enabled&&!isControl&&!isLayer&&!resultTiming&&c.name!="SETOPTION"&&c.name!="INCLUDE"&&c.name!="INFORMATION"&&c.name!="RESOLUTION"&&c.name!="CUSTOMOPTION"&&c.name!="CUSTOMFILE"&&c.name!="CUSTOMOFFSET"&&c.name!="IMAGE"&&c.name!="LR2FONT"&&c.name!="FONT"&&c.name!="ENDOFHEADER"&&!sceneWord(document_.header.type,c.name)&&!(document_.header.type==static_cast<int>(ScreenType::Select)&&selectBarWord(c.name))&&!(playLaneCount(document_.header.type)&&(c.name=="FADEOUT"||c.name=="CLOSE"))&&c.name!="TRANSCOLOR"&&!playNotePart(c.name)&&c.name!="DST_NOTE") {
                if(unknownWords_.insert(c.name).second)warning(c,"Command preserved for later phases: "+c.name);
            }
        }
        if(stack_.size()==1&&!branch.open.empty())document_.diagnostics.push_back({file,0,"Unclosed IF",false});
    }
public:
    explicit Parser(Options options):options_(std::move(options)),values_(options_.initialOptions){}
    Result run(const std::filesystem::path& input) {
        try {
            std::error_code ec;auto path=std::filesystem::canonical(input,ec);if(ec)fail(input,0,"Input file not found");
            if(!componentEqual(path.extension(),L".lr2skin"))fail(path,0,"Expected .lr2skin input");
            if(!options_.root.empty()){root_=std::filesystem::canonical(options_.root,ec);if(ec||!inside(root_,path))fail(path,0,"Input is outside permitted root");}
            else {root_=path.parent_path();for(auto p=root_;!p.empty();p=p.parent_path()){if(componentEqual(p.filename(),L"LR2files")){root_=p;break;}if(p==p.root_path())break;}}
            stack_.push_back(path);const auto lines=read(path);
            if(options_.root.empty()&&!componentEqual(root_.filename(),L"LR2files"))
                if(const auto theme=detachedThemeFolder(path,lines);!theme.empty())root_=theme;
            // Header discovery is a separate pass: INCLUDE contents are body data.
            Branch headerBranch;
            for(const auto& c:lines) {
                control(c,headerBranch);
                if(!headerBranch.skip()) {
                    if(c.name=="RESOLUTION") {
                        if(options_.headerOnly)readResolution(c);
                    } else header(c);
                }
            }
            if(!options_.headerOnly&&document_.header.type!=static_cast<int>(options_.screen))
                fail(path,0,"Unexpected INFORMATION screen type; expected "+std::to_string(static_cast<int>(options_.screen)));
            values_=options_.initialOptions;
            // The CSV loader publishes the selected option, not an assumed
            // false value for every unselected option. Explicit caller values
            // remain authoritative, including explicit false values.
            for(auto& o:document_.header.options){
                const auto choice=options_.choices.options.find(o.name);
                const auto named=choice==options_.choices.options.end()?o.choices.end():std::find(o.choices.begin(),o.choices.end(),choice->second);
                if(named!=o.choices.end()) {
                    // A user choice sets every ID of the group, so the skin's other branches are known false.
                    o.selected=static_cast<int>(named-o.choices.begin());
                    for(std::size_t i=0;i<o.choices.size();++i)values_[o.firstId+static_cast<int>(i)]=static_cast<int>(i)==o.selected;
                    continue;
                }
                bool selected=false;
                for(std::size_t i=0;i<o.choices.size();++i){auto it=values_.find(o.firstId+static_cast<int>(i));if(it!=values_.end()&&it->second&&!selected){selected=true;o.selected=static_cast<int>(i);}}
                if(selected||o.choices.empty())continue;
                // Nothing chosen: LR2 shows the first choice, so the group's other IDs are false unless a caller set them.
                values_.try_emplace(o.firstId,true);
                for(std::size_t i=1;i<o.choices.size();++i)values_.try_emplace(o.firstId+static_cast<int>(i),false);
            }
            for(auto& file:document_.header.files)
                if(const auto choice=options_.choices.files.find(file.name);choice!=options_.choices.files.end())file.selected=choice->second;
            for(auto& offset:document_.header.offsets) {
                if(const auto choice=options_.choices.offsets.find(offset.name);choice!=options_.choices.offsets.end())
                    for(std::size_t i=0;i<offset.values.size();++i)offset.values[i]=offset.enabled[i]?choice->second[i]:0;
                auto& total=document_.offsetValues[offset.id];
                for(std::size_t i=0;i<total.size();++i)total[i]+=offset.values[i];
            }
            collectWarnings_=true;
            if(!options_.headerOnly) {
                body(lines,path);
                inferResolution();
            }
            document_.root=root_;
            document_.optionValues=std::move(values_);
            Result out;out.diagnostics=document_.diagnostics;out.document=std::move(document_);return out;
        } catch(const std::exception& e) {
            if(document_.diagnostics.empty()||!document_.diagnostics.back().error)document_.diagnostics.push_back({input,0,e.what(),true});
        } catch(...) {document_.diagnostics.push_back({input,0,"Unknown parser failure",true});}
        return {std::nullopt,std::move(document_.diagnostics)};
    }
};
}
std::filesystem::path resolveAssetPath(const std::filesystem::path& root,
                                       const std::filesystem::path& declaringFile,
                                       const std::string& value) {
    return canonicalAssetPath(root,declaringFile,value);
}
std::filesystem::path resolveWildcardAsset(const std::filesystem::path& root,
                                           const std::filesystem::path& declaringFile,
                                           const std::string& value,const std::vector<CustomFile>& files) {
    return wildcardAssetPath(root,declaringFile,value,files);
}
std::vector<std::string> listWildcardChoices(const std::filesystem::path& root,
                                             const std::filesystem::path& declaringFile,
                                             const std::string& pattern) {
    std::vector<std::string> stems;
    try {
        const auto parts=wildcardParts(pattern);
        // Containment compares path components, so the root must be in canonical form.
        for(const auto& name:wildcardMatches(std::filesystem::canonical(root),declaringFile,parts))
            stems.push_back(utf8(std::wstring_view(name).substr(parts.prefix.size(),name.size()-parts.prefix.size()-parts.suffix.size())));
    } catch(const std::exception&) {
        stems.clear();
    }
    return stems;
}
std::filesystem::path resolveAssetDirectory(const std::filesystem::path& root,
                                            const std::filesystem::path& declaringFile,
                                            const std::string& value) {
    return canonicalAssetPath(root,declaringFile,value,true);
}
Result parse(const std::filesystem::path& file,const Options& options) noexcept {
    try{return Parser(options).run(file);}catch(...){return {};}
}
}
