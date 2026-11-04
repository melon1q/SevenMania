#pragma once
#include "skin/SkinInternal.h"
#include "FontPolicy.h"
#include <set>
#include <fstream>
#include <algorithm>
#include <charconv>
#include <windows.h>
namespace sevenmania::editor {
using namespace skin;using J=bms::JsonValue;using T=J::Type;
inline J str(std::wstring v){J j;j.type=T::String;j.string=std::move(v);return j;}
inline J num(double v){J j;j.type=T::Number;j.number=v;return j;}
inline J jsonBool(bool v){J j;j.type=T::Boolean;j.boolean=v;return j;}
inline std::string serialize(const J& j){
 switch(j.type){case T::Null:return "null";case T::Boolean:return j.boolean?"true":"false";
 case T::Number:{char b[64];auto r=std::to_chars(b,b+64,j.number);if(r.ec!=std::errc{})throw std::runtime_error("number");return {b,r.ptr};}
 case T::String:{std::string out="\"";for(unsigned char c:detail::utf8(j.string)){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<32){char b[7];sprintf_s(b,"\\u%04x",c);out+=b;}else out+=char(c);}return out+'"';}
 case T::Array:{std::string out="[";for(auto& v:j.array){if(out.size()>1)out+=',';out+=serialize(v);}return out+']';}
 case T::Object:{std::string out="{";for(auto& [k,v]:j.object){if(out.size()>1)out+=',';out+=serialize(str(k))+':'+serialize(v);}return out+'}';}}
 throw std::runtime_error("JSON");
}
inline Bytes bytes(std::string s){return {s.begin(),s.end()};}
inline unsigned crc(const Bytes& b){unsigned c=~0u;for(auto v:b){c^=v;for(int i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320:0);}return ~c;}
inline void u16(Bytes& b,unsigned n){b.push_back(static_cast<BYTE>(n));b.push_back(static_cast<BYTE>(n>>8));}
inline void u32(Bytes& b,unsigned n){u16(b,n);u16(b,n>>16);}
inline Bytes zip(const detail::Archive& files){Bytes out;struct Record{std::string name;unsigned offset,size,crc;};std::vector<Record> records;
 for(auto& [name,data]:files){(void)detail::safePath(name);Record r{name,static_cast<unsigned>(out.size()),static_cast<unsigned>(data->size()),crc(*data)};records.push_back(r);
 u32(out,0x04034b50);u16(out,20);u16(out,0x800);u16(out,0);u32(out,0);u32(out,r.crc);u32(out,r.size);u32(out,r.size);u16(out,static_cast<unsigned>(name.size()));u16(out,0);out.insert(out.end(),name.begin(),name.end());out.insert(out.end(),data->begin(),data->end());}
 unsigned central=static_cast<unsigned>(out.size());for(auto& r:records){u32(out,0x02014b50);u16(out,20);u16(out,20);u16(out,0x800);u16(out,0);u32(out,0);u32(out,r.crc);u32(out,r.size);u32(out,r.size);u16(out,static_cast<unsigned>(r.name.size()));u16(out,0);u16(out,0);u16(out,0);u16(out,0);u32(out,0);u32(out,r.offset);out.insert(out.end(),r.name.begin(),r.name.end());}
 unsigned length=static_cast<unsigned>(out.size())-central;u32(out,0x06054b50);u16(out,0);u16(out,0);u16(out,static_cast<unsigned>(records.size()));u16(out,static_cast<unsigned>(records.size()));u32(out,length);u32(out,central);u16(out,0);return out;}
