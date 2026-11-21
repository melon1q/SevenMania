#include "Lr2Preview.h"
#include "Lr2Font.h"
#include "skin/SkinInternal.h"
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <bit>
#include <ctime>
#include <fstream>
#include <limits>
#include <unordered_set>

namespace sevenmania::skin::lr2 {
using Microsoft::WRL::ComPtr;
namespace {
struct SkinViewport {
    float scale,x,y,width,height,stretchX,stretchY;
    float translationX() const noexcept {return -x*stretchX;}
    float translationY() const noexcept {return -y*stretchY;}
};
SkinViewport skinViewport(int width,int height,unsigned designWidth,unsigned designHeight,bool stretch) noexcept {
    const float scale=std::min(float(width)/designWidth,float(height)/designHeight);
    const float w=designWidth*scale,h=designHeight*scale;
    return {scale,(width-w)*.5f,(height-h)*.5f,w,h,stretch?width/w:1.f,stretch?height/h:1.f};
}
bool usesPreviewClock(int timer) {
    // Start input, ready and play can run from preview time zero.
    // Fadeout, failed and key-triggered timers need a game event.
    return timer<=0||timer==1||timer==40||timer==41;
}
bool panelTimer(int timer) {
    return (timer>=21&&timer<=26)||(timer>=31&&timer<=36);
}
bool selectPanelButton(int id) {return id>=1&&id<=6;}
bool selectVolumeSlider(int id) {return id>=17&&id<=19;}
// Result timers: 2 the closing fade; 150 and 151 the graph (beatoraja TIMER_RESULTGRAPH_BEGIN/END, both on
// from the start); 152 the score update (TIMER_RESULT_UPDATESCORE).
bool resultTimer(int timer) {return timer==2||(timer>=150&&timer<=152);}
// Decide timers: 2 the closing fade, 3 a key skipped the screen.
bool decideTimer(int timer) {return timer==2||timer==3;}
bool resultExComparison(int id) {return id==150||id==152||id==170||id==172;}
// MY BEST target numbers and the previous min BP, which depend on the record.
bool resultTargetNumber(int id) {return (id>=120&&id<=123)||id==125||id==126||(id>=130&&id<=134)||id==176;}
bool selectButton(int id) {return SelectFrame{}.buttonState(id).has_value();}
// Select timers 100-119: an LR2 key held (1P 0-9, 2P 10-19). 17 and 18 open and close the course editor,
// which SevenMania lacks, so they never start.
bool selectKeyTimer(int timer) {return timer>=100&&timer<=119;}
bool selectCourseTimer(int timer) {return timer==17||timer==18;}
// 15 and 16 open and close the song's text viewer (TIMER_README_BEGIN/END), which SevenMania lacks.
bool selectReadmeTimer(int timer) {return timer==15||timer==16;}
// 2 is the fade after a song is chosen.
bool selectTimer(int timer) {return timer==2||timer==11||panelTimer(timer)||selectKeyTimer(timer)||selectCourseTimer(timer)||selectReadmeTimer(timer);}
// Numbers that do not depend on a song: the clock and the three volume sliders.
bool selectSystemNumber(int id) {return (id>=21&&id<=26)||(id>=57&&id<=59);}
// Levels of the other difficulties filed with the selected chart.
bool selectFolderNumber(int id) {return id>=45&&id<=49;}
bool usesPlayClock(int timer) {
    return usesPreviewClock(timer)||timer==2||timer==3||timer==48||timer==42||timer==44||timer==46||timer==47||timer==446||timer==447||timer==140||
        (timer>=50&&timer<=69)||(timer>=70&&timer<=89)||
        (timer>=100&&timer<=119)||(timer>=120&&timer<=139);
}

Bytes decodeDds(IWICImagingFactory* factory,SkinResource& resource,const skin::Limits& limits,std::uint64_t remaining) {
    const auto& bytes=*resource.bytes;
    const auto u32=[&](std::size_t p) {
        return std::uint32_t(bytes[p]) | std::uint32_t(bytes[p+1])<<8 |
            std::uint32_t(bytes[p+2])<<16 | std::uint32_t(bytes[p+3])<<24;
    };
    if(bytes.size()<128||u32(0)!=0x20534444||u32(4)!=124||u32(76)!=32)
        throw std::runtime_error("Invalid DDS header");
    if(u32(112)!=0||u32(24)>1||(u32(8)&0x00800000))
        throw std::runtime_error("Unsupported DDS layout (expected a 2D texture)");
    resource.height=u32(12);
    resource.width=u32(16);
    const auto pixels=std::uint64_t(resource.width)*resource.height;
    if(!pixels||resource.width>limits.imageDimension||resource.height>limits.imageDimension||
       pixels>limits.imagePixels||pixels*4>remaining)
        throw std::runtime_error("DDS image dimensions or decoded byte limit exceeded");
    if(u32(80)&0x4) {
        constexpr std::uint32_t dxt1=0x31545844,dxt3=0x33545844,dxt5=0x35545844;
        const auto format=u32(84);
        if(format!=dxt1&&format!=dxt3&&format!=dxt5)
            throw std::runtime_error("Unsupported DDS FourCC (expected DXT1, DXT3 or DXT5)");
        const unsigned mipCount=(u32(8)&0x00020000)?u32(28):1;
        const auto maxMips=static_cast<unsigned>(std::bit_width(std::max(resource.width,resource.height)));
        if(!mipCount||mipCount>maxMips)
            throw std::runtime_error("Invalid DDS mip count");
        const std::size_t blockBytes=format==dxt1?8:16;
        std::size_t available=bytes.size()-128;
        for(unsigned mip=0;mip<mipCount;++mip) {
            const auto w=std::max(1u,resource.width>>mip);
            const auto h=std::max(1u,resource.height>>mip);
            const auto levelBytes=((w+3)/4)*((h+3)/4)*blockBytes;
            if(levelBytes>available)throw std::runtime_error("Truncated DDS block data");
            available-=levelBytes;
        }
        resource.kind=ResourceKind::Dds;
        return detail::decodeImage(factory,resource);
    }
    // Legacy A8R8G8B8 uses straight alpha. Only the first mip is needed.
    if(u32(80)!=0x41||u32(88)!=32||u32(92)!=0x00ff0000||u32(96)!=0x0000ff00||
       u32(100)!=0x000000ff||u32(104)!=0xff000000)
        throw std::runtime_error("Unsupported DDS layout (expected 2D A8R8G8B8)");
    const std::size_t rowBytes=resource.width*4;
    const std::size_t pitch=(u32(8)&0x8)?u32(20):rowBytes;
    if(pitch<rowBytes||pitch>(bytes.size()-128)/resource.height)
        throw std::runtime_error("Invalid or truncated DDS pixel rows");
    Bytes output(static_cast<std::size_t>(pixels)*4);
    for(unsigned y=0;y<resource.height;++y) {
        const auto* source=bytes.data()+128+y*pitch;
        auto* dest=output.data()+y*rowBytes;
        for(std::size_t x=0;x<rowBytes;x+=4) {
            const unsigned alpha=source[x+3];
            for(std::size_t i=0;i<3;++i)dest[x+i]=static_cast<BYTE>((source[x+i]*alpha+127)/255);
            dest[x+3]=static_cast<BYTE>(alpha);
        }
    }
    return output;
}

Bytes decodeTga(SkinResource& resource,const skin::Limits& limits,std::uint64_t remaining,bool& hasAlpha) {
    const auto& bytes=*resource.bytes;
    constexpr std::size_t headerSize=18;
    if(bytes.size()<headerSize)throw std::runtime_error("Truncated TGA header");
    const unsigned depth=bytes[16], descriptor=bytes[17], attributes=descriptor&15;
    const bool rle=bytes[2]==10;
    if(bytes[1]!=0||(bytes[2]!=2&&!rle)||(depth!=24&&depth!=32)||(descriptor&0xc0)||
       (attributes!=0&&(depth!=32||attributes!=8)))
        throw std::runtime_error("Unsupported TGA layout (expected 24-bit or 32-bit true color)");
    hasAlpha=attributes==8;
    resource.width=unsigned(bytes[12])|(unsigned(bytes[13])<<8);
    resource.height=unsigned(bytes[14])|(unsigned(bytes[15])<<8);
    const auto count=std::uint64_t(resource.width)*resource.height;
    if(!count||resource.width>limits.imageDimension||resource.height>limits.imageDimension||
       count>limits.imagePixels||count*4>remaining)
        throw std::runtime_error("TGA image dimensions or decoded byte limit exceeded");
    const std::size_t pixelBytes=depth/8;
    std::size_t input=headerSize+bytes[0];
    if(input>bytes.size()||(!rle&&count>(bytes.size()-input)/pixelBytes))
        throw std::runtime_error("Truncated TGA image data");
    Bytes output(static_cast<std::size_t>(count)*4);
    for(std::size_t pixel=0;pixel<count;) {
        unsigned packet=0;
        if(rle) {
            if(input==bytes.size())throw std::runtime_error("Truncated TGA packet header");
            packet=bytes[input++];
        }
        const auto length=rle?std::size_t((packet&127)+1):static_cast<std::size_t>(count);
        const bool repeated=(packet&128)!=0;
        const auto stored=(repeated?1:length)*pixelBytes;
        if(length>count-pixel||stored>bytes.size()-input)
            throw std::runtime_error("Invalid or truncated TGA pixel packet");
        for(std::size_t i=0;i<length;++i,++pixel) {
            const auto* source=bytes.data()+input+(repeated?0:i*pixelBytes);
            const auto column=pixel%resource.width,row=pixel/resource.width;
            const auto x=(descriptor&0x10)?resource.width-1-column:column;
            const auto y=(descriptor&0x20)?row:resource.height-1-row;
            auto* dest=output.data()+(y*resource.width+x)*4;
            const unsigned alpha=hasAlpha?source[3]:255;
            for(std::size_t c=0;c<3;++c)dest[c]=static_cast<BYTE>((source[c]*alpha+127)/255);
            dest[3]=static_cast<BYTE>(alpha);
        }
        input+=stored;
    }
    return output;
}

void applyColorKey(Bytes& pixels,const ColorKey& key,const ColorKey* previous=nullptr) {
    for(std::size_t i=0;i<pixels.size();i+=4) {
        auto* p=pixels.data()+i;
        if(previous&&p[3]==0) {
            // The cached RGB variant lost only the exact color it keyed out.
            p[0]=static_cast<BYTE>((*previous)[2]);
            p[1]=static_cast<BYTE>((*previous)[1]);
            p[2]=static_cast<BYTE>((*previous)[0]);
            p[3]=255;
        }
        if(p[2]==key[0]&&p[1]==key[1]&&p[0]==key[2])std::fill_n(p,4,BYTE{0});
    }
}
}

struct PreviewScene::Impl {
    bool stretch{};
    struct Image {
        unsigned width, height;
        Bytes pixels;
        ComPtr<ID2D1Bitmap1> bitmap;
    };
    struct Layer {
        std::size_t image;
        ImageSource source;
        ImageGrid grid;
        ComPtr<ID2D1Effect> color;
        ComPtr<IDWriteTextFormat> textFormat;
        int font=-1; // Loaded #LR2FONT for a text layer, or -1 for the system font.
        // A select button without a picture: only its click area and state matter.
        bool transparent=false;
    };
    // Glyph sheets are decoded and uploaded the first time a character on them is drawn.
    struct LoadedFont {
        Font font;
        std::unordered_map<int,Image> sheets;
        std::unordered_set<int> unusable;
        ComPtr<ID2D1Effect> tint;
    };
    static constexpr std::size_t maxFontSheets=96;
    std::vector<std::optional<LoadedFont>> fonts;
    std::size_t fontSheets{},fontBytes{};
    struct BarTitleLayout { int font=-1; Destination key{}; };
    std::array<std::optional<BarTitleLayout>,2> barTitles;
    ID2D1Bitmap1* fontSheet(LoadedFont& loaded,int index);
    unsigned width{}, height{};
    int sceneMilliseconds{},startInputMilliseconds{},closeMilliseconds{},fadeoutMilliseconds{};
    // Result #STARTINPUT's second and third values.
    int resultRankMilliseconds{},resultUpdateMilliseconds{};
    int skipMilliseconds{},loadStartMilliseconds{},loadEndMilliseconds{},playStartMilliseconds{};
    std::array<int,2> scratchSide{};
    bool resultScreen{},decideScreen{};
    std::size_t noteOrder{};
    bool hasBga{},hasLaneCover{};
    std::optional<ScreenData> screen;
    ComPtr<ID2D1SolidColorBrush> textBrush;
    // Blend 3 and above: a white texel inverts the target under a sprite; blend 4 multiplies
    // the sprite with a copy of the target.
    ComPtr<ID2D1Bitmap1> white, backdrop;
    ComPtr<ID2D1Effect> placement, multiply;
    std::array<std::optional<std::size_t>,10> barBodies{},barLamps{},barLevels{},barRanks{};
    std::vector<std::size_t> barFlashes;
    std::array<std::array<std::optional<Destination>,maxSelectBars>,2> barPositions{};
    int barCenter=-1,firstClickable{},lastClickable{};
    std::size_t barOrder{};
    std::vector<SelectControl> controls;
    SelectBar controlClip;
    ComPtr<IDWriteTextFormat> barText;
    ComPtr<ID2D1Bitmap1> banner;
    Bytes bannerPixels;
    UINT bannerWidth{},bannerHeight{};
    bool bannerUploadFailed{};
    std::filesystem::path bannerPath;
    ComPtr<ID2D1Bitmap> bga;
    std::uint64_t bgaRevision{};
    struct Note { std::size_t image; ImageGrid grid; int cycle,timer; };
    // Indexed by SevenMania lane; lanes the key mode lacks are empty.
    std::optional<std::array<PlayLane,16>> lanes;
    std::array<bool,16> usedLanes{};
    int playType=0;
    std::array<std::array<std::optional<Note>,playNoteParts>,16> notes,autoNotes;
    bool autoplay{};
    std::vector<Image> images;
    std::vector<Layer> layers;
    ComPtr<ID2D1DeviceContext> context;
};

ID2D1Bitmap1* PreviewScene::Impl::fontSheet(LoadedFont& loaded,int index) {
    if(!context||loaded.unusable.contains(index))return nullptr;
    const auto upload=[&](Image& image) -> ID2D1Bitmap1* {
        if(image.bitmap)return image.bitmap.Get();
        const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        if(FAILED(context->CreateBitmap(D2D1::SizeU(image.width,image.height),image.pixels.data(),
            image.width*4,&properties,&image.bitmap)))loaded.unusable.insert(index);
        return image.bitmap.Get();
    };
    if(const auto found=loaded.sheets.find(index);found!=loaded.sheets.end())return upload(found->second);
    loaded.unusable.insert(index); // Cleared below on success, so a bad sheet is tried once.
    try {
        auto file=loaded.font.textureFile(index);
        if(!file)return nullptr;
        const skin::Limits limits;
        SkinResource resource;
        resource.path=loaded.font.textures[static_cast<std::size_t>(index)];
        resource.bytes=std::make_shared<Bytes>(std::move(*file));
        std::string extension=resource.path.size()>=4?resource.path.substr(resource.path.size()-4):std::string{};
        for(char& c:extension)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
        Bytes pixels;
        bool hasAlpha=true;
        if(extension==".tga")pixels=decodeTga(resource,limits,limits.totalDecodedImageBytes,hasAlpha);
        else {
            ComPtr<IWICImagingFactory> factory;
            if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))))return nullptr;
            detail::imageInfo(resource,limits,true);
            pixels=detail::decodeImage(factory.Get(),resource,&hasAlpha);
        }
        if(!resource.width||!resource.height||pixels.size()<std::size_t(resource.width)*resource.height*4)return nullptr;
        // A long song list can touch many sheets of a CJK font; start over rather than grow without bound.
        constexpr std::size_t fontBudget=32u<<20;
        if(pixels.size()>fontBudget)return nullptr;
        if(fontSheets>=maxFontSheets||fontBytes>fontBudget-pixels.size()) {
            for(auto& font:fonts)if(font)font->sheets.clear();
            fontSheets=fontBytes=0;
        }
        auto& sheet=loaded.sheets.emplace(index,Image{resource.width,resource.height,std::move(pixels),{}}).first->second;
        loaded.unusable.erase(index);
        fontBytes+=sheet.pixels.size();
        ++fontSheets;
        return upload(sheet);
    } catch(const std::exception&) {
        return nullptr;
    }
}

