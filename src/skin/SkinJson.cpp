#include "SkinInternal.h"
#include <windows.h>
#include <algorithm>
#include <climits>
#include <set>

namespace sevenmania::skin::detail {
std::wstring wide(const std::string& text, const std::string& path) {
    if (text.empty()) return {};
    if (text.size() > INT_MAX) fail(ErrorCode::LimitExceeded,path,"UTF-8 text too large");
    const int size = MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
    if (!size) fail(ErrorCode::InvalidJson,path,"Invalid UTF-8");
    std::wstring result(size,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),result.data(),size);
    return result;
}
std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
    if(!size) fail(ErrorCode::InvalidJson,"","Unpaired Unicode surrogate");
    std::string out(size,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(text.size()),out.data(),size,nullptr,nullptr);
    return out;
}
std::string safePath(const std::string& path) {
    if(path.empty() || path.size()>240 || path.front()=='/' || path.find('\\')!=std::string::npos)
        fail(ErrorCode::UnsafePath,path,"Expected a relative package path using '/' separators");
    const auto text=wide(path,path);
    for(wchar_t c:text) if(c<32 || c==127 || std::wstring(L":*?\"<>|%").find(c)!=std::wstring::npos)
        fail(ErrorCode::UnsafePath,path,"Forbidden path character or external path");
    std::size_t start=0;
    while(start<text.size()) {
        const auto end=text.find(L'/',start);
        auto part=text.substr(start,end==std::wstring::npos?end:end-start);
        if(part.empty() || part==L"." || part==L".." || part.back()==L'.' || part.back()==L' ')
            fail(ErrorCode::UnsafePath,path,"Unsafe path component");
        auto base=part.substr(0,part.find(L'.'));
        while(!base.empty()&&(base.back()==L' '||base.back()==L'.'))base.pop_back();
        for(auto& c:base) if(c>=L'a'&&c<=L'z') c-=32;
        if(base==L"CON" || base==L"PRN" || base==L"AUX" || base==L"NUL" || base==L"CONIN$" || base==L"CONOUT$" ||
            (base.size()==4 && (base.starts_with(L"COM")||base.starts_with(L"LPT")) &&
                (base[3]>=L'0'&&base[3]<=L'9' || base[3]==0xb9 || base[3]==0xb2 || base[3]==0xb3)))
            fail(ErrorCode::UnsafePath,path,"Reserved device name");
        if(end==std::wstring::npos) break;
        start=end+1;
        if(start==text.size()) fail(ErrorCode::UnsafePath,path,"Empty path component");
    }
    return path;
}
bms::JsonValue json(const Bytes& data,const std::string& path,const Limits& limits) {
    if(data.size()>limits.jsonBytes) fail(ErrorCode::LimitExceeded,path,"JSON byte limit exceeded");
    auto text=wide(std::string(data.begin(),data.end()),path);
    if(!text.empty()&&text.front()==0xfeff) text.erase(0,1);
    // Check before allocating the DOM. Keep the existing BMS parser untouched.
    std::vector<std::set<std::wstring>> keys;
    std::uint32_t depth=0,nodes=0;
    for(std::size_t i=0;i<text.size();++i) {
        const auto c=text[i];
        if(c==L'"') {
            const auto start=i++;bool ended=false;
            for(;i<text.size();++i) {
                if(text[i]==L'\\') { if(++i==text.size()) break; }
                else if(text[i]==L'"') { ended=true;break; }
            }
            if(!ended) fail(ErrorCode::InvalidJson,path,"Unterminated string");
            std::size_t next=i+1;
            while(next<text.size() && (text[next]==L' '||text[next]==L'\r'||text[next]==L'\n'||text[next]==L'\t')) ++next;
            if(next<text.size()&&text[next]==L':'&&!keys.empty()) {
                const auto token=text.substr(start,i-start+1);
                std::wstring decoded;
                try {decoded=bms::JsonReaderCpp(token).read().string;}
                catch(const std::exception& e){fail(ErrorCode::InvalidJson,path,e.what());}
                if(!keys.back().insert(decoded).second) fail(ErrorCode::InvalidJson,path,"Duplicate object key");
            }
        } else {
            if(c>127) fail(ErrorCode::InvalidJson,path,"Non-ASCII JSON syntax");
            if(c==L'{'||c==L'[') { if(++depth>limits.jsonDepth) fail(ErrorCode::LimitExceeded,path,"JSON nesting limit exceeded"); }
            if(c==L'}'||c==L']') { if(!depth) fail(ErrorCode::InvalidJson,path,"Unbalanced JSON");--depth; }
            if(c==L'{') keys.emplace_back();
            if(c==L'}') { if(keys.empty()) fail(ErrorCode::InvalidJson,path,"Unbalanced object");keys.pop_back(); }
            if(c==L'{'||c==L'['||c==L','||c==L':') if(++nodes>limits.jsonNodes) fail(ErrorCode::LimitExceeded,path,"JSON node limit exceeded");
        }
    }
    try {
        auto result=bms::JsonReaderCpp(text).read();
        const auto unicode=[&](const auto& self,const bms::JsonValue& v)->void {
            if(v.type==bms::JsonValue::Type::String) (void)utf8(v.string);
            for(const auto& [k,x]:v.object) { (void)utf8(k);self(self,x); }
            for(const auto& x:v.array) self(self,x);
        };
        unicode(unicode,result);return result;
    } catch(const Failure&) { throw; }
    catch(const std::exception& e) { fail(ErrorCode::InvalidJson,path,e.what()); }
}
}