inline void write(const std::filesystem::path& p,const Bytes& b){std::ofstream f(p,std::ios::binary|std::ios::trunc);f.write(reinterpret_cast<const char*>(b.data()),b.size());f.close();if(!f)throw std::runtime_error("File write failed");}
struct Document {
 detail::Archive files;J manifest;std::map<std::string,J> screens;std::string screen="select";
 void fresh(){files.clear();screens.clear();manifest=detail::json(bytes(R"({"format":"SevenMania.Skin","formatVersion":1,"id":"user.skin","name":"New Skin","author":"User","skinVersion":"1.0.0","description":"","resolution":{"width":1920,"height":1080},"supportedScreens":["select","play","result"],"resourcePaths":{}})"),"manifest",{});
 for(auto s:{"select","play","result"})screens[s]=detail::json(bytes(std::string("{\"screen\":\"")+s+"\",\"elements\":[]}"),s,{});screen="select";}
 void open(const std::filesystem::path& p){auto result=SkinLoader{}.load(p);if(!result)throw std::runtime_error(result.error->message);auto data=detail::readZip(p,{});Document next;next.files=data;next.manifest=detail::json(*data.at("manifest.json"),"manifest",{});for(auto kind:result.package->manifest.supportedScreens){std::string name=screenName(kind);next.screens[name]=detail::json(*data.at("screens/"+name+".json"),name,{});}for(auto& [name,j]:next.screens){(void)name;auto& es=j.object[L"elements"].array;std::stable_sort(es.begin(),es.end(),[](const auto& a,const auto& b){return a[L"z"].num()<b[L"z"].num();});}std::set<std::wstring> legacyResources;
 for(const auto& [id,r]:next.manifest[L"resourcePaths"].object){const auto path=r.type==T::String?r.str():r[L"path"].str();if(sevenmania::isLegacyHudFont(detail::utf8(path)))legacyResources.insert(id);}
 for(auto& [name,root]:next.screens){(void)name;for(auto& e:root.object[L"elements"].array){if(sevenmania::isLegacyHudFont(detail::utf8(e[L"font"].str()))){e.object[L"font"]=str(L"Michroma");if(!e[L"resource"].str().empty())legacyResources.insert(e[L"resource"].str());}}}
 for(const auto& id:legacyResources){auto& resources=next.manifest.object[L"resourcePaths"].object;const auto found=resources.find(id);if(found!=resources.end()){auto path=found->second.type==T::String?found->second.str():found->second[L"path"].str();next.files.erase(detail::utf8(path));resources.erase(found);}for(auto& [name,root]:next.screens){(void)name;for(auto& e:root.object[L"elements"].array)if(e[L"resource"].str()==id){e.object.erase(L"resource");e.object[L"font"]=str(L"Michroma");}}}
 for(auto it=next.files.begin();it!=next.files.end();)if(sevenmania::isLegacyHudFont(it->first))it=next.files.erase(it);else ++it;
 next.screen=next.screens.begin()->first;*this=std::move(next);}
 detail::Archive archive() const{auto a=files;a["manifest.json"]=std::make_shared<const Bytes>(bytes(serialize(manifest)));for(auto& [name,j]:screens)a["screens/"+name+".json"]=std::make_shared<const Bytes>(bytes(serialize(j)));return a;}
 void save(const std::filesystem::path& path) const{auto tmp=path;tmp+=L".pending.semaskin";if(std::filesystem::exists(tmp))throw std::runtime_error("Pending save exists; preserve it before saving");try{write(tmp,zip(archive()));auto valid=SkinLoader{}.load(tmp);if(!valid)throw std::runtime_error(valid.error->message);if(!MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace saved file: "+std::to_string(GetLastError()));}catch(...){std::error_code ec;std::filesystem::remove(tmp,ec);throw;}}
 void openFolder(const std::filesystem::path& folder){detail::Archive a;std::uint64_t total=0;for(auto& e:std::filesystem::recursive_directory_iterator(folder)){if(e.is_symlink()||(GetFileAttributesW(e.path().c_str())&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Linked project paths are not allowed");if(e.is_directory())continue;auto name=detail::utf8(std::filesystem::relative(e.path(),folder).generic_wstring());detail::safePath(name);auto size=e.file_size();if(size>16u*1024*1024||(total+=size)>64u*1024*1024||a.size()>=1024)throw std::runtime_error("Project resource limit");std::ifstream f(e.path(),std::ios::binary);auto b=std::make_shared<Bytes>(size);f.read(reinterpret_cast<char*>(b->data()),size);if(!f)throw std::runtime_error("Project read failed");a[name]=b;}
 auto temp=folder.parent_path()/(folder.filename().wstring()+L".editor-import.semaskin");if(std::filesystem::exists(temp))throw std::runtime_error("Import staging file exists");try{write(temp,zip(a));open(temp);std::filesystem::remove(temp);}catch(...){std::error_code ec;std::filesystem::remove(temp,ec);throw;}}
 void saveFolder(const std::filesystem::path& folder) const{auto stage=folder;stage+=L".editor-pending";auto backup=folder;static unsigned sequence=0;backup+=L".editor-backup-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++sequence);if(std::filesystem::exists(stage)||std::filesystem::exists(backup))throw std::runtime_error("Project pending/backup already exists; preserve it first");auto checked=folder;checked+=L".editor-check.semaskin";if(std::filesystem::exists(checked))throw std::runtime_error("Validation file exists");save(checked);std::filesystem::remove(checked);std::filesystem::create_directories(stage);for(auto& [name,data]:archive()){detail::safePath(name);auto path=stage/std::filesystem::path(detail::wide(name,""));std::filesystem::create_directories(path.parent_path());write(path,*data);}bool moved=false;try{if(std::filesystem::exists(folder)){std::filesystem::rename(folder,backup);moved=true;}std::filesystem::rename(stage,folder);}catch(...){if(moved&&!std::filesystem::exists(folder))std::filesystem::rename(backup,folder);throw;}}
 void replaceImage(size_t index,const std::filesystem::path& path){
  if(index>=elements().size()||elements()[index][L"type"].str()!=L"image")throw std::runtime_error("Select an image layer first");
  std::ifstream input(path,std::ios::binary|std::ios::ate);const auto size=input.tellg();if(size<=0||size>16*1024*1024)throw std::runtime_error("Image read/size limit");
  auto data=std::make_shared<Bytes>(size_t(size));input.seekg(0);if(!input.read(reinterpret_cast<char*>(data->data()),size))throw std::runtime_error("Image read failed");
  static unsigned sequence=0;const auto id=L"replacement-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++sequence);
  SkinResource resource;resource.path="assets/images/"+detail::utf8(id)+detail::utf8(path.extension().wstring());resource.bytes=data;try{detail::imageInfo(resource,{});}catch(const detail::Failure& error){throw std::runtime_error(error.error.message);}
  auto next=*this;next.files[resource.path]=data;next.manifest.object[L"resourcePaths"].object[id]=str(detail::wide(resource.path,"replacement"));
  auto& e=next.elements()[index];e.object[L"resource"]=str(id);e.object.erase(L"source");
  *this=std::move(next);
 }
 const std::vector<J>& elements() const{return screens.at(screen)[L"elements"].array;}
 std::vector<J>& elements(){return screens.at(screen).object[L"elements"].array;}
};
}