PreviewScene::PreviewScene() : impl_(std::make_unique<Impl>()) {}
PreviewScene::~PreviewScene() = default;
std::size_t PreviewScene::imageCount() const noexcept {return impl_->images.size();}
std::size_t PreviewScene::layerCount() const noexcept {return impl_->layers.size();}
int PreviewScene::buttonStates(int id) const noexcept {
    int fewest=0;
    for(const auto& layer:impl_->layers) {
        const auto& source=layer.source;
        if(source.kind!=LayerKind::Button||source.parameters[0]!=id)continue;
        const int count=layer.grid.columns*layer.grid.rows;
        if(count<=1)continue; // one picture for every state
        const int states=source.parameters[4]>0?source.parameters[4]:count;
        if(states>0&&(fewest==0||states<fewest))fewest=states;
    }
    return fewest;
}
bool PreviewScene::hasBga() const noexcept {return impl_->hasBga;}
bool PreviewScene::hasLaneCover() const noexcept {return impl_->hasLaneCover;}
void PreviewScene::setStretch(bool stretch) noexcept {
    if(impl_->stretch==stretch)return;
    impl_->stretch=stretch;
    impl_->controls.clear();
}
int PreviewScene::sceneMilliseconds() const noexcept {return impl_->sceneMilliseconds;}
int PreviewScene::startInputMilliseconds() const noexcept {return impl_->startInputMilliseconds;}
ResultInput PreviewScene::resultInput() const noexcept {
    return {impl_->startInputMilliseconds,impl_->resultRankMilliseconds,impl_->resultUpdateMilliseconds};
}
int PreviewScene::skipMilliseconds() const noexcept {return impl_->skipMilliseconds;}
int PreviewScene::loadStartMilliseconds() const noexcept {return impl_->loadStartMilliseconds;}
int PreviewScene::loadEndMilliseconds() const noexcept {return impl_->loadEndMilliseconds;}
int PreviewScene::playStartMilliseconds() const noexcept {return impl_->playStartMilliseconds;}
int PreviewScene::scratchSide(int player) const noexcept {return player==0||player==1?impl_->scratchSide[std::size_t(player)]:0;}
int PreviewScene::closeMilliseconds() const noexcept {return impl_->closeMilliseconds;}
int PreviewScene::fadeoutMilliseconds() const noexcept {return impl_->fadeoutMilliseconds;}

double PlayFrame::timer(int id,double sceneMilliseconds) const noexcept {
    if(id<=0)return sceneMilliseconds;
    switch(id) {
        case 1: return sceneMilliseconds;
        case 2: return fadeoutAge;
        case 3: return failedAge;
        case 48: return fullComboAge;
        case 40: return readyAge;
        case 41: return playAge;
        case 42: return gaugeIncreaseAge;
        case 44: return gaugeMaxAge;
        case 46: return judgementAge;
        case 47: return judgementAge2P;
        case 446: return combo>0?judgementAge:-1;
        case 447: return combo>0?judgementAge2P:-1;
        case 140: return rhythmAge;
        case 150: case 151: case 152: return result?sceneMilliseconds:-1;
    }
    if(id>=50&&id<=69)return bomb[id-50];
    if(id>=70&&id<=89)return hold[id-70];
    if(id>=100&&id<=119)return keyOn[id-100];
    if(id>=120&&id<=139)return keyOff[id-120];
    return -1;
}

std::optional<float> PlayFrame::graph(int type) const noexcept {
    const auto share=[](double part,double whole){return whole>0?float(std::clamp(part/whole,0.,1.)):0.f;};
    switch(type) {
        case 1: return share(elapsed,elapsed+remaining); // Position in the song.
        case 2: return loadProgress; // Loading before the song; 1 once it is over.
        case 10: return share(exScore,2.*noteCount); // Score so far against the song's maximum.
        case 11: return share(exScore,2.*judgedNotes); // The same pace kept to the end.
        // Best and target scores: 12/14 where they stand at this point of the song, 13/15 at the end.
        case 12: return share(bestExScore.value_or(0)*judgedShare(),2.*noteCount);
        case 13: return share(bestExScore.value_or(0),2.*noteCount);
        case 14: return share(targetExScore*judgedShare(),2.*noteCount);
        case 15: return share(targetExScore,2.*noteCount);
    }
    return std::nullopt;
}

std::optional<bool> PlayFrame::option(int id) const noexcept {
    if(id>=150&&id<=155)return difficulty==id-150;
    // Rank bands are ninths of the maximum, counted in 27ths: F below 6, then E, D, C, B, A, AA, AAA.
    if((id>=200&&id<=207)||(id>=340&&id<=347)) {
        // Rank of the score so far, measured against the notes already judged.
        constexpr int bounds[]{0,6,9,12,15,18,21,24,28};
        const int rank=7-(id>=340?id-340:id-200);
        const long long earned=27LL*exScore,possible=2LL*judgedNotes;
        if(possible<=0)return rank==0;
        return earned>=bounds[rank]*possible&&earned<bounds[rank+1]*possible;
    }
    if(id>=220&&id<=227) {
        // Rank already secured even if every remaining note is missed.
        constexpr int bounds[]{24,21,18,15,12,9,6,0};
        return noteCount>0&&27LL*exScore>=bounds[id-220]*2LL*noteCount;
    }
    if(id>=230&&id<=240)return static_cast<int>(std::clamp(gauge,0.,100.)/10)==id-230;
    switch(id) {
        case 30: return !bgaExtended;
        case 31: return bgaExtended;
        case 32: return !autoplay;
        case 33: return autoplay;
        case 38: return !scoreGraph;
        case 39: return scoreGraph;
        // 54/55 and 56/57: auto scratch off/on for 1P and 2P; SevenMania has no auto scratch.
        case 80: return !loaded;
        case 81: return loaded;
        case 34: case 50: case 54: case 56: case 82: return true;
        case 35: case 36: case 37: case 51: case 55: case 57: case 83: case 84: return false;
        case 160: return keys==7;
        case 161: return keys==5;
        case 162: return keys==14;
        case 163: return keys==10;
        case 164: return keys==9;
        case 40: return !bgaEnabled;
        case 41: return bgaEnabled;
        case 42: return !hardGauge;
        case 43: return hardGauge;
        case 170: return !hasBga;
        case 171: return hasBga;
        case 172: return !hasLongNotes;
        case 173: return hasLongNotes;
        case 176: return minBpm==maxBpm;
        case 177: return minBpm!=maxBpm;
        case 241: case 242: case 243: case 244: case 245: case 246: return judge==246-id;
        case 247: case 248: case 249: case 250: case 251: case 252: return judge2P==252-id;
        case 270: return false;
    }
    return std::nullopt;
}

std::optional<bool> SelectFrame::option(int id) const noexcept {
    if(id>=21&&id<=26)return panel==id-20;
    if(id>=100&&id<=105)return songSelected&&clearLamp==id-100;
    if(id>=150&&id<=155)return songSelected&&difficulty==id-150;
    if(id>=160&&id<=164) {
        constexpr int keyCounts[]{7,5,14,10,9};
        return songSelected&&keys==keyCounts[id-160];
    }
    if(id>=180&&id<=183)return songSelected&&judgeRank==id-180;
    // 110 AAA down to 117 F: the best score rank; a chart without a score has none of them.
    if(id>=110&&id<=117)return songSelected&&rank==118-id;
    // 70-74: that difficulty's level fits the level graph; 75-79: it is beyond the graph.
    if(id>=70&&id<=79) {
        const bool beyond=songSelected&&folderLevels[static_cast<std::size_t>((id-70)%5)]>levelLimit();
        return beyond==(id>=75);
    }
    // Charts of each difficulty beside the selected one: none, any, exactly one, several.
    if(id>=500&&id<=519) {
        const int count=songSelected?folderCharts[static_cast<std::size_t>((id-500)%5)]:0;
        switch((id-500)/5) {
            case 0: return count==0;
            case 1: return count>0;
            case 2: return count==1;
            default: return count>1;
        }
    }
    // No internet ranking and no rival: the "idle" side of each pair is the true one.
    if(id>=601&&id<=608)return false;
    // Rival lamps and ranks, and the course stages: SevenMania has neither.
    if((id>=640&&id<=645)||(id>=650&&id<=657)||(id>=580&&id<=584)||(id>=590&&id<=594))return false;
    // Options of the best play. SevenMania has no random, assist or lane effect choice to record, so a
    // played chart was played without them; the gauge is the best clear's when it is known.
    if(id>=118&&id<=123)return songSelected&&bestGauge==id-118; // Groove, survival, death, easy, p-attack, g-attack.
    if(id>=126&&id<=131)return songSelected&&clearLamp>0&&id==126;
    if(id>=134&&id<=137)return songSelected&&clearLamp>0&&id==134;
    if(id>=143&&id<=145)return false; // Extra mode, double battle, SP to DP.
    // The score is always saved; the clear lamp a clear could reach follows the gauge.
    if(id>=60&&id<=66) {
        switch(id) {
            case 61: return true;
            case 63: return gaugeCell==3;
            case 64: return gaugeCell==0;
            case 65: return gaugeCell==1;
            default: return false;
        }
    }
    switch(id) {
        case 38: return !scoreGraph;
        case 39: return scoreGraph;
        case 40: return !bgaEnabled;
        case 41: return bgaEnabled;
        case 42: return gaugeCell!=1;
        case 43: return gaugeCell==1;
        // No song text viewer and no replays.
        case 174: case 196: return true;
        case 175: case 197: return false;
        case 1: return !songSelected;
        case 2: case 5: return songSelected;
        case 3: case 4: return false; // Courses.
        case 10: case 11: case 12: case 13: return false; // Double and battle play.
        case 170: return !(songSelected&&hasBga);
        case 171: return songSelected&&hasBga;
        case 172: return !(songSelected&&hasLongNotes);
        case 173: return songSelected&&hasLongNotes;
        case 176: return !(songSelected&&bpmChange);
        case 177: return songSelected&&bpmChange;
        case 184: return false;
        case 192: return !hasBanner;
        case 193: return hasBanner;
        case 620: case 624: return true;
        case 621: case 625: return false;
    }
    return std::nullopt;
}

