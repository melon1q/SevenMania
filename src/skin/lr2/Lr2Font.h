#pragma once
#include "Lr2Parser.h"
#include <windows.h>
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string_view>
#include <unordered_map>

// LR2 bitmap fonts (.lr2font) and the DX Library archives (.dxa) stock themes ship them in.
namespace sevenmania::skin::lr2 {
using FontBytes=std::vector<std::uint8_t>;

// DX Library archive, format version 3 with the library's default key: the form LR2's own
// themes use. Other versions and custom keys are not read.
class DxArchive {
public:
    static constexpr std::uintmax_t maxArchiveBytes=64u<<20;
    static constexpr std::size_t maxEntryBytes=16u<<20;

    static std::optional<DxArchive> open(const std::filesystem::path& file) {
        std::error_code error;
        const auto size=std::filesystem::file_size(file,error);
        if(error||size<headerBytes||size>maxArchiveBytes)return std::nullopt;
        DxArchive archive;
        archive.raw_.resize(static_cast<std::size_t>(size));
        std::ifstream input(file,std::ios::binary);
        if(!input.read(reinterpret_cast<char*>(archive.raw_.data()),static_cast<std::streamsize>(size)))return std::nullopt;
        const auto head=archive.plain(0,headerBytes);
        const auto tableSize=u32(head,4),nameTable=u32(head,12),fileTable=u32(head,16),directoryTable=u32(head,20);
        archive.dataStart_=u32(head,8);
        if(u16(head,0)!=0x5844||u16(head,2)!=3||nameTable>size||tableSize>size-nameTable||
           fileTable>directoryTable||directoryTable>tableSize||tableSize-directoryTable<directoryBytes)return std::nullopt;
        const auto table=archive.plain(nameTable,tableSize);
        // The root directory lists its files as a run of fixed-size records.
        const auto count=u32(table,directoryTable+8),first=u32(table,directoryTable+12);
        if(first>directoryTable-fileTable||count>(directoryTable-fileTable-first)/fileBytes)return std::nullopt;
        for(std::uint32_t i=0;i<count;++i) {
            const std::size_t record=fileTable+first+std::size_t(i)*fileBytes;
            const auto name=u32(table,record),attributes=u32(table,record+4);
            Entry entry{{},u32(table,record+32),u32(table,record+36),u32(table,record+40)};
            if(attributes&0x10)continue; // Subdirectories are not needed for fonts.
            if(name>fileTable||fileTable-name<4)return std::nullopt;
            // The name is stored twice, upper-cased first, each padded to a multiple of four bytes.
            const std::size_t length=std::size_t(u16(table,name))*4;
            if(length>fileTable-name-4||length*2>fileTable-name-4)return std::nullopt;
            const auto* text=reinterpret_cast<const char*>(table.data())+name+4;
            entry.name.assign(text,strnlen(text,length));
            const bool packed=entry.stored!=0xffffffffu;
            const std::uint64_t bytes=packed?entry.stored:entry.size;
            if(entry.size>maxEntryBytes||archive.dataStart_>size||entry.address>size-archive.dataStart_||
               bytes>size-archive.dataStart_-entry.address)return std::nullopt;
            archive.entries_.push_back(std::move(entry));
        }
        return archive;
    }

