#include "SkinRenderTestSupport.h"
#include "editor/EditorDocument.h"
#include "skin/SelectionSlotLayout.h"
#include <algorithm>
#include <iostream>

using namespace skin_test;
using namespace sevenmania::skin;

int wmain(int argc,wchar_t** argv) {
    try {
        if(argc<3||argc>4)return 2;
        hr(CoInitializeEx(nullptr,COINIT_MULTITHREADED));
        struct Apartment {~Apartment(){CoUninitialize();}} apartment;
        const auto output=std::filesystem::path(argv[2]);std::filesystem::create_directories(output);
        auto loaded=SkinLoader{}.load(argv[1]);require(bool(loaded),"Default package failed to load");
        {auto synchronized=*loaded.package;synchronizeSelectionSlots(synchronized);
        const auto& select=synchronized.screens.at(ScreenKind::Select).elements;
        const auto find=[&](const char* id)->const SkinElement&{return *std::find_if(select.begin(),select.end(),[&](const auto& e){return e.id==id;});};
        require(find("slot-0").x<find("slot-2").x&&find("slot-2").x<find("slot-1").x,"Selection speed/gauge/judgement order differs from play");
        require(find("slot-0").resource=="hi-speed-slot"&&find("slot-2").resource=="gauge-mode-slot","Selection images do not match gameplay");
        require(find("slot-value-2").x==find("slot-2").x&&find("slot-value-1").x==find("slot-1").x,"Selection text detached from icon");
        auto& editable=synchronized.screens.at(ScreenKind::Select).elements;
        auto style=std::find_if(editable.begin(),editable.end(),[](const auto& e){return e.id=="slot-value-0";});
        style->textStyle.fontSize=37;style->textStyle.font="Michroma";style->textStyle.align=TextAlign::Right;style->tint={12,34,56,255};style->opacity=.6;
        synchronizeSelectionSlots(synchronized);
        require(find("slot-value-0").textStyle.fontSize==37&&find("slot-value-0").textStyle.font=="Michroma"&&find("slot-value-0").textStyle.align==TextAlign::Right,"Slot synchronization overwrote edited font properties");
        require(find("slot-value-0").tint.r==12&&find("slot-value-0").tint.g==34&&find("slot-value-0").opacity==.6,"Slot synchronization overwrote edited color/opacity");
        auto& play=synchronized.screens.at(ScreenKind::Play).elements;auto image=std::find_if(play.begin(),play.end(),[](const auto& e){return e.id=="gear-icon-1";});image->resource="slot";
        synchronizeSelectionSlots(synchronized);require(find("slot-2").resource=="slot","Edited gameplay image did not propagate");}
        const auto& elements=loaded.package->screens.at(ScreenKind::Play).elements;
        const auto hud=std::find_if(elements.begin(),elements.end(),[](const auto& e){return e.binding=="play.judgementCombo";});
        require(hud!=elements.end()&&hud->hud.judgementMode=="image"&&hud->hud.comboMode=="image","Default HUD is not image/image");
        for(const auto& id:hud->hud.judgements)require(!id.empty()&&loaded.package->resources.at(id).kind==ResourceKind::Png,"Judgement PNG missing");
        for(const auto& id:hud->hud.digits)require(!id.empty()&&loaded.package->resources.at(id).kind==ResourceKind::Png,"Digit PNG missing");
        Surface surface(1920,1080,argc>3?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP);SkinRenderer renderer;
        require(!renderer.prepare(loaded.package,ScreenKind::Play,surface.target.Get()),"Default HUD GPU prepare failed");
        const auto prepared=renderer.stats();require(prepared.hudAtlases==1&&prepared.hudFontGlyphs==0,"Default HUD fell back to font glyphs");
        SkinDataProvider data;
        const auto update=[&](int grade,int combo){data.beginFrame(ScreenKind::Play);data.set(BindingId::PlayJudgementGrade,std::int64_t(grade));data.set(BindingId::PlayJudgementAge,0.0);data.set(BindingId::PlayCombo,std::int64_t(combo));data.endFrame();require(!renderer.update(&data.snapshot()),"Default HUD update failed");};
        update(-1,0);const auto blank=surface.render(renderer);Bytes previous;
        for(int grade=0;grade<6;++grade){update(grade,123);auto pixels=surface.render(renderer);require(pixels!=blank,"Default judgement invisible");require(previous.empty()||pixels!=previous,"Default judgement PNG did not change");previous=pixels;save(output/("grade-"+std::to_string(grade)+".png"),pixels,1920,1080);}
        for(const auto combo:{0,9,10,99,100,999,1000}){update(0,combo);auto pixels=surface.render(renderer);require(pixels!=previous,"Combo digits did not change");previous=pixels;}
        for(int i=0;i<256;++i){update(0,i);surface.render(renderer);}
        require(renderer.stats().decodedImages==prepared.decodedImages&&renderer.stats().uploadedBitmaps==prepared.uploadedBitmaps,"Gameplay rebuilt default HUD images");
        require(renderer.stats().hudFontGlyphs==0,"Default HUD used font fallback during playback");
        sevenmania::editor::Document document;
        document.open(argv[1]);document.screen="play";
        const auto image=std::find_if(document.elements().begin(),document.elements().end(),[](const auto& e){return e[L"type"].str()==L"image";});
        require(image!=document.elements().end(),"No replacement test image");
        const auto index=size_t(image-document.elements().begin());const auto before=*image;
        const auto replacement=output/L"replacement.png";sevenmania::editor::write(replacement,*loaded.package->resources.at(hud->hud.digits[0]).bytes);
        document.replaceImage(index,replacement);const auto after=document.elements()[index];
        for(const auto* field:{L"x",L"y",L"width",L"height",L"rotation",L"opacity",L"extensions",L"id"})require(sevenmania::editor::serialize(before[field])==sevenmania::editor::serialize(after[field]),"Image replacement changed geometry/style");
        require(before[L"resource"].str()!=after[L"resource"].str(),"Replacement did not get a private resource");
        require(document.manifest[L"resourcePaths"][before[L"resource"].str()].type!=bms::JsonValue::Type::Null,"Shared old resource was removed");
        sevenmania::editor::write(output/L"broken.png",Bytes{1,2,3});bool rejected=false;
        try{document.replaceImage(index,output/L"broken.png");}catch(const std::exception&){rejected=true;}
        require(rejected&&document.elements()[index][L"resource"].str()==after[L"resource"].str(),"Failed replacement changed document");
        document.screen="select";
        for(const auto* slot:{L"slot-0",L"slot-1",L"slot-2"}){
            const auto at=std::find_if(document.elements().begin(),document.elements().end(),[&](const auto& e){return e[L"id"].str()==slot;});require(at!=document.elements().end(),"Selection slot missing");
            const auto slotIndex=size_t(at-document.elements().begin());const auto geometry=*at;
            document.replaceImage(slotIndex,replacement);
            const auto& updated=document.elements()[slotIndex];
            require(updated[L"width"].num()==geometry[L"width"].num()&&updated[L"height"].num()==geometry[L"height"].num()&&updated[L"x"].num()==geometry[L"x"].num()&&updated[L"y"].num()==geometry[L"y"].num(),"Slot replacement changed selection geometry");
            const auto paired=std::wstring(slot)==L"slot-0"?L"gear-icon-0":std::wstring(slot)==L"slot-1"?L"gear-icon-2":L"gear-icon-1";
            const auto& play=document.screens.at("play")[L"elements"].array;
            const auto source=std::find_if(play.begin(),play.end(),[&](const auto& e){return e[L"id"].str()==paired;});
            require(source!=play.end()&&(*source)[L"resource"].str()==updated[L"resource"].str(),"Replacement would be overwritten by gameplay synchronization");
        }
        document.save(output/L"roundtrip.semaskin");
        auto roundtrip=SkinLoader{}.load(output/L"roundtrip.semaskin");require(bool(roundtrip),"Default editor roundtrip failed");
        {auto normalized=*roundtrip.package;synchronizeSelectionSlots(normalized);
        for(const auto& e:normalized.screens.at(ScreenKind::Select).elements)if(e.id=="slot-0"||e.id=="slot-1"||e.id=="slot-2")require(e.resource.starts_with("replacement-"),"Saved slot replacement reverted after synchronization");}
        SkinRenderer restored;require(!restored.prepare(roundtrip.package,ScreenKind::Play,surface.target.Get()),"Saved default HUD failed to render");
        require(restored.stats().hudAtlases==1&&restored.stats().hudFontGlyphs==0,"Editor lost default image references");
        std::cout<<checks<<" default image HUD checks passed; one atlas, zero font fallback glyphs\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