std::optional<int> SelectFrame::recordNumber(int id) const noexcept {
    if(!songSelected)return std::nullopt;
    const auto rate=[this](int value) {return record.noteCount>0?std::optional(static_cast<int>(100LL*value/record.noteCount)):std::nullopt;};
    switch(id) {
        case 71: return record.exScore;
        case 73: return record.exScore&&record.noteCount>0?std::optional(static_cast<int>(100LL*(*record.exScore)/(2LL*record.noteCount))):std::nullopt;
        case 75: return record.maxCombo;
        case 76: return record.missCount;
        case 77: return record.playCount;
        case 78: return record.clearCount;
        case 79: return record.playCount&&record.clearCount?std::optional(*record.playCount-*record.clearCount):std::nullopt;
    }
    if(id>=80&&id<=89&&record.judgements) {
        const int count=(*record.judgements)[static_cast<std::size_t>((id-80)%5)];
        return id<85?std::optional(count):rate(count);
    }
    return std::nullopt;
}

std::optional<int> SelectFrame::totalNumber(int id) const noexcept {
    const auto clamp=[](long long value){return static_cast<int>(std::clamp<long long>(value,0,std::numeric_limits<int>::max()));};
    switch(id) {
        case 30: return clamp(totals.playCount);
        case 31: return clamp(totals.clearCount);
        case 32: return clamp(totals.playCount-totals.clearCount);
    }
    if(id>=33&&id<=37)return clamp(totals.judgements[static_cast<std::size_t>(id-33)]);
    return std::nullopt;
}

std::optional<int> SelectFrame::buttonState(int id) const noexcept {
    if(selectPanelButton(id))return panel==id?1:0;
    switch(id) {
        case 11: return keyMode;
        case 40: return gaugeCell;
        case 46: return laneCover>0?1:0;     // Lane cover off / on.
        case 50: return laneCover>0?2:0;     // Off, hidden, sudden, hidden+sudden: the cover is a sudden.
        case 70: return scoreGraph?1:0;
        case 72: return bgaEnabled?0:2;      // On, autoplay only, off.
        case 75: return autoJudge?1:0;
        case 80: return screenMode?1:0;      // Window, fullscreen.
        case 55: return hiSpeedFix;          // Off, start, max, main, min BPM, then SevenMania's constant.
        case 12: case 13: case 14: case 15: case 16: case 57: case 74: return 0;
    }
    // Features SevenMania lacks, held at their first ("none") cell: difficulty filter, text viewer,
    // tag reset, replay, effects/EQ/pitch, 2P and battle options, random, assist, flip, HS-FIX,
    // turntable side, ghost, BGA size, targets, replay saving, IR, help pages and the course editor.
    // The main help (200) launcher stays unsupported: its picture looks usable.
    if(id==10||(id>=17&&id<=29)||id==32||id==33||(id>=41&&id<=45)||id==51||id==54||id==56||id==58||
       id==71||id==73||id==76||id==77||id==83||(id>=92&&id<=96)||(id>=201&&id<=206)||id==210||
       (id>=230&&id<=234)||(id>=240&&id<=243)||id==253||(id>=260&&id<=268))
        return 0;
    return std::nullopt;
}

bool SelectFrame::clickableButton(int id) noexcept {
    return selectPanelButton(id)||id==11||id==12||id==13||id==14||id==15||id==16||id==40||id==46||id==50||
        id==55||id==57||id==70||id==72||id==74||id==75;
}

bool SelectFrame::isOptionText(int ref) noexcept {
    return ref==1||ref==60||(ref>=63&&ref<=68)||(ref>=70&&ref<=76)||ref==79||ref==80||ref==84||ref==85;
}

std::wstring SelectFrame::optionText(int ref) const {
    const auto pick=[](int index,std::initializer_list<const wchar_t*> names) {
        return index>=0&&static_cast<std::size_t>(index)<names.size()?std::wstring(names.begin()[index]):std::wstring();
    };
    switch(ref) {
        case 1: return L"MY BEST"; // The target is the best score.
        case 60: return pick(keyMode,{L"ALL",L"5KEYS",L"7KEYS",L"10KEYS",L"14KEYS",L"9KEYS"});
        case 65: case 66: return gaugeCell==1?L"HARD":gaugeCell==3?L"EASY":L"NORMAL";
        case 71: return scoreGraph?L"ON":L"OFF";
        case 73: return laneCover>0?L"ON":L"OFF";
        case 75: return L"NORMAL";
        case 76: return bgaEnabled?L"ON":L"OFF";
        case 79: return pick(screenMode,{L"WINDOW",L"BORDERLESS",L"FULLSCREEN"});
        case 80: return pick(autoJudge,{L"OFF",L"SUGGEST",L"REALTIME"});
        case 84: return laneCover>0?L"SUDDEN":L"OFF";
        case 74: return pick(hiSpeedFix,{L"OFF",L"START BPM",L"MAX BPM",L"MAIN BPM",L"MIN BPM",L"CONSTANT"});
    }
    // Random, assist, flip and ghost for either side: SevenMania has none of them.
    return isOptionText(ref)?L"OFF":L"";
}

std::wstring SelectFrame::text(int ref) const {
    if(ref>=10&&ref<=16)return song.songText[static_cast<std::size_t>(ref-10)];
    return optionText(ref);
}

std::optional<int> SelectFrame::settingNumber(int id) const noexcept {
    if(id==10)return static_cast<int>(std::lround(hiSpeed*100));
    if(id==12)return judgeTiming;
    return std::nullopt;
}

std::optional<float> SelectFrame::fixedSlider(int id) noexcept {
    if((id>=10&&id<=16)||id==26)return .5f; // EQ bands and pitch at their centre.
    if(id>=20&&id<=25)return 0.f;          // Effect parameters.
    return std::nullopt;
}

std::optional<float> SelectFrame::graph(int type) const noexcept {
    int value=-1;
    if(type==3)value=level;
    else if(type>=5&&type<=9)value=folderLevels[static_cast<std::size_t>(type-5)];
    else return std::nullopt;
    if(!songSelected||value<=0)return 0.f;
    return float(std::min(value,levelLimit()))/float(levelLimit());
}

std::optional<int> PlayFrame::number(int id) const noexcept {
    if(id>=21&&id<=26) {
        const std::time_t now=std::time(nullptr);
        std::tm local{};
        if(localtime_s(&local,&now))return std::nullopt;
        const int parts[]{local.tm_year+1900,local.tm_mon+1,local.tm_mday,local.tm_hour,local.tm_min,local.tm_sec};
        return parts[id-21];
    }
    switch(id) {
        case 76: case 177: return judgementCounts[3]+judgementCounts[4];
        case 120: return result?bestPoints:std::nullopt;
        case 121: return result?bestExScore:std::nullopt;
        case 122: case 123: {
            if(!result||!bestExScore)return std::nullopt;
            const int rate=noteCount?static_cast<int>(10000LL*(*bestExScore)/(2LL*noteCount)):0;
            return id==122?rate/100:rate%100;
        }
        case 125: return result&&bestMaxCombo>=0?std::optional(bestMaxCombo):std::nullopt;
        case 126: return result&&bestMaxCombo>=0?std::optional(noteCount):std::nullopt;
        case 130: case 131: case 132: case 133: case 134:
            return result&&bestJudgements?std::optional((*bestJudgements)[static_cast<std::size_t>(id-130)]):std::nullopt;
        case 176: return result?bestMissCount:std::nullopt;
        case 115: case 116: {
            const int rate=noteCount?static_cast<int>(10000LL*exScore/(2LL*noteCount)):0;
            return id==115?rate/100:rate%100;
        }
        case 150: case 170: return result?bestExScore:std::nullopt;
        case 152: case 172:
            if(result&&bestExScore)return exScore-*bestExScore;
            return std::nullopt;
        case 171: return exScore;
        // The target score, and how far the score so far is ahead of the target's pace.
        case 151: return targetExScore;
        case 108: return exScore-static_cast<int>(std::lround(targetExScore*judgedShare()));
        case 173: return bestMaxCombo;
        case 174: return maxCombo;
        case 175: return maxCombo-std::max(0,bestMaxCombo);
        case 10: return static_cast<int>(hiSpeed*100+.5);
        case 14: return laneCover*10;
        case 90: return static_cast<int>(maxBpm+.5);
        case 91: return static_cast<int>(minBpm+.5);
        case 71: case 101: return exScore;
        case 72: return noteCount*2;
        case 100: return points;
        case 102: case 103: {
            const int rate=noteCount?static_cast<int>(10000LL*exScore/(2LL*noteCount)):0;
            return id==102?rate/100:rate%100;
        }
        case 104: return combo;
        case 75: case 105: return maxCombo;
        case 74: case 106: return noteCount;
        case 107: return static_cast<int>(gauge/2)*2;
        case 110: case 111: case 112: case 113: case 114: return judgementCounts[id-110];
        case 160: return static_cast<int>(bpm+.5);
        case 161: return static_cast<int>(elapsed)/60;
        case 162: return static_cast<int>(elapsed)%60;
        case 163: return static_cast<int>(remaining)/60;
        case 164: return static_cast<int>(remaining)%60;
    }
    return std::nullopt;
}

