#include "skin/SkinPackage.h"
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <functional>

using namespace sevenmania::skin;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Bytes bytes(const std::string& s){return {s.begin(),s.end()};}
const Bytes pixel{0x89,0x50,0x4e,0x47,0xd,0xa,0x1a,0xa,0,0,0,0xd,0x49,0x48,0x44,0x52,0,0,0,1,0,0,0,1,8,6,0,0,0,0x1f,0x15,0xc4,0x89,0,0,0,0xb,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0xf8,0xf,4,0,9,0xfb,3,0xfd,0xfb,0x5e,0x6b,0x2b,0,0,0,0,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};
std::uint32_t crc(const Bytes& data){std::uint32_t c=0xffffffff;for(auto b:data){c^=b;for(int i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320:0);}return ~c;}
void u16(Bytes& b,std::uint16_t v){b.push_back(v&255);b.push_back(v>>8);}
void u32(Bytes& b,std::uint32_t v){u16(b,std::uint16_t(v));u16(b,std::uint16_t(v>>16));}
void put32(Bytes& b,std::size_t at,std::uint32_t v){for(int i=0;i<4;++i)b.at(at+i)=static_cast<std::uint8_t>(v>>(i*8));}
struct Entry {std::string name;Bytes data;std::uint16_t method=0,flags=0;std::uint32_t attr=0100644u<<16;std::optional<Bytes> compressed;std::optional<std::uint32_t> declaredSize;};
Bytes zip(const std::vector<Entry>& entries) {
    struct Record {Entry e;Bytes packed;std::uint32_t offset;};std::vector<Record> records;Bytes out;
    for(const auto& e:entries) {
        Bytes packed=e.data;
        if(e.method==8){packed.clear();packed.push_back(1);u16(packed,static_cast<std::uint16_t>(e.data.size()));u16(packed,static_cast<std::uint16_t>(~e.data.size()));packed.insert(packed.end(),e.data.begin(),e.data.end());}
        if(e.compressed)packed=*e.compressed;
        const auto offset=static_cast<std::uint32_t>(out.size());
        const auto declared=e.declaredSize.value_or(static_cast<std::uint32_t>(e.data.size()));
        u32(out,0x04034b50);u16(out,20);u16(out,e.flags);u16(out,e.method);u32(out,0);
        u32(out,(e.flags&8)?0:crc(e.data));u32(out,(e.flags&8)?0:static_cast<std::uint32_t>(packed.size()));u32(out,(e.flags&8)?0:declared);
        u16(out,static_cast<std::uint16_t>(e.name.size()));u16(out,0);out.insert(out.end(),e.name.begin(),e.name.end());out.insert(out.end(),packed.begin(),packed.end());
        if(e.flags&8){u32(out,0x08074b50);u32(out,crc(e.data));u32(out,static_cast<std::uint32_t>(packed.size()));u32(out,static_cast<std::uint32_t>(e.data.size()));}
        records.push_back({e,std::move(packed),offset});
    }
    const auto central=static_cast<std::uint32_t>(out.size());
    for(const auto& r:records){const auto& e=r.e;
        u32(out,0x02014b50);u16(out,0x314);u16(out,20);u16(out,e.flags);u16(out,e.method);u32(out,0);
        u32(out,crc(e.data));u32(out,static_cast<std::uint32_t>(r.packed.size()));u32(out,e.declaredSize.value_or(static_cast<std::uint32_t>(e.data.size())));u16(out,static_cast<std::uint16_t>(e.name.size()));
        u16(out,0);u16(out,0);u16(out,0);u16(out,0);u32(out,e.attr);u32(out,r.offset);out.insert(out.end(),e.name.begin(),e.name.end());
    }
    const auto length=static_cast<std::uint32_t>(out.size())-central;
    u32(out,0x06054b50);u16(out,0);u16(out,0);u16(out,static_cast<std::uint16_t>(entries.size()));u16(out,static_cast<std::uint16_t>(entries.size()));u32(out,length);u32(out,central);u16(out,0);
    return out;
}
std::string manifest(std::string resources=R"({"pixel":"assets/images/pixel.png"})",std::string screens=R"(["result"])" ,int version=1) {
    return std::string(R"({"format":"SevenMania.Skin","formatVersion":)")+std::to_string(version)+R"(,"id":"test.partial","name":"테스트","author":"SevenMania","skinVersion":"1.0.0","description":"Loader only","resolution":{"width":1920,"height":1080},"supportedScreens":)"+screens+R"(,"resourcePaths":)"+resources+"}";
}
std::string layout(std::string elements=R"([{"id":"pixel","type":"image","resource":"pixel","x":0,"y":0,"width":1,"height":1}])") {
    return R"({"screen":"result","elements":)"+elements+"}";
}
std::vector<Entry> normal(){return {{"manifest.json",bytes(manifest())},{"screens/result.json",bytes(layout())},{"assets/images/pixel.png",pixel}};}
void write(const std::filesystem::path& file,const Bytes& data){std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));require(bool(out),"fixture write failed");}
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=3)return 2;
    const auto folder=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(folder);
    unsigned checks=0;
    try {
        SkinLoader loader;
        const auto load=[&](const std::string& name,const Bytes& data,const Limits& limits=Limits{}) {
            const auto file=folder/(name+".semaskin");write(file,data);return SkinLoader(limits).load(file);
        };
        const auto failure=[&](const std::string& name,const Bytes& data,ErrorCode code,const Limits& limits=Limits{}) {
            const auto r=load(name,data,limits);
            if(r||!r.error||r.error->code!=code) {
                std::cerr<<"Unexpected result: "<<name<<" code="<<(r.error?int(r.error->code):-1)<<" detail="<<(r.error?r.error->message:"")<<'\n';
                throw std::runtime_error("negative test failed");
            }
            ++checks;
        };
        auto valid=load("normal",zip(normal()));require(bool(valid),"valid stored package failed");++checks;
        require(valid.package->screens.size()==1&&valid.package->screens.contains(ScreenKind::Result)&&valid.package->resources.at("pixel").width==1,"partial model/image metadata wrong");++checks;
        auto deflated=normal();for(auto& e:deflated)e.method=8;
        require(bool(load("deflate",zip(deflated))),"deflate package failed");++checks;
        for(auto& e:deflated)e.flags=8;
        require(bool(load("descriptor",zip(deflated))),"data descriptor package failed");++checks;
        auto sample=loader.load(argv[2]);require(bool(sample),"committed deflated sample failed");++checks;
        auto full=normal();full[0].data=bytes(manifest(R"({"pixel":"assets/images/pixel.png"})",R"(["select","play","result"])"));
        for(const auto name:{"select","play"}){auto s=layout();s.replace(s.find("result"),6,name);full.push_back({std::string("screens/")+name+".json",bytes(s)});}
        auto all=load("all-screens",zip(full));require(all&&all.package->screens.size()==3,"all three screen models failed");++checks;
        for(const auto& property:std::vector<std::string>{
            R"("rotation":"bad")",R"("rotation":360001)",R"("visible":1)",R"("tint":{"r":256,"g":0,"b":0})",
            R"("tint":{"r":1.5,"g":0,"b":0})",R"("tint":{"r":255,"g":0})",
            R"("source":{"x":0,"y":0,"width":2,"height":1})",R"("source":{"x":-1,"y":0,"width":1,"height":1})",
            R"("clip":{"x":0,"y":0,"width":-1,"height":1})",R"("clip":{"x":0,"y":0,"width":1,"height":"bad"})"}) {
            auto bad=normal();
            bad[1].data=bytes(layout("[{\"id\":\"bad\",\"type\":\"image\",\"resource\":\"pixel\",\"x\":0,\"y\":0,\"width\":1,\"height\":1,"+property+"}]"));
            require(!load("invalid-render-property",zip(bad)),"Invalid rendering property accepted");++checks;
        }
        for(const auto& property:std::vector<std::string>{
            R"("fontSize":0)",R"("fontSize":257)",R"("font":"abcdefghijklmnopqrstuvwxyzabcdef")",
            R"("align":"bad")",R"("verticalAlign":false)",R"("clipText":1)",R"("numberFormat":{"decimals":10})",
            R"("numberFormat":{"minDigits":33})",R"("numberFormat":{"grouping":1})",R"("default":{})"}) {
            auto bad=normal();bad[1].data=bytes(layout("[{\"id\":\"bad\",\"type\":\"text\",\"x\":0,\"y\":0,\"width\":1,\"height\":1,"+property+"}]"));
            require(!load("invalid-text-property",zip(bad)),"Invalid text property accepted");++checks;
        }
        auto defaultSize=normal();
        auto defaultManifest=manifest();const auto resolutionAt=defaultManifest.find("\"resolution\"");
        defaultManifest.erase(resolutionAt,defaultManifest.find("\"supportedScreens\"",resolutionAt)-resolutionAt);
        defaultSize[0].data=bytes(defaultManifest);
        auto defaultLoaded=load("default-resolution",zip(defaultSize));
        require(defaultLoaded&&defaultLoaded.package->screens.at(ScreenKind::Result).resolution.width==1920&&defaultLoaded.package->screens.at(ScreenKind::Result).resolution.height==1080,"Default resolution not inherited");++checks;
        auto entries=normal();entries.erase(entries.begin());failure("no-manifest",zip(entries),ErrorCode::MissingFile);
        entries=normal();entries[0].data=bytes("{bad");failure("bad-json",zip(entries),ErrorCode::InvalidJson);
        entries=normal();entries[0].data=bytes(manifest("{}",R"(["result"])" ,2));failure("future-version",zip(entries),ErrorCode::UnsupportedVersion);
        entries=normal();entries.pop_back();failure("missing-image",zip(entries),ErrorCode::MissingFile);
        entries=normal();entries.erase(entries.begin()+1);failure("missing-screen",zip(entries),ErrorCode::MissingFile);
        failure("not-zip",bytes("not a ZIP"),ErrorCode::InvalidZip);
        auto truncated=zip(normal());truncated.resize(truncated.size()-8);failure("truncated",truncated,ErrorCode::InvalidZip);
        auto zip64=zip(normal());zip64[zip64.size()-12]=255;zip64[zip64.size()-11]=255;failure("zip64",zip64,ErrorCode::InvalidZip);
        auto mismatched=zip(normal());mismatched[30]='x';failure("local-name-mismatch",mismatched,ErrorCode::InvalidZip);
        auto offset=zip(normal());put32(offset,offset.size()-6,0xfffffff0);failure("bad-directory-offset",offset,ErrorCode::InvalidZip);
        for(const auto& name:{"../escape.png","/abs.png","C:/abs.png","assets/images/../escape.png","assets\\images\\bad.png","assets/images/NUL.png","assets/images/trailing. ","assets/images/x:ads.png","assets/images/%2e%2e.png"}) {
            entries=normal();entries.push_back({name,pixel});failure("unsafe-"+std::to_string(checks),zip(entries),ErrorCode::UnsafePath);
        }
        entries=normal();entries.push_back({"assets/images/pixel.png",pixel});failure("duplicate-path",zip(entries),ErrorCode::InvalidZip);
        entries=normal();entries.push_back({"assets/images/PIXEL.png",pixel});failure("case-collision",zip(entries),ErrorCode::InvalidZip);
        entries=normal();entries.back().attr=0120777u<<16;failure("symlink",zip(entries),ErrorCode::UnsafePath);
        entries=normal();entries.back().flags=1;failure("encrypted",zip(entries),ErrorCode::InvalidZip);
        entries=normal();entries.back().method=99;failure("compression",zip(entries),ErrorCode::InvalidZip);
        for(const auto& path:{"assets/images/video.mp4","assets/images/a.webm","assets/images/a.avi","assets/images/a.exe","animations/a.js","assets/images/a.dds"}) {
            entries=normal();entries.push_back({path,bytes("payload")});failure("blocked-type-"+std::to_string(checks),zip(entries),ErrorCode::UnsupportedResource);
        }
        auto badCrc=zip(normal());badCrc[30+std::string("manifest.json").size()+5]^=1;failure("crc",badCrc,ErrorCode::InvalidZip);
        Limits small;small.files=2;failure("entry-limit",zip(normal()),ErrorCode::LimitExceeded,small);
        small=Limits{};small.totalBytes=10;failure("total-limit",zip(normal()),ErrorCode::LimitExceeded,small);
        small=Limits{};small.archiveBytes=10;failure("archive-limit",zip(normal()),ErrorCode::LimitExceeded,small);
        small=Limits{};small.jsonBytes=10;failure("json-limit",zip(normal()),ErrorCode::LimitExceeded,small);
        small=Limits{};small.fileBytes=10;failure("file-limit",zip(normal()),ErrorCode::LimitExceeded,small);
        small=Limits{};small.jsonNodes=2;failure("node-limit",zip(normal()),ErrorCode::LimitExceeded,small);
        small=Limits{};small.elements=0;failure("element-limit",zip(normal()),ErrorCode::LimitExceeded,small);
        small=Limits{};small.totalDecodedImageBytes=3;failure("decoded-budget",zip(normal()),ErrorCode::LimitExceeded,small);
        entries=normal();entries[0].data=bytes(manifest(R"({"pixel":"../outside.png"})"));failure("manifest-traversal",zip(entries),ErrorCode::UnsafePath);
        const Bytes zerosDeflate{0xed,0xc1,0x31,1,0,0,0,0xc2,0xa0,0xf5,0x4f,0x6d,0xc,0x1f,0xa0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x80,0xb7,1};
        entries=normal();Entry bomb{"animations/bomb.json",Bytes(16384,0),8};bomb.compressed=zerosDeflate;entries.push_back(bomb);
        failure("compression-bomb",zip(entries),ErrorCode::LimitExceeded);
        entries.back().declaredSize=32u<<20;failure("huge-declared-size",zip(entries),ErrorCode::LimitExceeded);
        entries.back().declaredSize=1;failure("lying-output-size",zip(entries),ErrorCode::InvalidZip);
        entries=normal();entries[0].data=bytes(manifest("{}",R"(["result","result"])"));failure("duplicate-screen",zip(entries),ErrorCode::InvalidManifest);
        entries=normal();auto text=manifest();text.insert(1,R"("formatVersion":1,)");entries[0].data=bytes(text);failure("duplicate-json-key",zip(entries),ErrorCode::InvalidJson);
        entries=normal();text=manifest();text.replace(text.find("Loader only"),11,"\\uD800");entries[0].data=bytes(text);failure("surrogate",zip(entries),ErrorCode::InvalidJson);
        entries=normal();entries[0].data={0xff};failure("bad-utf8",zip(entries),ErrorCode::InvalidJson);
        entries=normal();text=manifest();text.replace(text.find("1.0.0"),5,"01.0.0");entries[0].data=bytes(text);failure("bad-skin-version",zip(entries),ErrorCode::InvalidManifest);
        entries=normal();entries[1].data=bytes("{\"screen\":\"result\",\"elements\":[],\"extensions\":"+std::string(70,'[')+"0"+std::string(70,']')+"}");failure("depth",zip(entries),ErrorCode::LimitExceeded);
        entries=normal();entries.back().data=bytes("not png");failure("bad-image",zip(entries),ErrorCode::InvalidImage);
        entries=normal();auto big=pixel;big[16]=0x7f;entries.back().data=big;
        failure("huge-image",zip(entries),ErrorCode::LimitExceeded);
        {
            std::ifstream input(std::filesystem::path(argv[2]).parent_path()/L"pixel.jpg",std::ios::binary);
            Bytes jpeg{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};require(!jpeg.empty(),"JPEG fixture missing");
            entries=normal();entries[0].data=bytes(manifest(R"({"pixel":"assets/images/pixel.jpg"})"));entries.back()={"assets/images/pixel.jpg",jpeg};
            const auto jpg=load("jpeg",zip(entries));require(jpg&&jpg.package->resources.at("pixel").width==2,"JPEG metadata load failed");++checks;
        }
        entries=normal();entries[1].data=bytes(layout(R"([{"id":"late","type":"text","text":"later","x":0,"y":0,"width":20,"height":10,"z":5},{"id":"early","type":"number","binding":"result.score","x":0,"y":0,"width":20,"height":10,"z":-1}])"));
        auto layers=load("layers",zip(entries));require(layers&&layers.package->screens.at(ScreenKind::Result).elements[0].id=="early","layer sorting/binding metadata failed");++checks;
        Bytes font(32,0);font[1]=1;font[5]=1;font[12]='h';font[13]='e';font[14]='a';font[15]='d';font[23]=28;font[27]=4;
        entries=normal();entries[0].data=bytes(manifest(R"({"font":"assets/fonts/test.ttf"})"));entries.pop_back();entries.push_back({"assets/fonts/test.ttf",font});
        entries[1].data=bytes(layout(R"([{"id":"text","type":"text","resource":"font","text":"test","x":0,"y":0,"width":10,"height":10}])"));
        require(bool(load("font-metadata",zip(entries))),"font directory metadata rejected");++checks;
        entries[1].data=bytes(layout(R"([{"id":"text","type":"text","resource":"font","font":"BankGothic-Bold","text":"test","x":0,"y":0,"width":10,"height":10}])"));
        auto legacyFont=load("legacy-font-alias",zip(entries));require(bool(legacyFont),"Legacy font alias rejected");const auto& legacyElement=legacyFont.package->screens.at(ScreenKind::Result).elements.front();require(legacyElement.textStyle.font=="Michroma"&&legacyElement.resource.empty(),"Legacy font did not resolve to built-in Michroma");
        entries.back().data[23]=255;failure("font-out-of-range",zip(entries),ErrorCode::UnsupportedResource);
        entries=normal();entries.push_back({"licenses/Michroma-OFL.txt",bytes("SIL OPEN FONT LICENSE Version 1.1")});require(bool(load("font-license",zip(entries))),"Font license text rejected");
        entries.back().name="assets/images/license.txt";failure("license-outside-notices",zip(entries),ErrorCode::UnsupportedResource);

        entries=normal();entries[1].data=bytes(layout(R"([{"id":"bad","type":"script","x":0,"y":0,"width":1,"height":1}])"));failure("script-element",zip(entries),ErrorCode::InvalidScreen);
        entries=normal();entries[0].data=bytes(manifest(R"({"pixel":{"path":"assets/images/missing.png","required":false}})"));entries.pop_back();
        entries[1].data=bytes(layout("[]"));require(bool(load("optional-unused",zip(entries))),"optional unused resource rejected");++checks;
        entries[1].data=bytes(layout());failure("optional-referenced",zip(entries),ErrorCode::MissingFile);
        entries=normal();entries[0].data=bytes(manifest(R"({"pixel":"assets/images/pixel.png","alias":"assets/images/pixel.png"})"));
        auto alias=load("shared",zip(entries));require(alias&&alias.package->resources.at("pixel").bytes==alias.package->resources.at("alias").bytes,"same file did not share resource bytes");++checks;
        auto sprite=normal();sprite[1].data=bytes(layout(R"([{"id":"sprite","type":"sprite","resource":"pixel","x":0,"y":0,"width":1,"height":1,"sprite":{"frameWidth":2,"frameHeight":1,"columns":1,"rows":1}}])"));failure("sprite-bounds",zip(sprite),ErrorCode::InvalidScreen);
        SkinManager manager;require(!manager.registerFile(folder/"normal.semaskin"),"registration failed");
        require(!manager.select(ScreenKind::Result,"test.partial"),"selection failed");auto held=manager.selected(ScreenKind::Result);
        require(manager.select(ScreenKind::Play,"test.partial").has_value()&&manager.selected(ScreenKind::Result)==held&&!manager.selected(ScreenKind::Play),"unsupported screen changed selection");++checks;
        require(manager.registerFile(folder/"bad-json.semaskin").has_value()&&manager.selected(ScreenKind::Result)==held&&manager.size()==1,"failed load changed state");++checks;
        require(manager.registerFile(folder/"normal.semaskin")->code==ErrorCode::DuplicateId,"duplicate ID accepted");++checks;
        SkinPackage converted=*held;converted.manifest.id="lr2.future-test";
        auto mutableBytes=std::make_shared<Bytes>(pixel);converted.resources.at("pixel").bytes=mutableBytes;
        require(!manager.registerModel(std::move(converted)),"container-neutral converted IR rejected");++checks;
        (*mutableBytes)[0]=0;
        require(manager.find("lr2.future-test")->resources.at("pixel").bytes->front()==137,"converted resource mutable alias escaped snapshot");++checks;
        SkinPackage bad=*held;bad.screens.at(ScreenKind::Result).elements[0].x=std::numeric_limits<double>::infinity();require(!loader.prepare(std::move(bad)),"invalid converted IR accepted");++checks;
        bad=*held;bad.screens.at(ScreenKind::Result).extensions.type=bms::JsonValue::Type::Object;
        auto& value=bad.screens.at(ScreenKind::Result).extensions.object[L"bad"];value.type=bms::JsonValue::Type::Number;value.number=std::numeric_limits<double>::quiet_NaN();
        require(!loader.prepare(std::move(bad)),"nonfinite converted extension accepted");++checks;
        require(manager.unregisterSkin("test.partial")&&!manager.selected(ScreenKind::Result)&&held->resources.at("pixel").width==1,"held package lifetime broken");++checks;
        std::weak_ptr<const SkinPackage> lifetime;
        for(int i=0;i<100;++i){auto r=loader.load(folder/"normal.semaskin");require(bool(r),"repeat load failed");lifetime=r.package;}
        require(lifetime.expired(),"package retained after unload");++checks;
        std::uint32_t rng=0x12345678;
        const auto original=zip(normal());
        for(int i=0;i<300;++i){
            auto mutated=original;
            for(int j=0;j<3;++j){rng=rng*1664525u+1013904223u;const auto index=rng%mutated.size();rng=rng*1664525u+1013904223u;mutated[index]^=static_cast<std::uint8_t>(1u<<(rng%8));}
            const auto r=load("mutant",mutated);require(bool(r)||r.error.has_value(),"malformed package returned neither result nor error");
        }
        ++checks;
        std::cout<<"SevenMania skin checks passed: "<<checks<<" (including 100 load/release cycles).\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
