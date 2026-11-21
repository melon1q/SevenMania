#pragma once
#include "Lr2Parser.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <string_view>

namespace sevenmania::skin::lr2 {
using Destination = std::array<int,19>;
enum class LayerKind { Image, JudgeLine, MeasureLine, Judge, Combo, Number, Gauge, Bga, Text, BarBody, BarLamp, Button, Slider,
    BarLevel, BarRank, BarFlash, BarGraph, OnMouse, GaugeChart, Count };
inline constexpr int maxSelectBars=64;
struct ImageSource {
    std::array<int,10> values{};
    std::vector<Destination> destinations;
    std::size_t imageCount{};
    Diagnostic location;
    int acceleration{}, blend{}, filter{}, center{}, loop{}, timer{};
    // Player of a judgement, combo, judge line or measure line: 0 is 1P, 1 is 2P.
    int side{};
    std::array<int,3> conditions{};
    LayerKind kind{};
    std::array<int,5> parameters{};
    // Sum of the CUSTOMOFFSET values named by this object's first offset list: x, y, w, h, r, a.
    std::optional<std::array<int,6>> offset;
    // Built-in offset 1 or 2: the 1P or 2P turntable's rotation, added while drawing; 0 for none.
    int turntable{};
};
inline std::optional<LayerKind> layerKind(std::string_view name,bool play,bool screen=false,bool select=false) noexcept {
    if(name=="IMAGE")return LayerKind::Image;
    if(name=="JUDGELINE")return LayerKind::JudgeLine;
    if(screen&&name=="TEXT")return LayerKind::Text;
    if(select&&name=="BUTTON")return LayerKind::Button;
    if((select||play)&&name=="SLIDER")return LayerKind::Slider;
    if(screen&&name=="GAUGECHART_1P")return LayerKind::GaugeChart;
    if(select&&name=="ONMOUSE")return LayerKind::OnMouse;
    if((play||screen)&&name=="BARGRAPH")return LayerKind::BarGraph;
    if((play||screen)&&name=="NUMBER")return LayerKind::Number;
    if(!play)return std::nullopt;
    if(name=="LINE")return LayerKind::MeasureLine;
    if(name=="NOWJUDGE_1P"||name=="NOWJUDGE_2P")return LayerKind::Judge;
    if(name=="NOWCOMBO_1P"||name=="NOWCOMBO_2P")return LayerKind::Combo;
    if(name=="GROOVEGAUGE")return LayerKind::Gauge;
    if(name=="BGA")return LayerKind::Bga;
    return std::nullopt;
}
// Parts drawn on every song bar, relative to the bar. Lamps, levels and ranks come in numbered variants.
inline std::optional<LayerKind> barPartKind(std::string_view name) noexcept {
    if(name=="BAR_LAMP")return LayerKind::BarLamp;
    if(name=="BAR_LEVEL")return LayerKind::BarLevel;
    if(name=="BAR_RANK")return LayerKind::BarRank;
    if(name=="BAR_FLASH")return LayerKind::BarFlash;
    return std::nullopt;
}
inline int barPartVariants(std::optional<LayerKind> kind) noexcept {
    return kind==LayerKind::BarLamp?10:kind==LayerKind::BarLevel?7:kind==LayerKind::BarRank?9:0;
}
// Song-bar commands the select screen understands.
inline bool selectBarWord(std::string_view name) noexcept {
    if(name=="BAR_CENTER"||name=="BAR_AVAILABLE"||name=="SRC_BAR_BODY"||name=="DST_BAR_BODY_ON"||name=="DST_BAR_BODY_OFF"||
       name=="SRC_BAR_TITLE"||name=="DST_BAR_TITLE")return true;
    return (name.starts_with("SRC_")||name.starts_with("DST_"))&&barPartKind(name.substr(4)).has_value();
}
using ColorKey = std::array<int,3>;
enum class PlayNotePart { Tap, Start, Body, End, HeldBody, Mine, Count };
inline constexpr std::size_t playNoteParts=static_cast<std::size_t>(PlayNotePart::Count);
// SRC_AUTO_* declares the same parts for autoplay; autoNoteWord tells them apart.
inline bool autoNoteWord(std::string_view name) noexcept {return name.starts_with("SRC_AUTO_");}
inline std::optional<PlayNotePart> playNotePart(std::string_view name) noexcept {
    if(!name.starts_with("SRC_"))return std::nullopt;
    name.remove_prefix(autoNoteWord(name)?9:4);
    if(name=="NOTE")return PlayNotePart::Tap;
    if(name=="LN_START")return PlayNotePart::Start;
    if(name=="LN_END")return PlayNotePart::End;
    if(name=="LN_BODY"||name=="LN_BODY_INACTIVE")return PlayNotePart::Body;
    if(name=="LN_BODY_ACTIVE")return PlayNotePart::HeldBody;
    if(name=="MINE")return PlayNotePart::Mine;
    return std::nullopt;
}
struct ImageDeclaration {
    std::string path;
    Diagnostic location;
    ColorKey colorKey;
};
struct FontDeclaration {
    std::string path;
    Diagnostic location;
};
// #FONT: a system font (DX Library: size, thickness 0-9, font type, face names to try).
struct SystemFontDeclaration {
    int size{}, thickness=-1, type{};
    std::vector<std::string> faces;
};
// Bar title text: an #LR2FONT index and a position relative to the bar.
struct BarTitle {
    int font=-1;
    std::optional<Destination> destination;
};
struct ImageModel {
    std::vector<ImageDeclaration> images;
    // #LR2FONT declarations in order; SRC_TEXT and SRC_BAR_TITLE refer to them by index.
    std::vector<FontDeclaration> fonts;
    // #FONT declarations in order. A text layer whose LR2FONT is unavailable draws with the
    // system font of the same index when one is declared.
    std::vector<SystemFontDeclaration> systemFonts;
    std::array<BarTitle,2> barTitles;
    std::vector<ImageSource> sources;
    // Indexed by LR2 lane (see playLaneUsed).
    std::array<std::array<std::optional<ImageSource>,playNoteParts>,maxPlayLanes> noteSources;
    // SRC_AUTO_* parts, drawn instead of noteSources while autoplay is on.
    std::array<std::array<std::optional<ImageSource>,playNoteParts>,maxPlayLanes> autoNoteSources;
    std::array<std::optional<Destination>,maxPlayLanes> tapDestinations;
    std::optional<std::size_t> noteOrder;
    std::array<std::array<std::optional<Destination>,maxSelectBars>,2> selectBarPositions;
    int barCenter=-1,firstClickable=0,lastClickable=maxSelectBars-1;
    std::optional<std::size_t> barOrder;
};
struct ImageGrid { int x, y, width, height, columns, rows; };
struct ImageFrame {
    float x{}, y{}, width{}, height{}, alpha{};
    int sourceX{}, sourceY{}, sourceWidth{}, sourceHeight{};
    float red{}, green{}, blue{}, angle{}, pivotX{}, pivotY{};
};

inline std::optional<ImageGrid> imageGrid(const ImageSource& source,int imageWidth,int imageHeight) noexcept {
    const auto& v=source.values;
    const int x=v[2]==-1&&v[4]==-1?0:v[2], y=v[3]==-1&&v[5]==-1?0:v[3];
    const int w=v[4]==-1?imageWidth:v[4], h=v[5]==-1?imageHeight:v[5];
    const int columns=std::max(1,v[6]), rows=std::max(1,v[7]);
    if(imageWidth<=0||imageHeight<=0||x<0||y<0||w<columns||h<rows||columns>4096||rows>4096)return std::nullopt;
    // A divisible grid samples only complete cells. Some skins include unused
    // remainder pixels in the declared rectangle beyond the atlas edge.
    if(x>imageWidth-w/columns*columns||y>imageHeight-h/rows*rows)return std::nullopt;
    return ImageGrid{x,y,w/columns,h/rows,columns,rows};
}

inline std::optional<ImageFrame> sampleImage(const ImageSource& source,const ImageGrid& grid,
                                             double milliseconds,float outputWidth,float outputHeight,
                                             unsigned designWidth,unsigned designHeight,
                                             std::optional<double> spriteMilliseconds=std::nullopt) noexcept {
    const auto& keys=source.destinations;
    if(keys.empty()||!designWidth||!designHeight||!std::isfinite(milliseconds)||milliseconds<0||
       outputWidth<=0||outputHeight<=0)return std::nullopt;
    double time=milliseconds;
    const double end=keys.back()[0];
    if(source.loop<0 && time>end)return std::nullopt;
    if(source.loop>=0 && end>0 && time>source.loop && end>=source.loop)
        time=end==source.loop?end:source.loop+std::fmod(time-source.loop,end-source.loop);
    if(time<keys.front()[0])return std::nullopt;

    const auto next=std::upper_bound(keys.begin(),keys.end(),time,
        [](double value,const Destination& key){return value<key[0];});
    const auto& a=*(next-1);
    const auto& b=next==keys.end()?a:*next;
    float t=next==keys.end()?0.f:float((time-a[0])/(double(b[0])-a[0]));
    switch(source.acceleration) {
        case 1: t*=t; break;
        case 2: t=1-(1-t)*(1-t); break;
        case 3: t=0; break;
    }
    const auto lerp=[&](std::size_t i){return std::lerp(float(a[i]),float(b[i]),t);};
    const auto channel=[&](std::size_t i){return std::clamp(lerp(i)/255.f,0.f,1.f);};
    // The CSV stores a top-left position. Normalize signed sizes per key before interpolating.
    const auto position=[&](std::size_t i) {
        return std::lerp(float(a[i])+float(std::min(0,a[i+2])),
                         float(b[i])+float(std::min(0,b[i+2])),t);
    };
    const auto size=[&](std::size_t i){return std::lerp(std::abs(float(a[i])),std::abs(float(b[i])),t);};
    const int cycle=source.values[8], count=grid.columns*grid.rows;
    const double spriteTime=spriteMilliseconds.value_or(milliseconds);
    const int frame=cycle>0&&spriteTime>=0?std::min(count-1,int(std::fmod(spriteTime,double(cycle))*count/cycle)):0;
    constexpr float pivotX[]{.5f,0,.5f,1,0,.5f,1,0,.5f,1};
    constexpr float pivotY[]{.5f,1,1,1,.5f,.5f,.5f,0,0,0};
    const int center=std::clamp(source.center,0,9);
    return ImageFrame{position(1)*outputWidth/designWidth,position(2)*outputHeight/designHeight,
        size(3)*outputWidth/designWidth,size(4)*outputHeight/designHeight,channel(6),
        grid.x+frame%grid.columns*grid.width,grid.y+frame/grid.columns*grid.height,grid.width,grid.height,
        channel(7),channel(8),channel(9),-std::trunc(lerp(12)),pivotX[center],pivotY[center]};
}

inline std::optional<ImageFrame> sampleImage(const ImageSource& source,int imageWidth,int imageHeight,
                                             double milliseconds,float outputWidth,float outputHeight,
                                             unsigned designWidth,unsigned designHeight) noexcept {
    const auto grid=imageGrid(source,imageWidth,imageHeight);
    if(!grid)return std::nullopt;
    return sampleImage(source,*grid,milliseconds,outputWidth,outputHeight,designWidth,designHeight);
}

// Preserve image slots and source order, including missing or malformed image declarations.
inline std::optional<ImageModel> imageModel(const Document& document,std::vector<Diagnostic>* diagnostics=nullptr,
                                           bool play=false) {
    ImageModel out;
    const auto location=[](const Command& c){return Diagnostic{c.source,c.line,{},false};};
    const auto warn=[&](const Command& c,const char* message) {
        if(diagnostics)diagnostics->push_back({c.source,c.line,message,false});
    };
    const auto read=[](const Command& c,std::size_t index,int& value) {
        if(index>=c.fields.size())return true;
        std::string_view s=c.fields[index];
        const auto first=s.find_first_not_of(" \t");
        if(first==s.npos)return true;
        s=s.substr(first,s.find_last_not_of(" \t")-first+1);
        if(s.front()=='!') {
            s.remove_prefix(1);
            const auto [p,e]=std::from_chars(s.data(),s.data()+s.size(),value);
            if(e!=std::errc{}||p!=s.data()+s.size()||value<0)return false;
            value=-value;
            return true;
        }
        if(s.front()=='+')s.remove_prefix(1);
        const auto [p,e]=std::from_chars(s.data(),s.data()+s.size(),value);
        return e==std::errc{}&&p==s.data()+s.size();
    };
    ColorKey colorKey{0,255,0};
    std::array<std::optional<std::size_t>,static_cast<std::size_t>(LayerKind::Count)> currentLayers{};
    std::array<std::optional<std::size_t>,12> currentJudges{},currentCombos{}; // 1P 0-5, 2P 6-11
    // Double play reads the 2P judgement, combo and lines; the other play types keep 1P only.
    const bool doublePlay=document.header.type==static_cast<int>(ScreenType::Play14Key)||
                          document.header.type==static_cast<int>(ScreenType::Play10Key);
    const auto commandSide=[](const Command& c){return c.name.ends_with("_2P")?1:0;};
    std::array<std::array<std::optional<std::size_t>,10>,3> currentBarParts{};
    const bool select=document.header.type==static_cast<int>(ScreenType::Select);
    const auto currentLayer=[&](LayerKind kind,int id)->std::optional<std::size_t>& {
        if(kind==LayerKind::Judge)return currentJudges[static_cast<std::size_t>(id)];
        if(kind==LayerKind::Combo)return currentCombos[static_cast<std::size_t>(id)];
        if(barPartVariants(kind))return currentBarParts[kind==LayerKind::BarLamp?0:kind==LayerKind::BarLevel?1:2][id];
        return currentLayers[static_cast<std::size_t>(kind)];
    };
    for(const auto& c:document.commands) {
        if(!c.enabled)continue;
        auto kind=c.name.size()>4?layerKind(std::string_view(c.name).substr(4),play,select||
            document.header.type==static_cast<int>(ScreenType::Decide)||document.header.type==static_cast<int>(ScreenType::Result),select):std::nullopt;
        if(select&&c.name.size()>4&&(c.name.starts_with("SRC_")||c.name.starts_with("DST_")))
            if(const auto part=barPartKind(std::string_view(c.name).substr(4)))kind=part;
        if(select&&c.name=="SRC_BAR_BODY")kind=LayerKind::BarBody;
        if(select&&c.name=="BAR_CENTER") {
            int center=-1;
            if(read(c,1,center)&&center>=0&&center<maxSelectBars)out.barCenter=center;
        } else if(select&&c.name=="BAR_AVAILABLE") {
            int first=-1,last=-1;
            if(read(c,1,first)&&read(c,2,last)&&first>=0&&first<=last&&last<maxSelectBars) {
                out.firstClickable=first;out.lastClickable=last;
            }
        } else if(select&&(c.name=="DST_BAR_BODY_ON"||c.name=="DST_BAR_BODY_OFF")) {
            int slot=-1; Destination key{};
            bool valid=c.fields.size()>6&&read(c,1,slot)&&slot>=0&&slot<maxSelectBars;
            for(std::size_t i=0;valid&&i<key.size();++i)valid=read(c,i+2,key[i]);
            if(valid) {
                if(!out.barOrder)out.barOrder=out.sources.size();
                auto& position=out.selectBarPositions[c.name=="DST_BAR_BODY_ON"][slot];
                if(!position||key[0]>=(*position)[0])position=key;
            } else warn(c,"Invalid select bar position");
        } else if(c.name=="TRANSCOLOR") {
            ColorKey next{};
            bool valid=c.fields.size()>=4;
            for(std::size_t i=0;valid&&i<next.size();++i) {
                const auto& field=c.fields[i+1];
                valid=field.find_first_not_of(" \t")!=field.npos && field.find('!')==field.npos &&
                    read(c,i+1,next[i]) && next[i]>=0 && next[i]<=255;
            }
            if(valid)colorKey=next;
            else warn(c,"Invalid TRANSCOLOR; expected three RGB values from 0 through 255");
        } else if(c.name=="FONT") {
            SystemFontDeclaration font;
            if(c.fields.size()>1&&!read(c,1,font.size))font.size=0;
            if(c.fields.size()>2&&!read(c,2,font.thickness))font.thickness=-1;
            if(c.fields.size()>3&&!read(c,3,font.type))font.type=0;
            for(std::size_t i=4;i<c.fields.size();++i)if(!c.fields[i].empty())font.faces.push_back(c.fields[i]);
            out.systemFonts.push_back(std::move(font));
        } else if(c.name=="LR2FONT") {
            out.fonts.push_back({c.fields.size()>1?c.fields[1]:std::string{},location(c)});
        } else if(select&&(c.name=="SRC_BAR_TITLE"||c.name=="DST_BAR_TITLE")) {
            int type=-1;
            if(!read(c,1,type)||type<0||type>1)continue;
            auto& title=out.barTitles[static_cast<std::size_t>(type)];
            if(c.name=="SRC_BAR_TITLE") {
                if(!read(c,2,title.font))title.font=-1;
            } else if(!title.destination) {
                // time, x, y, width, size, acceleration, alpha, red, green, blue
                Destination key{};
                bool valid=c.fields.size()>6;
                for(std::size_t i=0;valid&&i<10;++i)valid=read(c,i+2,key[i]);
                if(valid&&key[4]>0)title.destination=key;
                else warn(c,"Invalid bar title destination");
            }
        } else if(c.name=="IMAGE") {
            if(!diagnostics&&c.fields.size()<2)return std::nullopt;
            out.images.push_back({c.fields.size()>1?c.fields[1]:std::string{},location(c),colorKey});
        } else if(play&&c.name=="DST_NOTE") {
            int lane=-1;
            Destination key{};
            bool valid=c.fields.size()>6 && c.fields[1].find_first_not_of(" \t")!=std::string::npos &&
                read(c,1,lane) && lane>=0 && lane<maxPlayLanes;
            for(std::size_t i=0;valid&&i<=4;++i)valid=read(c,i+2,key[i]);
            if(!valid||key[3]<=0||key[4]<=0)warn(c,"Invalid DST_NOTE geometry");
            else if(!playLaneUsed(document.header.type,lane))warn(c,("DST_NOTE lane "+std::to_string(lane)+" is not used by this key mode").c_str());
            else if(!out.tapDestinations[lane]) {
                out.tapDestinations[lane]=key;
                if(!out.noteOrder)out.noteOrder=out.sources.size();
            }
        } else if(const auto part=play?playNotePart(c.name):std::nullopt;
                  (kind&&c.name.starts_with("SRC_"))||part) {
            const bool note=part.has_value();
            ImageSource source;
            int id=0;
            if(const int variants=barPartVariants(kind);variants&&(!read(c,1,id)||id<0||id>=variants))continue;
            if(kind&&(*kind==LayerKind::Judge||*kind==LayerKind::Combo)&&
               (!read(c,1,id)||id<0||id>5)) {warn(c,"Invalid LR2 judgement index");continue;}
            if(kind&&(*kind==LayerKind::Judge||*kind==LayerKind::Combo)&&commandSide(c)) {
                if(!doublePlay) {warn(c,"2P judgement is only drawn by 14KEY and 10KEY skins");continue;}
                id+=6;
            }
            if(!note)currentLayer(*kind,id).reset();
            bool valid=kind==LayerKind::Bga||(c.fields.size()>2 && !c.fields[2].empty());
            for(std::size_t i=0;i<source.values.size();++i)valid=read(c,i+1,source.values[i])&&valid;
            if(kind==LayerKind::Number||kind==LayerKind::Combo||kind==LayerKind::Gauge||kind==LayerKind::Judge||
               kind==LayerKind::Button||kind==LayerKind::Slider||kind==LayerKind::BarLevel||kind==LayerKind::BarGraph||
               kind==LayerKind::OnMouse||kind==LayerKind::GaugeChart)
                for(std::size_t i=0;i<source.parameters.size();++i)valid=read(c,i+11,source.parameters[i])&&valid;
            if(!valid) {
                if(!diagnostics)return std::nullopt;
                warn(c,"Invalid SRC_IMAGE fields"); continue;
            }
            source.imageCount=out.images.size();
            source.location=location(c);
            if(note) {
                const int lane=source.values[0];
                if(c.fields[1].find_first_not_of(" \t")==std::string::npos||lane<0||lane>=maxPlayLanes) {
                    warn(c,"Invalid note source lane");
                    continue;
                }
                if(!playLaneUsed(document.header.type,lane)) {
                    warn(c,("Note source lane "+std::to_string(lane)+" is not used by this key mode").c_str());
                    continue;
                }
                auto& parts=(autoNoteWord(c.name)?out.autoNoteSources:out.noteSources)[lane];
                if(c.name=="SRC_LN_BODY"||c.name=="SRC_AUTO_LN_BODY") {
                    auto& held=parts[static_cast<std::size_t>(PlayNotePart::HeldBody)];
                    if(!held)held=source;
                    source.values[8]=source.values[9]=0;
                }
                auto& entry=parts[static_cast<std::size_t>(*part)];
                if(!entry)entry=std::move(source);
            } else if((*kind!=LayerKind::JudgeLine&&*kind!=LayerKind::MeasureLine)||source.values[0]==0||
                      (doublePlay&&source.values[0]==1)) {
                source.kind=*kind;
                if(*kind==LayerKind::JudgeLine||*kind==LayerKind::MeasureLine)source.side=source.values[0];
                else if(*kind==LayerKind::Judge||*kind==LayerKind::Combo)source.side=id>=6;
                currentLayer(*kind,id)=out.sources.size();
                out.sources.push_back(std::move(source));
            } else warn(c,"Unsupported SRC_JUDGELINE player index");
        } else if(c.name=="SRC_IMAGESET") {
            currentLayer(LayerKind::Image,0).reset();
        } else if(kind&&c.name.starts_with("DST_")) {
            int id=0;
            if(const int variants=barPartVariants(kind);variants&&(!read(c,1,id)||id<0||id>=variants))continue;
            if((*kind==LayerKind::Judge||*kind==LayerKind::Combo)&&
               (!read(c,1,id)||id<0||id>5)) {warn(c,"Invalid LR2 judgement index");continue;}
            if((*kind==LayerKind::Judge||*kind==LayerKind::Combo)&&commandSide(c)) {
                if(!doublePlay) {warn(c,"2P judgement is only drawn by 14KEY and 10KEY skins");continue;}
                id+=6;
            }
            if(*kind==LayerKind::JudgeLine||*kind==LayerKind::MeasureLine) {
                int player=-1;
                if(c.fields.size()<2||!read(c,1,player)||player<0||player>(doublePlay?1:0)) {
                    warn(c,"Unsupported DST_JUDGELINE player index"); continue;
                }
                // Each player's DST follows the most recent SRC, which must be for the same player.
                if(const auto& line=currentLayer(*kind,0);line&&out.sources[*line].side!=player) {
                    warn(c,"Image destination has no supported source"); continue;
                }
            }
            const auto index=currentLayer(*kind,id);
            if(!index) {warn(c,"Image destination has no supported source"); continue;}
            auto* current=&out.sources[*index];
            Destination key{};
            bool valid=c.fields.size()>2;
            for(std::size_t i=0;i<key.size();++i)valid=read(c,i+2,key[i])&&valid;
            if(!valid) {
                if(!diagnostics)return std::nullopt;
                warn(c,"Invalid image destination fields"); continue;
            }
            if(!current->acceleration)current->acceleration=key[5];
            if(!current->blend)current->blend=key[10];
            if(!current->filter)current->filter=key[11];
            if(!current->center && key[13]>=0 && key[13]<=9)current->center=key[13];
            if(!current->loop)current->loop=key[14];
            if(!current->timer)current->timer=key[15];
            if(current->conditions==std::array<int,3>{})current->conditions={key[16],key[17],key[18]};
            current->destinations.push_back(key);
            // Offset IDs follow the three conditions. They belong to the object, so the
            // first DST line that names any applies to every keyframe.
            std::array<int,6> offset{};
            bool named=false;
            for(std::size_t i=21;i<c.fields.size();++i) {
                int offsetId=0;
                if(!read(c,i,offsetId)||!offsetId)continue;
                const auto found=document.offsetValues.find(offsetId);
                if(found==document.offsetValues.end()&&play&&(offsetId==1||offsetId==2)) {
                    if(!current->turntable)current->turntable=offsetId;
                    continue;
                }
                if(found==document.offsetValues.end()) {
                    const auto message="Unsupported destination offset: "+std::to_string(offsetId);
                    warn(c,message.c_str());
                    continue;
                }
                named=true;
                for(std::size_t axis=0;axis<offset.size();++axis)offset[axis]+=found->second[axis];
            }
            if(named&&!current->offset)current->offset=offset;
        }
    }
    for(auto& source:out.sources) {
        if(!source.offset)continue;
        const auto& offset=*source.offset;
        for(auto& key:source.destinations) {
            key[1]+=offset[0];key[2]+=offset[1];key[3]+=offset[2];key[4]+=offset[3];
            key[12]+=offset[4];key[6]+=offset[5];
        }
    }
    for(auto& source:out.sources)
        std::stable_sort(source.destinations.begin(),source.destinations.end(),
                         [](const Destination& a,const Destination& b){return a[0]<b[0];});
    return out;
}
}