namespace {
// LR2 blends: 0 and 1 alpha, 2 add, 3 subtract, 4 multiply, 9 and 10 invert what is under the sprite.
constexpr bool supportedBlend(int blend) noexcept {return (blend>=0&&blend<=4)||blend==9||blend==10;}
}
bool PreviewScene::load(const Document& document,std::vector<Diagnostic>& diagnostics,bool play,const ScreenData* screen) {
    auto next=std::make_unique<Impl>();
    next->stretch=impl_->stretch;
    if(screen)next->screen=*screen;
    const bool decide=document.header.type==static_cast<int>(ScreenType::Decide);
    const bool resultScreen=document.header.type==static_cast<int>(ScreenType::Result);
    next->resultScreen=resultScreen;
    next->decideScreen=decide;
    for(const auto& command:document.commands) {
        if(!command.enabled||command.fields.size()<2)continue;
        int* duration=nullptr;
        int limit=30000;
        if((decide||resultScreen)&&command.name=="SCENETIME")duration=&next->sceneMilliseconds;
        else if((decide||resultScreen)&&command.name=="STARTINPUT") {
            duration=&next->startInputMilliseconds;
            // Result: when the score update shows and how long it stays before a key leaves. Empty is 0.
            for(std::size_t field=2;resultScreen&&field<=3&&field<command.fields.size();++field) {
                const auto& text=command.fields[field];
                if(text.empty())continue;
                int value{};
                const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
                if(error==std::errc{}&&end==text.data()+text.size()&&value>=0&&value<=limit)
                    (field==2?next->resultRankMilliseconds:next->resultUpdateMilliseconds)=value;
                else diagnostics.push_back({command.source,command.line,"Invalid STARTINPUT duration",false});
            }
        }
        else if(command.name=="FADEOUT"&&(play||resultScreen||decide||document.header.type==static_cast<int>(ScreenType::Select))) {
            duration=&next->fadeoutMilliseconds;limit=10000;
        }
        else if(play&&command.name=="CLOSE") {duration=&next->closeMilliseconds;limit=10000;}
        else if(decide&&command.name=="SKIP")duration=&next->skipMilliseconds;
        else if(play&&command.name=="LOADSTART")duration=&next->loadStartMilliseconds;
        else if(play&&command.name=="LOADEND")duration=&next->loadEndMilliseconds;
        else if(play&&command.name=="PLAYSTART")duration=&next->playStartMilliseconds;
        else if(play&&command.name=="SCRATCHSIDE") {
            // 1P and 2P scratch side: 0 left, 1 right. Empty fields keep the left side.
            for(std::size_t player=0;player<2&&player+1<command.fields.size();++player) {
                const auto& field=command.fields[player+1];
                if(field.empty())continue;
                int value{};
                const auto [end,error]=std::from_chars(field.data(),field.data()+field.size(),value);
                if(error==std::errc{}&&end==field.data()+field.size()&&(value==0||value==1))next->scratchSide[player]=value;
                else diagnostics.push_back({command.source,command.line,"Invalid SCRATCHSIDE value",false});
            }
            continue;
        }
        if(!duration)continue;
        const auto& field=command.fields[1];
        int value{};
        const auto [end,error]=std::from_chars(field.data(),field.data()+field.size(),value);
        if(error==std::errc{}&&end==field.data()+field.size()&&value>=0&&value<=limit)*duration=value;
        else diagnostics.push_back({command.source,command.line,"Invalid "+command.name+" duration",false});
    }
    next->width=document.header.width;
    next->height=document.header.height;
    if(!next->width||!next->height)return false;
    auto model=imageModel(document,&diagnostics,play);
    if(!model)return false;
    const auto warn=[&](const Diagnostic& location,std::string message) {
        diagnostics.push_back({location.source,location.line,std::move(message),false});
    };
    ComPtr<IWICImagingFactory> factory;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))) {
        diagnostics.push_back({{},0,"Cannot initialize WIC image decoder",true});
        return false;
    }
    const skin::Limits limits;
    std::uint64_t encodedBytes=0, decodedBytes=0;
    // A failed image still owns its index. Cache failures as well as successful decodes.
    struct CachedImage {
        std::optional<std::size_t> first;
        std::map<ColorKey,std::size_t> keyed;
    };
    std::map<std::pair<std::filesystem::path,bool>,CachedImage> paths;
    struct Slot {bool visited{}; std::optional<std::size_t> image;};
    const auto stageIndex=model->images.size();
    const bool select=document.header.type==static_cast<int>(ScreenType::Select);
    if(select) {
        next->barCenter=model->barCenter;
        next->firstClickable=model->firstClickable;
        next->lastClickable=model->lastClickable;
        next->barPositions=model->selectBarPositions;
    }
    if(screen&&!screen->stageImage.empty())model->images.push_back({"Stage image",{}, {}});
    std::vector<Slot> slots(model->images.size());
    auto loadImage=[&](std::size_t index)->std::optional<std::size_t> {
        auto& slot=slots[index];
        if(slot.visited)return slot.image;
        slot.visited=true;
        const auto& [name,location,key]=model->images[index];
        try {
            const bool stage=index==stageIndex;
            const auto path=stage?screen->stageImage:resolveWildcardAsset(document.root,location.source,name,document.header.files);
            auto [entry,inserted]=paths.try_emplace(std::pair{path,stage});
            auto& cached=entry->second;
            const auto checkBudget=[&](std::uint64_t size) {
                if(next->images.size()>=limits.files)throw std::runtime_error("Image count limit exceeded");
                if(size>limits.totalDecodedImageBytes-decodedBytes)
                    throw std::runtime_error("Decoded image budget exceeded");
            };
            if(inserted) {
                auto extension=path.extension().wstring();
                for(auto& c:extension)if(c>=L'A'&&c<=L'Z')c+=L'a'-L'A';
                if(extension!=L".png"&&extension!=L".jpg"&&extension!=L".jpeg"&&extension!=L".bmp"&&extension!=L".dds"&&extension!=L".tga")
                    throw std::runtime_error("Unsupported image format (expected PNG, JPEG, BMP, DDS or TGA)");
                std::error_code ec;
                const auto size=std::filesystem::file_size(path,ec);
                if(ec||size==0||size>limits.fileBytes||size>limits.totalBytes-encodedBytes)
                    throw std::runtime_error("Image file is empty, unreadable or exceeds byte limits");
                checkBudget(0);
                auto bytes=std::make_shared<Bytes>(static_cast<std::size_t>(size));
                std::ifstream input(path,std::ios::binary);
                if(!input.read(reinterpret_cast<char*>(bytes->data()),static_cast<std::streamsize>(size)))
                    throw std::runtime_error("Cannot read image bytes");
                encodedBytes+=size;
                SkinResource resource;
                resource.path=detail::utf8(path.stem().wstring()+extension);
                resource.bytes=std::move(bytes);
                Bytes pixels;
                bool hasAlpha=true;
                if(extension==L".dds")pixels=decodeDds(factory.Get(),resource,limits,limits.totalDecodedImageBytes-decodedBytes);
                else if(extension==L".tga")pixels=decodeTga(resource,limits,limits.totalDecodedImageBytes-decodedBytes,hasAlpha);
                else {
                    detail::imageInfo(resource,limits,true);
                    checkBudget(std::uint64_t(resource.width)*resource.height*4);
                    pixels=detail::decodeImage(factory.Get(),resource,&hasAlpha);
                }
                if(stage)hasAlpha=true;
                if(!hasAlpha)applyColorKey(pixels,key);
                const auto imageIndex=next->images.size();
                if(!hasAlpha)cached.keyed.emplace(key,imageIndex);
                next->images.push_back({resource.width,resource.height,std::move(pixels),{}});
                decodedBytes+=next->images.back().pixels.size();
                cached.first=imageIndex;
            }
            if(!cached.first)return std::nullopt;
            if(cached.keyed.empty())slot.image=cached.first;
            else if(const auto variant=cached.keyed.find(key);variant!=cached.keyed.end())slot.image=variant->second;
            else {
                const auto& [previous,baseIndex]=*cached.keyed.begin();
                const auto& base=next->images[baseIndex];
                checkBudget(base.pixels.size());
                auto pixels=base.pixels;
                applyColorKey(pixels,key,&previous);
                const auto imageIndex=next->images.size();
                next->images.push_back({base.width,base.height,std::move(pixels),{}});
                decodedBytes+=next->images.back().pixels.size();
                cached.keyed.emplace(key,imageIndex);
                slot.image=imageIndex;
            }
        } catch(const detail::Failure& error) {
            warn(location,"IMAGE "+std::to_string(index)+" ("+name+"): "+error.error.message);
        } catch(const std::exception& error) {
            warn(location,"IMAGE "+std::to_string(index)+" ("+name+"): "+error.what());
        }
        return slot.image;
    };

    std::optional<std::size_t> solidImages[2];
    ComPtr<IDWriteFactory> textFactory;
    ComPtr<IDWriteFontCollection> systemFonts;
    // A #FONT declaration picks the face and weight: its first installed face, else Segoe UI.
    const auto makeTextFormat=[&](DWRITE_TEXT_ALIGNMENT align,const SystemFontDeclaration* declared=nullptr) {
        ComPtr<IDWriteTextFormat> format;
        if(!textFactory&&FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
                    reinterpret_cast<IUnknown**>(textFactory.GetAddressOf()))))return format;
        std::wstring face=L"Segoe UI";
        auto weight=DWRITE_FONT_WEIGHT_NORMAL;
        if(declared) {
            if(!systemFonts)textFactory->GetSystemFontCollection(&systemFonts);
            for(const auto& name:declared->faces) {
                const int n=MultiByteToWideChar(CP_UTF8,0,name.data(),int(name.size()),nullptr,0);
                if(n<=0)continue;
                std::wstring candidate(std::size_t(n),L'\0');
                MultiByteToWideChar(CP_UTF8,0,name.data(),int(name.size()),candidate.data(),n);
                UINT32 at{};BOOL exists=FALSE;
                if(systemFonts&&SUCCEEDED(systemFonts->FindFamilyName(candidate.c_str(),&at,&exists))&&exists){face=candidate;break;}
            }
            // DX Library thickness 0-9; -1 is its default (6).
            const int thickness=declared->thickness<0?6:std::clamp(declared->thickness,0,9);
            weight=static_cast<DWRITE_FONT_WEIGHT>(std::clamp(thickness*100,100,900));
            if(thickness==6)weight=DWRITE_FONT_WEIGHT_NORMAL;
        }
        if(SUCCEEDED(textFactory->CreateTextFormat(face.c_str(),nullptr,weight,
            DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,32.f,L"",&format))) {
            format->SetTextAlignment(align);
            format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        return format;
    };
    // Fonts are read only when a text object uses them: a title font archive is several megabytes.
    next->fonts.resize(model->fonts.size());
    std::vector<bool> fontTried(model->fonts.size());
    const auto useFont=[&](int index) {
        if(index<0||static_cast<std::size_t>(index)>=model->fonts.size())return -1;
        const auto slot=static_cast<std::size_t>(index);
        if(!fontTried[slot]) {
            fontTried[slot]=true;
            const auto& declaration=model->fonts[slot];
            if(auto font=loadFont(document.root,declaration.location.source,declaration.path))
                next->fonts[slot]=Impl::LoadedFont{std::move(*font),{},{},{}};
            else warn(declaration.location,"LR2FONT unavailable; using the system font: "+declaration.path);
        }
        return next->fonts[slot]?index:-1;
    };
    for(std::size_t order=0;order<model->sources.size();++order) {
        auto& source=model->sources[order];
        if(model->noteOrder==order)next->noteOrder=next->layers.size();
        if(model->barOrder==order)next->barOrder=next->layers.size();
        const auto& location=source.location;
        const int index=source.values[1];
        const bool bga=source.kind==LayerKind::Bga;
        const bool solid=index==110||index==111;
        const bool text=source.kind==LayerKind::Text;
        const bool body=source.kind==LayerKind::BarBody;
        const bool lamp=source.kind==LayerKind::BarLamp;
        const bool banner=select&&source.kind==LayerKind::Image&&index==102;
        const bool stage=source.kind==LayerKind::Image&&index==100&&screen&&stageIndex<slots.size();
        // No picture: an empty rectangle, or a 1-pixel placeholder too small for its cells.
        const bool transparent=select&&source.kind==LayerKind::Button&&(source.values[4]==0||source.values[5]==0||
            (source.values[4]==1&&source.values[6]>1)||(source.values[5]==1&&source.values[7]>1));
        if(source.destinations.empty()&&!body) {warn(location,"SRC_IMAGE has no valid DST_IMAGE"); continue;}
        if(source.kind==LayerKind::BarGraph&&!(select&&SelectFrame{}.graph(source.parameters[0]))&&
           !(play&&PlayFrame{}.graph(source.parameters[0]))) {
            warn(location,"Unsupported bar graph: "+std::to_string(source.parameters[0]));continue;
        }
        if(source.kind==LayerKind::Number&&!(select&&(source.parameters[0]==96||selectSystemNumber(source.parameters[0])||
                                                      SelectFrame::isSettingNumber(source.parameters[0])||
                                                      selectFolderNumber(source.parameters[0])||
                                                      SelectFrame::isRecordNumber(source.parameters[0])||
                                                      SelectFrame::isTotalNumber(source.parameters[0])))&&
           !(resultScreen&&(resultExComparison(source.parameters[0])||resultTargetNumber(source.parameters[0])))&&
           !PlayFrame{}.number(source.parameters[0])) {
            warn(location,"Unsupported number reference: "+std::to_string(source.parameters[0]));continue;
        }
        if(source.kind==LayerKind::Button) {
            const int id=source.parameters[0];
            if(!selectButton(id)) {
                warn(location,"Unsupported select button: "+std::to_string(id));continue;
            }
        }
        if(source.kind==LayerKind::Slider) {
            const int id=source.parameters[2];
            // In play the song position (6) and the 1P/2P lane cover (4/5) move a slider.
            if(play?id!=6&&id!=4&&id!=5:id!=1&&!selectVolumeSlider(id)&&!SelectFrame::fixedSlider(id)) {
                warn(location,std::string(play?"Unsupported play slider: ":"Unsupported select slider: ")+std::to_string(id));continue;
            }
        }
        if(!bga&&!solid&&!transparent&&!text&&!stage&&!banner&&(index<0||index>=100||std::size_t(index)>=source.imageCount)) {
            warn(location,"Unsupported or undeclared SRC_IMAGE index: "+std::to_string(index)); continue;
        }
        if(!supportedBlend(source.blend)) {
            warn(location,"Unsupported DST_IMAGE blend: "+std::to_string(source.blend)); continue;
        }
        if(!(play?usesPlayClock(source.timer):usesPreviewClock(source.timer))&&
           !(select&&selectTimer(source.timer))&&!(resultScreen&&resultTimer(source.timer))&&!(decide&&decideTimer(source.timer))) {
            warn(location,"Unsupported destination timer: "+std::to_string(source.timer)); continue;
        }
        bool visible=true;
        for(const int condition:source.conditions) {
            if(!condition)continue;
            if(condition==std::numeric_limits<int>::min()) {visible=false; break;}
            if(select&&SelectFrame{}.option(std::abs(condition)).has_value())continue;
            if(play&&PlayFrame{}.option(std::abs(condition)).has_value())continue;
            const auto found=document.optionValues.find(std::abs(condition));
            if(found==document.optionValues.end()) {
                warn(location,"DST_IMAGE condition unavailable in preview: "+std::to_string(condition));
                visible=false;
            } else if(found->second!=(condition>0))visible=false;
        }
        if(!visible)continue;
        if(next->layers.size()>=limits.elements) {
            warn(location,"Preview image layer limit exceeded");
            break;
        }
        std::optional<std::size_t> image;
        if(text) {
            const int ref=source.values[2];
            if(!((ref>=10&&ref<=16)||(select&&SelectFrame::isOptionText(ref)))||
               (!select&&(!screen||screen->songText[ref-10].empty())))continue;
            const int font=useFont(source.values[1]);
            const int fontIndex=source.values[1];
            const auto* declared=font<0&&fontIndex>=0&&std::size_t(fontIndex)<model->systemFonts.size()?&model->systemFonts[std::size_t(fontIndex)]:nullptr;
            auto format=makeTextFormat(source.values[3]==1?DWRITE_TEXT_ALIGNMENT_CENTER:
                source.values[3]==2?DWRITE_TEXT_ALIGNMENT_TRAILING:DWRITE_TEXT_ALIGNMENT_LEADING,declared);
            if(!format)continue;
            next->layers.push_back({0,std::move(source),{0,0,1,1,1,1},{},std::move(format)});
            next->layers.back().font=font;
            continue;
        }
        if(source.kind==LayerKind::GaugeChart) {
            // A line drawn from the recorded gauge; the picture only gives its thickness.
            if(document.header.type!=static_cast<int>(ScreenType::Result)||source.parameters[0]<=0||source.parameters[1]<=0) {
                warn(location,"Unsupported gauge chart");continue;
            }
            next->layers.push_back({0,std::move(source),{0,0,1,1,1,1},{}});
            continue;
        }
        if(bga||banner) {
            if(bga)next->hasBga=true;
            next->layers.push_back({0,std::move(source),{0,0,1,1,1,1},{}});
            continue;
        }
        if(solid||transparent) {
            auto& slot=solidImages[transparent?1:index-110];
            if(!slot) {
                const BYTE gray=!transparent&&index==110?0:255;
                next->images.push_back({1,1,{gray,gray,gray,255},{}});
                slot=next->images.size()-1;
            }
            image=slot;
            source.values[8]=source.values[9]=0;
        } else image=loadImage(stage?stageIndex:static_cast<std::size_t>(index));
        if(!image) {warn(location,"SRC_IMAGE refers to unavailable IMAGE "+std::to_string(index)); continue;}
        const auto& pixels=next->images[*image];
        const auto grid=solid||transparent?std::optional(ImageGrid{0,0,1,1,1,1}):
            imageGrid(source,static_cast<int>(pixels.width),static_cast<int>(pixels.height));
        if(!grid) {warn(location,"SRC_IMAGE rectangle or sprite grid exceeds image bounds"); continue;}
        if(!(play?usesPlayClock(source.values[9]):usesPreviewClock(source.values[9]))&&
           !(select&&selectTimer(source.values[9]))&&!(resultScreen&&resultTimer(source.values[9]))&&
           !(decide&&decideTimer(source.values[9]))) {
            warn(location,"SRC_IMAGE gameplay timer is inactive; showing its first sprite frame");
            source.values[8]=0;
        } else if(!play&&!select) {
            // Decide and result timers the screen really runs; any other (1, 40, 41) follows the scene clock.
            const auto screenTimer=[&](int timer){return timer<=0||(resultScreen&&resultTimer(timer))||(decide&&decideTimer(timer));};
            if(!screenTimer(source.timer)||!screenTimer(source.values[9]))warn(location,"Gameplay timer simulated with the preview clock");
        }
        if((body||lamp)&&source.values[0]>=0&&source.values[0]<10)
            (body?next->barBodies:next->barLamps)[source.values[0]]=next->layers.size();
        if(source.kind==LayerKind::BarLevel||source.kind==LayerKind::BarRank)
            (source.kind==LayerKind::BarLevel?next->barLevels:next->barRanks)[static_cast<std::size_t>(source.values[0])]=next->layers.size();
        if(source.kind==LayerKind::BarFlash)next->barFlashes.push_back(next->layers.size());
        if(play&&source.kind==LayerKind::Slider&&(source.parameters[2]==4||source.parameters[2]==5))next->hasLaneCover=true;
        next->layers.push_back({*image,std::move(source),*grid,{}});
        next->layers.back().transparent=transparent;
    }
    if(select) {
        if(next->barCenter<0||!model->barOrder||
           (!next->barPositions[0][next->barCenter]&&!next->barPositions[1][next->barCenter])||
           !next->barBodies[0]) {
            diagnostics.push_back({{},0,"Missing required select bar body, center or destination",true});
            return false;
        }
        next->barText=makeTextFormat(DWRITE_TEXT_ALIGNMENT_LEADING);
        if(!next->barText)return false;
        for(std::size_t type=0;type<model->barTitles.size();++type) {
            const auto& title=model->barTitles[type];
            if(!title.destination)continue;
            if(const int font=useFont(title.font);font>=0)next->barTitles[type]=Impl::BarTitleLayout{font,*title.destination};
        }
        ComPtr<IDWriteInlineObject> ellipsis;
        if(SUCCEEDED(textFactory->CreateEllipsisTrimmingSign(next->barText.Get(),&ellipsis))) {
            const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER,0,0};
            next->barText->SetTrimming(&trimming,ellipsis.Get());
        }
        if(model->barOrder==model->sources.size())next->barOrder=next->layers.size();
    }
    if(!model->noteOrder||model->noteOrder==model->sources.size())next->noteOrder=next->layers.size();
    if(play) {
        const int type=document.header.type;
        std::array<PlayLane,16> lanes{};
        // Lanes the key mode lacks (5KEY's last two keys, 7KEY's 2P side) stay empty at the right edge.
        std::optional<PlayLane> edge;
        for(std::size_t lane=0;lane<lanes.size();++lane) {
            const int mapped=lr2Lane(type,static_cast<int>(lane));
            if(mapped<0)continue;
            const auto& dst=model->tapDestinations[static_cast<std::size_t>(mapped)];
            if(!dst||(*dst)[1]<0||(*dst)[2]<0||
                std::int64_t((*dst)[1])+(*dst)[3]>next->width||
                std::int64_t((*dst)[2])+(*dst)[4]>next->height) {
                diagnostics.push_back({{},0,"Missing or out-of-bounds DST_NOTE lane "+std::to_string(mapped),true});
                return false;
            }
            const auto& key=*dst;
            lanes[lane]={key[1],key[1]+key[3],key[2]+key[4],key[4]};
            next->usedLanes[lane]=true;
            if(!edge||lanes[lane].right>=edge->right)edge=lanes[lane];
        }
        if(!edge) {
            diagnostics.push_back({{},0,"Unsupported play screen type "+std::to_string(type),true});
            return false;
        }
        for(std::size_t lane=0;lane<lanes.size();++lane)
            if(!next->usedLanes[lane])lanes[lane]={edge->right,edge->right,edge->noteBottom,edge->noteHeight};
        next->lanes=lanes;
        next->playType=type;
        for(std::size_t lane=0;lane<lanes.size();++lane) {
          if(!next->usedLanes[lane])continue;
          const auto mapped=static_cast<std::size_t>(lr2Lane(type,static_cast<int>(lane)));
          for(std::size_t part=0;part<playNoteParts;++part) {
            // Autoplay parts are optional; a missing or unusable one falls back to the usual part.
            if(const auto& source=model->autoNoteSources[mapped][part];source&&source->values[1]>=0&&
               source->values[1]<100&&std::size_t(source->values[1])<source->imageCount) {
                if(const auto image=loadImage(static_cast<std::size_t>(source->values[1]))) {
                    const auto& asset=next->images[*image];
                    if(const auto grid=imageGrid(*source,int(asset.width),int(asset.height)))
                        next->autoNotes[lane][part]=Impl::Note{*image,*grid,source->values[8],source->values[9]};
                }
            }
            const auto& source=model->noteSources[mapped][part];
            if(!source&&part!=static_cast<std::size_t>(PlayNotePart::Tap))continue;
            std::optional<std::size_t> image;
            if(source&&source->values[1]>=0&&source->values[1]<100&&
                std::size_t(source->values[1])<source->imageCount)
                image=loadImage(static_cast<std::size_t>(source->values[1]));
            if(image) {
                const auto& asset=next->images[*image];
                if(const auto grid=imageGrid(*source,int(asset.width),int(asset.height))) {
                    next->notes[lane][part]=Impl::Note{*image,*grid,source->values[8],source->values[9]};
                    continue;
                }
            }
            warn(source?source->location:Diagnostic{},"Using native note for LR2 lane "+std::to_string(mapped));
          }
        }
    }
    if(next->layers.empty()&&!play) {
        diagnostics.push_back({{},0,"No supported IMAGE/SRC_IMAGE/DST_IMAGE layers were loaded",true});
        return false;
    }
    impl_=std::move(next);
    return true;
}