    // Names are compared without case, as the archive stores an upper-cased copy for lookup.
    std::optional<FontBytes> read(std::string_view name) const {
        std::string wanted(name);
        for(char& c:wanted)if(c>='a'&&c<='z')c=static_cast<char>(c-'a'+'A');
        for(const auto& entry:entries_) {
            if(entry.name!=wanted)continue;
            const std::size_t position=dataStart_+entry.address;
            if(entry.stored==0xffffffffu)return plain(position,entry.size);
            return unpack(plain(position,entry.stored),entry.size);
        }
        return std::nullopt;
    }
    std::size_t entryCount() const noexcept {return entries_.size();}

private:
    struct Entry { std::string name; std::uint32_t address{},size{},stored{}; };
    static constexpr std::size_t headerBytes=24,fileBytes=44,directoryBytes=16;
    static std::uint32_t u16(const FontBytes& bytes,std::size_t at) {return bytes[at]|(std::uint32_t(bytes[at+1])<<8);}
    static std::uint32_t u32(const FontBytes& bytes,std::size_t at) {return u16(bytes,at)|(u16(bytes,at+2)<<16);}
    // Every byte is XORed with a 12-byte key selected by its position in the file.
    FontBytes plain(std::size_t position,std::size_t size) const {
        constexpr std::uint8_t key[12]{0x55,0xaa,0x20,0x55,0x55,0x06,0x55,0xaa,0x55,0xd5,0x7c,0x66};
        FontBytes out(raw_.begin()+static_cast<std::ptrdiff_t>(position),raw_.begin()+static_cast<std::ptrdiff_t>(position+size));
        for(std::size_t i=0;i<out.size();++i)out[i]^=key[(position+i)%12];
        return out;
    }
    // DX Library's LZ scheme: a marker byte introduces a back-reference of 4 or more bytes.
    static std::optional<FontBytes> unpack(const FontBytes& packed,std::size_t expected) {
        if(packed.size()<9||u32(packed,0)!=expected||u32(packed,4)>packed.size())return std::nullopt;
        const std::size_t end=u32(packed,4);
        const std::uint8_t marker=packed[8];
        FontBytes out;
        out.reserve(expected);
        std::size_t at=9;
        while(at<end&&out.size()<expected) {
            if(packed[at]!=marker){out.push_back(packed[at++]);continue;}
            if(at+1>=end)return std::nullopt;
            unsigned code=packed[at+1];
            at+=2;
            if(code==marker){out.push_back(marker);continue;}
            if(code>marker)--code;
            std::size_t length=code>>3;
            if(code&4) {
                if(at>=end)return std::nullopt;
                length|=std::size_t(packed[at++])<<5;
            }
            length+=4;
            const unsigned width=(code&3)+1;
            if(width>3||end-at<width)return std::nullopt;
            std::size_t distance=0;
            for(unsigned i=0;i<width;++i)distance|=std::size_t(packed[at+i])<<(8*i);
            at+=width;
            ++distance;
            if(distance>out.size()||length>expected-out.size())return std::nullopt;
            for(std::size_t i=0;i<length;++i)out.push_back(out[out.size()-distance]);
        }
        if(out.size()!=expected)return std::nullopt;
        return out;
    }
    FontBytes raw_;
    std::size_t dataStart_{};
    std::vector<Entry> entries_;
};

struct FontGlyph { int texture{},x{},y{},width{},height{}; };
// Glyph table of one .lr2font. Texture pixels are fetched on demand: a full CJK font
// spans well over a hundred 256x256 sheets and a screen uses a handful.
struct Font {
    static constexpr std::size_t maxTextures=1024,maxGlyphs=70000;
    int size{},margin{};
    std::vector<std::string> textures;
    std::unordered_map<wchar_t,FontGlyph> glyphs;
    std::filesystem::path directory;      // Loose files beside the .lr2font ...
    std::optional<DxArchive> archive;     // ... or entries of the archive that holds it.

