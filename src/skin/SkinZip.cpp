#include "SkinInternal.h"
#include <windows.h>
#include <zlib.h>
#include <algorithm>
#include <fstream>
#include <set>

namespace sevenmania::skin::detail {
namespace {
struct Entry { std::string path; std::uint16_t flags{},method{}; std::uint32_t crc{},packed{},size{},local{}; bool directory{}; };
class Zip {
    Bytes bytes_;
    const Limits& limits_;
    void need(std::size_t at,std::size_t count) const {
        if(at>bytes_.size() || count>bytes_.size()-at) fail(ErrorCode::InvalidZip,"","Truncated ZIP range");
    }
    std::uint16_t u16(std::size_t p) const { need(p,2);return std::uint16_t(bytes_[p]|bytes_[p+1]<<8); }
    std::uint32_t u32(std::size_t p) const { need(p,4);return std::uint32_t(u16(p))|std::uint32_t(u16(p+2))<<16; }
    std::string str(std::size_t p,std::size_t n) const {need(p,n);return {bytes_.begin()+p,bytes_.begin()+p+n};}
    void extra(std::size_t p,std::size_t n) const {
        need(p,n);const auto end=p+n;
        while(p<end) {
            if(end-p<4) fail(ErrorCode::InvalidZip,"","Malformed extra field");
            const auto tag=u16(p),len=u16(p+2);p+=4;
            if(len>end-p) fail(ErrorCode::InvalidZip,"","Malformed extra field length");
            if(tag==1 || tag==0x7075) fail(ErrorCode::InvalidZip,"","ZIP64 and alternate Unicode path fields are unsupported");
            p+=len;
        }
    }
    void allowed(const Entry& e) const {
        const auto& p=e.path;
        if(e.directory) return;
        const auto ext=p.substr(p.find_last_of('.')==std::string::npos?p.size():p.find_last_of('.'));
        const bool valid = p=="manifest.json" || (p.starts_with("licenses/")&&ext==".txt") ||
            (p.starts_with("screens/")&&ext==".json") || (p.starts_with("animations/")&&ext==".json") ||
            ((p.starts_with("assets/images/")||p.starts_with("assets/sprites/")) && (ext==".png"||ext==".jpg"||ext==".jpeg")) ||
            (p.starts_with("assets/fonts/")&&(ext==".ttf"||ext==".otf"));
        if(!valid) fail(ErrorCode::UnsupportedResource,p,"Unsupported entry; v1 accepts JSON, PNG, JPEG, TTF and OTF only (no video/scripts/DDS)");
    }
public:
    Zip(const std::filesystem::path& file,const Limits& limits):limits_(limits) {
        std::ifstream input(file,std::ios::binary|std::ios::ate);
        if(!input) fail(ErrorCode::Io,"","Cannot open skin package");
        const auto size=input.tellg();
        if(size<0 || std::uint64_t(size)>limits.archiveBytes) fail(ErrorCode::LimitExceeded,"","Archive byte limit exceeded");
        bytes_.resize(static_cast<std::size_t>(size));input.seekg(0);
        if(!bytes_.empty()&&!input.read(reinterpret_cast<char*>(bytes_.data()),std::streamsize(bytes_.size()))) fail(ErrorCode::Io,"","Cannot read complete package");
    }
    Archive read() {
        if(bytes_.size()<22) fail(ErrorCode::InvalidZip,"","Missing ZIP end record");
        std::size_t end=bytes_.size()-22;bool found=false;
        const auto first=bytes_.size()>65557?bytes_.size()-65557:0;
        for(;;) {
            if(u32(end)==0x06054b50 && u16(end+20)==bytes_.size()-end-22) {found=true;break;}
            if(end==first)break;--end;
        }
        if(!found) fail(ErrorCode::InvalidZip,"","Missing ZIP end record or trailing data");
        const auto count=u16(end+10);const auto cdSize=u32(end+12),cdOffset=u32(end+16);
        if(u16(end+4)||u16(end+6)||u16(end+8)!=count||count==0xffff||cdSize==0xffffffff||cdOffset==0xffffffff)
            fail(ErrorCode::InvalidZip,"","Multi-disk and ZIP64 packages are unsupported");
        if(count>limits_.files) fail(ErrorCode::LimitExceeded,"","ZIP entry count limit exceeded");
        need(cdOffset,cdSize);
        if(std::uint64_t(cdOffset)+cdSize!=end) fail(ErrorCode::InvalidZip,"","Invalid central directory extent");
        std::size_t p=cdOffset;std::uint64_t total=0;
        std::vector<Entry> entries;std::set<std::wstring> names;
        for(unsigned i=0;i<count;++i) {
            need(p,46);
            if(u32(p)!=0x02014b50 || u16(p+6)>20 || u16(p+34)) fail(ErrorCode::InvalidZip,"","Invalid/unsupported central entry");
            Entry e;e.flags=u16(p+8);e.method=u16(p+10);e.crc=u32(p+16);e.packed=u32(p+20);e.size=u32(p+24);e.local=u32(p+42);
            if((e.flags&~0x080eu)!=0 || (e.method!=0&&e.method!=8)) fail(ErrorCode::InvalidZip,"","Encrypted or unsupported compression entry");
            const auto nameLen=u16(p+28),extraLen=u16(p+30),commentLen=u16(p+32);
            need(p+46,std::size_t(nameLen)+extraLen+commentLen);auto name=str(p+46,nameLen);
            if(!(e.flags&0x800)&&std::any_of(name.begin(),name.end(),[](unsigned char c){return c>=128;}))
                fail(ErrorCode::InvalidZip,name,"Non-ASCII paths must carry the ZIP UTF-8 flag");
            e.directory=!name.empty()&&name.back()=='/';if(e.directory)name.pop_back();
            e.path=safePath(name);
            const auto attrs=u32(p+38),type=(attrs>>16)&0170000;
            if((attrs&0x400)|| (type && type!=0100000 && type!=0040000) || (e.directory && type==0100000) || (!e.directory && type==0040000))
                fail(ErrorCode::UnsafePath,e.path,"Symlink/device/reparse entry is forbidden");
            auto key=wide(e.path,e.path);std::wstring lower(key.size(),L'\0');
            if(LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,key.data(),int(key.size()),lower.data(),int(lower.size()),nullptr,nullptr,0)!=int(key.size()))
                fail(ErrorCode::UnsafePath,e.path,"Cannot canonicalize path case");
            if(!names.insert(lower).second) fail(ErrorCode::InvalidZip,e.path,"Duplicate or case-colliding entry");
            extra(p+46+nameLen,extraLen);allowed(e);
            if(e.size>limits_.fileBytes || (total+=e.size)>limits_.totalBytes ||
                (e.size && (!e.packed || std::uint64_t(e.size)>std::uint64_t(e.packed)*limits_.compressionRatio)))
                fail(ErrorCode::LimitExceeded,e.path,"Uncompressed size/compression ratio limit exceeded");
            if(e.directory&&(e.size||e.packed||e.crc||e.method)) fail(ErrorCode::InvalidZip,e.path,"Nonempty directory entry");
            if(e.path.ends_with(".json")&&e.size>limits_.jsonBytes) fail(ErrorCode::LimitExceeded,e.path,"JSON byte limit exceeded");
            entries.push_back(e);p+=46+std::size_t(nameLen)+extraLen+commentLen;
            if(p>end) fail(ErrorCode::InvalidZip,e.path,"Entry exceeds central directory");
        }
        if(p!=end) fail(ErrorCode::InvalidZip,"","Central directory entry count/size mismatch");
        std::vector<std::pair<std::size_t,std::size_t>> spans;
        Archive result;
        for(const auto& e:entries) {
            const std::size_t at=e.local;need(at,30);
            if(at>=cdOffset || u32(at)!=0x04034b50 || u16(at+4)>20 || u16(at+6)!=e.flags || u16(at+8)!=e.method)
                fail(ErrorCode::InvalidZip,e.path,"Invalid/mismatched local header");
            const auto nameLen=u16(at+26),extraLen=u16(at+28);
            const auto expected=e.path+(e.directory?"/":"");
            if(str(at+30,nameLen)!=expected) fail(ErrorCode::InvalidZip,e.path,"Local/central name mismatch");
            extra(at+30+nameLen,extraLen);
            for(unsigned field=0;field<3;++field) {
                const std::uint32_t expectedValue=field==0?e.crc:field==1?e.packed:e.size;
                const auto value=u32(at+14+field*4);
                if(value!=expectedValue && (!(e.flags&8)||value)) fail(ErrorCode::InvalidZip,e.path,"Local/central sizes or CRC differ");
            }
            const std::size_t data=at+30+nameLen+extraLen;need(data,e.packed);std::size_t finish=data+e.packed;
            if(e.flags&8) {
                if(u32(finish)==0x08074b50)finish+=4;
                if(u32(finish)!=e.crc||u32(finish+4)!=e.packed||u32(finish+8)!=e.size) fail(ErrorCode::InvalidZip,e.path,"Invalid data descriptor");
                finish+=12;
            }
            if(finish>cdOffset)fail(ErrorCode::InvalidZip,e.path,"Local data overlaps central directory");
            spans.emplace_back(at,finish);
            if(e.directory)continue;
            auto out=std::make_shared<Bytes>(std::max<std::uint32_t>(1,e.size));
            if(e.method==0) {
                if(e.packed!=e.size)fail(ErrorCode::InvalidZip,e.path,"Stored size mismatch");
                std::copy_n(bytes_.data()+data,e.size,out->data());
            } else {
                z_stream stream{};
                if(inflateInit2(&stream,-MAX_WBITS)!=Z_OK) fail(ErrorCode::InvalidZip,e.path,"Inflater initialization failed");
                struct End {z_stream* stream;~End(){inflateEnd(stream);}} guard{&stream};
                stream.next_in=bytes_.data()+data;stream.avail_in=e.packed;stream.next_out=out->data();stream.avail_out=static_cast<uInt>(out->size());
                const int status=inflate(&stream,Z_FINISH);
                if(status!=Z_STREAM_END || stream.total_out!=e.size || stream.total_in!=e.packed)
                    fail(ErrorCode::InvalidZip,e.path,"Deflate stream/declared size mismatch");
            }
            out->resize(e.size);
            if(crc32(0,out->data(),static_cast<uInt>(out->size()))!=e.crc)fail(ErrorCode::InvalidZip,e.path,"CRC mismatch");
            result.emplace(e.path,std::move(out));
        }
        std::sort(spans.begin(),spans.end());std::size_t cursor=0;
        for(const auto& span:spans) {if(span.first!=cursor)fail(ErrorCode::InvalidZip,"","Overlapping local entries or unsupported ZIP prefix/gap");cursor=span.second;}
        if(cursor!=cdOffset)fail(ErrorCode::InvalidZip,"","Unexpected data before central directory");
        return result;
    }
};
}
Archive readZip(const std::filesystem::path& file,const Limits& limits) { return Zip(file,limits).read(); }
}
