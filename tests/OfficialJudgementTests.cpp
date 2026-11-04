#include "OfficialJudgement.h"
#include <iostream>
#include <limits>
#include <stdexcept>
void require(bool v){if(!v)throw std::runtime_error("official judgement boundary mismatch");}
int main(){try{
    constexpr int expected[4][5]={{7,20,39,128,135},{13,30,68,155,170},{17,43,106,183,205},{21,60,135,210,240}};
    for(int rank=0;rank<4;++rank){auto w=bms::officialJudgeWindow(rank);
        require(w.pgreat==expected[rank][0]&&w.great==expected[rank][1]&&w.good==expected[rank][2]&&w.badFast==expected[rank][3]&&w.badSlow==expected[rank][4]);
        require(bms::judgeOfficialMilliseconds(0,rank)==TimingJudgement::PGreat);
        for(double sign:{-1.,1.}){
            require(bms::judgeOfficialMilliseconds(sign*w.pgreat,rank)==TimingJudgement::PGreat);
            require(bms::judgeOfficialMilliseconds(sign*(w.pgreat+.000001),rank)==TimingJudgement::Great);
            require(bms::judgeOfficialMilliseconds(sign*w.great,rank)==TimingJudgement::Great);
            require(bms::judgeOfficialMilliseconds(sign*(w.great+.000001),rank)==TimingJudgement::Good);
            require(bms::judgeOfficialMilliseconds(sign*w.good,rank)==TimingJudgement::Good);
            require(bms::judgeOfficialMilliseconds(sign*(w.good+.000001),rank)==TimingJudgement::Bad);
            int bad=sign<0?w.badFast:w.badSlow;
            require(bms::judgeOfficialMilliseconds(sign*bad,rank)==TimingJudgement::Bad);
            require(!bms::judgeOfficialMilliseconds(sign*(bad+.000001),rank));
        }
    }
    require(!bms::judgeOfficialMilliseconds(std::numeric_limits<double>::quiet_NaN(),2));
    require(bms::officialJudgeWindow(4).pgreat==17&&bms::officialJudgementId=="SM-JUDGE-1");
    std::cout<<"SM-JUDGE-1 integer tables and fractional inclusive/asymmetric boundaries passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
