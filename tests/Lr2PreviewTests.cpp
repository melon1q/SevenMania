#include "skin/lr2/Lr2Preview.h"
#include "skin/lr2/Lr2Diagnostics.h"
#include "skin/lr2/Lr2Font.h"
#include "skin/lr2/Lr2PlayTimeline.h"
#include "SkinRenderTestSupport.h"
#include <d2d1_1.h>
#include <array>
#include <numeric>

using namespace skin_test;
using namespace sevenmania::skin::lr2;

namespace {
struct PreviewSurface : Surface {
    ComPtr<ID2D1DeviceContext> drawing;
    PreviewSurface(unsigned w=640,unsigned h=480) : Surface(w,h,D3D_DRIVER_TYPE_WARP) {
        ComPtr<IDXGIDevice> dxgi;
        ComPtr<IDXGISurface> surface;
        ComPtr<ID2D1Device> d2d;
        ComPtr<ID2D1Bitmap1> bitmap;
        hr(device.As(&dxgi));
        hr(texture.As(&surface));
        hr(D2D1CreateDevice(dxgi.Get(),nullptr,&d2d));
        hr(d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&drawing));
        const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
        hr(drawing->CreateBitmapFromDxgiSurface(surface.Get(),&properties,&bitmap));
        drawing->SetTarget(bitmap.Get());
        drawing->SetDpi(96,96);
    }
    void render(const PreviewScene& scene,double time,const PlayFrame* play=nullptr,const SelectFrame* select=nullptr,const ScreenTimers& timers={}) {
        drawing->BeginDraw();
        drawing->Clear(D2D1::ColorF(0,0,0));
        scene.draw(drawing.Get(),time,width,height,play,DrawPass::All,select,timers);
        hr(drawing->EndDraw());
        context->CopyResource(staging.Get(),texture.Get());
    }
    void pixel(unsigned x,unsigned y,int r,int g,int b,const char* message) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
        const auto* p=static_cast<const BYTE*>(mapped.pData)+y*mapped.RowPitch+x*4;
        const std::array<int,3> actual{p[2],p[1],p[0]};
        context->Unmap(staging.Get(),0);
        if(std::abs(actual[0]-r)>2||std::abs(actual[1]-g)>2||std::abs(actual[2]-b)>2) {
            std::cerr<<"pixel ("<<x<<","<<y<<"): "<<actual[0]<<","<<actual[1]<<","<<actual[2]<<"\n";
            require(false,message);
        }
        require(true,message);
    }
    std::uint64_t frameSignature() {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
        std::uint64_t signature=0;
        for(unsigned y=0;y<height;++y) {
            const auto* row=static_cast<const BYTE*>(mapped.pData)+y*mapped.RowPitch;
            for(unsigned x=0;x<width*4;x+=4)
                signature=(signature^row[x]^(unsigned(row[x+1])<<8)^(unsigned(row[x+2])<<16))*1099511628211ull;
        }
        context->Unmap(staging.Get(),0);
        return signature;
    }
};

const Bytes sheet{
    0,255,0,255, 0,255,0,255, 255,255,255,255, 255,255,255,255,
    0,255,0,255, 0,255,0,255, 255,255,255,255, 255,255,255,255
};
void fixture(const std::filesystem::path& path) {
    const auto assets=path.parent_path().parent_path()/"Shared";
    std::filesystem::create_directories(assets);
    std::filesystem::create_directories(path.parent_path());
    save(assets/"sheet.png",sheet,4,2);
    save(assets/"sheet2.png",Bytes{255,0,0,255},1,1);
    // 1x1 24-bit BMP with padded row, BGRA blue-gray.
    const unsigned char bmp[]{
        'B','M',58,0,0,0,0,0,0,0,54,0,0,0, 40,0,0,0,1,0,0,0,1,0,0,0,
        1,0,24,0,0,0,0,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 64,32,16,0
    };
    std::ofstream(assets/"bg.BMP",std::ios::binary).write(reinterpret_cast<const char*>(bmp),sizeof(bmp));
    std::ofstream(assets/"unsupported.gif",std::ios::binary).put('x');
    std::array<unsigned char,132> dds{};
    const auto field=[&](unsigned offset,std::uint32_t value) {
        for(unsigned i=0;i<4;++i)dds[offset+i]=static_cast<unsigned char>(value>>(i*8));
    };
    field(0,0x20534444); field(4,124); field(8,0x100f); field(12,1); field(16,1); field(20,4);
    field(76,32); field(80,0x41); field(88,32); field(92,0xff0000); field(96,0xff00);
    field(100,0xff); field(104,0xff000000); field(108,0x1000); field(128,0x80ff0000);
    std::ofstream(assets/"red.dds",std::ios::binary).write(reinterpret_cast<const char*>(dds.data()),dds.size());
    std::ofstream(path,std::ios::binary)<<
        "#INFORMATION,0,Phase 2 Graphics Test,SevenMania\n"
        "#CUSTOMFILE,Background,.\\LR2files\\Shared\\*.BMP,bg\n"
        "#IMAGE,LR2files\\Shared\\*.BMP\n"
        "#IMAGE,missing.png\n"
        "#IMAGE,../Shared/sheet.png\n"
        "#IMAGE,LR2files\\Shared\\sheet.png\n"
        "#IMAGE,LR2files\\Shared\\unsupported.gif\n"
        "#IMAGE,C:\\forbidden.png\n"
        "#IMAGE,LR2files\\Shared\\red.dds\n"
        "#IMAGE,LR2files\\Shared\\S*.PNG\n"
        "#INCLUDE,parts.csv\n";
    std::ofstream(path.parent_path()/"parts.csv",std::ios::binary)<<
        "#SRC_IMAGE,0,0,0,0,-1,-1,1,1,0,0\n"
        "#DST_IMAGE,0,0,0,0,640,480,0,255,255,255,255,1\n"
        "#SRC_IMAGE,0,2,0,0,4,2,2,1,1000,0\n"
        "#DST_IMAGE,0,1000,120,40,40,20,0,1,255,0,0,1,0,0,0,0\n"
        "#DST_IMAGE,0,0,40,40,40,20,0,255,255,255,0,1,0,0,0,0\n"
        "#SRC_IMAGE,0,3,2,0,2,2,1,1,0,0\n"
        "#DST_IMAGE,0,0,200,40,30,10,0,128,0,0,255,2,1,-90,7\n"
        "#SRC_IMAGE,0,6,0,0,-1,-1\n"
        "#DST_IMAGE,0,0,300,40,30,30,0,255,255,255,255\n"
        "#SRC_IMAGE,0,1,0,0,1,1\n"
        "#DST_IMAGE,0,0,0,0,10,10,0,255,255,255,255\n"
        "#SRC_IMAGE,0,4,0,0,1,1\n"
        "#DST_IMAGE,0,0,0,0,10,10,0,255,255,255,255\n"
        "#SRC_IMAGE,0,5,0,0,1,1\n"
        "#DST_IMAGE,0,0,0,0,10,10,0,255,255,255,255\n"
        "#SRC_IMAGE,0,2,4,0,1,1\n"
        "#DST_IMAGE,0,0,0,0,10,10,0,255,255,255,255\n"
        "#SRC_IMAGE,0,99,0,0,1,1\n"
        "#DST_IMAGE,0,0,0,0,10,10,0,255,255,255,255\n"
        // Two files match S*.PNG and no CUSTOMFILE names one: the first in name order is the sheet.
        "#SRC_IMAGE,0,7,2,0,2,2,1,1,0,0\n"
        "#DST_IMAGE,0,0,500,300,20,20,0,255,255,255,255\n";
}

void compressedDds(const std::filesystem::path& path,char format,bool truncated=false) {
    std::array<unsigned char,160> dds{};
    const bool mipmapped=format=='5'&&!truncated;
    const unsigned blockBytes=format=='1'?8:16;
    const auto field=[&](unsigned offset,std::uint32_t value) {
        for(unsigned i=0;i<4;++i)dds[offset+i]=static_cast<unsigned char>(value>>(i*8));
    };
    field(0,0x20534444); field(4,124); field(8,0x81007); field(12,4); field(16,4); field(20,blockBytes);
    field(76,32); field(80,4); field(84,0x00545844|(std::uint32_t(format)<<24)); field(108,0x1000);
    if(mipmapped) {
        field(8,0xa1007); field(28,2); field(108,0x401008);
        dds[144]=255;
        field(152,0x001f);
    }
    if(format=='1') {
        // RGB565 black then green, with a transparent first column in each row.
        field(128,0x07e00000); field(132,0x57575757);
    } else {
        if(format=='3')std::fill_n(dds.begin()+128,8,BYTE{0x88});
        else dds[128]=128;
        field(136,0xf800);
    }
    std::ofstream(path,std::ios::binary).write(reinterpret_cast<const char*>(dds.data()),
        128+blockBytes*(mipmapped?2:1)-(truncated?1:0));
}

void saveRaster(const std::filesystem::path& path,const GUID& container,const GUID& pixelFormat,
                UINT width,BYTE* row,UINT rowBytes,IWICPalette* palette=nullptr) {
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    hr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    hr(factory->CreateStream(&stream));
    hr(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE));
    hr(factory->CreateEncoder(container,nullptr,&encoder));
    hr(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
    hr(encoder->CreateNewFrame(&frame,nullptr));
    hr(frame->Initialize(nullptr));
    hr(frame->SetSize(width,1));
    auto format=pixelFormat;
    hr(frame->SetPixelFormat(&format));
    require(format==pixelFormat,"fixture pixel format");
    if(palette)hr(frame->SetPalette(palette));
    hr(frame->WritePixels(1,rowBytes,rowBytes,row));
    hr(frame->Commit());
    hr(encoder->Commit());
}

// Writes a DX Library archive (version 3, default key) holding root-level files.
// `packed` entries carry an already compressed stream and its unpacked size.
struct ArchiveFile { std::string name; Bytes data; std::uint32_t unpacked{}; };
void writeArchive(const std::filesystem::path& path,const std::vector<ArchiveFile>& files) {
    const auto put32=[](Bytes& out,std::uint32_t value){for(int i=0;i<4;++i)out.push_back(static_cast<BYTE>(value>>(i*8)));};
    const auto putName=[&](Bytes& out,const std::string& name) {
        const std::size_t units=(name.size()+4)/4;
        out.push_back(static_cast<BYTE>(units));out.push_back(0);out.push_back(0);out.push_back(0);
        std::string upper=name;
        for(char& c:upper)if(c>='a'&&c<='z')c=static_cast<char>(c-'a'+'A');
        for(const auto& text:{upper,name}) {
            out.insert(out.end(),text.begin(),text.end());
            out.insert(out.end(),units*4-text.size(),0);
        }
    };
    Bytes data,names,table;
    std::vector<std::uint32_t> nameOffsets{0};
    putName(names,"");
    for(const auto& file:files){nameOffsets.push_back(static_cast<std::uint32_t>(names.size()));putName(names,file.name);}
    const auto record=[&](std::uint32_t name,std::uint32_t attributes,std::uint32_t address,std::uint32_t size,std::uint32_t stored) {
        put32(table,name);put32(table,attributes);
        table.insert(table.end(),24,0);
        put32(table,address);put32(table,size);put32(table,stored);
    };
    record(0,0x10,0,0,0xffffffffu);
    for(std::size_t i=0;i<files.size();++i) {
        const auto& file=files[i];
        record(nameOffsets[i+1],0x20,static_cast<std::uint32_t>(data.size()),
            file.unpacked?file.unpacked:static_cast<std::uint32_t>(file.data.size()),
            file.unpacked?static_cast<std::uint32_t>(file.data.size()):0xffffffffu);
        data.insert(data.end(),file.data.begin(),file.data.end());
    }
    const auto fileTable=static_cast<std::uint32_t>(names.size());
    const auto directoryTable=static_cast<std::uint32_t>(fileTable+table.size());
    put32(table,0);put32(table,0xffffffffu);put32(table,static_cast<std::uint32_t>(files.size()));put32(table,44);
    Bytes out;
    out.push_back('D');out.push_back('X');out.push_back(3);out.push_back(0);
    put32(out,static_cast<std::uint32_t>(names.size()+table.size()));
    put32(out,24);put32(out,static_cast<std::uint32_t>(24+data.size()));put32(out,fileTable);put32(out,directoryTable);
    out.insert(out.end(),data.begin(),data.end());
    out.insert(out.end(),names.begin(),names.end());
    out.insert(out.end(),table.begin(),table.end());
    constexpr BYTE key[12]{0x55,0xaa,0x20,0x55,0x55,0x06,0x55,0xaa,0x55,0xd5,0x7c,0x66};
    for(std::size_t i=0;i<out.size();++i)out[i]^=key[i%12];
    std::ofstream(path,std::ios::binary).write(reinterpret_cast<const char*>(out.data()),static_cast<std::streamsize>(out.size()));
}

