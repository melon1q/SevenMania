#pragma once
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <stdexcept>
#include <string>
#include <vector>
#include <filesystem>
#include <sstream>

namespace bms {
inline std::wstring decodeBmsText(const std::vector<char>& bytes,const std::filesystem::path& directory={}) {
    if (bytes.empty()) return {};
    const auto u = [&](size_t i) { return static_cast<uint32_t>(static_cast<unsigned char>(bytes[i])); };
    const auto utf32 = [&](bool little) {
        std::wstring out;
        if ((bytes.size() - 4) % 4) throw std::runtime_error("truncated UTF-32");
        for (size_t i = 4; i < bytes.size(); i += 4) {
            uint32_t cp = little ? u(i) | u(i+1)<<8 | u(i+2)<<16 | u(i+3)<<24 : u(i)<<24 | u(i+1)<<16 | u(i+2)<<8 | u(i+3);
            if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) throw std::runtime_error("invalid Unicode");
            if (cp < 0x10000) out.push_back(static_cast<wchar_t>(cp));
            else { cp -= 0x10000; out.push_back(static_cast<wchar_t>(0xd800 + (cp>>10))); out.push_back(static_cast<wchar_t>(0xdc00 + (cp&1023))); }
        }
        return out;
    };
    if (bytes.size() >= 4 && u(0)==0xff && u(1)==0xfe && !u(2) && !u(3)) return utf32(true);
    if (bytes.size() >= 4 && !u(0) && !u(1) && u(2)==0xfe && u(3)==0xff) return utf32(false);
    if (bytes.size() >= 2 && ((u(0)==0xff && u(1)==0xfe) || (u(0)==0xfe && u(1)==0xff))) {
        if (bytes.size()%2) throw std::runtime_error("truncated UTF-16");
        const bool little = u(0)==0xff; std::wstring out;
        for (size_t i=2;i<bytes.size();i+=2) out.push_back(static_cast<wchar_t>(little ? u(i)|(u(i+1)<<8) : (u(i)<<8)|u(i+1)));
        return out;
    }
    const bool bom = bytes.size()>=3 && u(0)==0xef && u(1)==0xbb && u(2)==0xbf;
    std::string ascii(bytes.begin(), bytes.end());
    std::transform(ascii.begin(), ascii.end(), ascii.begin(), [](unsigned char c) { return c < 128 ? static_cast<char>(tolower(c)) : ' '; });
    UINT cp = 0;
    const auto declaration = ascii.find("#charset");
    if (!bom && declaration != std::string::npos) {
        const auto end = ascii.find_first_of("\r\n", declaration);
        const auto name = ascii.substr(declaration+8, end-declaration-8);
        if (name.find("euc-kr")!=std::string::npos || name.find("cp949")!=std::string::npos || name.find("windows-949")!=std::string::npos) cp=949;
        else if (name.find("shift")!=std::string::npos || name.find("932")!=std::string::npos) cp=932;
        else if (name.find("utf-8")!=std::string::npos) cp=CP_UTF8;
    }
    const char* data=bytes.data()+(bom?3:0); const int size=static_cast<int>(bytes.size()-(bom?3:0));
    const auto decode = [&](UINT page, DWORD flags) {
        int count=MultiByteToWideChar(page,flags,data,size,nullptr,0); if (count<=0) return std::wstring{};
        std::wstring out(count,L'\0'); MultiByteToWideChar(page,flags,data,size,out.data(),count); return out;
    };
    if (bom || cp==CP_UTF8 || cp==0) {
        auto out=decode(CP_UTF8,MB_ERR_INVALID_CHARS); if (!out.empty()) return out;
        if (bom || cp==CP_UTF8) throw std::runtime_error("invalid UTF-8");
    }
    if (cp) { auto out=decode(cp,MB_ERR_INVALID_CHARS); if (out.empty()) throw std::runtime_error("invalid declared charset"); return out; }
    auto sjis=decode(932,MB_ERR_INVALID_CHARS), korean=decode(949,MB_ERR_INVALID_CHARS);
    if(!directory.empty() && !sjis.empty() && !korean.empty()) {
        const auto assetMatches=[&](const std::wstring& text) {
            unsigned matches=0,checked=0;std::wistringstream lines(text);std::wstring line;
            while(checked<16 && std::getline(lines,line)) {
                const auto split=line.find_first_of(L" \t");if(split==std::wstring::npos)continue;
                auto key=line.substr(0,split);std::transform(key.begin(),key.end(),key.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});
                if(key.size()!=6 || !key.starts_with(L"#wav"))continue;
                auto value=line.substr(split+1);const auto begin=value.find_first_not_of(L" \t");if(begin==std::wstring::npos)continue;value.erase(0,begin);
                const auto end=value.find_last_not_of(L" \t\r");value.resize(end+1);
                if(value.size()>1 && value.front()==L'"' && value.back()==L'"')value=value.substr(1,value.size()-2);
                if(std::all_of(value.begin(),value.end(),[](wchar_t c){return c<128;}))continue;
                ++checked;auto file=directory/value;std::error_code ec;
                if(std::filesystem::is_regular_file(file,ec)){++matches;continue;}
                for(const auto* ext:{L".wav",L".ogg",L".mp3"}) {
                    file.replace_extension(ext);if(std::filesystem::is_regular_file(file,ec)){++matches;break;}
                }
            }return matches;
        };
        const auto jpMatches=assetMatches(sjis),krMatches=assetMatches(korean);
        if(jpMatches!=krMatches)return jpMatches>krMatches?sjis:korean;
    }
    // Repeated WAV filenames are not independent language evidence: Japanese
    // brackets can turn into hundreds of valid Hangul characters under CP949.
    // Judge the human metadata instead of letting asset counts bias detection.
    const auto metadata=[](const std::wstring& text) {
        std::wstring out;size_t begin=0;
        while(begin<text.size()) {
            const auto end=text.find(L'\n',begin);auto line=text.substr(begin,end==std::wstring::npos?end:end-begin);
            const auto at=line.find_first_not_of(L" \t\r");if(at!=std::wstring::npos)line.erase(0,at);
            const auto space=line.find_first_of(L" \t");auto key=line.substr(0,space);
            std::transform(key.begin(),key.end(),key.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});
            if(key==L"#title" || key==L"#subtitle" || key==L"#artist" || key==L"#subartist" || key==L"#genre")out+=line+L"\n";
            if(end==std::wstring::npos)break;begin=end+1;
        }return out;
    };
    const auto jpMetadata=metadata(sjis),krMetadata=metadata(korean);
    const auto& jp=jpMetadata.empty()?sjis:jpMetadata;
    const auto& kr=krMetadata.empty()?korean:krMetadata;
    const auto hangul=std::count_if(kr.begin(),kr.end(),[](wchar_t c){return c>=0xac00&&c<=0xd7a3;});
    const auto kana=std::count_if(jp.begin(),jp.end(),[](wchar_t c){return c>=0x3040&&c<=0x30ff;});
    if (!korean.empty() && (sjis.empty() || (hangul>=3 && hangul>kana*2))) return korean;
    if (!sjis.empty()) return sjis;
    throw std::runtime_error("unknown chart encoding");
}
} // namespace bms
