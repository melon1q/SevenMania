#pragma once
#include "Lr2Parser.h"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>
#include <utility>

// Per-skin CUSTOMOPTION / CUSTOMFILE / CUSTOMOFFSET selections in one UTF-8 text file.
//   [c:/path/to/play.lr2skin]
//   option<TAB>name<TAB>choice
//   file<TAB>name<TAB>filename stem
//   offset<TAB>name<TAB>x,y,w,h,r,a
namespace sevenmania::skin::lr2 {
namespace config_detail {
inline constexpr std::uintmax_t maxStoreBytes=1u<<20;
using Sections=std::vector<std::pair<std::string,std::vector<std::string>>>;

inline std::string skinKey(const std::filesystem::path& skin) {
    std::error_code error;
    auto path=std::filesystem::weakly_canonical(skin,error);
    if(error)path=skin;
    const auto text=path.generic_u8string();
    std::string key(text.begin(),text.end());
    for(char& c:key)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
    return key;
}
inline Sections readSections(const std::filesystem::path& store) {
    Sections sections;
    std::error_code error;
    const auto size=std::filesystem::file_size(store,error);
    if(error||size>maxStoreBytes)return sections;
    std::ifstream input(store,std::ios::binary);
    std::string line;
    while(std::getline(input,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.size()>2&&line.front()=='['&&line.back()==']')sections.push_back({line.substr(1,line.size()-2),{}});
        else if(!line.empty()&&!sections.empty())sections.back().second.push_back(std::move(line));
    }
    return sections;
}
// Names come from CSV fields; a tab or line break in one would corrupt the record.
inline bool storable(const std::string& text) {
    return !text.empty()&&text.find_first_of("\t\r\n")==text.npos;
}
}

inline SkinChoices loadSkinChoices(const std::filesystem::path& store,const std::filesystem::path& skin) {
    SkinChoices choices;
    const auto key=config_detail::skinKey(skin);
    for(const auto& [name,lines]:config_detail::readSections(store)) {
        if(name!=key)continue;
        for(const auto& line:lines) {
            const auto first=line.find('\t'),second=line.find('\t',first==line.npos?first:first+1);
            if(second==line.npos)continue;
            const auto kind=line.substr(0,first),item=line.substr(first+1,second-first-1),value=line.substr(second+1);
            if(item.empty())continue;
            if(kind=="option")choices.options[item]=value;
            else if(kind=="file")choices.files[item]=value;
            else if(kind=="offset") {
                std::array<int,6> values{};
                std::size_t start=0;
                bool valid=true;
                for(std::size_t i=0;valid&&i<values.size();++i) {
                    const auto end=std::min(value.find(',',start),value.size());
                    const auto [parsed,error]=std::from_chars(value.data()+start,value.data()+end,values[i]);
                    valid=error==std::errc{}&&parsed==value.data()+end&&(i+1==values.size())==(end==value.size());
                    start=end+1;
                }
                if(valid)choices.offsets[item]=values;
            }
        }
    }
    return choices;
}

// Replaces this skin's section and leaves the other skins' selections untouched.
inline bool saveSkinChoices(const std::filesystem::path& store,const std::filesystem::path& skin,const SkinChoices& choices) {
    using config_detail::storable;
    auto sections=config_detail::readSections(store);
    const auto key=config_detail::skinKey(skin);
    if(!storable(key))return false;
    std::erase_if(sections,[&](const auto& section){return section.first==key;});
    std::vector<std::string> lines;
    for(const auto& [name,choice]:choices.options)if(storable(name)&&storable(choice))lines.push_back("option\t"+name+"\t"+choice);
    for(const auto& [name,file]:choices.files)if(storable(name)&&storable(file))lines.push_back("file\t"+name+"\t"+file);
    for(const auto& [name,values]:choices.offsets) {
        if(!storable(name))continue;
        std::string text="offset\t"+name+"\t";
        for(std::size_t i=0;i<values.size();++i)text+=(i?",":"")+std::to_string(values[i]);
        lines.push_back(std::move(text));
    }
    if(!lines.empty())sections.push_back({key,std::move(lines)});
    std::ostringstream text;
    for(const auto& [name,rows]:sections) {
        text<<'['<<name<<"]\n";
        for(const auto& row:rows)text<<row<<'\n';
    }
    // Write beside the store, then swap, so an interrupted save keeps the previous file.
    auto temporary=store;
    temporary+=L".tmp";
    {
        std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
        const auto bytes=text.str();
        output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
        if(!output)return false;
    }
    std::error_code error;
    std::filesystem::rename(temporary,store,error);
    if(error)std::filesystem::remove(temporary,error);
    return !error;
}
}
