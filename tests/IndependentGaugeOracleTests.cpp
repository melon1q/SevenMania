#include "IndependentGauge.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main(int argc,char** argv) {
    using namespace sevenmania::independent_gauge;
    if(argc!=2) {std::cerr<<"Reference CSV path required\n";return 2;}
    std::ifstream input(argv[1]);
    if(!input) {std::cerr<<"Cannot open reference CSV\n";return 2;}
    std::string line;std::size_t checked=0,failures=0;
    while(std::getline(input,line)) {
        int profile,kind,event;std::size_t notes;double total;float before,rate,expected;char comma;
        std::istringstream row(line);
        if(!(row>>profile>>comma) || comma!=',' || !(row>>total>>comma) || comma!=',' ||
           !(row>>notes>>comma) || comma!=',' || !(row>>kind>>comma) || comma!=',' ||
           !(row>>event>>comma) || comma!=',' || !(row>>before>>comma) || comma!=',' ||
           !(row>>rate>>comma) || comma!=',' || !(row>>expected)) {
            std::cerr<<"Invalid reference row "<<checked+1<<'\n';return 2;
        }
        Bank bank;
        if(!bank.configure(static_cast<Profile>(profile),total,notes))return 2;
        bank.setValue(static_cast<Kind>(kind),before);
        bank.apply(static_cast<Event>(event),rate);
        const auto actual=bank.value(static_cast<Kind>(kind));
        if(std::abs(actual-expected)>0.00005f) {
            if(failures<12)std::cerr<<"Mismatch: "<<line<<" actual="<<actual<<'\n';
            ++failures;
        }
        ++checked;
    }
    std::cout<<"Compared "<<checked<<" reference events; mismatches: "<<failures<<'\n';
    return checked>0 && failures==0?0:1;
}
