#pragma once
#include "Lr2Parser.h"
#include <algorithm>
#include <charconv>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// Groups loader diagnostics so compatibility gaps can be ranked by frequency.
namespace sevenmania::skin::lr2 {
struct DiagnosticGroup {
    std::string category;
    std::size_t count{};
    bool error{};
    // Trailing numeric argument (timer, option, number or image ID) and its frequency.
    std::map<int,std::size_t> ids;
    // Trailing text argument such as an unhandled command name.
    std::map<std::string,std::size_t> names;
};

inline std::vector<DiagnosticGroup> summarizeDiagnostics(const std::vector<Diagnostic>& diagnostics) {
    std::map<std::pair<bool,std::string>,DiagnosticGroup> groups;
    for(const auto& diagnostic:diagnostics) {
        std::string_view message=diagnostic.message;
        std::string category;
        std::optional<int> id;
        std::string name;
        if(message.starts_with("IMAGE ")) {
            // "IMAGE <index> (<name>): <reason>" varies per file; group by the reason.
            const auto reason=message.find("): ");
            category="IMAGE: "+std::string(reason==message.npos?message.substr(6):message.substr(reason+3));
        } else {
            const auto digits=message.find_last_not_of("0123456789");
            if(digits!=message.npos&&digits+1<message.size()) {
                auto first=digits+1;
                if(message[digits]=='-'&&digits>0&&message[digits-1]==' ')--first;
                int value{};
                const auto number=message.substr(first);
                const auto [end,error]=std::from_chars(number.data(),number.data()+number.size(),value);
                if(error==std::errc{}&&end==number.data()+number.size()&&first>0&&message[first-1]==' ') {
                    id=value;
                    message=message.substr(0,first-1);
                    if(message.ends_with(':'))message.remove_suffix(1);
                }
            }
            if(!id)if(const auto colon=message.find(": ");colon!=message.npos) {
                name=message.substr(colon+2);
                message=message.substr(0,colon);
            }
            category=message;
        }
        auto& group=groups[{diagnostic.error,category}];
        group.category=std::move(category);
        group.error=diagnostic.error;
        ++group.count;
        if(id)++group.ids[*id];
        if(!name.empty())++group.names[name];
    }
    std::vector<DiagnosticGroup> out;
    out.reserve(groups.size());
    for(auto& [key,group]:groups)out.push_back(std::move(group));
    std::stable_sort(out.begin(),out.end(),[](const DiagnosticGroup& a,const DiagnosticGroup& b) {
        return a.error!=b.error?a.error:a.count>b.count;
    });
    return out;
}

inline std::string formatDiagnosticSummary(const std::vector<DiagnosticGroup>& groups) {
    std::string text;
    for(const auto& group:groups) {
        text+=(group.error?"error   ":"warning ")+std::to_string(group.count)+"x  "+group.category;
        if(!group.ids.empty()||!group.names.empty()) {
            text+="  [";
            bool first=true;
            const auto item=[&](const std::string& label,std::size_t count) {
                if(!first)text+=' ';
                first=false;
                text+=label;
                if(count>1)text+="x"+std::to_string(count);
            };
            for(const auto& [id,count]:group.ids)item(std::to_string(id),count);
            for(const auto& [name,count]:group.names)item(name,count);
            text+=']';
        }
        text+='\n';
    }
    return text;
}
}
