#include "SkinInternal.h"
#include "SkinEntrance.h"
#include "FontPolicy.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace sevenmania::skin {
using namespace detail;
namespace {
using J=bms::JsonValue;using T=J::Type;
void object(const J& j,const std::string& path) {if(j.type!=T::Object)fail(ErrorCode::InvalidModel,path,"Expected JSON object");}
void fields(const J& j,const std::string& path,std::initializer_list<const wchar_t*> allowed) {
    object(j,path);
    for(const auto& [key,value]:j.object) {
        (void)value;
        if(std::none_of(allowed.begin(),allowed.end(),[&](const auto* name){return key==name;})) fail(ErrorCode::InvalidModel,path,"Unknown field: "+utf8(key));
    }
}
std::string string(const J& j,const std::string& path,std::size_t max=256,bool empty=false) {
    if(j.type!=T::String || j.string.size()>max || (!empty&&j.string.empty()) || j.string.find(L'\0')!=std::wstring::npos)
        fail(ErrorCode::InvalidModel,path,"Expected bounded string");
    return utf8(j.string);
}
double number(const J& j,const std::string& path,double low,double high) {
    if(j.type!=T::Number || !std::isfinite(j.number)||j.number<low||j.number>high)fail(ErrorCode::InvalidModel,path,"Number outside supported range");
    return j.number;
}
std::uint32_t integer(const J& j,const std::string& path,std::uint32_t low,std::uint32_t high) {
    const auto n=number(j,path,low,high);
    if(n!=std::floor(n))fail(ErrorCode::InvalidModel,path,"Expected integer");
    return static_cast<std::uint32_t>(n);
}
Resolution resolution(const J& j,const std::string& path) {
    fields(j,path,{L"width",L"height"});
    return {integer(j[L"width"],path,1,16384),integer(j[L"height"],path,1,16384)};
}
ScreenKind screen(const std::string& name,const std::string& path) {
    if(name=="settingssummary")return ScreenKind::SettingsSummary;
    if(name=="songintro")return ScreenKind::SongIntro;
    if(name=="songinformation")return ScreenKind::SongInformation;
    if(name=="select")return ScreenKind::Select;if(name=="play")return ScreenKind::Play;if(name=="result")return ScreenKind::Result;
    fail(ErrorCode::InvalidScreen,path,"Unknown screen name");
}
void identifier(const std::string& text,const std::string& path) {
    if(text.empty()||text.size()>96||text.front()=='.' || std::any_of(text.begin(),text.end(),[](unsigned char c){return !((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-');}))
        fail(ErrorCode::InvalidModel,path,"ID must use 1..96 lowercase ASCII letters, digits, '.', '_' or '-'");
}
bool version(const std::string& text) {
    if(text.empty()||text.size()>64)return false;
    std::size_t pos=0;
    for(int part=0;part<3;++part) {
        const auto start=pos;while(pos<text.size()&&text[pos]>='0'&&text[pos]<='9')++pos;
        if(pos==start || (pos-start>1&&text[start]=='0'))return false;
        if(part<2) {if(pos==text.size()||text[pos++]!='.')return false;}
    }
    if(pos==text.size())return true;
    const auto identifiers=[&](bool prerelease) {
        for(;;) {
            const auto start=pos;bool numeric=true;
            while(pos<text.size()) {
                const auto c=text[pos];
                if(!((c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='-'))break;
                numeric&=c>='0'&&c<='9';++pos;
            }
            if(pos==start || (prerelease&&numeric&&pos-start>1&&text[start]=='0'))return false;
            if(pos==text.size()||text[pos]!='.')return true;
            ++pos;
        }
    };
    if(text[pos]=='-'){++pos;if(!identifiers(true))return false;}
    if(pos<text.size()&&text[pos]=='+'){++pos;if(!identifiers(false))return false;}
    return pos==text.size();
}
bool resourcePath(const std::string& path) {
    (void)safePath(path);const auto dot=path.find_last_of('.');const auto ext=dot==std::string::npos?"":path.substr(dot);
    return ((path.starts_with("assets/images/")||path.starts_with("assets/sprites/"))&&(ext==".png"||ext==".jpg"||ext==".jpeg")) ||
        (path.starts_with("assets/fonts/")&&(ext==".ttf"||ext==".otf")) || (path.starts_with("animations/")&&ext==".json");
}
SkinManifest manifest(const J& root) {
    const std::string path="manifest.json";
    fields(root,path,{L"format",L"formatVersion",L"id",L"name",L"author",L"skinVersion",L"description",L"resolution",L"supportedScreens",L"resourcePaths"});
    SkinManifest m;
    m.format=string(root[L"format"],path);m.formatVersion=integer(root[L"formatVersion"],path,0,0xffffffffu);
    if(m.format!="SevenMania.Skin"||m.formatVersion!=1)fail(ErrorCode::UnsupportedVersion,path,"Expected SevenMania.Skin formatVersion 1");
    m.id=string(root[L"id"],path,96);m.name=string(root[L"name"],path);m.author=string(root[L"author"],path);
    m.skinVersion=string(root[L"skinVersion"],path,64);m.description=string(root[L"description"],path,4096,true);
    if(root.object.contains(L"resolution"))m.resolution=resolution(root[L"resolution"],path);
    const auto& screens=root[L"supportedScreens"];
    if(screens.type!=T::Array||screens.array.empty()||screens.array.size()>6)fail(ErrorCode::InvalidManifest,path,"supportedScreens must list 1..6 screens");
    for(const auto& value:screens.array)m.supportedScreens.push_back(screen(string(value,path),path));
    object(root[L"resourcePaths"],path);
    for(const auto& [key,value]:root[L"resourcePaths"].object) {
        ResourcePath item;
        if(value.type==T::String)item.path=string(value,path,240);
        else {
            fields(value,path,{L"path",L"required"});item.path=string(value[L"path"],path,240);
            if(value.object.contains(L"required")) {
                if(value[L"required"].type!=T::Boolean)fail(ErrorCode::InvalidManifest,path,"required must be boolean");
                item.required=value[L"required"].boolean;
            }
        }
        m.resourcePaths.emplace(utf8(key),std::move(item));
    }
    return m;
}
SkinRect rectangle(const J& j,const std::string& path) {
    fields(j,path,{L"x",L"y",L"width",L"height"});
    return {number(j[L"x"],path,-1000000,1000000),number(j[L"y"],path,-1000000,1000000),
        number(j[L"width"],path,0.001,1000000),number(j[L"height"],path,0.001,1000000)};
}
SkinScreen layout(const J& root,ScreenKind kind,const Resolution& base,const std::string& path,const Limits& limits) {
    fields(root,path,{L"screen",L"resolution",L"elements",L"extensions"});
    if(screen(string(root[L"screen"],path),path)!=kind)fail(ErrorCode::InvalidScreen,path,"Screen field does not match filename");
    SkinScreen out;out.kind=kind;out.resolution=root.object.contains(L"resolution")?resolution(root[L"resolution"],path):base;
    if(root.object.contains(L"extensions")) {object(root[L"extensions"],path);out.extensions=root[L"extensions"];}
    const auto& elements=root[L"elements"];
    if(elements.type!=T::Array||elements.array.size()>limits.elements)fail(ErrorCode::LimitExceeded,path,"elements must be a bounded array");
    for(const auto& e:elements.array) {
        fields(e,path,{L"id",L"type",L"resource",L"x",L"y",L"width",L"height",L"z",L"opacity",L"text",L"binding",L"sprite",L"extensions",L"rotation",L"tint",L"visible",L"source",L"clip",L"font",L"fontSize",L"align",L"verticalAlign",L"clipText",L"numberFormat",L"default",L"boxMode",L"hud",L"cornerRadius"});
        SkinElement item;item.id=string(e[L"id"],path,96);
        const auto type=string(e[L"type"],path);
        if(type=="image")item.kind=ElementKind::Image;else if(type=="text")item.kind=ElementKind::Text;
        else if(type=="number")item.kind=ElementKind::Number;else if(type=="sprite")item.kind=ElementKind::Sprite;
        else if(type=="box")item.kind=ElementKind::Box;
        else fail(ErrorCode::InvalidScreen,path,"Unsupported element type (no executable/script elements)");
        if(e.object.contains(L"boxMode"))item.boxMode=string(e[L"boxMode"],path,16);
        item.x=number(e[L"x"],path,-1000000,1000000);item.y=number(e[L"y"],path,-1000000,1000000);
        item.width=number(e[L"width"],path,0.001,1000000);item.height=number(e[L"height"],path,0.001,1000000);
        if(e.object.contains(L"z")) {const double z=number(e[L"z"],path,-1000000,1000000);if(z!=std::floor(z))fail(ErrorCode::InvalidScreen,path,"z must be integer");item.z=static_cast<std::int32_t>(z);}
        if(e.object.contains(L"opacity"))item.opacity=number(e[L"opacity"],path,0,1);
        if(e.object.contains(L"cornerRadius"))item.cornerRadius=number(e[L"cornerRadius"],path,0,1000000);
        if(e.object.contains(L"rotation"))item.rotation=number(e[L"rotation"],path,-360000,360000);
        if(e.object.contains(L"visible")) {
            if(e[L"visible"].type!=T::Boolean)fail(ErrorCode::InvalidScreen,path,"visible must be boolean");
            item.visible=e[L"visible"].boolean;
        }
        if(e.object.contains(L"source"))item.source=rectangle(e[L"source"],path);
        if(e.object.contains(L"clip"))item.clip=rectangle(e[L"clip"],path);
        if(e.object.contains(L"tint")) {
            const auto& t=e[L"tint"];fields(t,path,{L"r",L"g",L"b",L"a"});
            item.tint={static_cast<std::uint8_t>(integer(t[L"r"],path,0,255)),
                static_cast<std::uint8_t>(integer(t[L"g"],path,0,255)),static_cast<std::uint8_t>(integer(t[L"b"],path,0,255)),
                static_cast<std::uint8_t>(t.object.contains(L"a")?integer(t[L"a"],path,0,255):255)};
        }
        if(e.object.contains(L"font"))item.textStyle.font=string(e[L"font"],path,31);
        if(e.object.contains(L"fontSize"))item.textStyle.fontSize=integer(e[L"fontSize"],path,1,256);
        if(e.object.contains(L"align")) {
            const auto a=string(e[L"align"],path);
            if(a=="left")item.textStyle.align=TextAlign::Left;
            else if(a=="center")item.textStyle.align=TextAlign::Center;
            else if(a=="right")item.textStyle.align=TextAlign::Right;
            else fail(ErrorCode::InvalidScreen,path,"Unknown horizontal alignment");
        }
        if(e.object.contains(L"verticalAlign")) {
            const auto a=string(e[L"verticalAlign"],path);
            if(a=="top")item.textStyle.verticalAlign=TextVerticalAlign::Top;
            else if(a=="center")item.textStyle.verticalAlign=TextVerticalAlign::Center;
            else if(a=="bottom")item.textStyle.verticalAlign=TextVerticalAlign::Bottom;
            else fail(ErrorCode::InvalidScreen,path,"Unknown vertical alignment");
        }
        if(e.object.contains(L"clipText")) {
            if(e[L"clipText"].type!=T::Boolean)fail(ErrorCode::InvalidScreen,path,"clipText must be boolean");
            item.textStyle.clip=e[L"clipText"].boolean;
        }
        if(e.object.contains(L"numberFormat")) {
            const auto& n=e[L"numberFormat"];fields(n,path,{L"decimals",L"minDigits",L"grouping"});
            if(n.object.contains(L"decimals"))item.numberFormat.decimals=integer(n[L"decimals"],path,0,9);
            if(n.object.contains(L"minDigits"))item.numberFormat.minDigits=integer(n[L"minDigits"],path,0,32);
            if(n.object.contains(L"grouping")) {
                if(n[L"grouping"].type!=T::Boolean)fail(ErrorCode::InvalidScreen,path,"grouping must be boolean");
                item.numberFormat.grouping=n[L"grouping"].boolean;
            }
        }
        if(e.object.contains(L"default")) {
            const auto& v=e[L"default"];
            if(v.type==T::String)item.defaultValue=string(v,path,4096,true);
            else if(v.type==T::Boolean)item.defaultValue=v.boolean;
            else if(v.type==T::Number) {
                const auto n=number(v,path,-1e308,1e308);
                if(n==std::floor(n)&&std::abs(n)<=9007199254740991.0)item.defaultValue=static_cast<std::int64_t>(n);
                else item.defaultValue=n;
            } else if(v.type!=T::Null)fail(ErrorCode::InvalidScreen,path,"default must be a scalar");
        }
        if(e.object.contains(L"hud")) {
            const auto& h=e[L"hud"];fields(h,path,{L"judgementMode",L"comboMode",L"judgements",L"digits",L"judgementWidth",L"judgementHeight",L"digitWidth",L"digitHeight",L"spacing",L"gap",L"displayMs"});
            if(h.object.contains(L"judgementMode"))item.hud.judgementMode=string(h[L"judgementMode"],path,16);
            if(h.object.contains(L"comboMode"))item.hud.comboMode=string(h[L"comboMode"],path,16);
            const wchar_t* names[]={L"pgreat",L"great",L"good",L"bad",L"poor",L"mine"};
            if(h.object.contains(L"judgements")){const auto& j=h[L"judgements"];fields(j,path,{L"pgreat",L"great",L"good",L"bad",L"poor",L"mine"});for(int k=0;k<6;++k)if(j.object.contains(names[k]))item.hud.judgements[k]=string(j[names[k]],path,96,true);}
            if(h.object.contains(L"digits")){const auto& d=h[L"digits"];if(d.type!=T::Array||d.array.size()!=10)fail(ErrorCode::InvalidScreen,path,"hud.digits needs ten resource IDs");for(int k=0;k<10;++k)item.hud.digits[k]=string(d.array[k],path,96,true);}
            for(auto entry:{std::pair{L"judgementWidth",&item.hud.judgementWidth},std::pair{L"judgementHeight",&item.hud.judgementHeight},std::pair{L"digitWidth",&item.hud.digitWidth},std::pair{L"digitHeight",&item.hud.digitHeight},std::pair{L"spacing",&item.hud.spacing},std::pair{L"gap",&item.hud.gap},std::pair{L"displayMs",&item.hud.displayMs}})
                if(h.object.contains(entry.first))*entry.second=number(h[entry.first],path,0,10000);
        }
        if(e.object.contains(L"resource"))item.resource=string(e[L"resource"],path,96);
        if(e.object.contains(L"text"))item.text=string(e[L"text"],path,4096,true);
        if(e.object.contains(L"binding"))item.binding=string(e[L"binding"],path,128);
        if(e.object.contains(L"sprite")) {
            const auto& s=e[L"sprite"];fields(s,path,{L"frameWidth",L"frameHeight",L"columns",L"rows"});
            item.sprite=SpriteGrid{integer(s[L"frameWidth"],path,1,8192),integer(s[L"frameHeight"],path,1,8192),integer(s[L"columns"],path,1,4096),integer(s[L"rows"],path,1,4096)};
        }
        if(e.object.contains(L"extensions")){object(e[L"extensions"],path);item.extensions=e[L"extensions"];}
        out.elements.push_back(std::move(item));
    }
    return out;
}
void validate(SkinPackage& p,const Limits& l) {
    for(auto& [kind,screen]:p.screens){(void)kind;for(auto& e:screen.elements){
        const bool legacy=sevenmania::isLegacyHudFont(e.textStyle.font);
        if(legacy){e.textStyle.font="Michroma";e.resource.clear();}
        else if(!e.resource.empty()&&p.resources.contains(e.resource)&&sevenmania::isLegacyHudFont(p.resources.at(e.resource).path)){e.textStyle.font="Michroma";e.resource.clear();}
    }}
    auto& m=p.manifest;
    if(m.format!="SevenMania.Skin"||m.formatVersion!=1)fail(ErrorCode::UnsupportedVersion,"manifest.json","Unsupported format version");
    identifier(m.id,"manifest.id");
    if(m.name.empty()||m.name.size()>1024||m.author.empty()||m.author.size()>1024||m.description.size()>16384||!version(m.skinVersion))
        fail(ErrorCode::InvalidManifest,"manifest.json","Invalid metadata or skinVersion; expected major.minor.patch with optional suffix");
    for(const auto& [s,max]:{std::pair{&m.name,256u},std::pair{&m.author,256u},std::pair{&m.description,4096u}}) {
        if(wide(*s,"manifest.json").size()>max||s->find('\0')!=std::string::npos)fail(ErrorCode::InvalidManifest,"manifest.json","Metadata string limit or NUL violation");
    }
    const auto validResolution=[](const Resolution& r){return r.width&&r.height&&r.width<=16384&&r.height<=16384;};
    if(!validResolution(m.resolution)||m.supportedScreens.empty()||m.supportedScreens.size()>6)fail(ErrorCode::InvalidManifest,"manifest.json","Invalid resolution/screens");
    std::set<ScreenKind> declared;
    for(const auto s:m.supportedScreens)if(!screenName(s)||!declared.insert(s).second)fail(ErrorCode::InvalidManifest,"manifest.json","Duplicate/invalid supported screen");
    if(p.screens.size()!=declared.size())fail(ErrorCode::MissingFile,"screens/","Declared screen is missing or extra screen is present");
    if(m.resourcePaths.size()>l.files||p.resources.size()>l.files)fail(ErrorCode::LimitExceeded,"manifest.json","Resource count limit exceeded");
    std::uint64_t bytes=0,decoded=0,extensionText=0;
    std::uint32_t extensionNodes=0;
    const auto extension=[&](const J& root,const std::string& path) {
        if(root.type!=T::Null&&root.type!=T::Object)fail(ErrorCode::InvalidModel,path,"extensions must be an object");
        std::vector<std::pair<const J*,unsigned>> stack{{&root,0}};
        while(!stack.empty()) {
            const auto [value,depth]=stack.back();stack.pop_back();
            if(depth>l.jsonDepth||++extensionNodes>l.jsonNodes)fail(ErrorCode::LimitExceeded,path,"Extension DOM limits exceeded");
            if(value->type<T::Null||value->type>T::Object)fail(ErrorCode::InvalidModel,path,"Invalid extension JSON type");
            if(value->type==T::Number&&!std::isfinite(value->number))fail(ErrorCode::InvalidModel,path,"Nonfinite extension number");
            if(value->type==T::String){extensionText+=value->string.size()*sizeof(wchar_t);(void)utf8(value->string);}
            if(value->object.size()+value->array.size()>l.jsonNodes-extensionNodes)fail(ErrorCode::LimitExceeded,path,"Extension node limit exceeded");
            for(const auto& [key,child]:value->object){extensionText+=key.size()*sizeof(wchar_t);(void)utf8(key);stack.emplace_back(&child,depth+1);}
            for(const auto& child:value->array)stack.emplace_back(&child,depth+1);
            if(extensionText>l.jsonBytes)fail(ErrorCode::LimitExceeded,path,"Extension text budget exceeded");
        }
    };
    for(const auto& [id,path]:m.resourcePaths) {
        identifier(id,"resourcePaths");
        if(!resourcePath(path.path))fail(ErrorCode::UnsupportedResource,path.path,"Unsupported resource path/type");
        if(path.required&&!p.resources.contains(id))fail(ErrorCode::MissingFile,path.path,"Required resource is missing");
    }
    for(auto& [id,r]:p.resources) {
        if(!m.resourcePaths.contains(id)||m.resourcePaths.at(id).path!=r.path||r.id!=id||!r.bytes)
            fail(ErrorCode::InvalidModel,id,"Resource is undeclared or inconsistent");
        if(r.bytes->size()>l.fileBytes||(bytes+=r.bytes->size())>l.totalBytes)fail(ErrorCode::LimitExceeded,r.path,"Resource byte limit exceeded");
        imageInfo(r,l);
        decoded+=std::uint64_t(r.width)*r.height*4;
        if(decoded>l.totalDecodedImageBytes)fail(ErrorCode::LimitExceeded,r.path,"Decoded image budget exceeded");
    }
    for(auto& [kind,s]:p.screens) {
        if(!declared.contains(kind)||s.kind!=kind||!validResolution(s.resolution))fail(ErrorCode::InvalidScreen,"screens/","Invalid screen model");
        if(s.elements.size()>l.elements)fail(ErrorCode::LimitExceeded,screenName(kind),"Element count limit exceeded");
        extension(s.extensions,screenName(kind));
        std::set<std::string> ids;
        for(const auto& e:s.elements) {
            identifier(e.id,screenName(kind));if(!ids.insert(e.id).second)fail(ErrorCode::DuplicateId,e.id,"Duplicate element ID");
            for(double n:{e.x,e.y,e.width,e.height,e.opacity,e.rotation,e.cornerRadius})if(!std::isfinite(n))fail(ErrorCode::InvalidScreen,e.id,"Nonfinite element geometry");
            if(std::abs(e.x)>1000000||std::abs(e.y)>1000000||e.width<=0||e.height<=0||e.width>1000000||e.height>1000000||e.opacity<0||e.opacity>1||std::abs(std::int64_t(e.z))>1000000)
                fail(ErrorCode::InvalidScreen,e.id,"Invalid element geometry/layer/opacity");
            if(e.cornerRadius<0||e.cornerRadius>1000000)fail(ErrorCode::InvalidScreen,e.id,"Invalid corner radius");
            if(std::abs(e.rotation)>360000)fail(ErrorCode::InvalidScreen,e.id,"Rotation out of range");
            for(const auto* rect:{&e.source,&e.clip})if(*rect) {
                const auto& r=**rect;
                for(double n:{r.x,r.y,r.width,r.height})if(!std::isfinite(n))fail(ErrorCode::InvalidScreen,e.id,"Nonfinite rectangle");
                if(std::abs(r.x)>1000000||std::abs(r.y)>1000000||r.width<=0||r.height<=0||r.width>1000000||r.height>1000000)
                    fail(ErrorCode::InvalidScreen,e.id,"Invalid rectangle");
            }
            if(e.source&&e.kind!=ElementKind::Image&&e.kind!=ElementKind::Sprite)fail(ErrorCode::InvalidScreen,e.id,"Source rectangle needs an image");
            const auto& style=e.textStyle;
            if(style.font.empty()||style.font.find('\0')!=std::string::npos||wide(style.font,e.id).size()>31||!style.fontSize||style.fontSize>256||
                style.align<TextAlign::Left||style.align>TextAlign::Right||style.verticalAlign<TextVerticalAlign::Top||style.verticalAlign>TextVerticalAlign::Bottom||
                e.numberFormat.decimals>9||e.numberFormat.minDigits>32)fail(ErrorCode::InvalidScreen,e.id,"Invalid text/number style");
            if(const auto* text=std::get_if<std::string>(&e.defaultValue);text&&(text->size()>16384||text->find('\0')!=std::string::npos||wide(*text,e.id).size()>4096))
                fail(ErrorCode::InvalidScreen,e.id,"Invalid default text");
            if(const auto* real=std::get_if<double>(&e.defaultValue);real&&!std::isfinite(*real))fail(ErrorCode::InvalidScreen,e.id,"Nonfinite default");
            if(e.kind==ElementKind::Number&&std::holds_alternative<bool>(e.defaultValue))fail(ErrorCode::InvalidScreen,e.id,"Number default cannot be boolean");
            if(e.text.size()>16384||e.binding.size()>512)fail(ErrorCode::LimitExceeded,e.id,"Text/binding too long");
            if(e.text.find('\0')!=std::string::npos||e.binding.find('\0')!=std::string::npos)fail(ErrorCode::InvalidModel,e.id,"NUL in text/binding");
            if(wide(e.text,e.id).size()>4096||wide(e.binding,e.id).size()>128)fail(ErrorCode::LimitExceeded,e.id,"Text/binding character limit exceeded");
            const auto& hud=e.hud;
            if((hud.judgementMode!="font"&&hud.judgementMode!="image")||(hud.comboMode!="font"&&hud.comboMode!="image"))fail(ErrorCode::InvalidScreen,e.id,"HUD modes must be font or image");
            for(double n:{hud.judgementWidth,hud.judgementHeight,hud.digitWidth,hud.digitHeight,hud.spacing,hud.gap,hud.displayMs})if(!std::isfinite(n)||n<0||n>10000)fail(ErrorCode::InvalidScreen,e.id,"Invalid HUD dimensions/time");
            if(hud.judgementWidth<=0||hud.judgementHeight<=0||hud.digitWidth<=0||hud.digitHeight<=0||hud.displayMs<=0)fail(ErrorCode::InvalidScreen,e.id,"HUD size/time must be positive");
            if(hud.images()&&(e.kind!=ElementKind::Text||e.binding!="play.judgementCombo"||kind!=ScreenKind::Play))fail(ErrorCode::InvalidScreen,e.id,"HUD images need a play.judgementCombo text element");
            for(const auto& id:hud.judgements)if(!id.empty())identifier(id,e.id);
            for(const auto& id:hud.digits)if(!id.empty())identifier(id,e.id);
            extension(e.extensions,e.id);(void)Entrance::parse(e.extensions);
            if(e.kind!=ElementKind::Image&&e.kind!=ElementKind::Sprite&&e.kind!=ElementKind::Text&&e.kind!=ElementKind::Number&&e.kind!=ElementKind::Box)fail(ErrorCode::InvalidScreen,e.id,"Unknown element kind");
            if(e.kind==ElementKind::Box&&(e.boxMode!="solid"&&e.boxMode!="title"&&e.boxMode!="bga"&&e.boxMode!="hybrid"))fail(ErrorCode::InvalidScreen,e.id,"Invalid box mode");
            if(e.kind==ElementKind::Box&&!e.resource.empty())fail(ErrorCode::InvalidScreen,e.id,"Box cannot use a resource");
            const bool image=e.kind==ElementKind::Image||e.kind==ElementKind::Sprite;
            if(image&&e.resource.empty())fail(ErrorCode::MissingFile,e.id,"Image/sprite needs a resource ID");
            if(!e.resource.empty()) {
                identifier(e.resource,e.id);
                if(!p.resources.contains(e.resource))fail(ErrorCode::MissingFile,e.resource,"Element resource is absent (including optional assets)");
                const auto& r=p.resources.at(e.resource);
                if(image?(r.kind!=ResourceKind::Png&&r.kind!=ResourceKind::Jpeg):r.kind!=ResourceKind::Font)
                    fail(ErrorCode::InvalidScreen,e.id,"Element resource type mismatch");
                if(e.source&&(e.source->x<0||e.source->y<0||e.source->x+e.source->width>r.width||e.source->y+e.source->height>r.height))
                    fail(ErrorCode::InvalidScreen,e.id,"Source rectangle exceeds image");
                if(e.kind==ElementKind::Sprite) {
                    if(!e.sprite)fail(ErrorCode::InvalidScreen,e.id,"Sprite needs grid metadata");
                    const auto& g=*e.sprite;
                    if(!g.frameWidth||!g.frameHeight||!g.columns||!g.rows||std::uint64_t(g.columns)*g.rows>4096||
                        std::uint64_t(g.frameWidth)*g.columns>r.width||std::uint64_t(g.frameHeight)*g.rows>r.height)
                        fail(ErrorCode::InvalidScreen,e.id,"Sprite grid exceeds image");
                }
            }
            if(e.kind!=ElementKind::Sprite&&e.sprite)fail(ErrorCode::InvalidScreen,e.id,"Grid only belongs to a sprite");
        }
        std::stable_sort(s.elements.begin(),s.elements.end(),[](const auto& a,const auto& b){return a.z<b.z;});
    }
}
template<class Work> LoadResult guarded(Work work) noexcept {
    try {return {std::make_shared<const SkinPackage>(work()),{}};}
    catch(const Failure& e){return {{},e.error};}
    catch(const std::bad_alloc&){return {{},SkinError{ErrorCode::AllocationFailed,"","Skin allocation failed"}};}
    catch(const std::exception& e){return {{},SkinError{ErrorCode::InvalidModel,"",e.what()}};}
    catch(...){return {{},SkinError{ErrorCode::InvalidModel,"","Unknown skin loading failure"}};}
}
}
const char* screenName(ScreenKind kind) noexcept {
    switch(kind){case ScreenKind::SettingsSummary:return "settingssummary";case ScreenKind::SongIntro:return "songintro";case ScreenKind::SongInformation:return "songinformation";case ScreenKind::Select:return "select";case ScreenKind::Play:return "play";case ScreenKind::Result:return "result";default:return nullptr;}
}
LoadResult SkinLoader::load(const std::filesystem::path& file) const noexcept {
    return guarded([&] {
        auto ext=file.extension().wstring();for(auto& c:ext)if(c>=L'A'&&c<=L'Z')c+=32;
        if(ext!=L".semaskin")fail(ErrorCode::InvalidZip,"","Expected .semaskin extension");
        auto archive=readZip(file,limits_);
        if(!archive.contains("manifest.json"))fail(ErrorCode::MissingFile,"manifest.json","Manifest is missing");
        SkinPackage p;p.manifest=manifest(json(*archive.at("manifest.json"),"manifest.json",limits_));
        for(const auto kind:p.manifest.supportedScreens) {
            const std::string path=std::string("screens/")+screenName(kind)+".json";
            if(!archive.contains(path))fail(ErrorCode::MissingFile,path,"Declared screen is missing");
            p.screens.emplace(kind,layout(json(*archive.at(path),path,limits_),kind,p.manifest.resolution,path,limits_));
        }
        for(const auto& [id,r]:p.manifest.resourcePaths) {
            (void)safePath(r.path);
            if(archive.contains(r.path))p.resources.emplace(id,SkinResource{id,r.path,{},archive.at(r.path),0,0});
        }
        // Validate even unused entries; malformed or disguised payloads cannot hide there.
        for(const auto& [path,data]:archive) {
            if(path=="manifest.json")continue;
            if(path.starts_with("licenses/")&&path.ends_with(".txt")){if(data->size()>65536)fail(ErrorCode::LimitExceeded,path,"License text too long");const std::string notice(data->begin(),data->end());if(notice.find('\0')!=std::string::npos)fail(ErrorCode::UnsupportedResource,path,"License must be UTF-8 text");(void)wide(notice,path);continue;}
            if(path.starts_with("screens/")) {
                const bool listed=std::any_of(p.manifest.supportedScreens.begin(),p.manifest.supportedScreens.end(),[&](auto kind){return path==std::string("screens/")+screenName(kind)+".json";});
                if(!listed)fail(ErrorCode::InvalidScreen,path,"Screen file is not declared");
            } else {SkinResource resource{"",path,{},data,0,0};imageInfo(resource,limits_);}
        }
        validate(p,limits_);return p;
    });
}
LoadResult SkinLoader::prepare(SkinPackage model) const noexcept {
    return guarded([&]{
        validate(model,limits_);
        // A converter may still hold mutable aliases to its byte buffers.
        // Freeze one copy per shared source; all elements/aliases keep sharing it.
        std::map<const Bytes*,std::shared_ptr<const Bytes>> snapshots;
        for(auto& [id,r]:model.resources) {
            (void)id;
            const auto source=r.bytes.get();
            if(!snapshots.contains(source))snapshots.emplace(source,std::make_shared<const Bytes>(*r.bytes));
            r.bytes=snapshots.at(source);
        }
        return std::move(model);
    });
}
}