std::optional<std::array<PlayLane,16>> PreviewScene::playLanes(int width,int height) const noexcept {
    if(!impl_->lanes||width<=0||height<=0)return std::nullopt;
    auto lanes=*impl_->lanes;
    const auto viewport=skinViewport(width,height,impl_->width,impl_->height,impl_->stretch);
    const float sx=viewport.scale*viewport.stretchX,sy=viewport.scale*viewport.stretchY;
    const float dx=impl_->stretch?0.f:viewport.x,dy=impl_->stretch?0.f:viewport.y;
    for(std::size_t index=0;index<lanes.size();++index) {
        auto& lane=lanes[index];
        lane.left=int(std::lround(dx+lane.left*sx));
        lane.right=int(std::lround(dx+lane.right*sx));
        lane.noteBottom=int(std::lround(dy+lane.noteBottom*sy));
        lane.noteHeight=std::max(1,int(std::lround(lane.noteHeight*sy)));
        if(impl_->usedLanes[index]&&lane.right<=lane.left)return std::nullopt;
    }
    return lanes;
}

NoteSprite PreviewScene::noteSprite(int lane,double milliseconds,PlayNotePart part,double heldMilliseconds) const noexcept {
    const auto index=static_cast<std::size_t>(part);
    if(lane<0||lane>=16||index>=playNoteParts)return {};
    const auto& usual=impl_->notes[lane];
    const auto& automatic=impl_->autoNotes[lane];
    const bool useAuto=impl_->autoplay&&(automatic[index]||(part==PlayNotePart::HeldBody&&
        automatic[static_cast<std::size_t>(PlayNotePart::Body)]));
    const auto& parts=useAuto?automatic:usual;
    const auto& note=parts[index]||part!=PlayNotePart::HeldBody?parts[index]:parts[static_cast<std::size_t>(PlayNotePart::Body)];
    if(!note)return {};
    // Hold timers are 70 plus the LR2 lane (70-79 1P, 80-89 2P).
    const double elapsed=note->timer<=0?milliseconds:note->timer==70+lr2Lane(impl_->playType,lane)?heldMilliseconds:-1;
    const auto& grid=note->grid;
    const int count=grid.columns*grid.rows;
    const int frame=note->cycle>0&&elapsed>=0&&std::isfinite(elapsed)?
        std::min(count-1,int(std::fmod(elapsed,double(note->cycle))*count/note->cycle)):0;
    return {impl_->images[note->image].bitmap.Get(),float(grid.x+frame%grid.columns*grid.width),
        float(grid.y+frame/grid.columns*grid.height),float(grid.width),float(grid.height)};
}

void PreviewScene::setAutoplay(bool autoplay) noexcept {impl_->autoplay=autoplay;}

void PreviewScene::releaseDevice() {
    impl_->controls.clear();
    for(auto& font:impl_->fonts)if(font){
        for(auto& entry:font->sheets)entry.second.bitmap.Reset();
        font->unusable.clear();
        font->tint.Reset();
    }
    impl_->bga.Reset();
    impl_->banner.Reset();
    impl_->bannerUploadFailed=false;
    impl_->textBrush.Reset();
    impl_->white.Reset();
    impl_->backdrop.Reset();
    impl_->placement.Reset();
    impl_->multiply.Reset();
    for(auto& layer:impl_->layers)layer.color.Reset();
    for(auto& image:impl_->images)image.bitmap.Reset();
    impl_->context.Reset();
}

int PreviewScene::selectCenter() const noexcept {return impl_->barCenter;}

void SelectControl::unrotate(float& px,float& py) const noexcept {
    px=(px-viewportOffsetX)/viewportScaleX;
    py=(py-viewportOffsetY)/viewportScaleY;
    const auto inverse=D2D1::Matrix3x2F::Rotation(-angle,D2D1::Point2F(pivotX,pivotY));
    const auto point=inverse.TransformPoint(D2D1::Point2F(px,py));
    px=point.x;
    py=point.y;
}

bool SelectControl::contains(float px,float py) const noexcept {
    unrotate(px,py);
    return px>=x+std::min(0.f,dx)&&px<x+width+std::max(0.f,dx)&&
           py>=y+std::min(0.f,dy)&&py<y+height+std::max(0.f,dy);
}

float SelectControl::sliderPosition(float px,float py) const noexcept {
    unrotate(px,py);
    const float position=dx!=0.f?(px-x-width*.5f)/dx:dy!=0.f?(py-y-height*.5f)/dy:0.f;
    return std::clamp(position,0.f,1.f);
}

bool SelectControl::increment(float px,float py) const noexcept {
    unrotate(px,py);
    // LR2's "plusonly" field: 1 always adds; 0 subtracts on the left half and adds on the right.
    if(clickMode>0)return true;
    if(clickMode<0)return py<y+height*.5f;
    return px>=x+width*.5f;
}

std::optional<SelectControl> PreviewScene::selectControlAt(float x,float y) const noexcept {
    const auto& clip=impl_->controlClip;
    if(x<clip.x||y<clip.y||x>=clip.x+clip.width||y>=clip.y+clip.height)return std::nullopt;
    for(auto it=impl_->controls.rbegin();it!=impl_->controls.rend();++it)
        if(it->contains(x,y))return *it;
    return std::nullopt;
}

std::optional<SelectBar> PreviewScene::fittedSelectBar(int slot,int width,int height) const noexcept {
    if(impl_->barCenter<0||slot<0||slot>=maxSelectBars||width<=0||height<=0)return std::nullopt;
    const auto& active=impl_->barPositions[slot==impl_->barCenter][slot];
    const auto& key=active?active:impl_->barPositions[slot!=impl_->barCenter][slot];
    if(!key||(*key)[3]<=0||(*key)[4]<=0)return std::nullopt;
    const float scale=std::min(float(width)/impl_->width,float(height)/impl_->height);
    return SelectBar{(width-impl_->width*scale)*.5f+(*key)[1]*scale,
        (height-impl_->height*scale)*.5f+(*key)[2]*scale,(*key)[3]*scale,(*key)[4]*scale,
        slot>=impl_->firstClickable&&slot<=impl_->lastClickable};
}

std::optional<SelectBar> PreviewScene::selectBar(int slot,int width,int height) const noexcept {
    auto bar=fittedSelectBar(slot,width,height);
    if(bar&&impl_->stretch) {
        const auto viewport=skinViewport(width,height,impl_->width,impl_->height,true);
        bar->x=(bar->x-viewport.x)*viewport.stretchX;
        bar->y=(bar->y-viewport.y)*viewport.stretchY;
        bar->width*=viewport.stretchX;
        bar->height*=viewport.stretchY;
    }
    return bar;
}