void fontRenders(const std::filesystem::path& dir) {
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir/"loose");
    // 4x2 glyphSheet, top-left origin: a red 2x2 cell for 'A' and a blue one for 'B'.
    Bytes glyphSheet(18);
    glyphSheet[2]=2;glyphSheet[12]=4;glyphSheet[14]=2;glyphSheet[16]=32;glyphSheet[17]=0x28;
    for(int row=0;row<2;++row)for(const BYTE blue:{BYTE{0},BYTE{0},BYTE{255},BYTE{255}}) {
        glyphSheet.push_back(blue);glyphSheet.push_back(0);glyphSheet.push_back(static_cast<BYTE>(255-blue));glyphSheet.push_back(255);
    }
    const std::string definition="//comment\n#S,10,\n#M,1,\n#T,0,font.tga\n#T,1,..\\escape.tga\n#R,65,0,0,0,2,2,\n#R,66,0,2,0,2,2,\n"
        "#R,288,0,0,0,2,2,\n#R,608,0,2,0,2,2,\n#R,67,1,0,0,2,2,\n#R,68,0,0,0,0,2,\n";
    // "ABCABCABC" as three literals and one back-reference of length 6 at distance 3.
    const Bytes packed{9,0,0,0, 15,0,0,0, 0xff, 'A','B','C', 0xff,0x10,0x02};
    writeArchive(dir/"fnt.dxa",{{"font.lr2font",Bytes(definition.begin(),definition.end())},{"font.tga",glyphSheet},{"packed.bin",packed,9}});
    auto archive=DxArchive::open(dir/"fnt.dxa");
    require(archive&&archive->entryCount()==3,"archive directory");
    const auto unpacked=archive->read("PACKED.bin");
    require(unpacked&&std::string(unpacked->begin(),unpacked->end())=="ABCABCABC"&&archive->read("font.tga")==glyphSheet&&!archive->read("absent"),
        "stored and compressed archive entries");
    {
        std::ofstream broken(dir/"broken.dxa",std::ios::binary);
        broken<<"not an archive at all, just text long enough";
    }
    require(!DxArchive::open(dir/"broken.dxa")&&!DxArchive::open(dir/"absent.dxa"),"invalid archives are rejected");
    writeArchive(dir/"truncated.dxa",{{"packed.bin",Bytes{9,0,0,0, 13,0,0,0, 0xff, 'A', 0xff,0x10,0x02},9}});
    require(!DxArchive::open(dir/"truncated.dxa")->read("packed.bin"),"a back-reference before the start of the output is refused");

    const auto skin=dir/"decide.lr2skin";
    std::ofstream(skin,std::ios::binary)<<"#INFORMATION,6,Font test,SevenMania\n#LR2FONT,fnt\\font.lr2font\n#LR2FONT,missing\\font.lr2font\n"
        "#SRC_TEXT,0,0,10,0\n#DST_TEXT,0,0,100,100,200,20,0,255,255,255,255\n"
        "#SRC_TEXT,0,0,11,2\n#DST_TEXT,0,0,300,200,6,20,0,255,255,255,255\n"
        "#SRC_TEXT,0,0,12,1\n#DST_TEXT,0,0,400,300,200,20,0,255,0,255,0\n"
        "#SRC_TEXT,0,1,13,0\n#DST_TEXT,0,0,100,400,200,20,0,255,255,255,255\n";
    auto font=loadFont(dir,skin,"fnt\\font.lr2font");
    require(font&&font->archive&&font->size==10&&font->margin==1&&font->covers(L"AB〜～あ")&&!font->covers(L"AZ")&&
        !font->glyph(L'C')&&!font->glyph(L'D'),"font table from an archive: Shift_JIS codes, both wave dashes, invalid rows dropped");
    std::ofstream(dir/"loose/font.lr2font",std::ios::binary)<<definition;
    std::ofstream(dir/"loose/font.tga",std::ios::binary).write(reinterpret_cast<const char*>(glyphSheet.data()),static_cast<std::streamsize>(glyphSheet.size()));
    auto loose=loadFont(dir,skin,"loose/font.lr2font");
    require(loose&&!loose->archive&&loose->textureFile(0)==glyphSheet&&!loose->textureFile(1)&&!loose->textureFile(7)&&
        !loadFont(dir,skin,"missing\\font.lr2font")&&!loadFont(dir,skin,"..\\font.lr2font"),"loose font files and unusable declarations");

    Options options;options.screen=ScreenType::Decide;
    auto result=parse(skin,options);
    require(bool(result),"font fixture parse");
    ScreenData song;
    song.songText={L"AB",L"AB",L"A",L"AB",L"",L"",L""};
    PreviewScene scene;
    require(scene.load(*result.document,result.diagnostics,false,&song)&&scene.layerCount()==4,"font fixture load");
    require(std::count_if(result.diagnostics.begin(),result.diagnostics.end(),[](const auto& d){
        return d.message.starts_with("LR2FONT unavailable");})==1,"a font that cannot be read is reported once");
    PreviewSurface output;
    require(scene.prepare(output.drawing.Get()),"font fixture upload");
    output.render(scene,0);
    // Size 20 over a size-10 font doubles each 2x2 glyph; the advance is (2 + margin 1) * 2.
    output.pixel(102,102,255,0,0,"first glyph at the text origin");
    output.pixel(108,102,0,0,255,"second glyph after width plus margin");
    output.pixel(105,102,0,0,0,"margin between glyphs is empty");
    output.pixel(102,105,0,0,0,"glyph height follows the text size");
    // Right-aligned text wider than its 6 px box is narrowed to fit it: glyphs become 2 px wide.
    output.pixel(295,202,255,0,0,"narrowed text starts at the left edge of its box");
    output.pixel(298,202,0,0,255,"narrowed second glyph");
    output.pixel(292,202,0,0,0,"right-aligned text ends at its anchor");
    output.pixel(301,202,0,0,0,"nothing is drawn right of the anchor");
    // Centred with its 6 px advance, tinted by the destination colour (red x green = black).
    output.pixel(399,302,0,0,0,"tint multiplies the glyph colour");
    scene.releaseDevice();
    std::filesystem::remove(dir/"loose/font.tga");
    PreviewSurface recreated;
    require(scene.prepare(recreated.drawing.Get()),"font fixture re-upload");
    recreated.render(scene,0);
    recreated.pixel(108,102,0,0,255,"glyph glyphSheets are rebuilt for a new device");
}

void colorKeyRenders(const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir);
    compressedDds(dir/"bc1.dds",'1');
    compressedDds(dir/"bc2.dds",'3');
    compressedDds(dir/"bc3.dds",'5');
    compressedDds(dir/"broken.dds",'5',true);
    BYTE rgb[]{0,255,0, 0,0,255, 0,254,0};
    saveRaster(dir/"key.png",GUID_ContainerFormatPng,GUID_WICPixelFormat24bppBGR,3,rgb,sizeof(rgb));
    saveRaster(dir/"key.bmp",GUID_ContainerFormatBmp,GUID_WICPixelFormat24bppBGR,3,rgb,sizeof(rgb));
    BYTE black[]{0,0,0};
    saveRaster(dir/"key.jpg",GUID_ContainerFormatJpeg,GUID_WICPixelFormat24bppBGR,1,black,sizeof(black));
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICPalette> palette;
    hr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    hr(factory->CreatePalette(&palette));
    WICColor colors[]{0xff00ff00,0x80ff0000};
    hr(palette->InitializeCustom(colors,2));
    BYTE indices[]{0,1};
    saveRaster(dir/"palette.png",GUID_ContainerFormatPng,GUID_WICPixelFormat8bppIndexed,2,indices,sizeof(indices),palette.Get());
    save(dir/"alpha.png",Bytes{0,0,255,128},1,1);
    const auto tga=[&](const char* name,BYTE type,BYTE depth,BYTE descriptor,const Bytes& data) {
        Bytes header(18);
        header[2]=type;header[12]=header[14]=2;header[16]=depth;header[17]=descriptor;
        std::ofstream file(dir/name,std::ios::binary);
        file.write(reinterpret_cast<const char*>(header.data()),header.size());
        file.write(reinterpret_cast<const char*>(data.data()),data.size());
    };
    // Bottom-right origin, one literal packet. Green is keyed, the other texels retain their positions.
    tga("key.tga",10,24,0x10,{3, 0,255,0, 0,0,255, 0,255,255, 255,255,255});
    tga("alpha.tga",2,32,0x28,{0,255,0,128, 0,255,0,128, 0,0,255,0, 0,0,255,0});
    tga("run.tga",10,32,0x20,{0x83, 0,0,255,0});
    tga("broken.tga",10,32,0x28,{0x84, 0,0,255,255});
    const auto path=dir/"compat.lr2skin";
    std::ofstream(path,std::ios::binary)<<
        "#INFORMATION,0,DDS and color-key test,SevenMania\n"
        "#IMAGE,key.png\n"
        "#IF,999\n"
        "#TRANSCOLOR,255,0,0\n"
        "#ENDIF\n"
        "#IMAGE,./key.png\n"
        "#INCLUDE,colors.csv\n"
        "#TRANSCOLOR,256,0,0\n"
        "#TRANSCOLOR,255,,0\n"
        "#IMAGE,./key.png\n"
        "#IMAGE,alpha.png\n"
        "#IMAGE,bc1.dds\n"
        "#IMAGE,bc2.dds\n"
        "#IMAGE,bc3.dds\n"
        "#IMAGE,broken.dds\n"
        "#TRANSCOLOR,0,255,0\n"
        "#IMAGE,bc1.dds\n"
        "#IMAGE,alpha.png\n"
        "#IMAGE,key.bmp\n"
        "#IMAGE,palette.png\n"
        "#TRANSCOLOR,0,0,0\n"
        "#IMAGE,key.jpg\n"
        "#TRANSCOLOR,0,255,0\n"
        "#IMAGE,key.tga\n#IMAGE,alpha.tga\n#IMAGE,run.tga\n#IMAGE,broken.tga\n"
        "#INCLUDE,layers.csv\n";
    std::ofstream(dir/"colors.csv",std::ios::binary)<<"#TRANSCOLOR,255,0,0\n#IMAGE,key.png\n";
    {
        std::ofstream layers(dir/"layers.csv",std::ios::binary);
        layers<<"#SRC_IMAGE,0,111,0,0,1,1\n#DST_IMAGE,0,0,0,0,640,480,0,255,0,0,255\n";
        for(int i=0;i<18;++i) {
            layers<<"#SRC_IMAGE,0,"<<i<<",0,0,-1,-1\n"
                  <<"#DST_IMAGE,0,0,"<<(i%5)*125+10<<','<<(i/5)*120+20<<",120,100,0,255,255,255,255\n";
        }
        layers<<"#SRC_JUDGELINE,0,111,0,0,1,1\n"
              <<"#DST_JUDGELINE,0,0,20,370,30,4,0,255,255,255,255\n";
    }

    auto result=parse(path);
    require(bool(result),"DDS/color-key fixture parse");
    PreviewScene scene;
    require(scene.load(*result.document,result.diagnostics),"DDS/color-key fixture load");
    require(scene.imageCount()==13&&scene.layerCount()==18,"key variants shared by path and alpha images shared across keys");
    require(std::count_if(result.diagnostics.begin(),result.diagnostics.end(),[](const auto& d){
        return d.line&&d.message.starts_with("Invalid TRANSCOLOR");})==2,"invalid color-key diagnostics");
    require(std::any_of(result.diagnostics.begin(),result.diagnostics.end(),[](const auto& d){
        return d.line&&d.message.find("Truncated DDS block data")!=std::string::npos;}),"truncated DDS diagnostic");
    require(std::any_of(result.diagnostics.begin(),result.diagnostics.end(),[](const auto& d){
        return d.line&&d.message.find("Invalid or truncated TGA pixel packet")!=std::string::npos;}),"invalid TGA packet diagnostic");
    PreviewSurface output;
    require(scene.prepare(output.drawing.Get()),"DDS/color-key fixture upload");
    output.render(scene,0);
    output.pixel(20,50,0,0,255,"default green key reveals background");
    output.pixel(65,50,255,0,0,"other RGB color survives default key");
    output.pixel(110,50,0,254,0,"near match survives exact color key");
    output.pixel(145,50,0,0,255,"disabled TRANSCOLOR leaves default active");
    output.pixel(270,50,0,255,0,"same texture with red key preserves original green");
    output.pixel(315,50,0,0,255,"included TRANSCOLOR keys red");
    output.pixel(440,50,0,0,255,"invalid declarations leave red key active");
    output.pixel(535,50,128,0,127,"PNG alpha survives a matching key");
    output.pixel(20,170,0,0,255,"DXT1 transparent texel");
    output.pixel(75,170,0,255,0,"DXT1 opaque green texel");
    output.pixel(150,170,136,0,119,"DXT3 straight color and partial alpha");
    output.pixel(275,170,128,0,127,"DXT5 straight color and partial alpha");
    output.pixel(400,170,0,0,255,"broken DDS leaves underlying layer");
    output.pixel(585,170,0,255,0,"DXT1 alpha overrides the green key");
    output.pixel(20,285,128,0,127,"valid alpha layer draws after broken DDS");
    output.pixel(145,285,0,0,255,"RGB BMP uses the default key");
    output.pixel(190,285,255,0,0,"RGB BMP retains the other color");
    output.pixel(285,285,0,255,0,"opaque palette green survives when palette has alpha");
    output.pixel(355,285,128,0,127,"PNG palette alpha survives");
    output.pixel(400,285,0,0,255,"black JPEG pixel uses the explicit key");
    output.pixel(525,290,255,255,255,"TGA origin top left");
    output.pixel(605,290,255,255,0,"TGA origin top right");
    output.pixel(525,355,255,0,0,"TGA origin bottom left");
    output.pixel(605,355,0,0,255,"RGB TGA honors TRANSCOLOR");
    output.pixel(25,372,255,255,255,"LR2 judge line layer");
    output.pixel(25,400,0,128,127,"raw TGA alpha");
    output.pixel(25,460,0,0,255,"raw TGA transparent pixel");
    output.pixel(150,400,255,0,0,"TGA repeated packet without specified alpha");
    output.pixel(275,400,0,0,255,"malformed TGA skipped");
    scene.releaseDevice();
    PreviewSurface recreated;
    require(scene.prepare(recreated.drawing.Get()),"recreate DDS/color-key textures");
    recreated.render(scene,0);
    recreated.pixel(270,50,0,255,0,"key variant survives recreation");
    recreated.pixel(275,170,128,0,127,"compressed source survives recreation");
}
}