    const FontGlyph* glyph(wchar_t character) const noexcept {
        const auto found=glyphs.find(character);
        return found==glyphs.end()?nullptr:&found->second;
    }
    bool covers(std::wstring_view text) const noexcept {
        return std::all_of(text.begin(),text.end(),[&](wchar_t c){return glyph(c)!=nullptr;});
    }
    // Encoded image file of one sheet, or nothing when it is undeclared or unreadable.
    std::optional<FontBytes> textureFile(int index) const {
        if(index<0||static_cast<std::size_t>(index)>=textures.size()||textures[static_cast<std::size_t>(index)].empty())return std::nullopt;
        const auto& name=textures[static_cast<std::size_t>(index)];
        if(archive)return archive->read(name);
        std::error_code error;
        const auto file=directory/std::filesystem::path(std::u8string(name.begin(),name.end()));
        const auto bytes=std::filesystem::file_size(file,error);
        if(error||!bytes||bytes>DxArchive::maxEntryBytes)return std::nullopt;
        FontBytes out(static_cast<std::size_t>(bytes));
        std::ifstream input(file,std::ios::binary);
        if(!input.read(reinterpret_cast<char*>(out.data()),static_cast<std::streamsize>(bytes)))return std::nullopt;
        return out;
    }
};

namespace font_detail {
// LR2 numbers glyphs by a packed Shift_JIS code: single bytes as they are, double bytes
// offset so the table has no gaps. 288 is the wave dash, which has two Unicode forms.
inline std::vector<wchar_t> characters(int code) {
    if(code==288)return {L'〜',L'～'};
    char bytes[2]{};
    int length=1;
    if(code>=256) {
        const int packed=code+(code>=8127?49281:32832);
        if(packed>0xffff)return {};
        bytes[0]=static_cast<char>(packed>>8);
        bytes[1]=static_cast<char>(packed&0xff);
        length=2;
    } else if(code>=0)bytes[0]=static_cast<char>(code);
    else return {};
    wchar_t wide[2]{};
    if(MultiByteToWideChar(932,MB_ERR_INVALID_CHARS,bytes,length,wide,2)!=1)return {};
    return {wide[0]};
}
inline std::optional<Font> parse(const FontBytes& text) {
    Font font;
    std::size_t start=0;
    while(start<text.size()) {
        auto end=std::find_if(text.begin()+static_cast<std::ptrdiff_t>(start),text.end(),[](std::uint8_t c){return c=='\r'||c=='\n';})-text.begin();
        const std::string_view line(reinterpret_cast<const char*>(text.data())+start,static_cast<std::size_t>(end)-start);
        start=static_cast<std::size_t>(end)+1;
        if(line.size()<3||line[0]!='#')continue;
        std::vector<std::string_view> fields;
        for(std::size_t at=0;;) {
            const auto comma=line.find(',',at);
            fields.push_back(line.substr(at,comma==line.npos?comma:comma-at));
            if(comma==line.npos)break;
            at=comma+1;
        }
        const auto number=[&](std::size_t index,int& value) {
            if(index>=fields.size())return false;
            auto field=fields[index];
            while(!field.empty()&&field.front()==' ')field.remove_prefix(1);
            while(!field.empty()&&field.back()==' ')field.remove_suffix(1);
            const auto [end,error]=std::from_chars(field.data(),field.data()+field.size(),value);
            return error==std::errc{}&&end==field.data()+field.size();
        };
        const char command=line[1]>='a'&&line[1]<='z'?static_cast<char>(line[1]-'a'+'A'):line[1];
        if(fields[0].size()!=2)continue;
        int index{};
        if(command=='S'){if(!number(1,font.size))return std::nullopt;}
        else if(command=='M'){if(!number(1,font.margin))return std::nullopt;}
        else if(command=='T') {
            if(!number(1,index)||index<0||static_cast<std::size_t>(index)>=Font::maxTextures||fields.size()<3)continue;
            // Sheets sit beside the font file. A name with a path could leave that folder.
            const auto name=fields[2];
            if(name.empty()||name.find_first_of("/\\:*?")!=name.npos||name==".."||name==".")continue;
            if(font.textures.size()<=static_cast<std::size_t>(index))font.textures.resize(static_cast<std::size_t>(index)+1);
            font.textures[static_cast<std::size_t>(index)]=std::string(name);
        } else if(command=='R') {
            FontGlyph glyph;
            if(!number(1,index)||!number(2,glyph.texture)||!number(3,glyph.x)||!number(4,glyph.y)||
               !number(5,glyph.width)||!number(6,glyph.height))continue;
            if(glyph.texture<0||static_cast<std::size_t>(glyph.texture)>=font.textures.size()||
               font.textures[static_cast<std::size_t>(glyph.texture)].empty()||
               glyph.x<0||glyph.y<0||glyph.width<=0||glyph.height<=0||glyph.width>4096||glyph.height>4096)continue;
            for(const wchar_t character:characters(index))
                if(font.glyphs.size()<Font::maxGlyphs)font.glyphs.insert_or_assign(character,glyph);
        }
    }
    if(font.size<=0||font.size>1024||font.margin<-1024||font.margin>1024||font.glyphs.empty())return std::nullopt;
    return font;
}
}

// Loads the font a #LR2FONT line names. When "<folder>\font.lr2font" is not on disk the
// sibling archive "<folder>.dxa" is tried, which is how LR2 itself reads its stock fonts.
inline std::optional<Font> loadFont(const std::filesystem::path& root,const std::filesystem::path& declaringFile,
                                    const std::string& value) {
    constexpr std::uintmax_t maxDefinitionBytes=4u<<20;
    try {
        const auto file=resolveAssetPath(root,declaringFile,value);
        std::error_code error;
        const auto bytes=std::filesystem::file_size(file,error);
        if(error||!bytes||bytes>maxDefinitionBytes)return std::nullopt;
        FontBytes text(static_cast<std::size_t>(bytes));
        std::ifstream input(file,std::ios::binary);
        if(!input.read(reinterpret_cast<char*>(text.data()),static_cast<std::streamsize>(bytes)))return std::nullopt;
        auto font=font_detail::parse(text);
        if(font)font->directory=file.parent_path();
        return font;
    } catch(const std::exception&) {
    }
    try {
        const auto separator=value.find_last_of("/\\");
        if(separator==value.npos||separator==0||separator+1>=value.size())return std::nullopt;
        auto archive=DxArchive::open(resolveAssetPath(root,declaringFile,value.substr(0,separator)+".dxa"));
        if(!archive)return std::nullopt;
        const auto text=archive->read(value.substr(separator+1));
        if(!text||text->size()>maxDefinitionBytes)return std::nullopt;
        auto font=font_detail::parse(*text);
        if(font)font->archive=std::move(archive);
        return font;
    } catch(const std::exception&) {
        return std::nullopt;
    }
}
}
