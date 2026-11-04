#pragma once
#include "RustBackend.h"
#include <cmath>
#include <cwctype>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace bms {
struct JsonValue {
    enum class Type { Null,Number,String,Boolean,Array,Object } type=Type::Null;
    double number{};bool boolean{};std::wstring string;
    std::vector<JsonValue> array;std::map<std::wstring,JsonValue> object;
    const JsonValue& operator[](const std::wstring& key) const {static const JsonValue missing;const auto i=object.find(key);return i==object.end()?missing:i->second;}
    double num(double fallback=0) const {return type==Type::Number?number:fallback;}
    std::wstring str(const std::wstring& fallback=L"") const {return type==Type::String?string:fallback;}
};
class JsonReaderCpp {
    const std::wstring& text_;size_t at_{};
    [[noreturn]] void fail() const {throw std::runtime_error("invalid JSON at character "+std::to_string(at_));}
    void space(){while(at_<text_.size() && (text_[at_]==L' ' || text_[at_]==L'\r' || text_[at_]==L'\n' || text_[at_]==L'\t'))++at_;}
    bool take(wchar_t c){space();if(at_<text_.size() && text_[at_]==c){++at_;return true;}return false;}
    std::wstring string(){
        if(!take(L'"'))fail();std::wstring out;
        while(at_<text_.size()){
            wchar_t c=text_[at_++];if(c==L'"')return out;if(c<32)fail();
            if(c!=L'\\'){out+=c;continue;}if(at_==text_.size())fail();c=text_[at_++];
            switch(c){case L'"':case L'\\':case L'/':out+=c;break;
            case L'b':out+=L'\b';break;case L'f':out+=L'\f';break;case L'n':out+=L'\n';break;case L'r':out+=L'\r';break;case L't':out+=L'\t';break;
            case L'u':{unsigned code{};for(int i=0;i<4;++i){if(at_==text_.size())fail();c=text_[at_++];code*=16;if(c>=L'0'&&c<=L'9')code+=c-L'0';else if(c>=L'a'&&c<=L'f')code+=c-L'a'+10;else if(c>=L'A'&&c<=L'F')code+=c-L'A'+10;else fail();}out+=static_cast<wchar_t>(code);break;}
            default:fail();}
        }fail();
    }
    JsonValue value(int depth){
        if(depth>128)fail();space();if(at_==text_.size())fail();JsonValue out;
        if(text_[at_]==L'"'){out.type=JsonValue::Type::String;out.string=string();}
        else if(take(L'{')){out.type=JsonValue::Type::Object;if(take(L'}'))return out;do{auto key=string();if(!take(L':'))fail();out.object[std::move(key)]=value(depth+1);}while(take(L','));if(!take(L'}'))fail();}
        else if(take(L'[')){out.type=JsonValue::Type::Array;if(take(L']'))return out;do{out.array.push_back(value(depth+1));}while(take(L','));if(!take(L']'))fail();}
        else if(text_.compare(at_,4,L"null")==0){at_+=4;}
        else if(text_.compare(at_,4,L"true")==0){out.type=JsonValue::Type::Boolean;out.boolean=true;at_+=4;}
        else if(text_.compare(at_,5,L"false")==0){out.type=JsonValue::Type::Boolean;at_+=5;}
        else {
            const auto start=at_;if(text_[at_]==L'-')++at_;if(at_==text_.size())fail();
            if(text_[at_]==L'0')++at_;else{if(text_[at_]<L'1'||text_[at_]>L'9')fail();while(at_<text_.size()&&iswdigit(text_[at_]))++at_;}
            if(at_<text_.size()&&text_[at_]==L'.'){++at_;const auto n=at_;while(at_<text_.size()&&iswdigit(text_[at_]))++at_;if(n==at_)fail();}
            if(at_<text_.size()&&(text_[at_]==L'e'||text_[at_]==L'E')){++at_;if(at_<text_.size()&&(text_[at_]==L'+'||text_[at_]==L'-'))++at_;const auto n=at_;while(at_<text_.size()&&iswdigit(text_[at_]))++at_;if(n==at_)fail();}
            out.type=JsonValue::Type::Number;out.number=std::stod(text_.substr(start,at_-start));if(!std::isfinite(out.number))fail();
        }return out;
    }
public:
    explicit JsonReaderCpp(const std::wstring& text):text_(text){}
    JsonValue read(){auto out=value(0);space();if(at_!=text_.size())fail();return out;}
};
class JsonReader {
    const std::wstring& text_;
public:
    explicit JsonReader(const std::wstring& text):text_(text){}
    JsonValue read(){JsonValue out;if(rust_backend::readJson(text_,out))return out;return JsonReaderCpp(text_).read();}
};
} // namespace bms