int wmain(int argc,wchar_t** argv) try {
    require(argc==2||argc==3,"expected output directory or --real FILE");
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    hr(initialized);
    struct Apartment {~Apartment(){CoUninitialize();}} apartment;
    if(argc==3&&(std::wstring_view(argv[1])==L"--bench"||std::wstring_view(argv[1])==L"--bench-select")) {
        // Frame cost of a 7KEY play (or select) skin on the GPU at 1920x1080: load, prepare, then draw/EndDraw times.
        const bool selectBench=std::wstring_view(argv[1])==L"--bench-select";
        Options options;
        options.screen=selectBench?ScreenType::Select:ScreenType::Play7Key;
        if(!selectBench)options.condition=[](int id)->std::optional<bool> {
            PlayFrame state;
            if(const auto enabled=state.option(std::abs(id)))return *enabled==(id>0);
            return std::nullopt;
        };
        const auto t0=std::chrono::steady_clock::now();
        auto result=parse(argv[2],options);
        require(static_cast<bool>(result),"bench skin parses");
        PreviewScene scene;
        ScreenData song;
        song.songText.fill(L"Text");
        require(scene.load(*result.document,result.diagnostics,!selectBench,&song),"bench skin loads");
        const auto t1=std::chrono::steady_clock::now();
        struct GpuSurface : Surface {
            ComPtr<ID2D1DeviceContext> drawing;
            GpuSurface() : Surface(1920,1080,D3D_DRIVER_TYPE_HARDWARE) {
                ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGISurface> surface;ComPtr<ID2D1Device> d2d;ComPtr<ID2D1Bitmap1> bitmap;
                hr(device.As(&dxgi));hr(texture.As(&surface));hr(D2D1CreateDevice(dxgi.Get(),nullptr,&d2d));
                hr(d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&drawing));
                const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
                hr(drawing->CreateBitmapFromDxgiSurface(surface.Get(),&properties,&bitmap));
                drawing->SetTarget(bitmap.Get());drawing->SetDpi(96,96);
            }
        } gpu;
        require(scene.prepare(gpu.drawing.Get()),"bench prepare");
        const auto t2=std::chrono::steady_clock::now();
        std::vector<double> draw,end;
        PlayFrame play;
        play.readyAge=play.playAge=0;play.gauge=50;play.bpm=150;play.noteCount=1000;
        for(int frame=0;frame<600;++frame) {
            const double ms=1000.+frame*1000./240;
            play.playAge=play.readyAge=ms;play.rhythmAge=std::fmod(ms,400.);
            play.combo=frame;play.judge=frame%5;play.judgementAge=std::fmod(ms,500.);
            for(std::size_t lane=0;lane<8;++lane)play.keyOn[lane]=(frame+lane)%20<10?std::fmod(ms,300.):-1;
            if(frame>=300)play.failedAge=(frame-300)*1000./240;
            SelectFrame select;
            if(selectBench) {
                // A list moving one row every 8 frames: every visible title changes as it scrolls.
                select.songSelected=true;select.keys=7;select.level=12;
                for(std::size_t row=0;row<select.rows.size();++row) {
                    select.rows[row].title=L"Song title number "+std::to_wstring(frame/8+row)+L" [ANOTHER]";
                    select.rows[row].level=std::to_wstring((frame/8+row)%13);select.rows[row].body=1;select.rows[row].row=int(row);
                }
                select.scroll=float(8-frame%8)/8.f;select.selectionAge=ms;
                select.song.songText.fill(L"Song title number "+std::to_wstring(frame/8));
            }
            const auto a=std::chrono::steady_clock::now();
            gpu.drawing->BeginDraw();gpu.drawing->Clear(D2D1::ColorF(0,0,0));
            scene.draw(gpu.drawing.Get(),ms,1920,1080,selectBench?nullptr:&play,DrawPass::All,selectBench?&select:nullptr,{});
            const auto b=std::chrono::steady_clock::now();
            hr(gpu.drawing->EndDraw());
            // Wait for the GPU: read one pixel back so the time includes the GPU's own work.
            gpu.context->CopyResource(gpu.staging.Get(),gpu.texture.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};hr(gpu.context->Map(gpu.staging.Get(),0,D3D11_MAP_READ,0,&mapped));gpu.context->Unmap(gpu.staging.Get(),0);
            const auto c=std::chrono::steady_clock::now();
            draw.push_back(std::chrono::duration<double,std::milli>(b-a).count());
            end.push_back(std::chrono::duration<double,std::milli>(c-b).count());
        }
        const auto stats=[](std::vector<double> v,const char* name) {
            std::sort(v.begin(),v.end());
            std::cout<<name<<" avg "<<std::accumulate(v.begin(),v.end(),0.)/v.size()<<" ms, p50 "<<v[v.size()/2]
                     <<", p99 "<<v[v.size()*99/100]<<", max "<<v.back()<<"\n";
        };
        std::cout<<"images "<<scene.imageCount()<<", layers "<<scene.layerCount()
                 <<", parse+load "<<std::chrono::duration<double,std::milli>(t1-t0).count()
                 <<" ms, prepare "<<std::chrono::duration<double,std::milli>(t2-t1).count()<<" ms\n";
        stats(draw,"draw (CPU)");stats(end,"EndDraw+GPU finish");
        if(!selectBench) {
            const auto p0=std::chrono::steady_clock::now();
            int sink=0;
            for(int i=0;i<1000;++i)if(const auto lanes=scene.playLanes(1920,1080))sink+=(*lanes)[0].noteBottom;
            std::cout<<"playLanes "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-p0).count()/1000
                     <<" ms per call ("<<sink<<")\n";
        }
        return 0;
    }
    if(argc==3&&std::wstring_view(argv[1])==L"--report") {
        // Compatibility report: which commands, timers, options and assets a skin loses on load.
        Result result;
        ScreenType type{};
        bool otherScreen=false;
        for(const auto screen:{ScreenType::Play7Key,ScreenType::Play5Key,ScreenType::Play14Key,ScreenType::Play10Key,ScreenType::Play9Key,
                                ScreenType::Select,ScreenType::Decide,ScreenType::Result}) {
            Options options;
            options.screen=type=screen;
            // One representative game state per screen: a selected song, a stage image, a cleared result.
            if(screen==ScreenType::Select)options.initialOptions={{0,false},{3,false},{21,false},{22,false},{23,false},
                {24,false},{25,false},{26,false},{30,true},{31,false},{40,false},{41,true},{46,false},{50,true},{51,false},{52,true},{53,false}};
            if(screen==ScreenType::Decide) {
                options.initialOptions={{52,true},{53,false},{190,false},{191,true}};
                for(int i=0;i<=5;++i)options.initialOptions[150+i]=i==3;
            }
            if(screen==ScreenType::Result) {
                // A cleared AAA play that beats an earlier best, as the host would describe it.
                ResultState state;
                state.cleared=true;state.exScore=190;state.noteCount=100;state.maxCombo=100;
                state.played=true;state.bestExScore=150;state.bestMaxCombo=80;state.bestMissCount=5;
                options.initialOptions=resultOptions(state);
            }
            if(playLaneCount(static_cast<int>(screen)))options.condition=[screen](int id)->std::optional<bool> {
                PlayFrame state;
                state.keys=playKeys(static_cast<int>(screen));
                if(const auto enabled=state.option(std::abs(id)))return *enabled==(id>0);
                return std::nullopt;
            };
            result=parse(argv[2],options);
            otherScreen=std::any_of(result.diagnostics.begin(),result.diagnostics.end(),
                [](const auto& d){return d.error&&d.message.starts_with("Unexpected INFORMATION screen type");});
            if(result||!otherScreen)break;
        }
        if(otherScreen) {
            std::cout<<"unsupported screen type: only play (0-4), select (5), decide (6) and result (7) skins load\n";
            return 1;
        }
        std::size_t textures{},layers{};
        bool loaded=false;
        if(result) {
            PreviewScene scene;
            ScreenData song;
            song.songText.fill(L"Text");
            // The decide screen shows the chart's stage image; stand in a small picture for it.
            if(type==ScreenType::Decide) {
                song.stageImage=std::filesystem::temp_directory_path()/L"SevenManiaLr2ReportStage.png";
                Bytes stage(640u*480u*4u,0);
                for(std::size_t alpha=3;alpha<stage.size();alpha+=4)stage[alpha]=255;
                save(song.stageImage,stage,640,480); // The size of a usual STAGEFILE.
            }
            loaded=scene.load(*result.document,result.diagnostics,playLaneCount(static_cast<int>(type))>0,&song);
            textures=scene.imageCount();layers=scene.layerCount();
            const auto& header=result.document->header;
            std::cout<<"type "<<header.type<<", "<<header.width<<"x"<<header.height<<", "
                <<header.options.size()<<" custom options, "<<header.files.size()<<" custom files, "
                <<header.offsets.size()<<" custom offsets\n";
            // The names below are the keys of lr2-skins.txt. Console output is raw UTF-8.
            for(const auto& option:header.options) {
                std::cout<<"option\t"<<option.name<<"\t";
                for(std::size_t i=0;i<option.choices.size();++i)
                    std::cout<<(i?" | ":"")<<(static_cast<int>(i)==option.selected?"*":"")<<option.choices[i];
                std::cout<<"\n";
            }
            for(const auto& file:header.files)
                std::cout<<"file\t"<<file.name<<"\t"<<file.pattern<<" (default "<<file.defaultName<<")\n";
            for(const auto& offset:header.offsets)std::cout<<"offset\t"<<offset.name<<"\tid "<<offset.id<<"\n";
        }
        std::cout<<(loaded?"loaded: ":result?"scene failed: ":"parse failed: ")<<textures<<" textures, "<<layers<<" layers, "
            <<result.diagnostics.size()<<" diagnostics\n"<<formatDiagnosticSummary(summarizeDiagnostics(result.diagnostics));
        return loaded?0:1;
    }
    if(argc==3) {
        const std::wstring_view mode=argv[1];
        const bool play=mode==L"--real-play"||mode==L"--real-play-fhd";
        require(play||mode==L"--real-fhd","expected --real-fhd, --real-play or --real-play-fhd");
        Options options;
        options.previewIncludes=true;
        auto result=parse(argv[2],options);
        if(!result)for(const auto& diagnostic:result.diagnostics)
            if(diagnostic.error)std::cerr<<diagnostic.line<<": "<<diagnostic.message<<"\n";
        require(bool(result),"real LR2 parse failed");
        if(mode==L"--real-fhd"||mode==L"--real-play-fhd") {
            result.document->header.width=1920;
            result.document->header.height=1080;
        }
        PreviewScene scene;
        const bool loaded=scene.load(*result.document,result.diagnostics,play);
        for(const auto& diagnostic:result.diagnostics)
            std::cout<<diagnostic.line<<": "<<diagnostic.message<<"\n";
        require(loaded,"real LR2 scene failed");
        const auto& header=result.document->header;
        const auto renderWidth=std::min(header.width,1920u);
        PreviewSurface output(renderWidth,header.height*renderWidth/header.width);
        require(scene.prepare(output.drawing.Get()),"real LR2 upload failed");
        if(play) {
            require(scene.layerCount()>0&&scene.playLanes(output.width,output.height).has_value(),"real LR2 playfield layout");
            for(int lane=0;lane<8;++lane)require(scene.noteSprite(lane,0).bitmap!=nullptr,"real LR2 tap sprite missing");
        }
        PlayFrame frame;
        frame.readyAge=frame.playAge=750;
        frame.bgaEnabled=true;
        frame.judge=5;frame.judgementAge=80;frame.combo=12;frame.gauge=82;frame.bpm=140;
        frame.keyOn[1]=100;frame.bomb[1]=80;
        output.render(scene,750,play?&frame:nullptr);
        const auto early=output.frameSignature();
        frame.readyAge=frame.playAge=1250;
        frame.judgementAge=150;frame.gauge=84;
        frame.keyOn[1]=-1;frame.keyOff[1]=30;
        output.render(scene,1250,play?&frame:nullptr);
        const auto later=output.frameSignature();
        require(early&&later&&early!=later,"real LR2 animation changes rendered pixels");
        std::cout<<scene.imageCount()<<" textures, "<<scene.layerCount()<<" layers\n";
        return 0;
    }
    const auto path=std::filesystem::path(argv[1])/"LR2files"/"Play"/"play.lr2skin";
    ImageSource digitSheet;
    digitSheet.values={0,0,0,0,464,31,11,1,0,0};
    require(imageGrid(digitSheet,462,31).has_value(),"Unused grid remainder must not reject LITONE5's BPM atlas");
    require(!imageGrid(digitSheet,461,31),"A sampled grid cell must stay inside the bitmap");
    fixture(path);
    auto result=parse(path);
    require(bool(result),"fixture parse");
    PreviewScene scene;
    require(scene.load(*result.document,result.diagnostics),"fixture image load");
    require(scene.imageCount()==3&&scene.layerCount()==5,"deduplicated images and valid layers");
    for(const auto token:{"missing.png","Unsupported image format","relative literal path","index: 99","exceeds image bounds"})
        require(std::any_of(result.diagnostics.begin(),result.diagnostics.end(),
            [&](const auto& d){return d.line!=0&&d.message.find(token)!=std::string::npos;}),"expected diagnostic with line number");

    PreviewSurface first;
    require(scene.prepare(first.drawing.Get()),"first upload");
    first.render(scene,0);
    first.pixel(50,50,0,255,0,"first sprite frame over background at LR2 top-left");
    first.pixel(95,50,16,32,64,"uncovered background");
    first.pixel(195,60,16,32,192,"rotated additive tinted cropped sprite");
    first.pixel(215,45,16,32,64,"unrotated destination must be empty");
    first.pixel(310,50,136,16,32,"premultiplied DDS over BMP");
    first.pixel(510,310,255,255,255,"wildcard image without CUSTOMFILE uses the first match");
    {
        // Play lead-in: scene (timer 0) first, READY (40) after LOADSTART+LOADEND or the load, song PLAYSTART later.
        const PlayLeadInTimes times{2200,2800,1000};
        auto at=playLeadIn(times,0,-1);
        require(at.sceneAge==0&&!at.loaded&&at.readyAge<0&&!at.start&&at.progress==0.f,"scene starts before loading");
        at=playLeadIn(times,3600,-1);
        require(!at.loaded&&at.progress>.49f&&at.progress<.51f&&at.readyAge<0,"load bar follows LOADSTART/LOADEND");
        at=playLeadIn(times,9000,-1);
        require(!at.loaded&&at.progress<1.f&&!at.start,"an unfinished load holds READY and the bar short of the end");
        at=playLeadIn(times,4000,300);
        require(!at.loaded&&at.readyAge<0,"a fast load still waits for the skin's minimum");
        at=playLeadIn(times,5500,300);
        require(at.loaded&&at.progress==1.f&&at.readyAge==500&&!at.start,"READY starts at LOADSTART+LOADEND");
        at=playLeadIn(times,6000,300);
        require(at.loaded&&at.readyAge==1000&&at.start,"the song starts PLAYSTART after READY");
        at=playLeadIn(times,6400,-1);
        require(!at.loaded&&at.readyAge<0,"a slow load delays READY");
        at=playLeadIn(times,8000,6500);
        require(at.loaded&&at.readyAge==1500&&at.start,"READY follows a slow load");
        at=playLeadIn(PlayLeadInTimes{},0,0);
        require(at.loaded&&at.readyAge==0&&at.start,"a skin without lead-in commands starts at once");
        // Lane cover: the setting while playing, closing smoothly during the ending; the setting never changes.
        const int setting=30;
        int stored=setting;
        require(laneCoverDisplay(stored,-1,1000)==.3f,"the drawn cover is the setting during the song");
        const float early=laneCoverDisplay(stored,100,1000),middle=laneCoverDisplay(stored,250,1000);
        require(early>.3f&&early<middle&&middle<1.f,"the cover lowers smoothly while the song ends");
        require(laneCoverDisplay(stored,500,1000)==1.f&&laneCoverDisplay(stored,5000,1000)==1.f,"the cover is closed by half the ending");
        require(laneCoverDisplay(stored,800,100000)==1.f&&laneCoverDisplay(stored,100,0)<1.f,"the closing time stays within 200-800 ms");
        require(laneCoverDisplay(150,-1,0)==1.f&&laneCoverDisplay(-5,-1,0)==0.f,"out-of-range settings are clamped");
        require(laneCoverClosingAge(-1,-1)<0&&laneCoverClosingAge(400,-1)<0&&laneCoverClosingAge(500,-1)==0&&
            laneCoverClosingAge(800,-1)==300,"the cover starts closing 500 ms after the last judgement");
        require(laneCoverClosingAge(100,250)==250&&laneCoverClosingAge(-1,40)==40,"an earlier FADEOUT, CLOSE or QUIT closes it at once");
        require(laneCoverClosingAge(2000,100)==1500,"the earlier start wins");
        require(laneCoverDisplay(stored,laneCoverClosingAge(450,-1),1000)==.3f,"still open just before 500 ms");
        require(stored==setting,"the lane cover setting is never written");
        PlayFrame loading;
        loading.loaded=false;loading.loadProgress=.25f;
        require(loading.option(80)==true&&loading.option(81)==false&&loading.graph(2)==.25f,"loading options and bar graph");
        PlayFrame playing;
        require(playing.option(80)==false&&playing.option(81)==true&&playing.graph(2)==1.f,"loaded by default during the song");
    }
    {
        // LR2 blends 3 (subtract), 4 (multiply), 9 and 10 (invert) over the 16,32,64 background.
        const auto blendPath=std::filesystem::path(argv[1])/"LR2files"/"Blend"/"blend.lr2skin";
        std::filesystem::create_directories(blendPath.parent_path());
        std::ofstream(blendPath,std::ios::binary)<<
            "#INFORMATION,0,Blend Test,SevenMania\n"
            "#IMAGE,LR2files\\Shared\\bg.BMP\n"
            "#IMAGE,LR2files\\Shared\\sheet.png\n"
            "#IMAGE,LR2files\\Shared\\sheet2.png\n"
            "#SRC_IMAGE,0,0,0,0,-1,-1\n"
            "#DST_IMAGE,0,0,0,0,640,480,0,255,255,255,255,1\n"
            "#SRC_IMAGE,0,1,2,0,2,2\n"
            "#DST_IMAGE,0,0,0,100,40,40,0,255,8,16,32,3\n"
            "#SRC_IMAGE,0,2,0,0,1,1\n"
            "#DST_IMAGE,0,0,100,100,40,40,0,255,255,255,255,4\n"
            "#SRC_IMAGE,0,1,2,0,2,2\n"
            "#DST_IMAGE,0,0,200,100,40,40,0,255,255,255,255,9\n"
            "#SRC_IMAGE,0,1,2,0,2,2\n"
            "#DST_IMAGE,0,0,300,100,40,40,0,255,255,255,255,10\n"
            "#SRC_IMAGE,0,1,2,0,2,2\n"
            "#DST_IMAGE,0,0,400,100,40,40,0,255,255,255,255,7\n";
        auto blendSkin=parse(blendPath);
        require(bool(blendSkin),"blend fixture parse");
        PreviewScene blends;
        require(blends.load(*blendSkin.document,blendSkin.diagnostics),"blend fixture load");
        const auto blendWarnings=std::count_if(blendSkin.diagnostics.begin(),blendSkin.diagnostics.end(),
            [](const auto& d){return d.message.find("Unsupported DST_IMAGE blend")!=std::string::npos;});
        require(blendWarnings==1,"only the undefined blend 7 is reported");
        PreviewSurface surface;
        require(blends.prepare(surface.drawing.Get()),"blend upload");
        surface.render(blends,0);
        surface.pixel(20,120,8,16,32,"blend 3 subtracts the tinted sprite");
        surface.pixel(120,120,0,0,64,"blend 4 multiplies the blue sprite with the target");
        surface.pixel(220,120,239,223,191,"blend 9 inverts the target");
        surface.pixel(320,120,239,223,191,"blend 10 inverts the target");
        surface.pixel(60,120,16,32,64,"subtract leaves pixels outside the sprite unchanged");
        surface.pixel(160,120,16,32,64,"multiply leaves pixels outside the sprite unchanged");
    }
    first.render(scene,500);
    first.pixel(90,50,136,80,32,"second sprite with interpolated position, RGB and opacity");
    first.pixel(50,50,16,32,64,"sprite moved away");
    first.render(scene,1000);
    first.pixel(50,50,0,255,0,"destination and sprite loops");

    scene.releaseDevice();
    std::filesystem::remove(path.parent_path().parent_path()/"Shared"/"sheet.png");
    PreviewSurface recreated;
    require(scene.prepare(recreated.drawing.Get()),"recreated GPU device uploads cached pixels");
    recreated.render(scene,500);
    recreated.pixel(90,50,136,80,32,"cached image after device recreation");
    fixture(path);
    require(scene.load(*result.document,result.diagnostics),"reload scene");
    require(scene.prepare(recreated.drawing.Get()),"reloaded upload");
    recreated.render(scene,0);
    recreated.pixel(50,50,0,255,0,"reloaded scene render");
    colorKeyRenders(std::filesystem::path(argv[1])/"ImageCompatibility");
    fontRenders(std::filesystem::path(argv[1])/"Fonts");
    const auto selectPath=path.parent_path()/"select.lr2skin";
    std::ofstream(selectPath)<<"#INFORMATION,5,Select test,SevenMania\n#IMAGE,../Shared/sheet.png\n"
        "#SRC_BAR_BODY,0,0,0,0,1,1\n#SRC_BAR_BODY,1,0,3,0,1,1\n"
        "#BAR_CENTER,2\n#BAR_AVAILABLE,1,2\n"
        "#DST_BAR_BODY_OFF,1,0,290,120,200,30,0,255,255,255,255\n"
        "#DST_BAR_BODY_ON,2,0,270,160,200,30,0,255,255,255,255\n"
        "#SRC_BAR_LAMP,1,0,0,0,1,1\n#DST_BAR_LAMP,1,0,3,4,10,10,0,255,255,255,255\n"
        "#SRC_IMAGE,0,102,0,0,-1,-1\n#DST_IMAGE,0,0,20,20,80,40,0,255,255,255,255\n"
        // Two-cell buttons: green when off, white when on. Panel 1, then key mode with a left/right split.
        "#SRC_BUTTON,0,0,0,0,4,1,2,1,0,0,1,1,0,0\n#DST_BUTTON,0,0,400,300,40,20,0,255,255,255,255\n"
        "#SRC_BUTTON,0,0,0,0,4,1,2,1,0,0,11,1,0,0\n#DST_BUTTON,0,0,400,340,40,20,0,255,255,255,255\n"
        "#SRC_BUTTON,0,0,0,0,4,1,2,1,0,0,99,1,0,0\n#DST_BUTTON,0,0,400,380,40,20,0,255,255,255,255\n"
        // Gauge button with cells for groove and survival only, and a layer shown for hard-cleared songs.
        "#SRC_BUTTON,0,0,0,0,4,1,2,1,0,0,40,1,0,0\n#DST_BUTTON,0,0,400,420,40,20,0,255,255,255,255\n"
        "#SRC_IMAGE,0,0,2,0,1,1\n#DST_IMAGE,0,0,500,420,20,20,0,255,255,255,255,0,0,0,0,0,0,104,160,-625\n"
        // Master volume moves right over 100 px; the list position moves down over 50 px.
        "#SRC_SLIDER,0,0,2,0,1,1,1,1,0,0,1,100,17,0\n#DST_SLIDER,0,0,100,400,10,10,0,255,255,255,255\n"
        "#SRC_SLIDER,0,0,2,0,1,1,1,1,0,0,2,50,1,0\n#DST_SLIDER,0,0,600,100,10,10,0,255,255,255,255\n"
        "#SRC_SLIDER,0,0,2,0,1,1,1,1,0,0,1,100,99,0\n#DST_SLIDER,0,0,100,440,10,10,0,255,255,255,255\n";
    Options selectOptions;selectOptions.screen=ScreenType::Select;
    auto selectDocument=parse(selectPath,selectOptions);
    require(selectDocument&&!parse(selectPath),"select skin type validation");
    PreviewScene selectScene;
    require(selectScene.load(*selectDocument.document,selectDocument.diagnostics),"select scene load");
    require(selectScene.selectCenter()==2&&selectScene.selectBar(1,1280,960)->x==580&&
        selectScene.selectBar(2,640,480)->clickable,"authored bar coordinates and click bounds");
    SelectFrame selection;
    selection.rows[1]={L"Folder",L"",1,1};
    selection.rows[2]={L"Selected song",L"9",0,0};
    require(selectScene.prepare(first.drawing.Get())&&selectScene.updateSelectBanner(path.parent_path().parent_path()/"Shared"/"bg.BMP"),
        "select atlas and banner upload");
    first.render(selectScene,0,nullptr,&selection);
    first.pixel(460,135,255,255,255,"folder bar texture");
    first.pixel(297,129,0,255,0,"folder lamp texture");
    first.pixel(460,175,0,255,0,"song bar texture");
    first.pixel(270,175,0,255,0,"selected bar keeps its authored edge without a synthetic outline");
    first.pixel(50,30,16,32,64,"selected song banner");
    selectScene.updateSelectBanner({});
    first.render(selectScene,0,nullptr,&selection);
    first.pixel(50,30,0,0,0,"clear banner when leaving the song");

    first.pixel(410,310,0,255,0,"closed panel button shows its first state");
    first.pixel(410,350,0,255,0,"key mode button shows the first filter cell");
    first.pixel(410,390,0,0,0,"unsupported button is not drawn");
    first.pixel(105,405,255,255,255,"volume slider at zero");
    first.pixel(605,105,255,255,255,"list slider at the top");
    const auto panelButton=selectScene.selectControlAt(410,310);
    require(panelButton&&panelButton->button==1&&!panelButton->slider,"panel button hit test");
    require(!selectScene.selectControlAt(445,310)&&!selectScene.selectControlAt(410,390),"no control outside supported buttons");
    const auto keyModeButton=selectScene.selectControlAt(410,350);
    require(keyModeButton&&keyModeButton->button==11&&!keyModeButton->increment(405,350)&&keyModeButton->increment(435,350),
        "split button decrements on the left half and increments on the right");
    const auto volume=selectScene.selectControlAt(105,405);
    require(volume&&volume->slider==17&&selectScene.selectControlAt(205,405)&&!selectScene.selectControlAt(215,405),
        "slider hit area covers its travel");
    require(volume->sliderPosition(155,405)==.5f&&volume->sliderPosition(0,405)==0.f&&volume->sliderPosition(600,405)==1.f,
        "slider position follows the thumb centre and clamps");
    require(!selectScene.selectControlAt(605,105)&&!selectScene.selectControlAt(105,445),
        "list slider is inert without rows and unsupported sliders are not controls");
    for(const auto token:{"Unsupported select button: 99","Unsupported select slider: 99"})
        require(std::any_of(selectDocument.diagnostics.begin(),selectDocument.diagnostics.end(),
            [&](const auto& d){return d.message==token;}),"unsupported select control diagnostic");

    selection.panel=1;selection.keyMode=1;selection.volumes[0]=.5f;selection.canScroll=true;selection.listPosition=1;
    first.render(selectScene,0,nullptr,&selection);
    first.pixel(410,310,255,255,255,"open panel button shows its second state");
    first.pixel(410,350,255,255,255,"key mode button follows the filter");
    first.pixel(105,405,0,0,0,"volume thumb left its origin");
    first.pixel(155,405,255,255,255,"volume thumb at half travel");
    first.pixel(605,155,255,255,255,"list thumb at the bottom");
    const auto list=selectScene.selectControlAt(605,105);
    require(list&&list->slider==1&&list->sliderPosition(605,130)==.5f,"vertical list slider position");
    selection.keyMode=2;
    first.render(selectScene,0,nullptr,&selection);
    // No cell for the state: nothing is drawn, but the button can still be clicked to move to a shown state.
    first.pixel(410,350,0,0,0,"button without a cell for its state draws nothing");
    require(selectScene.selectControlAt(410,350).has_value(),"button without a cell for its state stays clickable");
    selectScene.releaseDevice();
    require(!selectScene.selectControlAt(410,310),"controls are dropped with the device");

    // Bar parts, level graphs, hover layers and the bar slide.
    Bytes levelDigits;
    for(int digit=0;digit<10;++digit)levelDigits.insert(levelDigits.end(),{static_cast<BYTE>(digit*25),0,100,255}); // BGRA
    save(path.parent_path().parent_path()/"Shared"/"digits.png",levelDigits,10,1);
    const auto barPath=path.parent_path()/"bars.lr2skin";
    std::ofstream(barPath)<<"#INFORMATION,5,Bar test,SevenMania\n#IMAGE,../Shared/sheet.png\n#IMAGE,../Shared/digits.png\n"
        "#SRC_BAR_BODY,0,0,0,0,1,1\n#BAR_CENTER,1\n#BAR_AVAILABLE,0,2\n"
        "#DST_BAR_BODY_OFF,0,0,100,100,200,30,0,255,255,255,255\n"
        "#DST_BAR_BODY_OFF,1,0,100,200,200,30,0,255,255,255,255\n"
        "#DST_BAR_BODY_OFF,2,0,100,300,200,30,0,255,255,255,255\n"
        // Positions are relative to the bar: flash to its left, the rest to its right.
        "#SRC_BAR_FLASH,0,0,2,0,1,1,1,1,0,0\n#DST_BAR_FLASH,0,0,-20,0,10,30,0,255,255,255,255\n"
        "#SRC_BAR_RANK,8,0,2,0,1,1,1,1,0,0\n#DST_BAR_RANK,8,0,210,0,10,10,0,255,255,255,255\n"
        "#SRC_BAR_LEVEL,0,1,0,0,10,1,10,1,0,0,0,0,2\n#DST_BAR_LEVEL,0,0,220,10,10,10,0,255,255,255,255\n"
        "#SRC_BAR_LAMP,5,0,2,0,1,1,1,1,0,0\n#DST_BAR_LAMP,5,0,250,0,10,10,0,255,255,255,255\n"
        // Level of the hyper chart growing right, of the another chart growing down, and an unknown type.
        "#SRC_BARGRAPH,0,0,2,0,1,1,1,1,0,0,7,0\n#DST_BARGRAPH,0,0,400,100,120,10,0,255,255,255,255,0,0,0,0,0,0,507\n"
        "#SRC_BARGRAPH,0,0,2,0,1,1,1,1,0,0,8,1\n#DST_BARGRAPH,0,0,400,200,10,120,0,255,255,255,255\n"
        "#SRC_BARGRAPH,0,0,2,0,1,1,1,1,0,0,77,0\n#DST_BARGRAPH,0,0,400,400,10,10,0,255,255,255,255\n"
        // Shown while no panel is open and the cursor is in the 10x20 strip 10 px into the picture.
        "#SRC_ONMOUSE,0,0,2,0,1,1,1,1,0,0,-1,10,0,10,20\n#DST_ONMOUSE,0,0,500,400,40,20,0,255,255,255,255\n"
        "#SRC_NUMBER,0,1,0,0,10,1,10,1,0,0,47,0,2\n#DST_NUMBER,0,0,550,100,10,10,0,255,255,255,255\n";
    auto barDocument=parse(barPath,selectOptions);
    PreviewScene barScene;
    require(barDocument&&barScene.load(*barDocument.document,barDocument.diagnostics),"bar part scene load");
    require(std::none_of(barDocument.diagnostics.begin(),barDocument.diagnostics.end(),
        [](const auto& d){return d.message.starts_with("Command preserved");}),"bar parts, graphs and hover layers are understood");
    require(std::count_if(barDocument.diagnostics.begin(),barDocument.diagnostics.end(),
        [](const auto& d){return d.message=="Unsupported bar graph: 77";})==1,"unknown graph type is reported");
    SelectFrame bars;
    bars.rows[0].title=L"Above";
    bars.rows[1].title=L"Selected";bars.rows[1].lamp=5;bars.rows[1].difficulty=3;bars.rows[1].levelValue=37;bars.rows[1].rank=8;bars.rows[1].row=42;
    bars.rows[2].title=L"Below";
    bars.songSelected=true;bars.keys=7;bars.folderCharts[2]=1;bars.folderLevels[2]=6;bars.folderCharts[3]=2;bars.folderLevels[3]=3;
    require(*bars.option(507)&&!*bars.option(502)&&*bars.option(512)&&!*bars.option(517)&&*bars.option(518)&&*bars.option(500)&&
        *bars.option(72)&&!*bars.option(77)&&*bars.option(620)&&!*bars.option(621),"folder difficulty conditions");
    require(*bars.graph(7)==.5f&&*bars.graph(8)==.25f&&*bars.graph(5)==0.f&&!bars.graph(77),"level graphs are a share of the level limit");
    require(barScene.prepare(first.drawing.Get()),"bar part upload");
    first.render(barScene,0,nullptr,&bars);
    first.pixel(85,215,255,255,255,"flash on the selected bar");
    first.pixel(85,115,0,0,0,"no flash on other bars");
    first.pixel(101,215,0,255,0,"the skin's flash replaces the outline");
    first.pixel(315,205,255,255,255,"rank on a bar with a score");
    first.pixel(315,305,0,0,0,"no rank without a score");
    first.pixel(355,205,255,255,255,"full combo lamp");
    first.pixel(325,215,100,0,75,"level tens digit");
    first.pixel(335,215,100,0,175,"level ones digit");
    first.pixel(335,315,0,0,0,"no level digits without a numeric level");
    first.pixel(455,105,255,255,255,"horizontal level graph, filled half");
    first.pixel(465,105,0,0,0,"horizontal level graph, empty half");
    first.pixel(405,225,255,255,255,"vertical level graph, filled quarter");
    first.pixel(405,235,0,0,0,"vertical level graph, empty part");
    first.pixel(405,405,0,0,0,"unknown graph is not drawn");
    first.pixel(505,405,0,0,0,"hover layer hidden without the cursor");
    first.pixel(565,105,100,0,150,"level number of a sibling difficulty");
    bars.mouseX=515;bars.mouseY=410;bars.scroll=.5f;
    first.render(barScene,0,nullptr,&bars);
    first.pixel(505,405,255,255,255,"hover layer under the cursor");
    first.pixel(110,265,0,255,0,"bar half way from the slot below");
    first.pixel(110,205,0,0,0,"bar has not reached its slot");
    first.pixel(85,265,255,255,255,"flash travels with the bar");
    const auto movingHit=barScene.selectControlAt(110,265);
    require(movingHit&&movingHit->row==42&&!barScene.selectControlAt(110,205),"hit testing follows the moving bar");
    bars.scroll=-.5f;bars.panel=1;bars.mouseX=505;bars.folderCharts[2]=0;bars.folderLevels[3]=20;
    first.render(barScene,0,nullptr,&bars);
    first.pixel(110,165,0,255,0,"bar half way from the slot above");
    first.pixel(505,405,0,0,0,"hover layer needs its panel state and its own rectangle");
    first.pixel(455,105,0,0,0,"graph condition follows the folder");
    first.pixel(405,315,255,255,255,"level beyond the limit fills the graph");
    require(*bars.option(78)&&!*bars.option(73),"level beyond the graph");
    barScene.releaseDevice();
    PreviewSurface wide(1920,1080);
    bars.scroll=0;
    require(barScene.prepare(wide.drawing.Get()),"wide select upload");
    wide.render(barScene,0,nullptr,&bars);
    wide.pixel(305,460,0,0,0,"letterbox leaves the bar centered at FHD");
    require(!barScene.selectControlAt(305,460),"letterbox hit testing uses the centered canvas");
    barScene.setStretch(true);
    wide.render(barScene,0,nullptr,&bars);
    wide.pixel(305,460,0,255,0,"stretch expands the bar across the FHD canvas");
    const auto stretchedHit=barScene.selectControlAt(305,460);
    require(stretchedHit&&stretchedHit->row==42,"stretched hit testing matches the pixels");
    barScene.releaseDevice();

    // State IDs: ranks, timers and numbers answered by a play or result frame.
    PlayFrame rated;
    rated.noteCount=100;rated.judgedNotes=100;rated.exScore=180;
    require(*rated.option(200)&&!*rated.option(201)&&*rated.option(340)&&*rated.option(220)&&*rated.option(221),
        "0.9 of the maximum is AAA now and secures AAA");
    rated.judgedNotes=50;rated.exScore=100;
    require(*rated.option(200)&&!*rated.option(223)&&*rated.option(224)&&*rated.option(227),
        "a perfect first half is AAA now but secures only C");
    rated.judgedNotes=0;rated.exScore=0;
    require(*rated.option(207)&&!*rated.option(206)&&!*rated.option(220),"nothing judged yet is rank F");
    rated.rhythmAge=120;
    require(rated.timer(140,999)==120&&rated.timer(151,500)==-1&&!*rated.option(161),"beat timer and inactive result timer");
    rated.result=true;
    require(rated.timer(150,500)==500&&rated.timer(152,500)==500,"result timers run with the scene");
    rated.noteCount=100;rated.exScore=150;rated.maxCombo=42;rated.judgementCounts={1,2,3,4,5};
    require(rated.number(76)==9&&rated.number(177)==9&&rated.number(115)==75&&rated.number(116)==0&&rated.number(171)==150&&
        rated.number(174)==42&&*rated.number(22)>=1&&*rated.number(22)<=12&&*rated.number(21)>=2026&&!rated.number(30),
        "miss count, total rate, result score and clock numbers");
    SelectFrame songState;
    require(*songState.option(624)&&!*songState.option(625)&&!*songState.option(601)&&!*songState.option(100)&&
        *songState.option(170)&&*songState.option(1)&&!songState.option(999),"select conditions without a song");
    require(!*songState.option(110)&&!*songState.option(117),"select rank conditions are false without a song");
    songState.songSelected=true;songState.clearLamp=4;songState.keys=7;songState.judgeRank=1;songState.hasLongNotes=true;songState.bpmChange=true;
    songState.rank=8;
    require(*songState.option(110)&&!*songState.option(111),"select rank AAA condition");
    songState.rank=1;
    require(*songState.option(117)&&!*songState.option(110),"select rank F condition");
    songState.rank=0;
    require(!*songState.option(117),"select rank conditions are false without a score");
    // Record numbers: an empty field is not drawn, and old records leave every new field empty.
    require(!songState.recordNumber(71)&&!songState.recordNumber(77)&&!songState.recordNumber(80)&&!songState.recordNumber(73),
        "an old record has no EX, count or judgement numbers");
    songState.record.noteCount=200;songState.record.exScore=300;songState.record.missCount=7;
    songState.record.playCount=5;songState.record.clearCount=3;songState.record.maxCombo=150;
    songState.record.judgements=std::array{120,60,10,4,6};
    require(songState.recordNumber(71)==300&&songState.recordNumber(73)==75&&songState.recordNumber(75)==150&&
        songState.recordNumber(76)==7&&songState.recordNumber(77)==5&&songState.recordNumber(78)==3&&songState.recordNumber(79)==2&&
        songState.recordNumber(80)==120&&songState.recordNumber(84)==6&&songState.recordNumber(85)==60&&songState.recordNumber(87)==5&&
        !songState.recordNumber(90),"select record numbers");
    songState.record.noteCount=0;
    require(!songState.recordNumber(73)&&!songState.recordNumber(85)&&songState.recordNumber(80)==120,"rates need a note count");
    songState.songSelected=false;
    require(!songState.recordNumber(71),"no record numbers without a song");
    songState.songSelected=true;
    // BAR_RANK and the rank conditions rank EX score in ninths of the maximum, as LR2 does.
    require(exScoreRank(0,100)==0&&exScoreRank(10,0)==0&&exScoreRank(201,100)==0&&exScoreRank(1,100)==1&&
        exScoreRank(44,100)==1&&exScoreRank(45,100)==2&&exScoreRank(177,100)==7&&exScoreRank(178,100)==8&&exScoreRank(200,100)==8,
        "EX score rank ninths");
    require(*songState.option(104)&&!*songState.option(103)&&*songState.option(160)&&!*songState.option(161)&&*songState.option(181)&&
        *songState.option(173)&&!*songState.option(172)&&*songState.option(177)&&*songState.option(170)&&*songState.option(5),
        "select conditions follow the selected chart");

    // The same conditions and the gauge button on screen.
    require(selectScene.prepare(first.drawing.Get()),"select scene re-upload");
    selection.keyMode=0;selection.panel=0;
    first.render(selectScene,0,nullptr,&selection);
    first.pixel(410,430,0,255,0,"gauge button shows the groove cell");
    first.pixel(510,430,0,0,0,"hard-clear layer is hidden without a selected hard clear");
    const auto gaugeButton=selectScene.selectControlAt(410,430);
    require(gaugeButton&&gaugeButton->button==40,"gauge button is clickable");
    selection.gaugeCell=1;selection.songSelected=true;selection.clearLamp=4;selection.keys=7;
    first.render(selectScene,0,nullptr,&selection);
    first.pixel(410,430,255,255,255,"gauge button follows the gauge mode");
    first.pixel(510,430,255,255,255,"layer conditions are evaluated every frame");
    selection.gaugeCell=3;
    first.render(selectScene,0,nullptr,&selection);
    first.pixel(410,430,0,0,0,"gauge without a cell in this skin draws nothing");
    require(selectScene.selectControlAt(410,430).has_value(),"gauge without a cell stays clickable so it can move on");
    require(selectScene.buttonStates(40)>0&&selectScene.buttonStates(40)<=3,"button states follow the skin's cells");
    selectScene.releaseDevice();

    // Option panel: buttons, conditions, labels, setting numbers, key timers and fixed sliders.
    const auto panelPath=path.parent_path()/"panel.lr2skin";
    std::ofstream(panelPath)<<"#INFORMATION,5,Panel test,SevenMania\n#IMAGE,../Shared/sheet.png\n#IMAGE,../Shared/digits.png\n"
        "#SRC_BAR_BODY,0,0,0,0,1,1\n#BAR_CENTER,0\n#BAR_AVAILABLE,0,0\n"
        "#DST_BAR_BODY_OFF,0,0,600,460,10,10,0,255,255,255,255\n"
        // Hi-speed: no picture and fully transparent, shown with panel 1, clickable only while panel 1 is open.
        "#SRC_BUTTON,0,0,0,0,0,0,0,0,0,0,57,1,1\n#DST_BUTTON,0,0,100,100,40,20,0,0,255,255,255,0,0,0,0,0,21\n"
        // Judge timing on a 1-pixel placeholder with two cells, plus-only.
        "#SRC_BUTTON,0,0,0,0,1,1,1,2,0,0,74,1,0,1\n#DST_BUTTON,0,0,100,140,40,20,0,255,255,255,255\n"
        // Random (SevenMania has none), lane cover off/on and the autoplay launcher.
        "#SRC_BUTTON,0,0,0,0,4,1,2,1,0,0,42,1,0,0\n#DST_BUTTON,0,0,200,100,40,20,0,255,255,255,255\n"
        "#SRC_BUTTON,0,0,0,0,4,1,2,1,0,0,46,1,0,0\n#DST_BUTTON,0,0,200,140,40,20,0,255,255,255,255\n"
        "#SRC_BUTTON,0,0,0,0,4,1,2,1,0,0,16,1,0,0\n#DST_BUTTON,0,0,200,180,40,20,0,255,255,255,255\n"
        // 1P key 1 held, the course editor, score saved and BGA off.
        "#SRC_IMAGE,0,0,2,0,1,1\n#DST_IMAGE,0,0,300,100,10,10,0,255,255,255,255,0,0,0,0,0,101\n"
        "#SRC_IMAGE,0,0,2,0,1,1\n#DST_IMAGE,0,0,320,100,10,10,0,255,255,255,255,0,0,0,0,0,17\n"
        "#SRC_IMAGE,0,0,2,0,1,1\n#DST_IMAGE,0,0,340,100,10,10,0,255,255,255,255,0,0,0,0,0,0,61\n"
        "#SRC_IMAGE,0,0,2,0,1,1\n#DST_IMAGE,0,0,360,100,10,10,0,255,255,255,255,0,0,0,0,0,0,40\n"
        // An EQ band over 100 px, the gauge label and the hi-speed number.
        "#SRC_SLIDER,0,0,2,0,1,1,1,1,0,0,1,100,10,0\n#DST_SLIDER,0,0,100,300,10,10,0,255,255,255,255\n"
        "#SRC_TEXT,0,0,65,0\n#DST_TEXT,0,0,100,200,100,20,0,255,255,255,255\n"
        "#SRC_NUMBER,0,1,0,0,10,1,10,1,0,0,10,0,3\n#DST_NUMBER,0,0,100,250,10,10,0,255,255,255,255\n";
    auto panelDocument=parse(panelPath,selectOptions);
    PreviewScene panelScene;
    require(panelDocument&&panelScene.load(*panelDocument.document,panelDocument.diagnostics),"panel scene load");
    for(const auto& d:panelDocument.diagnostics)
        require(!d.message.starts_with("Unsupported")&&
            !d.message.starts_with("DST_IMAGE condition")&&!d.message.starts_with("SRC_IMAGE rectangle"),d.message.c_str());
    require(SelectFrame::clickableButton(16)&&SelectFrame{}.buttonState(16)==0,"autoplay launcher is a plain clickable button");
    require(panelScene.prepare(first.drawing.Get()),"panel scene upload");
    SelectFrame options;
    options.hiSpeed=2.5;options.panelOn[0]=100;
    first.render(panelScene,0,nullptr,&options);
    first.pixel(210,110,0,255,0,"a missing feature shows its first cell");
    first.pixel(210,150,0,255,0,"lane cover off cell");
    first.pixel(305,105,0,0,0,"key timer waits for the key");
    first.pixel(325,105,0,0,0,"the course editor timer never starts");
    first.pixel(345,105,255,255,255,"score saved condition");
    first.pixel(365,105,0,0,0,"BGA on hides the BGA-off layer");
    first.pixel(155,305,255,255,255,"EQ slider rests at its centre");
    first.pixel(105,255,100,0,50,"hi-speed hundreds digit");
    first.pixel(115,255,100,0,125,"hi-speed tens digit");
    require(!panelScene.selectControlAt(210,110)&&!panelScene.selectControlAt(150,305),"missing features are not controls");
    require(!panelScene.selectControlAt(110,110),"panel-1 button is inert while panel 1 is closed");
    const auto judgeButton=panelScene.selectControlAt(110,150);
    require(judgeButton&&judgeButton->button==74&&judgeButton->increment(102,150)&&judgeButton->increment(138,150),
        "plus-only button on a placeholder picture");
    const auto laneCoverButton=panelScene.selectControlAt(210,150);
    require(laneCoverButton&&laneCoverButton->button==46,"lane cover button is clickable");
    const auto normalLabel=first.frameSignature();
    options.gaugeCell=1;
    first.render(panelScene,0,nullptr,&options);
    require(first.frameSignature()!=normalLabel,"gauge label follows the gauge");
    options.panel=1;options.keyOn[1]=50;options.laneCover=30;options.bgaEnabled=false;
    first.render(panelScene,0,nullptr,&options);
    const auto hsButton=panelScene.selectControlAt(110,110);
    require(hsButton&&hsButton->button==57&&!hsButton->increment(105,110)&&hsButton->increment(135,110),
        "invisible hi-speed button splits left and right while its panel is open");
    first.pixel(110,110,0,0,0,"invisible button draws nothing");
    first.pixel(210,150,255,255,255,"lane cover on cell");
    first.pixel(305,105,255,255,255,"key timer runs while the key is held");
    first.pixel(365,105,255,255,255,"BGA-off layer follows the setting every frame");
    panelScene.releaseDevice();

    SelectFrame panelState;
    require(panelState.optionText(65)==L"NORMAL"&&panelState.optionText(63)==L"OFF"&&panelState.optionText(1)==L"MY BEST"&&
        panelState.optionText(76)==L"ON"&&panelState.optionText(84)==L"OFF"&&panelState.optionText(60)==L"ALL"&&
        panelState.optionText(80)==L"OFF"&&panelState.optionText(99).empty()&&!SelectFrame::isOptionText(69),"default panel labels");
    panelState.gaugeCell=3;panelState.laneCover=10;panelState.keyMode=2;panelState.autoJudge=1;panelState.screenMode=2;
    require(panelState.optionText(65)==L"EASY"&&panelState.optionText(84)==L"SUDDEN"&&panelState.optionText(73)==L"ON"&&
        panelState.optionText(60)==L"7KEYS"&&panelState.optionText(80)==L"SUGGEST"&&panelState.optionText(79)==L"FULLSCREEN",
        "panel labels follow the settings");
    require(panelState.buttonState(50)==2&&panelState.buttonState(46)==1&&panelState.buttonState(72)==0&&
        panelState.buttonState(75)==1&&panelState.buttonState(42)==0&&panelState.buttonState(260)==0&&
        panelState.buttonState(16)==0&&!panelState.buttonState(200)&&!panelState.buttonState(99),"button cells");
    panelState.hiSpeedFix=5;
    require(panelState.buttonState(55)==5&&panelState.optionText(74)==L"CONSTANT"&&SelectFrame::clickableButton(55),
        "HI-SPEED FIX button and label");
    require(SelectFrame::clickableButton(57)&&SelectFrame::clickableButton(13)&&!SelectFrame::clickableButton(42)&&
        !SelectFrame::clickableButton(80),"only connected buttons take clicks");
    panelState.judgeTiming=-12;panelState.hiSpeed=1.75;
    require(panelState.settingNumber(12)==-12&&panelState.settingNumber(10)==175&&!panelState.settingNumber(11),"setting numbers");
    require(SelectFrame::fixedSlider(16)==.5f&&SelectFrame::fixedSlider(20)==0.f&&!SelectFrame::fixedSlider(17),"fixed sliders");
    // Clear saving follows the gauge; the score is always saved.
    require(*panelState.option(61)&&!*panelState.option(60)&&*panelState.option(63)&&!*panelState.option(64)&&
        !*panelState.option(62)&&!*panelState.option(66),"save conditions with the easy gauge");
    panelState.gaugeCell=1;
    require(*panelState.option(65)&&*panelState.option(43)&&!*panelState.option(42),"save and gauge conditions with the hard gauge");
    panelState.scoreGraph=false;panelState.bgaEnabled=false;
    require(*panelState.option(38)&&!*panelState.option(39)&&*panelState.option(40)&&!*panelState.option(41),
        "score graph and BGA conditions");
    // Options of the best play: none without a play, normal random and no lane effect with one.
    require(!*panelState.option(118)&&!*panelState.option(126)&&!*panelState.option(134),"no best-play options without a song");
    panelState.songSelected=true;panelState.clearLamp=4;panelState.bestGauge=1;
    require(*panelState.option(119)&&!*panelState.option(118)&&*panelState.option(126)&&!*panelState.option(127)&&
        *panelState.option(134)&&!*panelState.option(136)&&!*panelState.option(143),"best-play options");
    panelState.bestGauge=-1;
    require(!*panelState.option(118)&&!*panelState.option(119)&&!*panelState.option(121),"unknown best gauge");
    require(*panelState.option(174)&&!*panelState.option(175)&&*panelState.option(196)&&!*panelState.option(197)&&
        !*panelState.option(580)&&!*panelState.option(590)&&!*panelState.option(640)&&!*panelState.option(657),
        "missing features answer on their none side");

    // Play endings, score graph and song-position slider.
    const auto endingPath=path.parent_path()/"ending.lr2skin";
    {
        std::ofstream ending(endingPath);
        ending<<"#INFORMATION,0,Ending test,SevenMania\n#IMAGE,../Shared/sheet.png\n#FADEOUT,550\n#CLOSE,2050\n";
        for(int lane=0;lane<8;++lane)ending<<"#DST_NOTE,"<<lane<<",0,"<<10+lane*12<<",400,10,5\n";
        // Failed, fading out and full combo, left to right.
        for(const auto [x,timer]:{std::pair{300,3},{320,2},{340,48}})
            ending<<"#SRC_IMAGE,0,0,2,0,1,1\n#DST_IMAGE,0,0,"<<x<<",0,10,10,0,255,255,255,255,0,0,0,0,0,"<<timer<<"\n";
        // The score graph grows up from y=300; a best-score graph has nothing to show; 99 is unknown.
        ending<<"#SRC_BARGRAPH,0,0,2,0,1,1,1,1,0,0,10,1\n#DST_BARGRAPH,0,0,400,300,10,-200,0,255,255,255,255\n"
            "#SRC_BARGRAPH,0,0,2,0,1,1,1,1,0,0,12,1\n#DST_BARGRAPH,0,0,420,300,10,-200,0,255,255,255,255\n"
            "#SRC_BARGRAPH,0,0,2,0,1,1,1,1,0,0,99,1\n#DST_BARGRAPH,0,0,440,300,10,-200,0,255,255,255,255\n"
            // Song position moves down over 100 px; slider 4 is the skin's lane cover, moved by the drawn cover share.
            "#SRC_SLIDER,0,0,2,0,1,1,1,1,0,0,2,100,6,1\n#DST_SLIDER,0,0,500,100,10,10,0,255,255,255,255\n"
            "#SRC_SLIDER,0,0,2,0,1,1,1,1,0,0,2,100,4,1\n#DST_SLIDER,0,0,520,100,10,10,0,255,255,255,255\n"
            // The target graph at the end of the song; score-graph and auto-scratch conditions.
            "#SRC_BARGRAPH,0,0,2,0,1,1,1,1,0,0,15,1\n#DST_BARGRAPH,0,0,460,300,10,-200,0,255,255,255,255\n"
            "#SRC_IMAGE,0,0,2,0,1,1\n#DST_IMAGE,0,0,560,0,10,10,0,255,255,255,255,0,0,0,0,0,0,39\n"
            "#SRC_IMAGE,0,0,2,0,1,1\n#DST_IMAGE,0,0,580,0,10,10,0,255,255,255,255,0,0,0,0,0,0,55\n"
            // The 1P turntable: a 40x4 bar turned by built-in offset 1, and autoplay notes for lane 1.
            "#SRC_IMAGE,0,0,2,0,1,1\n#DST_IMAGE,0,0,560,100,40,4,0,255,255,255,255,0,0,0,5,0,0,0,0,0,1\n"
            "#SRC_AUTO_NOTE,1,0,2,0,1,1\n#SRC_AUTO_LN_BODY,1,0,2,0,1,1\n#SRC_AUTO_MINE,1,0,2,0,1,1\n"
            // Usual notes for lanes 1 and 2 from another cell; lane 2 has no autoplay parts.
            "#SRC_NOTE,1,0,3,0,1,1\n#SRC_NOTE,2,0,3,0,1,1\n#SRC_MINE,2,0,3,0,1,1\n";
    }
    auto endingDocument=parse(endingPath);
    PreviewScene endingScene;
    require(endingDocument&&endingScene.load(*endingDocument.document,endingDocument.diagnostics,true),"play ending scene load");
    require(endingScene.closeMilliseconds()==2050&&endingScene.fadeoutMilliseconds()==550,"closing times are read from the skin");
    const auto reported=[&](std::string_view message) {
        return std::count_if(endingDocument.diagnostics.begin(),endingDocument.diagnostics.end(),
            [&](const auto& d){return d.message==message;});
    };
    require(reported("Unsupported bar graph: 99")==1&&!reported("Unsupported play slider: 4")&&endingScene.hasLaneCover()&&
        !reported("Command preserved for later phases: FADEOUT")&&!reported("Command preserved for later phases: CLOSE")&&
        !reported("Unsupported destination timer: 3")&&!reported("Unsupported destination offset: 1")&&
        !reported("Command preserved for later phases: SRC_AUTO_NOTE")&&
        !reported("DST_IMAGE condition unavailable in preview: 39")&&
        !reported("DST_IMAGE condition unavailable in preview: 55"),"play ending diagnostics");
    PlayFrame ending;
    ending.playAge=1000;ending.noteCount=100;ending.judgedNotes=50;ending.exScore=100;ending.elapsed=30;ending.remaining=30;
    {
        PlayFrame target=ending;
        require(*target.graph(12)==0.f&&*target.graph(15)==0.f&&*target.number(151)==0&&*target.number(108)==100,
            "no best score: target graphs empty, difference is the score itself");
        target.bestExScore=160;target.targetExScore=160;
        require(*target.graph(12)==.4f&&*target.graph(13)==.8f&&*target.graph(14)==.4f&&*target.graph(15)==.8f&&
            *target.number(151)==160&&*target.number(108)==20,"best and target graphs follow the song's progress");
        require(*target.option(39)&&!*target.option(38)&&!*target.option(55)&&*target.option(54),"score graph shown, no auto scratch");
        target.scoreGraph=false;
        require(!*target.option(39)&&*target.option(38),"score graph hidden");
    }
    require(*ending.graph(1)==.5f&&*ending.graph(10)==.5f&&*ending.graph(11)==1.f&&*ending.graph(13)==0.f&&!ending.graph(99)&&
        ending.timer(3,0)==-1&&ending.timer(2,0)==-1&&ending.timer(48,0)==-1,"play graphs and idle ending timers");
    require(endingScene.prepare(first.drawing.Get()),"play ending upload");
    {
        // SRC_AUTO_* replace the usual parts only during autoplay, and only the parts the skin declares.
        using sevenmania::skin::lr2::PlayNotePart;
        require(endingScene.noteSprite(1,0).bitmap&&endingScene.noteSprite(1,0).x==3&&
            !endingScene.noteSprite(1,0,PlayNotePart::Mine).bitmap,"usual notes outside autoplay");
        endingScene.setAutoplay(true);
        require(endingScene.noteSprite(1,0).x==2&&endingScene.noteSprite(1,0,PlayNotePart::Mine).x==2&&
            endingScene.noteSprite(1,0,PlayNotePart::Body).x==2&&endingScene.noteSprite(1,0,PlayNotePart::HeldBody).x==2,
            "autoplay draws SRC_AUTO_NOTE, LN body (also held) and mine");
        require(endingScene.noteSprite(2,0).x==3&&endingScene.noteSprite(2,0,PlayNotePart::Mine).x==3,
            "a lane without autoplay parts keeps its usual notes");
        PlayFrame automatic;
        automatic.autoplay=true;
        require(*automatic.option(33)&&!*automatic.option(32)&&*PlayFrame{}.option(32)&&!*PlayFrame{}.option(33),
            "autoplay options 32/33");
        endingScene.setAutoplay(false);
        require(endingScene.noteSprite(1,0).x==3,"usual notes again after autoplay");
    }
    ending.laneCoverShare=.5f;
    first.render(endingScene,1000,&ending);
    first.pixel(525,155,255,255,255,"half-closed lane cover slider moves half its range");
    first.pixel(525,105,0,0,0,"lane cover slider left its open position");
    ending.laneCoverShare=0;
    first.render(endingScene,1000,&ending);
    first.pixel(305,5,0,0,0,"no failure layer during play");
    first.pixel(405,250,255,255,255,"score graph filled from the bottom");
    first.pixel(405,150,0,0,0,"score graph empty above the score");
    first.pixel(425,250,0,0,0,"best-score graph has no record to show");
    first.pixel(505,155,255,255,255,"song position slider half way");
    first.pixel(505,105,0,0,0,"song position slider left its origin");
    first.pixel(525,105,255,255,255,"an open lane cover slider stays at its position");
    first.pixel(565,5,255,255,255,"score graph option is on");
    first.pixel(585,5,0,0,0,"auto scratch option is off");
    first.pixel(575,101,255,255,255,"turntable bar lies flat");
    first.pixel(580,115,0,0,0,"turntable bar not yet turned");
    ending.turntableAngle[0]=90;
    first.render(endingScene,1000,&ending);
    first.pixel(575,101,0,0,0,"turned turntable leaves its flat position");
    first.pixel(580,115,255,255,255,"turntable bar turned upright about its centre");
    ending.turntableAngle[0]=0;
    ending.bestExScore=160;ending.targetExScore=160;
    first.render(endingScene,1000,&ending);
    first.pixel(465,250,255,255,255,"target graph filled to the target");
    first.pixel(465,135,0,0,0,"target graph empty above the target");
    first.pixel(425,250,255,255,255,"best-score graph at the best's pace");
    ending.bestExScore.reset();ending.targetExScore=0;
    ending.failedAge=100;
    first.render(endingScene,1000,&ending);
    first.pixel(305,5,255,255,255,"failure layer");
    first.pixel(325,5,0,0,0,"no fade on failure");
    ending.failedAge=-1;ending.fadeoutAge=100;ending.fullComboAge=100;
    first.render(endingScene,1000,&ending);
    first.pixel(325,5,255,255,255,"closing fade layer");
    first.pixel(345,5,255,255,255,"full combo layer");
    endingScene.releaseDevice();

    // A 5KEY play skin places the scratch and five keys only.
    const auto fiveKeyPath=path.parent_path()/"five.lr2skin";
    {
        std::ofstream five(fiveKeyPath);
        five<<"#INFORMATION,1,Five key test,SevenMania\n#IMAGE,../Shared/sheet.png\n#CLOSE,1000\n";
        for(int lane=0;lane<6;++lane)five<<"#SRC_NOTE,"<<lane<<",0,2,0,1,1\n#DST_NOTE,"<<lane<<",0,"<<100+lane*20<<",400,20,5\n";
        // Shown for 5KEY charts, and hidden for 7KEY ones.
        five<<"#SRC_IMAGE,0,0,2,0,1,1\n#DST_IMAGE,0,0,300,0,10,10,0,255,255,255,255,0,0,0,0,0,0,161\n"
            "#SRC_IMAGE,0,0,2,0,1,1\n#DST_IMAGE,0,0,320,0,10,10,0,255,255,255,255,0,0,0,0,0,0,160\n";
    }
    Options fiveKeyOptions;fiveKeyOptions.screen=ScreenType::Play5Key;
    auto fiveKeyDocument=parse(fiveKeyPath,fiveKeyOptions);
    require(fiveKeyDocument&&!parse(fiveKeyPath),"a 5KEY skin is not accepted as a 7KEY one");
    PreviewScene fiveKeyScene;
    require(fiveKeyScene.load(*fiveKeyDocument.document,fiveKeyDocument.diagnostics,true)&&fiveKeyScene.closeMilliseconds()==1000,
        "5KEY play scene loads with six lanes");
    require(std::none_of(fiveKeyDocument.diagnostics.begin(),fiveKeyDocument.diagnostics.end(),
        [](const auto& d){return d.message.starts_with("Using native note")||d.message.starts_with("Command preserved");}),
        "lanes a 5KEY skin lacks are not reported");
    const auto fiveLanes=fiveKeyScene.playLanes(640,480);
    require(fiveLanes&&(*fiveLanes)[0].left==100&&(*fiveLanes)[5].right==220&&(*fiveLanes)[6].left==220&&(*fiveLanes)[7].right==220&&
        (*fiveLanes)[7].noteBottom==(*fiveLanes)[5].noteBottom,"unused lanes collapse onto the right edge");
    const auto fittedLanes=fiveKeyScene.playLanes(1280,720);
    fiveKeyScene.setStretch(true);
    const auto stretchedLanes=fiveKeyScene.playLanes(1280,720);
    require(fittedLanes&&stretchedLanes&&(*fittedLanes)[0].left==310&&(*stretchedLanes)[0].left==200&&
        (*stretchedLanes)[0].noteBottom==(*fittedLanes)[0].noteBottom,"wide output stretches note lanes horizontally");
    fiveKeyScene.setStretch(false);
    PlayFrame fiveKeyFrame;
    fiveKeyFrame.keys=5;fiveKeyFrame.playAge=0;
    require(*fiveKeyFrame.option(161)&&!*fiveKeyFrame.option(160)&&*PlayFrame{}.option(160),"key mode conditions");
    require(fiveKeyScene.prepare(first.drawing.Get()),"5KEY upload");
    first.render(fiveKeyScene,0,&fiveKeyFrame);
    first.pixel(305,5,255,255,255,"5KEY layer");
    first.pixel(325,5,0,0,0,"7KEY layer hidden");
    fiveKeyScene.releaseDevice();

    // A 14KEY skin places LR2 lanes 0-7 and 10-17, and draws each side's judgement.
    const auto doublePath=path.parent_path()/"double.lr2skin";
    {
        std::ofstream dp(doublePath);
        dp<<"#INFORMATION,2,Double test,SevenMania\n#IMAGE,../Shared/sheet.png\n";
        for(int lane:{0,1,2,3,4,5,6,7,10,11,12,13,14,15,16,17})
            dp<<"#SRC_NOTE,"<<lane<<",0,2,0,1,1\n#DST_NOTE,"<<lane<<",0,"<<(lane<10?20+lane*20:360+(lane-10)*20)<<",400,20,5\n";
        dp<<"#SRC_NOWJUDGE_1P,5,0,2,0,1,1,1,1\n#DST_NOWJUDGE_1P,5,0,100,100,10,10,0,255,255,255,255\n"
            "#SRC_NOWJUDGE_2P,5,0,2,0,1,1,1,1\n#DST_NOWJUDGE_2P,5,0,400,100,10,10,0,255,255,255,255\n"
            "#SRC_IMAGE,0,0,2,0,1,1\n#DST_IMAGE,0,0,300,0,10,10,0,255,255,255,255,0,0,0,0,0,0,162\n";
    }
    Options doubleOptions;doubleOptions.screen=ScreenType::Play14Key;
    auto doubleDocument=parse(doublePath,doubleOptions);
    PreviewScene doubleScene;
    require(doubleDocument&&doubleScene.load(*doubleDocument.document,doubleDocument.diagnostics,true),"14KEY play scene loads");
    const auto doubleLanes=doubleScene.playLanes(640,480);
    require(doubleLanes&&(*doubleLanes)[0].left==20&&(*doubleLanes)[7].left==160&&(*doubleLanes)[8].left==360&&
        (*doubleLanes)[15].left==500,"SevenMania lanes 8-15 use LR2 lanes 10-17");
    PlayFrame doubleFrame;
    doubleFrame.keys=14;doubleFrame.judge2P=5;doubleFrame.judgementAge2P=0;doubleFrame.playAge=0;
    require(*doubleFrame.option(162)&&!*doubleFrame.option(160)&&*doubleFrame.option(247)&&!*doubleFrame.option(241),
        "14KEY and 2P judgement conditions");
    require(doubleScene.prepare(first.drawing.Get()),"14KEY upload");
    first.render(doubleScene,0,&doubleFrame);
    first.pixel(305,5,255,255,255,"14KEY layer");
    first.pixel(405,105,255,255,255,"2P judgement");
    first.pixel(105,105,0,0,0,"1P judgement stays hidden");
    doubleScene.releaseDevice();
    require(lr2Lane(2,8)==10&&lr2Lane(3,6)==-1&&lr2Lane(3,13)==15&&lr2Lane(4,0)==1&&lr2Lane(4,8)==9&&lr2Lane(0,8)==-1&&
        lr2Side(2,9)==1&&lr2Side(4,8)==0,"lane mapping");

    // 9KEY uses LR2 lanes 1-9.
    const auto nineSkinPath=path.parent_path()/"nine.lr2skin";
    {
        std::ofstream nine(nineSkinPath);
        nine<<"#INFORMATION,4,Nine test,SevenMania\n#IMAGE,../Shared/sheet.png\n";
        for(int lane=1;lane<=9;++lane)nine<<"#SRC_NOTE,"<<lane<<",0,2,0,1,1\n#DST_NOTE,"<<lane<<",0,"<<lane*30<<",400,20,5\n";
    }
    Options nineOptions;nineOptions.screen=ScreenType::Play9Key;
    auto nineDocument=parse(nineSkinPath,nineOptions);
    PreviewScene nineScene;
    require(nineDocument&&nineScene.load(*nineDocument.document,nineDocument.diagnostics,true),"9KEY play scene loads");
    const auto nineLanes=nineScene.playLanes(640,480);
    require(nineLanes&&(*nineLanes)[0].left==30&&(*nineLanes)[8].left==270&&(*nineLanes)[9].left==290&&(*nineLanes)[9].right==290,
        "9KEY buttons use LR2 lanes 1-9");

    // The result screen's gauge chart and the previous best combo.
    const auto chartPath=path.parent_path()/"chart.lr2skin";
    std::ofstream(chartPath)<<"#INFORMATION,7,Chart test,SevenMania\n#IMAGE,../Shared/sheet.png\n#IMAGE,../Shared/digits.png\n"
        // A 200x100 field whose bottom-left corner is (100,300), revealed over the first second. Red for
        // the normal gauge, green for the hard gauge.
        "#SRC_GAUGECHART_1P,0,0,2,0,1,1,1,1,0,0,200,100,0,1000\n#DST_GAUGECHART_1P,0,0,100,300,4,4,0,255,255,0,0\n"
        "#SRC_GAUGECHART_1P,1,0,2,0,1,1,1,1,0,0,200,100,0,1000\n#DST_GAUGECHART_1P,1,0,100,300,4,4,0,255,0,255,0\n"
        "#SRC_NUMBER,0,1,0,0,10,1,10,1,0,0,173,0,2\n#DST_NUMBER,0,0,500,100,10,10,0,255,255,255,255\n"
        "#SRC_NUMBER,0,1,0,0,10,1,10,1,0,0,170,0,2\n#DST_NUMBER,0,0,500,120,10,10,0,255,255,255,255\n"
        "#SRC_NUMBER,0,1,0,0,10,1,10,1,0,0,152,0,2\n#DST_NUMBER,0,0,500,140,10,10,0,255,255,255,255\n";
    Options chartOptions;chartOptions.screen=ScreenType::Result;
    auto chartDocument=parse(chartPath,chartOptions);
    ScreenData played;
    played.numbers.result=true;played.numbers.bestMaxCombo=42;
    played.numbers.gaugeHistory={0,100,100,-1}; // Rises, holds, and stops before the last quarter.
    PreviewScene chartScene;
    require(chartDocument&&chartScene.load(*chartDocument.document,chartDocument.diagnostics,false,&played)&&chartScene.layerCount()==5,
        "gauge chart scene load");
    require(chartScene.prepare(first.drawing.Get()),"gauge chart upload");
    first.render(chartScene,1000);
    first.pixel(200,200,255,0,0,"gauge line at full for the normal gauge");
    first.pixel(280,200,0,0,0,"no line where the play never got to");
    first.pixel(200,280,0,0,0,"nothing under the line");
    first.pixel(515,105,100,0,50,"best max combo before this play");
    first.pixel(515,125,0,0,0,"legacy records leave previous EX hidden");
    first.render(chartScene,500);
    first.pixel(200,200,0,0,0,"gauge line is revealed over time");
    chartScene.releaseDevice();
    played.numbers.bestExScore=42;
    played.numbers.exScore=45;
    require(played.numbers.number(150)==42&&played.numbers.number(172)==3,"result EX comparison aliases");
    {
        PlayFrame target=played.numbers;
        target.noteCount=40;
        require(!target.number(120)&&!target.number(130)&&!target.number(176)&&target.number(121)==42&&
            target.number(122)==52&&target.number(123)==50&&target.number(125)==42&&target.number(126)==40,
            "target numbers of a record saved before judgements were kept");
        target.bestPoints=150000;target.bestMissCount=3;target.bestJudgements=std::array{20,2,1,1,2};
        require(target.number(120)==150000&&target.number(130)==20&&target.number(134)==2&&target.number(176)==3,
            "MY BEST target judgements and previous min BP");
        target.result=false;
        require(!target.number(121)&&!target.number(176),"target numbers are result-only");
    }
    require(chartScene.load(*chartDocument.document,chartDocument.diagnostics,false,&played)&&
        chartScene.prepare(first.drawing.Get()),"result with a previous EX score");
    first.render(chartScene,1000);
    first.pixel(515,125,100,0,50,"best EX digits are drawn");
    first.pixel(515,145,100,0,75,"EX comparison digits are drawn");
    played.numbers.exScore=39;
    require(played.numbers.number(152)==-3,"EX difference remains signed");
    chartScene.releaseDevice();

    const auto resultPath=path.parent_path()/"result.lr2skin";
    std::ofstream(resultPath)<<"#INFORMATION,7,Result test,SevenMania\n#IMAGE,../Shared/sheet.png\n"
        "#STARTINPUT,500,1600,500\n#SCENETIME,3000\n#FADEOUT,550\n"
        "#SRC_IMAGE,0,0,0,0,1,1\n#DST_IMAGE,0,0,0,0,10,10,0,255,255,255,255,0,0,0,0,0,151\n"
        "#SRC_IMAGE,0,111,0,0,1,1\n"
        "#DST_IMAGE,0,0,40,0,10,10,0,0,255,255,255,1,0,0,0,500,2\n"
        "#DST_IMAGE,0,500,40,0,10,10,0,255,255,255,255\n"
        "#SRC_IMAGE,0,0,2,0,1,1\n#DST_IMAGE,0,0,20,0,10,10,0,255,255,255,255,0,0,0,0,0,3\n";
    Options resultOptions;resultOptions.screen=ScreenType::Result;
    auto resultDocument=parse(resultPath,resultOptions);
    PreviewScene resultScene;
    require(resultDocument&&resultScene.load(*resultDocument.document,resultDocument.diagnostics)&&resultScene.layerCount()==2,
        "result graph and fadeout timers are accepted on a result skin");
    require(resultScene.startInputMilliseconds()==500&&resultScene.sceneMilliseconds()==3000&&
        resultScene.fadeoutMilliseconds()==550,"result timing declarations");
    require(std::count_if(resultDocument.diagnostics.begin(),resultDocument.diagnostics.end(),[](const auto& d){
        return d.message.starts_with("Unsupported destination timer");})==1,"timers without a source are still reported");
    require(resultScene.prepare(first.drawing.Get()),"result scene upload");
    first.render(resultScene,2000);
    first.pixel(45,5,0,0,0,"result exit layer is hidden before exit");
    first.render(resultScene,2000,nullptr,nullptr,ScreenTimers{250});
    first.pixel(45,5,128,128,128,"result exit uses its own elapsed time");
    first.render(resultScene,2000,nullptr,nullptr,ScreenTimers{520});
    first.pixel(45,5,255,255,255,"result exit holds the last frame");
    first.render(resultScene,100);
    first.pixel(5,5,0,255,0,"result timer layer is drawn");
    resultScene.releaseDevice();
    require(std::none_of(resultDocument.diagnostics.begin(),resultDocument.diagnostics.end(),[](const auto& d){
        return d.message=="Gameplay timer simulated with the preview clock";}),"result timers 2 and 151 are real screen timers");

    // Result #STARTINPUT,500,1600,500: input from 500 ms; the score update (timer 152) at 1600 ms or at the
    // first key; a key leaves 500 ms after the update.
    {
        const auto input=resultScene.resultInput();
        require(input.input==500&&input.rank==1600&&input.update==500,"result STARTINPUT rank and update times");
        require(resultKey(input,400,-1)==ResultKey::Ignored&&resultKey(input,600,-1)==ResultKey::ShowUpdate&&
            resultUpdateStart(input,600)==600&&resultUpdateAge(input,700,600)==100&&
            resultKey(input,900,600)==ResultKey::Ignored&&resultKey(input,1100,600)==ResultKey::Leave,
            "a key shows the update early and a later key leaves");
        require(resultUpdateAge(input,1500,-1)<0&&resultUpdateAge(input,1700,-1)==100&&
            resultKey(input,2000,-1)==ResultKey::Ignored&&resultKey(input,2100,-1)==ResultKey::Leave,
            "without a key the update shows at the rank time");
        const ResultInput immediate{500,0,0};
        require(resultUpdateAge(immediate,0,-1)==0&&resultKey(immediate,500,-1)==ResultKey::Leave,
            "a skin without a rank time shows the update at once, as before");
    }
    const auto updatePath=path.parent_path()/"result-update.lr2skin";
    std::ofstream(updatePath)<<"#INFORMATION,7,Update test,SevenMania\n#IMAGE,../Shared/sheet.png\n"
        "#STARTINPUT,500,1600,500\n"
        "#SRC_IMAGE,0,0,0,0,1,1\n#DST_IMAGE,0,0,0,0,10,10,0,255,255,255,255,0,0,0,0,0,152\n";
    auto updateDocument=parse(updatePath,resultOptions);
    PreviewScene updateScene;
    require(updateDocument&&updateScene.load(*updateDocument.document,updateDocument.diagnostics)&&updateScene.prepare(first.drawing.Get()),
        "result update scene");
    ScreenTimers hidden;hidden.updateScore=-1;
    first.render(updateScene,1000,nullptr,nullptr,hidden);
    first.pixel(5,5,0,0,0,"timer 152 layer waits for the score update");
    ScreenTimers shown;shown.updateScore=10;
    first.render(updateScene,1000,nullptr,nullptr,shown);
    first.pixel(5,5,0,255,0,"timer 152 layer appears with the score update");
    first.render(updateScene,1000);
    first.pixel(5,5,0,255,0,"without a host clock timer 152 follows the scene");
    updateScene.releaseDevice();

    // Result conditions from the record before the play.
    {
        ResultState state;
        state.cleared=true;state.exScore=170;state.noteCount=100;state.maxCombo=90;state.missCount=4;
        state.played=true;state.bestExScore=150;state.bestMaxCombo=95;state.bestMissCount=6;
        auto conditions=sevenmania::skin::lr2::resultOptions(state);
        // 170/200 is AA (7/9 <= 0.85 < 8/9); 150/200 is A.
        require(conditions[90]&&!conditions[91]&&conditions[301]&&!conditions[300]&&conditions[341]&&conditions[322]&&!conditions[321],
            "this play's rank and the best rank before it");
        require(conditions[330]&&conditions[336]&&!conditions[331]&&conditions[332]&&conditions[335],"score, miss and rank updates");
        require(conditions[50]&&!conditions[51]&&!conditions[333]&&!conditions[334]&&!conditions[352]&&!conditions[353]&&!conditions[354]&&
            conditions[350]&&!conditions[351]&&!conditions[11]&&!conditions[13],"IR, trial, battle and flip are fixed to none");
        state.exScore=150;state.maxCombo=96;state.missCount=6;
        conditions=sevenmania::skin::lr2::resultOptions(state);
        require(!conditions[330]&&conditions[331]&&!conditions[332]&&!conditions[335],"an equal score or miss count is not a new record");
        state.played=false;state.bestExScore.reset();state.bestMaxCombo.reset();state.bestMissCount.reset();
        conditions=sevenmania::skin::lr2::resultOptions(state);
        require(conditions[330]&&conditions[331]&&conditions[332]&&conditions[335]&&
            std::none_of(conditions.begin(),conditions.end(),[](const auto& o){return o.first>=320&&o.first<=327&&o.second;}),
            "a first play sets every record and has no best rank before it");
        state.played=true;
        conditions=sevenmania::skin::lr2::resultOptions(state);
        require(!conditions[330]&&!conditions[331]&&!conditions[332]&&!conditions[335],"an old record without the values updates nothing");
        state.played=false;state.recordSaved=false;
        conditions=sevenmania::skin::lr2::resultOptions(state);
        require(!conditions[330]&&!conditions[331]&&!conditions[332]&&!conditions[335],"practice plays set no record");
        require(conditions[32]&&!conditions[33],"a played result is not autoplay");
        state.autoplay=true;
        conditions=sevenmania::skin::lr2::resultOptions(state);
        require(conditions[33]&&!conditions[32]&&!conditions[330]&&!conditions[331]&&!conditions[332]&&!conditions[335]&&!conditions[336],
            "an autoplay result shows option 33 and sets no record");
        state.autoplay=false;
        state.exScore=0;
        conditions=sevenmania::skin::lr2::resultOptions(state);
        require(conditions[308]&&conditions[347]&&std::none_of(conditions.begin(),conditions.end(),[](const auto& o){return o.first>=300&&o.first<=307&&o.second;}),
            "no score has no result rank");
    }

    // Decide: #SCENETIME then timer 2 for #FADEOUT; a key after #SKIP starts timer 3 instead.
    {
        require(decideDuration(3000,550)==3550&&decideDuration(0,550)==0,"decide length includes the fade");
        require(decideFadeoutAge(3000,2999,-1)<0&&decideFadeoutAge(3000,3100,-1)==100&&decideFadeoutAge(3000,3100,1000)<0,
            "decide timer 2 runs after the scene time unless skipped");
        require(decideSkipAge(1100,1000)==100&&decideSkipAge(1100,-1)<0,"decide timer 3 runs from the skip");
        require(!decideCanSkip(250,300,3000,280,-1)&&decideCanSkip(250,300,3000,300,-1)&&!decideCanSkip(250,300,3000,400,350)&&
            !decideCanSkip(250,300,3000,3100,-1)&&!decideCanSkip(0,300,3000,1000,-1),"decide skip window");
        const auto decidePath=path.parent_path()/"decide-timers.lr2skin";
        std::ofstream(decidePath)<<"#INFORMATION,6,Decide test,SevenMania\n#IMAGE,../Shared/sheet.png\n"
            "#STARTINPUT,300\n#SCENETIME,3000\n#FADEOUT,550\n#SKIP,250\n"
            "#SRC_IMAGE,0,0,0,0,1,1\n#DST_IMAGE,0,0,0,0,10,10,0,255,255,255,255,0,0,0,0,0,2\n"
            "#SRC_IMAGE,0,0,1,0,1,1\n#DST_IMAGE,0,0,20,0,10,10,0,255,255,255,255,0,0,0,0,0,3\n"
            "#IF,990\n#SRC_IMAGE,0,0,2,0,1,1\n#DST_IMAGE,0,0,40,0,10,10,0,255,255,255,255\n#ENDIF\n";
        Options decideOptions;decideOptions.screen=ScreenType::Decide;
        auto decideDocument=parse(decidePath,decideOptions);
        PreviewScene decideScene;
        require(decideDocument&&decideScene.load(*decideDocument.document,decideDocument.diagnostics)&&decideScene.layerCount()==2&&
            decideScene.fadeoutMilliseconds()==550&&decideScene.startInputMilliseconds()==300,"decide timers and timing load");
        require(std::none_of(decideDocument.diagnostics.begin(),decideDocument.diagnostics.end(),[](const auto& d){
            return d.message.starts_with("Unsupported destination timer")||d.message.starts_with("Command preserved")||
                d.message.starts_with("Unknown condition ID")||d.message=="Gameplay timer simulated with the preview clock";}),
            "decide timers, timing commands and an unset skin option raise no warning");
        require(decideScene.prepare(first.drawing.Get()),"decide upload");
        first.render(decideScene,1000);
        first.pixel(5,5,0,0,0,"decide fade waits for the scene time");
        first.pixel(25,5,0,0,0,"decide skip layer waits for a skip");
        ScreenTimers fading;fading.fadeout=10;
        first.render(decideScene,3010,nullptr,nullptr,fading);
        first.pixel(5,5,0,255,0,"decide fade layer");
        ScreenTimers skipped;skipped.skip=10;
        first.render(decideScene,1010,nullptr,nullptr,skipped);
        first.pixel(25,5,0,255,0,"decide skip layer");
        decideScene.releaseDevice();
    }

    // Select numbers 30-37: every record added up.
    {
        SelectFrame totals;
        totals.totals={12,9,{1000,200,30,4,5000000000LL}};
        require(totals.totalNumber(30)==12&&totals.totalNumber(31)==9&&totals.totalNumber(32)==3&&totals.totalNumber(33)==1000&&
            totals.totalNumber(36)==4&&totals.totalNumber(37)==std::numeric_limits<int>::max()&&!totals.totalNumber(38)&&
            SelectFrame::isTotalNumber(30)&&SelectFrame::isTotalNumber(37)&&!SelectFrame::isTotalNumber(29),
            "total play numbers, saturating instead of wrapping");
    }

    const auto groups=summarizeDiagnostics({
        {{},3,"Unsupported destination timer: 3",false},{{},4,"Unsupported destination timer: 3",false},
        {{},5,"Unsupported destination timer: 140",false},{{},6,"DST_IMAGE condition unavailable in preview: -32",false},
        {{},7,"Command preserved for later phases: SRC_BAR_TITLE",false},{{},8,"IMAGE 4 (a.gif): Unsupported image format",false},
        {{},9,"IMAGE 7 (b.gif): Unsupported image format",false},{{},0,"Missing or out-of-bounds DST_NOTE lane 2",true}});
    require(groups.size()==5&&groups[0].error&&groups[0].ids.contains(2)&&
        groups[1].category=="Unsupported destination timer"&&groups[1].count==3&&groups[1].ids.at(3)==2&&groups[1].ids.at(140)==1,
        "diagnostics grouped by category with errors first and IDs counted");
    require(std::any_of(groups.begin(),groups.end(),[](const auto& g){return g.ids.contains(-32);})&&
        std::any_of(groups.begin(),groups.end(),[](const auto& g){return g.names.contains("SRC_BAR_TITLE");})&&
        std::any_of(groups.begin(),groups.end(),[](const auto& g){return g.category=="IMAGE: Unsupported image format"&&g.count==2;}),
        "negative IDs, command names and per-file image failures are grouped");
    require(formatDiagnosticSummary(groups).find("warning 3x  Unsupported destination timer  [3x2 140]\n")!=std::string::npos,
        "summary line format");
    std::cout<<checks<<" LR2 preview checks passed\n";
    return 0;
} catch(const std::exception& e) {
    std::cerr<<e.what()<<"\n";
    return 1;
}
