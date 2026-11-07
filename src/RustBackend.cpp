#include "RustBackend.h"
#include "JsonValue.h"
#include "SongLibrary.h"
#include <bit>
#include <cstddef>
#include <cwctype>
#include <stdexcept>
namespace bms::rust_backend {
#ifdef BMS_USE_RUST_BACKEND
namespace {
struct Buffer { unsigned char* data; size_t len, detail; int32_t status; };
struct Characters { uint8_t (*flags)(uint16_t); uint16_t (*lower)(uint16_t); };
extern "C" Buffer bms_rust_run(uint32_t, const uint16_t*, size_t, Characters, int32_t (*)(const void*), const void*);
extern "C" void bms_rust_free(unsigned char*, size_t);
extern "C" uint32_t bms_rust_abi_version();
extern "C" uint64_t bms_rust_successes(uint32_t);
static_assert(sizeof(wchar_t)==2 && sizeof(Buffer)==32 && offsetof(Buffer,status)==24);
std::atomic_bool active{true};
uint8_t flags(uint16_t c) noexcept { return uint8_t((std::iswspace(c)!=0) | ((std::iswdigit(c)!=0)<<1)); }
uint16_t lower(uint16_t c) noexcept { return static_cast<uint16_t>(std::towlower(c)); }
int32_t cancelled(const void* c) noexcept { return c && static_cast<const std::atomic_bool*>(c)->load() ? 1 : 0; }
struct Owned {
    Buffer value;
    Owned(unsigned op,std::wstring_view input,const std::atomic_bool* cancel=nullptr) : value{} {
        if(bms_rust_abi_version()!=1) throw std::runtime_error("Rust backend ABI mismatch");
        // No exceptions may cross these noexcept C callbacks; Rust retains no input pointer.
        value=bms_rust_run(op,reinterpret_cast<const uint16_t*>(input.data()),input.size(),{flags,lower},cancelled,cancel);
    }
    Owned(const Owned&)=delete;
    ~Owned(){bms_rust_free(value.data,value.len);}
    bool usable() const {
        if(value.status==3) throw LibraryError(L"불러오기를 취소했습니다.");
        // Invalid/limited/panicked input is retried with the original parser.
        if(value.status!=0) return false;
        if(!value.data || value.len>128*1024*1024) throw std::runtime_error("Invalid Rust buffer");
        return true;
    }
};
struct Reader {
    const Buffer& b; size_t at{};
    void need(size_t n) const {if(at>b.len || n>b.len-at)throw std::runtime_error("Invalid Rust payload");}
    uint8_t byte(){need(1);return b.data[at++];}
    uint32_t count(){need(4);uint32_t n=0;for(int i=0;i<4;++i)n|=uint32_t(byte())<<(8*i);return n;}
    std::wstring text(){const size_t n=count();if(n>(b.len-at)/2)throw std::runtime_error("Invalid Rust string");std::wstring s(n,L'\0');for(auto& c:s){const auto lo=byte();c=static_cast<wchar_t>(lo|(uint16_t(byte())<<8));}return s;}
    JsonValue json(unsigned depth=0){
        if(depth>128)throw std::runtime_error("Invalid Rust JSON depth");JsonValue v;
        switch(byte()){
        case 0:break;
        case 1:{v.type=JsonValue::Type::Number;uint64_t bits=0;for(int i=0;i<8;++i)bits|=uint64_t(byte())<<(8*i);v.number=std::bit_cast<double>(bits);break;}
        case 2:v.type=JsonValue::Type::String;v.string=text();break;
        case 3:v.type=JsonValue::Type::Boolean;v.boolean=byte()!=0;break;
        case 4:{v.type=JsonValue::Type::Array;const auto n=count();need(n);v.array.reserve(n);for(uint32_t i=0;i<n;++i)v.array.push_back(json(depth+1));break;}
        case 5:{v.type=JsonValue::Type::Object;const auto n=count();if(n>(b.len-at)/5)throw std::runtime_error("Invalid Rust object");for(uint32_t i=0;i<n;++i){auto key=text();v.object.emplace(std::move(key),json(depth+1));}break;}
        default:throw std::runtime_error("Invalid Rust JSON tag");
        }return v;
    }
    void finish()const{if(at!=b.len)throw std::runtime_error("Trailing Rust payload");}
};
}
bool enabled() noexcept{return active.load(std::memory_order_relaxed);}
void setEnabledForValidation(bool value) noexcept{active.store(value,std::memory_order_relaxed);}
uint64_t successes(unsigned op) noexcept{return bms_rust_successes(op);}
bool splitLines(std::wstring_view text,std::vector<std::wstring>& out){
    if(!enabled())return false;
    try {Owned b(1,text);if(!b.usable())return false;Reader r{b.value};const auto n=r.count();if(n>(b.value.len-r.at)/4)throw std::runtime_error("Invalid Rust lines");std::vector<std::wstring> result;result.reserve(n);for(uint32_t i=0;i<n;++i)result.push_back(r.text());r.finish();out=std::move(result);return true;}
    catch(const LibraryError&){throw;}catch(...){return false;}
}
bool readJson(std::wstring_view text,JsonValue& out){
    if(!enabled())return false;
    try {Owned b(2,text);if(!b.usable())return false;Reader r{b.value};auto result=r.json();r.finish();out=std::move(result);return true;}
    catch(const LibraryError&){throw;}catch(...){return false;}
}
bool discoverCharts(const std::filesystem::path& root,const std::atomic_bool* cancel,std::vector<std::filesystem::path>& out){
    if(!enabled())return false;
    try {Owned b(3,root.native(),cancel);if(!b.usable())return false;Reader r{b.value};const auto n=r.count();if(n>(b.value.len-r.at)/4)throw std::runtime_error("Invalid Rust paths");std::vector<std::filesystem::path> paths;paths.reserve(n);for(uint32_t i=0;i<n;++i)paths.emplace_back(r.text());r.finish();out=std::move(paths);return true;}
    catch(const LibraryError&){throw;}catch(...){return false;}
}
bool normalizeTitle(std::wstring_view text,std::wstring& out){
    if(!enabled())return false;
    try {Owned b(4,text);if(!b.usable())return false;Reader r{b.value};auto result=r.text();r.finish();out=std::move(result);return true;}
    catch(const LibraryError&){throw;}catch(...){return false;}
}
#else
bool enabled() noexcept{return false;}
void setEnabledForValidation(bool) noexcept{}
uint64_t successes(unsigned) noexcept{return 0;}
bool splitLines(std::wstring_view,std::vector<std::wstring>&){return false;}
bool readJson(std::wstring_view,JsonValue&){return false;}
bool discoverCharts(const std::filesystem::path&,const std::atomic_bool*,std::vector<std::filesystem::path>&){return false;}
bool normalizeTitle(std::wstring_view,std::wstring&){return false;}
#endif
}
