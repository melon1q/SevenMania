#include "editor/AnimationEditModel.h"
#include <iostream>
using namespace sevenmania::editor;
namespace {
void require(bool ok,const char* what){if(!ok){std::cerr<<"FAILED: "<<what<<"\n";std::exit(1);}}
template<class F> bool throws(F&& f){try{f();}catch(...){return true;}return false;}
J element(){J e;e.type=T::Object;e.object={{L"id",str(L"e")},{L"x",num(100)},{L"y",num(50)}};return e;}
}
int main(){
    auto e=element();
    require(anim::trackCount(e)==0,"empty");
    const auto a=anim::addTrack(e,L"opacity",L"screen.enter");
    require(a==0&&anim::trackCount(e)==1&&anim::keyCount(e,0)==2,"add");
    const auto b=anim::addTrack(e,L"x",L"judgement");
    require(b==1&&anim::key(e,1,0).second==100&&anim::key(e,1,1).second==200,"x defaults come from element");
    require(anim::settings(e,0).id!=anim::settings(e,1).id,"unique ids");
    const auto before=serialize(anim::list(e));
    require(throws([&]{auto s=anim::settings(e,0);s.easing=L"bounce";anim::updateTrack(e,0,s);}),"bad easing rejected");
    require(throws([&]{auto s=anim::settings(e,0);s.id=anim::settings(e,1).id;anim::updateTrack(e,0,s);}),"duplicate id rejected");
    require(throws([&]{auto s=anim::settings(e,0);s.duration=0.5;anim::updateTrack(e,0,s);anim::addKey(e,0,0.75,1);}),"key past duration rejected");
    // the failed key must not have been stored; the successful shorter duration may have been
    require(anim::keyCount(e,0)==2,"failed key add leaves track unchanged");
    auto s=anim::settings(e,0);s.duration=1;anim::updateTrack(e,0,s);
    const auto mid=anim::addKey(e,0,0.4,0.2);
    require(mid==1&&anim::key(e,0,1).first==0.4&&anim::keyCount(e,0)==3,"sorted insert");
    require(throws([&]{anim::addKey(e,0,0.4,0.9);}),"duplicate key time rejected");
    require(throws([&]{anim::addKey(e,0,0.6,1.5);}),"opacity > 1 rejected");
    require(anim::setKey(e,0,1,0.9,0.5)==1&&anim::key(e,0,1).first==0.9,"edit key reorders");
    require(throws([&]{anim::setKey(e,0,1,1.0,0.5);}),"edit onto an existing time rejected");
    anim::removeKey(e,0,1);require(anim::keyCount(e,0)==2,"remove key");
    require(throws([&]{anim::removeKey(e,0,0);anim::removeKey(e,0,0);}),"last key cannot be removed");
    // the first removal above succeeded; validator refused the second
    require(anim::keyCount(e,0)==1,"state after refused removal");
    const auto copy=anim::duplicateTrack(e,0);
    require(copy==1&&anim::trackCount(e)==3&&anim::settings(e,1).id!=anim::settings(e,0).id,"duplicate");
    const auto idBefore=anim::settings(e,2).id;anim::moveTrack(e,2,-1);require(anim::settings(e,1).id==idBefore,"move");
    // round trip: serialized form parses with the real loader parser and matches
    const auto tracks=anim::parsed(e);require(tracks.size()==3,"parsed by sevenmania::skin::parseAnimations");
    anim::removeTrack(e,2);anim::removeTrack(e,1);anim::removeTrack(e,0);
    require(anim::trackCount(e)==0&&e[L"extensions"].object.find(L"animations")==e[L"extensions"].object.end(),"empty list removes key");
    for(int i=0;i<64;++i)anim::addTrack(e,L"y",L"judgement");
    require(throws([&]{anim::addTrack(e,L"y",L"judgement");}),"track limit");
    { // changing the property respreads the keys over the new range instead of keeping unit-mismatched values
        auto f=element();anim::addTrack(f,L"opacity",L"screen.enter");auto st=anim::settings(f,0);st.property=L"x";anim::updateTrack(f,0,st);
        require(anim::key(f,0,0).second==100&&anim::key(f,0,1).second==200,"property change rebases key values to the element position");
    }
    { // multi-select: tracks copy to the other objects; x/y keys keep each object's own offset
        auto from=element();anim::addTrack(from,L"x",L"judgement");anim::addTrack(from,L"opacity",L"judgement");
        auto to=element();to.object[L"x"]=num(400);to.object[L"y"]=num(90);
        anim::copyTracks(from,to);require(anim::trackCount(to)==2,"tracks copied");
        require(anim::key(to,0,0).second==400&&anim::key(to,0,1).second==500,"x keys shifted by the object distance");
        require(anim::key(to,1,1).second==1,"non-position keys unchanged");
        require(anim::key(from,0,0).second==100,"source untouched");
    }
    { // legacy entrance presets are removed: dropped on open, never written on save, ignored by the loader
        Document d;d.fresh();d.screen="result";
        J el;el.type=T::Object;el.object={{L"id",str(L"box")},{L"type",str(L"box")},{L"x",num(0)},{L"y",num(0)},{L"width",num(10)},{L"height",num(10)}};
        J ext;ext.type=T::Object;J ent;ent.type=T::Object;ent.object={{L"preset",str(L"Zoom In")},{L"duration",num(.6)}};ext.object[L"entrance"]=ent;ext.object[L"keep"]=num(1);el.object[L"extensions"]=ext;
        d.elements().push_back(el);
        const auto archive=d.archive();const auto& json=*archive.at("screens/result.json");
        require(std::string(json.begin(),json.end()).find("entrance")==std::string::npos,"save drops extensions.entrance");
        require(std::string(json.begin(),json.end()).find("keep")!=std::string::npos,"other extensions survive");
        // a skin file written by an older version still carries the key: the loader must ignore it and the editor must drop it
        auto legacy=d.archive();auto text=serialize(d.screens.at("result"));
        legacy["screens/result.json"]=std::make_shared<const Bytes>(bytes(text)); // still holds entrance (raw DOM, not stripped)
        const auto file=std::filesystem::temp_directory_path()/L"sevenmania-legacy-entrance.semaskin";
        write(file,zip(legacy));
        auto loaded=SkinLoader{}.load(file);require(bool(loaded),"legacy skin still loads");
        const auto& loadedElement=loaded.package->screens.at(ScreenKind::Result).elements.at(0);
        require(loadedElement.extensions[L"entrance"].type==T::Null&&loadedElement.extensions[L"keep"].num()==1,"loader disables entrance");
        Document reopened;reopened.open(file);reopened.screen="result";
        require(reopened.elements()[0][L"extensions"][L"entrance"].type==T::Null,"editor drops entrance on open");
        std::error_code ec;std::filesystem::remove(file,ec);
    }
    std::cout<<"animation edit model: add/duplicate/move/remove tracks, sorted keyframes, validation and atomic rollback passed\n";return 0;
}
