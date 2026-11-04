#pragma once
#include <string>
#include <string_view>
namespace sevenmania {
inline bool isLegacyHudFont(std::string_view name) {std::string key;for(unsigned char c:name)if(c>='A'&&c<='Z')key+=char(c+32);else if((c>='a'&&c<='z')||(c>='0'&&c<='9'))key+=char(c);return key.find("bankgothic")!=std::string::npos;}
inline std::string canonicalFont(std::string_view name) {return isLegacyHudFont(name)?"Michroma":std::string(name);}
}