bool PreviewScene::updateSelectBanner(const std::filesystem::path& path) {
    if(!impl_->context)return false;
    const auto upload=[&]{
        if(impl_->bannerPixels.empty()||impl_->bannerUploadFailed)return false;
        const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        impl_->bannerUploadFailed=FAILED(impl_->context->CreateBitmap(D2D1::SizeU(impl_->bannerWidth,impl_->bannerHeight),
            impl_->bannerPixels.data(),impl_->bannerWidth*4,&properties,&impl_->banner));
        return !impl_->bannerUploadFailed;
    };
    if(path==impl_->bannerPath)return impl_->banner!=nullptr||upload();
    impl_->bannerPath=path;
    impl_->banner.Reset();
    impl_->bannerUploadFailed=false;
    impl_->bannerPixels.clear();
    if(path.empty())return false;
    std::error_code error;
    const auto bytes=std::filesystem::file_size(path,error);
    if(error||!bytes||bytes>skin::Limits{}.fileBytes)return false;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> pixels;
    UINT width{},height{};
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))||
       FAILED(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder))||
       FAILED(decoder->GetFrame(0,&frame))||FAILED(frame->GetSize(&width,&height))||
       !width||!height||width>2048||height>2048||
       FAILED(factory->CreateFormatConverter(&pixels))||
       FAILED(pixels->Initialize(frame.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return false;
    impl_->bannerPixels.resize(std::size_t(width)*height*4);
    if(FAILED(pixels->CopyPixels(nullptr,width*4,static_cast<UINT>(impl_->bannerPixels.size()),impl_->bannerPixels.data()))) {
        impl_->bannerPixels.clear();
        return false;
    }
    impl_->bannerWidth=width;impl_->bannerHeight=height;
    return upload();
}

ID2D1Bitmap* PreviewScene::updateBga(const std::uint32_t* pixels,int width,int height,std::uint64_t revision) {
    if(!impl_->context||!pixels||width<=0||height<=0)return nullptr;
    const auto size=D2D1::SizeU(width,height);
    if(impl_->bga) {
        const auto oldSize=impl_->bga->GetPixelSize();
        if(oldSize.width!=size.width||oldSize.height!=size.height)impl_->bga.Reset();
    }
    HRESULT result=S_OK;
    if(!impl_->bga) {
        result=impl_->context->CreateBitmap(size,pixels,size.width*4,
            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE)),
            &impl_->bga);
    } else if(revision!=impl_->bgaRevision)
        result=impl_->bga->CopyFromMemory(nullptr,pixels,size.width*4);
    if(FAILED(result))return nullptr;
    impl_->bgaRevision=revision;
    return impl_->bga.Get();
}

bool PreviewScene::prepare(ID2D1DeviceContext* target) {
    if(!target)return false;
    if(impl_->context.Get()==target)return true;
    releaseDevice();
    const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
    for(auto& image:impl_->images) {
        if(FAILED(target->CreateBitmap(D2D1::SizeU(image.width,image.height),image.pixels.data(),
                                      image.width*4,&properties,&image.bitmap))) {
            releaseDevice();
            return false;
        }
    }
    for(auto& layer:impl_->layers) {
        if(layer.source.kind==LayerKind::Bga||
           (impl_->barCenter>=0&&layer.source.kind==LayerKind::Image&&layer.source.values[1]==102)||
           layer.source.kind==LayerKind::BarBody)continue;
        if(layer.textFormat||layer.source.kind==LayerKind::GaugeChart) {
            if(!impl_->textBrush&&FAILED(target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White),&impl_->textBrush))) {
                releaseDevice();
                return false;
            }
            continue;
        }
        const bool colored=std::any_of(layer.source.destinations.begin(),layer.source.destinations.end(),
            [](const Destination& key){return key[7]!=255||key[8]!=255||key[9]!=255;});
        if(!colored&&layer.source.blend<=1)continue;
        if(FAILED(target->CreateEffect(CLSID_D2D1ColorMatrix,&layer.color)) ||
           FAILED(layer.color->SetValue(D2D1_COLORMATRIX_PROP_ALPHA_MODE,D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED))) {
            releaseDevice();
            return false;
        }
        layer.color->SetInput(0,impl_->images[layer.image].bitmap.Get());
    }
    if(impl_->barText&&!impl_->textBrush&&
       FAILED(target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White),&impl_->textBrush))) {
        releaseDevice();
        return false;
    }
    const bool blends=std::any_of(impl_->layers.begin(),impl_->layers.end(),[](const Impl::Layer& layer){return layer.source.blend>=3;});
    if(blends) {
        const std::uint32_t texel=0xffffffffu;
        const auto opaque=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        if(FAILED(target->CreateBitmap(D2D1::SizeU(1,1),&texel,4,&opaque,&impl_->white))||
           FAILED(target->CreateEffect(CLSID_D2D12DAffineTransform,&impl_->placement))||
           FAILED(target->CreateEffect(CLSID_D2D1Blend,&impl_->multiply))||
           FAILED(impl_->multiply->SetValue(D2D1_BLEND_PROP_MODE,D2D1_BLEND_MODE_MULTIPLY))) {
            releaseDevice();
            return false;
        }
        impl_->multiply->SetInputEffect(1,impl_->placement.Get());
    }
    impl_->context=target;
    return true;
}

void PreviewScene::draw(ID2D1DeviceContext* target,double milliseconds,int width,int height,
                        const PlayFrame* play,DrawPass pass,const SelectFrame* select,const ScreenTimers& timers) const {
    if(select)impl_->controls.clear();
    if(!target||impl_->context.Get()!=target||width<=0||height<=0)return;
    const auto viewport=skinViewport(width,height,impl_->width,impl_->height,impl_->stretch);
    const float scale=viewport.scale,w=viewport.width,h=viewport.height;
    const float offsetX=viewport.x,offsetY=viewport.y;
    const float translateX=impl_->stretch?viewport.translationX():0.f;
    const float translateY=impl_->stretch?viewport.translationY():0.f;
    if(select)impl_->controlClip=impl_->stretch?SelectBar{0,0,float(width),float(height)}:SelectBar{offsetX,offsetY,w,h};
    D2D1_MATRIX_3X2_F previous{};
    target->GetTransform(&previous);
    using D2D1::Matrix3x2F;
    const auto output=Matrix3x2F::Scale(viewport.stretchX,viewport.stretchY)*Matrix3x2F::Translation(translateX,translateY);
    const auto transform=[&](const Matrix3x2F& matrix) {target->SetTransform(matrix*output);};
    transform(Matrix3x2F::Identity());
    target->PushAxisAlignedClip(D2D1::RectF(offsetX,offsetY,offsetX+w,offsetY+h),D2D1_ANTIALIAS_MODE_ALIASED);
    const auto visible=[&](const ImageSource& source) {
        for(int id:source.conditions) {
            const int option=std::abs(id);
            if(play) {
                if(const auto value=play->option(option);value&&*value!=(id>0))return false;
            }
            if(select) {
                if(const auto enabled=select->option(option);enabled&&*enabled!=(id>0))return false;
            }
        }
        return true;
    };
    const auto screenTimer=[&](int timer) {
        if(impl_->resultScreen&&timer==2)return timers.fadeout;
        if(impl_->resultScreen&&timer==152)return timers.updateScore.value_or(milliseconds);
        if(impl_->decideScreen&&timer==2)return timers.fadeout;
        if(impl_->decideScreen&&timer==3)return timers.skip;
        if(select&&timer==2)return select->fadeoutAge;
        if(select&&selectReadmeTimer(timer))return -1.;
        if(select&&panelTimer(timer))return timer<30?select->panelOn[timer-21]:select->panelOff[timer-31];
        if(select&&selectKeyTimer(timer))return select->keyOn[static_cast<std::size_t>(timer-100)];
        if(select&&selectCourseTimer(timer))return -1.;
        if(select&&timer==11)return select->selectionAge;
        return milliseconds;
    };
    const auto sample=[&](const Impl::Layer& layer) {
        const auto& source=layer.source;
        const bool judge=source.kind==LayerKind::Judge||source.kind==LayerKind::Combo;
        const double judgeAge=play?(source.side?play->judgementAge2P:play->judgementAge):-1;
        const double time=play?(judge&&source.timer<=0?judgeAge:play->timer(source.timer,milliseconds)):
            screenTimer(source.timer);
        const double spriteTime=play?(judge&&source.values[9]<=0?judgeAge:play->timer(source.values[9],milliseconds)):screenTimer(source.values[9]);
        return sampleImage(source,layer.grid,time,w,h,impl_->width,impl_->height,spriteTime);
    };
    const auto sideJudge=[&](int side){return !play?-1:side?play->judge2P:play->judge;};
    const auto judgeLayer=[&](LayerKind kind,int side) -> const Impl::Layer* {
        if(sideJudge(side)<0)return nullptr;
        const auto match=std::find_if(impl_->layers.begin(),impl_->layers.end(),[&](const auto& layer){
            return layer.source.kind==kind&&layer.source.side==side&&layer.source.values[0]==sideJudge(side)&&visible(layer.source);
        });
        return match==impl_->layers.end()?nullptr:&*match;
    };
    const auto drawable=[](const std::optional<ImageFrame>& frame){
        return frame&&frame->alpha>0&&frame->width>0&&frame->height>0;
    };
    const auto digitCount=[](long long value,int limit){
        int count=1;
        for(value=std::abs(value);value>=10&&count<limit;value/=10)++count;
        return count;
    };
    std::array<const Impl::Layer*,2> judges{};
    std::array<std::optional<ImageFrame>,2> anchors{};
    std::array<float,2> judgeShifts{};
    for(int side=0;side<2;++side) {
        const auto* judge=judges[static_cast<std::size_t>(side)]=judgeLayer(LayerKind::Judge,side);
        const auto& anchor=anchors[static_cast<std::size_t>(side)]=judge?sample(*judge):std::nullopt;
        if(!drawable(anchor)||judge->source.parameters[0]==1||play->combo<=0)continue;
        if(const auto* combo=judgeLayer(LayerKind::Combo,side)) {
            const auto frame=sample(*combo);
            const int cells=combo->grid.columns*combo->grid.rows;
            if(drawable(frame)&&(cells%10==0||cells%11==0)) {
                const int digits=digitCount(play->combo,std::clamp(combo->source.parameters[2],1,15));
                judgeShifts[static_cast<std::size_t>(side)]=std::max(0.f,(frame->width+combo->source.parameters[4]*scale)*digits)*.5f;
            }
        }
    }
    const auto begin=pass==DrawPass::AfterNotes?impl_->noteOrder:0;
    const auto end=pass==DrawPass::BeforeNotes?impl_->noteOrder:impl_->layers.size();
    // Draws with an LR2 bitmap font. Returns false, drawing nothing, when the font lacks a
    // character (LR2 fonts cover Shift_JIS only) so the caller can use the system font.
    const auto drawFontText=[&](int fontIndex,std::wstring_view text,float anchorX,float y,float maxWidth,
                                float size,int align,const D2D1_COLOR_F& color) {
        if(fontIndex<0||static_cast<std::size_t>(fontIndex)>=impl_->fonts.size()||!impl_->fonts[static_cast<std::size_t>(fontIndex)]||
           text.empty()||text.size()>512||size<=0)return false;
        auto& loaded=*impl_->fonts[static_cast<std::size_t>(fontIndex)];
        const auto& font=loaded.font;
        const float glyphScale=size/font.size;
        float total=0;
        for(const wchar_t character:text) {
            const auto* glyph=font.glyph(character);
            if(!glyph||!impl_->fontSheet(loaded,glyph->texture))return false;
            total+=(glyph->width+font.margin)*glyphScale;
        }
        // LR2 narrows text that is wider than its box instead of clipping it.
        const float squeeze=maxWidth>0&&total>maxWidth?maxWidth/total:1.f;
        float x=align==1?anchorX-total*squeeze*.5f:align==2?anchorX-total*squeeze:anchorX;
        const bool tinted=color.r<.999f||color.g<.999f||color.b<.999f;
        if(tinted&&!loaded.tint&&(FAILED(target->CreateEffect(CLSID_D2D1ColorMatrix,&loaded.tint))||
            FAILED(loaded.tint->SetValue(D2D1_COLORMATRIX_PROP_ALPHA_MODE,D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED))))loaded.tint.Reset();
        for(const wchar_t character:text) {
            const auto& glyph=*font.glyph(character);
            auto* sheet=impl_->fontSheet(loaded,glyph.texture);
            const float width=glyph.width*glyphScale*squeeze,height=glyph.height*glyphScale;
            const auto source=D2D1::RectF(float(glyph.x),float(glyph.y),float(glyph.x+glyph.width),float(glyph.y+glyph.height));
            if(sheet&&tinted&&loaded.tint) {
                const D2D1_MATRIX_5X4_F matrix{color.r,0,0,0, 0,color.g,0,0, 0,0,color.b,0, 0,0,0,color.a, 0,0,0,0};
                loaded.tint->SetInput(0,sheet);
                loaded.tint->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX,matrix);
                transform(Matrix3x2F::Scale(width/glyph.width,height/glyph.height)*Matrix3x2F::Translation(x,y));
                const auto origin=D2D1::Point2F();
                target->DrawImage(loaded.tint.Get(),&origin,&source,D2D1_INTERPOLATION_MODE_LINEAR,D2D1_COMPOSITE_MODE_SOURCE_OVER);
            } else if(sheet) {
                transform(Matrix3x2F::Identity());
                target->DrawBitmap(sheet,D2D1::RectF(x,y,x+width,y+height),color.a,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,source);
            }
            x+=(glyph.width+font.margin)*glyphScale*squeeze;
        }
        return true;
    };
    const auto drawBarText=[&](std::wstring_view text,float x,float y,float textWidth,float em,D2D1_COLOR_F color) {
        if(text.empty()||textWidth<=0||em<=0)return;
        const float glyphScale=em/32.f;
        transform(Matrix3x2F::Scale(glyphScale,glyphScale)*Matrix3x2F::Translation(x,y));
        impl_->textBrush->SetColor(color);
        target->DrawText(text.data(),static_cast<UINT32>(text.size()),impl_->barText.Get(),
            D2D1::RectF(0,0,textWidth/glyphScale,44),impl_->textBrush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };
    // One sprite of a layer, stretched to the given size, with the layer's tint, blend and rotation.
    const auto drawSprite=[&](const Impl::Layer& layer,const ImageFrame& frame,float x,float y,float spriteWidth,float spriteHeight,
                              const D2D1_RECT_F& src) {
        if(spriteWidth<=0||spriteHeight<=0||src.right<=src.left||src.bottom<=src.top)return;
        const auto rotation=Matrix3x2F::Rotation(frame.angle,
            D2D1::Point2F(x+spriteWidth*frame.pivotX,y+spriteHeight*frame.pivotY));
        const int blend=layer.source.blend;
        const auto interpolation=layer.source.filter?D2D1_INTERPOLATION_MODE_LINEAR:D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR;
        if(!layer.color) {
            transform(rotation);
            target->DrawBitmap(impl_->images[layer.image].bitmap.Get(),
                D2D1::RectF(x,y,x+spriteWidth,y+spriteHeight),frame.alpha,
                layer.source.filter?D2D1_BITMAP_INTERPOLATION_MODE_LINEAR:D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR,src);
            return;
        }
        // The effect applies the matrix in straight alpha and premultiplies the result.
        const D2D1_MATRIX_5X4_F color{
            frame.red,0,0,0, 0,frame.green,0,0, 0,0,frame.blue,0, 0,0,0,frame.alpha, 0,0,0,0};
        layer.color->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX,color);
        const auto placed=Matrix3x2F::Scale(spriteWidth/(src.right-src.left),spriteHeight/(src.bottom-src.top))*
                             Matrix3x2F::Translation(x,y)*rotation;
        const auto origin=D2D1::Point2F();
        // Inverts the target under the sprite's rectangle; done twice it restores the pixels exactly.
        const auto invert=[&]{
            transform(Matrix3x2F::Scale(spriteWidth,spriteHeight)*Matrix3x2F::Translation(x,y)*rotation);
            const auto unit=D2D1::RectF(0,0,1,1);
            target->DrawImage(impl_->white.Get(),&origin,&unit,D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR,D2D1_COMPOSITE_MODE_MASK_INVERT);
        };
        if(blend==4&&impl_->multiply) {
            // Multiply: the blend effect reads a copy of the target, so the sprite's color scales what is under it.
            ComPtr<ID2D1Image> current;
            ComPtr<ID2D1Bitmap1> drawn;
            target->GetTarget(&current);
            if(current&&SUCCEEDED(current.As(&drawn))) {
                const auto size=drawn->GetPixelSize();
                if(!impl_->backdrop||impl_->backdrop->GetPixelSize().width!=size.width||impl_->backdrop->GetPixelSize().height!=size.height) {
                    impl_->backdrop.Reset();
                    const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
                        drawn->GetPixelFormat(),96,96);
                    target->CreateBitmap(size,nullptr,0,&properties,&impl_->backdrop);
                }
                target->Flush();
                if(impl_->backdrop&&SUCCEEDED(impl_->backdrop->CopyFromBitmap(nullptr,drawn.Get(),nullptr))) {
                    // Only the sprite's source rectangle enters the effect graph.
                    ComPtr<ID2D1Effect> crop;
                    if(SUCCEEDED(target->CreateEffect(CLSID_D2D1Crop,&crop))) {
                        crop->SetInputEffect(0,layer.color.Get());
                        crop->SetValue(D2D1_CROP_PROP_RECT,D2D1::Vector4F(src.left,src.top,src.right,src.bottom));
                        impl_->placement->SetInputEffect(0,crop.Get());
                        impl_->placement->SetValue(D2D1_2DAFFINETRANSFORM_PROP_TRANSFORM_MATRIX,
                            static_cast<D2D1_MATRIX_3X2_F>(Matrix3x2F::Translation(-src.left,-src.top)*placed*output));
                        impl_->placement->SetValue(D2D1_2DAFFINETRANSFORM_PROP_INTERPOLATION_MODE,
                            layer.source.filter?D2D1_2DAFFINETRANSFORM_INTERPOLATION_MODE_LINEAR:D2D1_2DAFFINETRANSFORM_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
                        impl_->multiply->SetInput(0,impl_->backdrop.Get());
                        // Only the device-pixel bounds of the placed sprite are written back.
                        const auto device=placed*output;
                        float left=1e9f,top=1e9f,right=-1e9f,bottom=-1e9f;
                        for(const auto corner:{D2D1::Point2F(x,y),D2D1::Point2F(x+spriteWidth,y),D2D1::Point2F(x,y+spriteHeight),D2D1::Point2F(x+spriteWidth,y+spriteHeight)}) {
                            const auto local=D2D1::Point2F(src.left+(corner.x-x)*(src.right-src.left)/spriteWidth,src.top+(corner.y-y)*(src.bottom-src.top)/spriteHeight);
                            const auto point=device.TransformPoint(local);
                            left=std::min(left,point.x);top=std::min(top,point.y);right=std::max(right,point.x);bottom=std::max(bottom,point.y);
                        }
                        const auto bounds=D2D1::RectF(std::floor(left),std::floor(top),std::ceil(right),std::ceil(bottom));
                        const auto corner=D2D1::Point2F(bounds.left,bounds.top);
                        target->SetTransform(Matrix3x2F::Identity());
                        target->DrawImage(impl_->multiply.Get(),&corner,&bounds,D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR,D2D1_COMPOSITE_MODE_SOURCE_OVER);
                        impl_->multiply->SetInput(0,nullptr);
                        impl_->placement->SetInputEffect(0,nullptr);
                        return;
                    }
                }
            }
        }
        if(blend==3)invert();
        transform(placed);
        const auto mode=blend==2||blend==3?D2D1_COMPOSITE_MODE_PLUS:
            blend==9||blend==10?D2D1_COMPOSITE_MODE_MASK_INVERT:D2D1_COMPOSITE_MODE_SOURCE_OVER;
        target->DrawImage(layer.color.Get(),&origin,&src,interpolation,mode);
        if(blend==3)invert();
    };
    const auto frameSource=[](const ImageFrame& frame) {
        return D2D1::RectF(float(frame.sourceX),float(frame.sourceY),float(frame.sourceX+frame.sourceWidth),
            float(frame.sourceY+frame.sourceHeight));
    };
    // A number from a digit sheet: 10 digits, 11 with a blank, or 24 with signs. age picks the animated set.
    const auto drawDigits=[&](const Impl::Layer& layer,const ImageFrame& frame,float left,float top,long long number,
                              double age,bool combo) {
        const auto& source=layer.source;
        const auto& grid=layer.grid;
        const int count=grid.columns*grid.rows;
        const int setSize=!combo&&count%24==0?24:count%10==0?10:count%11==0?11:0;
        if(!setSize)return;
        const int sets=count/setSize;
        const int cycle=source.values[8];
        int base=cycle>0&&age>=0?setSize*std::min(sets-1,int(std::fmod(age,double(cycle))*sets/cycle)):0;
        if(setSize==24&&number<0)base+=12;
        const int digits=std::clamp(source.parameters[2],1,15)+(setSize==24?1:0);
        const float spacing=frame.width+source.parameters[4]*scale;
        const int align=source.parameters[1];
        std::array<int,16> glyphs{};
        auto magnitude=std::abs(number);
        int first=digits-1;
        for(int slot=digits-1;slot>=(setSize==24?1:0);--slot) {
            if(slot==digits-1||magnitude){glyphs[slot]=int(magnitude%10);first=slot;magnitude/=10;}
            else if(setSize!=10){glyphs[slot]=source.parameters[3]==1?0:10;first=slot;}
            else if(source.parameters[3]==1){glyphs[slot]=0;first=slot;}
        }
        if(setSize==24){first=0;glyphs[0]=11;}
        const float shift=combo&&align==1?(digits+first)*spacing*.5f:
            align==1?first*spacing:align==2?first*spacing*.5f:0;
        for(int slot=first;slot<digits;++slot) {
            const int index=base+glyphs[slot];
            const float sx=float(grid.x+index%grid.columns*grid.width),sy=float(grid.y+index/grid.columns*grid.height);
            drawSprite(layer,frame,left+slot*spacing-shift,top,frame.width,frame.height,
                D2D1::RectF(sx,sy,sx+grid.width,sy+grid.height));
        }
    };
    const auto drawBars=[&]() {
        if(!select||!impl_->barText)return;
        // While the list is moving, each bar is on its way from the neighbouring slot.
        const auto movingBar=[&](int slot) {
            auto bar=fittedSelectBar(slot,width,height);
            if(!bar||select->scroll==0)return bar;
            const auto from=fittedSelectBar(slot+(select->scroll>0?1:-1),width,height);
            if(!from)return bar;
            const float travelled=std::min(1.f,std::abs(select->scroll));
            bar->x=std::lerp(bar->x,from->x,travelled);
            bar->y=std::lerp(bar->y,from->y,travelled);
            return bar;
        };
        // A part placed relative to its bar.
        const auto drawPart=[&](std::size_t index,const SelectBar& bar) {
            const auto& part=impl_->layers[index];
            if(!visible(part.source))return;
            if(const auto frame=sample(part);drawable(frame))
                drawSprite(part,*frame,bar.x+frame->x,bar.y+frame->y,frame->width,frame->height,frameSource(*frame));
        };
        for(int slot=0;slot<maxSelectBars;++slot) {
            const auto& row=select->rows[slot];
            const auto bar=movingBar(slot);
            if(!bar||row.title.empty())continue;
            if(bar->clickable&&row.row>=0) {
                SelectControl control;
                control.x=bar->x;control.y=bar->y;
                control.width=bar->width;control.height=bar->height;
                control.row=row.row;
                impl_->controls.push_back(control);
            }
            const int body=std::clamp(row.body,0,9);
            const auto asset=impl_->barBodies[body].value_or(*impl_->barBodies[0]);
            const auto& layer=impl_->layers[asset];
            const auto& grid=layer.grid;
            transform(Matrix3x2F::Identity());
            target->DrawBitmap(impl_->images[layer.image].bitmap.Get(),
                D2D1::RectF(bar->x,bar->y,bar->x+bar->width,bar->y+bar->height),1,
                D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
                D2D1::RectF(float(grid.x),float(grid.y),float(grid.x+grid.width),float(grid.y+grid.height)));
            const bool selected=slot==impl_->barCenter;
            if(selected)for(const auto flash:impl_->barFlashes)drawPart(flash,*bar);
            if(const auto index=impl_->barLamps[std::clamp(row.lamp,0,9)])drawPart(*index,*bar);
            const auto color=selected?D2D1::ColorF(1.f,.95f,.75f):D2D1::ColorF(D2D1::ColorF::White);
            // The skin's level digits for the chart's difficulty, else its "no difficulty" digits, else text.
            bool levelDrawn=false;
            if(row.levelValue>=0) {
                const auto difficulty=static_cast<std::size_t>(std::clamp(row.difficulty,0,6));
                if(const auto index=impl_->barLevels[difficulty]?impl_->barLevels[difficulty]:impl_->barLevels[0]) {
                    const auto& digits=impl_->layers[*index];
                    levelDrawn=true;
                    if(const auto frame=sample(digits);drawable(frame)&&visible(digits.source))
                        drawDigits(digits,*frame,bar->x+frame->x,bar->y+frame->y,row.levelValue,milliseconds,false);
                }
            }
            if(!levelDrawn)drawBarText(row.level,bar->x+4*scale,bar->y+bar->height*.25f,28*scale,bar->height*.43f,color);
            const float titleLeft=bar->x+34*scale;
            const float titleWidth=std::min(bar->x+bar->width,offsetX+w)-titleLeft-5*scale;
            // The skin's own title font and position when it can spell the title, else the system font.
            const auto& title=impl_->barTitles[0]?impl_->barTitles[0]:impl_->barTitles[1];
            bool drawn=false;
            if(title) {
                const auto& key=title->key;
                const float x=bar->x+key[1]*scale;
                const float room=std::min({key[3]*scale,bar->x+bar->width-x,offsetX+w-x})-5*scale;
                drawn=room>0&&drawFontText(title->font,row.title,x,bar->y+key[2]*scale,room,key[4]*scale,0,
                    D2D1::ColorF(key[7]/255.f,key[8]/255.f,key[9]/255.f,std::clamp(key[6]/255.f,0.f,1.f)));
            }
            if(!drawn)drawBarText(row.title,titleLeft,bar->y+bar->height*.18f,titleWidth,bar->height*.55f,color);
            // Over the title: a long title runs underneath the rank.
            if(const auto index=impl_->barRanks[std::clamp(row.rank,0,8)];index&&row.rank>0)drawPart(*index,*bar);
        }
    };
    for(std::size_t i=begin;i<end;++i) {
        if(select&&i==impl_->barOrder)drawBars();
        const auto& layer=impl_->layers[i];
        const auto& source=layer.source;
        if(source.kind==LayerKind::BarBody||source.kind==LayerKind::BarLamp||source.kind==LayerKind::BarLevel||
           source.kind==LayerKind::BarRank||source.kind==LayerKind::BarFlash)continue;
        if(!visible(source))continue;
        if((source.kind==LayerKind::Judge||source.kind==LayerKind::Combo)&&
           sideJudge(source.side)!=source.values[0])continue;
        auto frame=sample(layer);
        // A select button keeps its click area while fully transparent, as skins use invisible buttons.
        const bool clickArea=select&&source.kind==LayerKind::Button&&frame&&frame->width>0&&frame->height>0;
        if(!drawable(frame)&&!clickArea)continue;
        if(play&&source.turntable)frame->angle+=play->turntableAngle[static_cast<std::size_t>(source.turntable-1)];
        float left=offsetX+frame->x, top=offsetY+frame->y;
        if(select&&source.kind==LayerKind::Image&&source.values[1]==102) {
            if(impl_->banner) {
                transform(Matrix3x2F::Identity());
                target->DrawBitmap(impl_->banner.Get(),D2D1::RectF(left,top,left+frame->width,top+frame->height),
                    frame->alpha,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
            continue;
        }
        if(layer.textFormat) {
            if(!select&&!impl_->screen)continue;
            const auto text=select?select->text(source.values[2]):impl_->screen->songText[source.values[2]-10];
            if(drawFontText(layer.font,text,left,top,frame->width,frame->height,source.values[3],
                D2D1::ColorF(frame->red,frame->green,frame->blue,frame->alpha)))continue;
            auto* brush=impl_->textBrush.Get();
            const float glyphScale=frame->height/32.f;
            const float textAnchor=source.values[3]==1?.5f:source.values[3]==2?1.f:0.f;
            transform(Matrix3x2F::Scale(glyphScale,glyphScale)*
                Matrix3x2F::Translation(left-frame->width*textAnchor,top));
            brush->SetColor(D2D1::ColorF(frame->red,frame->green,frame->blue,frame->alpha));
            target->DrawText(text.data(),static_cast<UINT32>(text.size()),layer.textFormat.Get(),
                D2D1::RectF(0,0,frame->width/glyphScale,48.f),brush,D2D1_DRAW_TEXT_OPTIONS_CLIP);
            continue;
        }
        for(std::size_t side=0;side<judges.size();++side)if(&layer==judges[side])left-=judgeShifts[side];
        if(play&&source.kind==LayerKind::JudgeLine)top+=play->judgeLineOffset/viewport.stretchY;
        if(source.kind==LayerKind::Bga) {
            if(!play||!play->bgaEnabled||!play->bga)continue;
            const auto rotation=Matrix3x2F::Rotation(frame->angle,
                D2D1::Point2F(left+frame->width*frame->pivotX,top+frame->height*frame->pivotY));
            transform(rotation);
            target->DrawBitmap(play->bga,D2D1::RectF(left,top,left+frame->width,top+frame->height),frame->alpha,
                source.filter?D2D1_BITMAP_INTERPOLATION_MODE_LINEAR:D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
            continue;
        }
        const auto sprite=[&](float x,float y,int sx,int sy) {
            drawSprite(layer,*frame,x,y,frame->width,frame->height,
                D2D1::RectF(float(sx),float(sy),float(sx+frame->sourceWidth),float(sy+frame->sourceHeight)));
        };
        const auto cell=[&](float x,float y,int index) {
            const auto& g=layer.grid;
            sprite(x,y,g.x+index%g.columns*g.width,g.y+index/g.columns*g.height);
        };
        if(select&&source.kind==LayerKind::Button) {
            const int id=source.parameters[0];
            const int count=layer.grid.columns*layer.grid.rows;
            // A single picture serves every state.
            const int state=count==1?0:select->buttonState(id).value_or(0);
            const int states=source.parameters[4]>0?source.parameters[4]:count;
            const int cellsPerState=count/states;
            if(!cellsPerState)continue;
            const auto& grid=layer.grid;
            const int animation=(frame->sourceY-grid.y)/grid.height*grid.columns+(frame->sourceX-grid.x)/grid.width;
            const int animationCell=static_cast<int>(static_cast<std::int64_t>(animation)*cellsPerState/count);
            // A value the picture has no cell for draws nothing, but the button stays clickable so the
            // player can move on to a value it does show.
            if(state<states&&!layer.transparent&&frame->alpha>0)cell(left,top,state*cellsPerState+animationCell);
            // The panel field: 0 always, -1 only while no panel is open, 1 or more only with that panel open.
            const int panelField=source.parameters[2];
            const bool panelAllows=panelField==0||(panelField<0?select->panel==0:select->panel==panelField);
            if(source.parameters[1]==1&&panelAllows&&SelectFrame::clickableButton(id)&&((id!=15&&id!=16)||select->songSelected)) {
                impl_->controls.push_back({id,0,source.parameters[3],left,top,frame->width,frame->height,0,0,
                    frame->angle,left+frame->width*frame->pivotX,top+frame->height*frame->pivotY});
            }
        } else if((select||play)&&source.kind==LayerKind::Slider) {
            constexpr std::array<float,4> horizontal{0,1,0,-1},vertical{-1,0,1,0};
            const int direction=source.parameters[0], id=source.parameters[2];
            const float range=source.parameters[1]*scale;
            if(direction<0||direction>=4||range<0)continue;
            const bool volume=select&&selectVolumeSlider(id);
            const auto fixed=select?SelectFrame::fixedSlider(id):std::nullopt;
            const float value=play?(id==4||id==5?std::clamp(play->laneCoverShare,0.f,1.f):play->graph(1).value_or(0.f)):volume?select->volumes[id-17]:fixed?*fixed:select->listPosition;
            const float dx=horizontal[direction]*range,dy=vertical[direction]*range;
            sprite(left+dx*value,top+dy*value,frame->sourceX,frame->sourceY);
            if(select&&range>0&&(volume||(id==1&&select->canScroll))&&source.parameters[3]==0) {
                impl_->controls.push_back({0,id,0,left,top,frame->width,frame->height,dx,dy,
                    0,0,0});
            }
        } else if(source.kind==LayerKind::BarGraph) {
            // The filled part of the picture, stretched over the same share of the destination.
            const auto value=select?select->graph(source.parameters[0]):play?play->graph(source.parameters[0]):std::nullopt;
            if(!value||*value<=0)continue;
            const float filled=std::min(1.f,*value);
            auto src=frameSource(*frame);
            // A negative size in the skin means the bar grows back from the far edge: up, or to the left.
            const auto& last=source.destinations.back();
            if(source.parameters[1]==1) {
                src.top=src.bottom-float(frame->sourceHeight)*filled;
                const float length=frame->height*filled;
                drawSprite(layer,*frame,left,last[4]<0?top+frame->height-length:top,frame->width,length,src);
            } else {
                src.right=src.left+float(frame->sourceWidth)*filled;
                const float length=frame->width*filled;
                drawSprite(layer,*frame,last[3]<0?left+frame->width-length:left,top,length,frame->height,src);
            }
        } else if(source.kind==LayerKind::GaugeChart) {
            const auto* numbers=impl_->screen?&impl_->screen->numbers:nullptr;
            if(!numbers||numbers->gaugeHistory.size()<2||(source.values[0]==1)!=numbers->hardGauge||!impl_->textBrush)continue;
            const auto& history=numbers->gaugeHistory;
            // The line is revealed from left to right between the two times the skin gives.
            const double start=source.parameters[2],finish=source.parameters[3];
            const double shown=finish>start?std::clamp((milliseconds-start)/(finish-start),0.,1.):1.;
            const auto lastPoint=static_cast<std::size_t>(double(history.size()-1)*shown);
            const float fieldWidth=source.parameters[0]*scale,fieldHeight=source.parameters[1]*scale;
            // The destination is the chart's bottom-left corner.
            const auto point=[&](std::size_t index) {
                return D2D1::Point2F(left+fieldWidth*float(index)/float(history.size()-1),
                    top-fieldHeight*std::clamp(history[index],0.f,100.f)/100.f);
            };
            impl_->textBrush->SetColor(D2D1::ColorF(frame->red,frame->green,frame->blue,frame->alpha));
            transform(Matrix3x2F::Identity());
            for(std::size_t index=1;index<=lastPoint&&history[index]>=0;++index)
                target->DrawLine(point(index-1),point(index),impl_->textBrush.Get(),std::max(1.f,frame->width));
        } else if(source.kind==LayerKind::OnMouse) {
            // Shown while the cursor is inside a rectangle given relative to the picture.
            // Panel -1 means "no panel open", 0 any state, otherwise that panel.
            const int panel=source.parameters[0];
            if(!select||(panel<0?select->panel!=0:panel>0&&select->panel!=panel))continue;
            const float hoverX=left+source.parameters[1]*scale,hoverY=top+source.parameters[2]*scale;
            const float mouseX=(select->mouseX-translateX)/viewport.stretchX;
            const float mouseY=(select->mouseY-translateY)/viewport.stretchY;
            if(mouseX<hoverX||mouseY<hoverY||mouseX>=hoverX+source.parameters[3]*scale||
               mouseY>=hoverY+source.parameters[4]*scale)continue;
            sprite(left,top,frame->sourceX,frame->sourceY);
        } else if(source.kind==LayerKind::MeasureLine) {
            if(!play)continue;
            for(float offset:play->measureOffsets)
                sprite(left,top+(offset+play->judgeLineOffset)/viewport.stretchY,frame->sourceX,frame->sourceY);
        } else if(source.kind==LayerKind::Gauge) {
            if(!play)continue;
            const int count=layer.grid.columns*layer.grid.rows;
            if(count<4)continue;
            const int lit=static_cast<int>(std::clamp(play->gauge,0.,100.)/2);
            for(int segment=0;segment<50;++segment) {
                const int color=play->hardGauge||segment>=40?0:1;
                cell(left+segment*source.parameters[0]*scale,top,color+(segment<lit?0:2));
            }
        } else if(source.kind==LayerKind::Number||source.kind==LayerKind::Combo) {
            const bool combo=source.kind==LayerKind::Combo;
            const auto* numbers=play?play:select?&select->song.numbers:impl_->screen?&impl_->screen->numbers:nullptr;
            if(!numbers)continue;
            auto value=combo?std::optional(numbers->combo):numbers->number(source.parameters[0]);
            if(select&&SelectFrame::isSettingNumber(source.parameters[0]))
                value=select->settingNumber(source.parameters[0]);
            else if(select&&SelectFrame::isTotalNumber(source.parameters[0]))
                value=select->totalNumber(source.parameters[0]);
            else if(select&&source.parameters[0]>=57&&source.parameters[0]<=59)
                value=static_cast<int>(std::lround(select->volumes[static_cast<std::size_t>(source.parameters[0]-57)]*100));
            else if(select&&selectFolderNumber(source.parameters[0])) {
                const int folderLevel=select->folderLevels[static_cast<std::size_t>(source.parameters[0]-45)];
                if(!select->songSelected||folderLevel<0)continue;
                value=folderLevel;
            } else if(select&&!selectSystemNumber(source.parameters[0])) {
                if(!select->songSelected)continue;
                if(source.parameters[0]==96&&select->level>=0)value=select->level;
                // The best record's numbers come from the record; play-time counters have no meaning here.
                const int id=source.parameters[0];
                if(SelectFrame::isRecordNumber(id)) {
                    value=select->recordNumber(id);
                    if(!value)continue;
                }
                if((id>=100&&id<=104)||id==107||(id>=110&&id<=116)||id==171||id==174||id==177)continue;
                if(id==105&&select->clearLamp==0)continue;
            }
            if(!combo&&(source.parameters[0]==173||source.parameters[0]==175)&&numbers->bestMaxCombo<0)continue;
            if(!value||(combo&&!*value))continue;
            if(*value<0&&(source.parameters[0]==152||source.parameters[0]==172)&&
               (layer.grid.columns*layer.grid.rows)%24!=0)continue;
            const double age=play&&source.values[9]>0?play->timer(source.values[9],milliseconds):
                combo?(source.side?numbers->judgementAge2P:numbers->judgementAge):milliseconds;
            if(combo) {
                const auto& anchor=anchors[static_cast<std::size_t>(source.side)];
                if(!drawable(anchor))continue;
                left+=anchor->x;top+=anchor->y;
            }
            drawDigits(layer,*frame,left,top,*value,age,combo);
        } else {
            sprite(left,top,frame->sourceX,frame->sourceY);
        }
    }
    if(select&&impl_->barOrder==end)drawBars();
    if(select&&impl_->stretch)for(auto& control:impl_->controls) {
        control.viewportScaleX=viewport.stretchX;control.viewportScaleY=viewport.stretchY;
        control.viewportOffsetX=translateX;control.viewportOffsetY=translateY;
    }
    target->PopAxisAlignedClip();
    target->SetTransform(previous);
}
}
