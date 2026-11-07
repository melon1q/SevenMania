#pragma once
#include "Judgement.h"
#include <algorithm>
#include <cstddef>
#include <array>
#include <string>

enum class GrooveGaugeMode : int { Normal, Easy, Hard };

class GrooveGauge {
    double value_ = 20.0;
    size_t noteCount_{};
    GrooveGaugeMode mode_ = GrooveGaugeMode::Normal;
    bool failed_{};
    double total_ = 200;
    bool authored_{},failAtZero_{};double initial_=20,clear_=80;
    std::array<double,5> awards_{};
public:
    void configure(size_t noteCount, GrooveGaugeMode mode, double total = 200) {
        authored_=false;
        noteCount_ = noteCount; total_=total>0?total:200;
        mode_ = mode;
        reset();
    }
    void configureAuthored(std::wstring level,std::wstring extra) {
        // Published nanasigroove 1.552 compatibility table. Older 1.2x
        // releases interpret these aliases differently.
        if(level==L"gtp-easy"){level=L"beginner";if(extra==L"off")extra=L"easy";}
        else if(level==L"gtp-normal")level=L"normal";
        else if(level==L"gtp-full" || level==L"gtp-extra" || level==L"gtp-cool") {
            level=L"hyper";extra=extra==L"easy"?L"off":extra==L"off"?L"hard":extra;
        } else if(level==L"gtp-10miss") {
            level=L"danger";extra=extra==L"easy"?L"off":extra==L"off"?L"hard":extra;
        }
        authored_=true;initial_=25;clear_=80;failAtZero_=false;awards_={1,1,.5,-3,-5};
        if(level==L"beginner") {initial_=30;clear_=70;awards_={1.2,1.2,.6,-2.4,-4};}
        if(level==L"hyper") {initial_=50;clear_=90;failAtZero_=true;awards_={1,.9,0,-4.5,-7.5};}
        if(level==L"danger") {initial_=100;clear_=0;failAtZero_=true;awards_={0,0,0,-20,-20};}
        if(extra==L"easy" && level==L"beginner")awards_={1.5,1.5,.75,-2.25,-3.75};
        if(extra==L"easy" && level==L"normal")awards_={1.1,1.1,.54,-2.71,-4.5};
        if(extra==L"hard") {
            if(level==L"beginner"){initial_=25;clear_=80;awards_={1.1,1,.5,-2.71,-4.5};}
            if(level==L"normal"){initial_=80;clear_=0;failAtZero_=true;awards_={.9,.8,.25,-3.31,-5.5};}
            if(level==L"hyper"){initial_=50;clear_=100;awards_={1,0,0,-9,-15};}
            if(level==L"danger")awards_={0,0,0,-100,-100};
        }
        if(extra==L"veryhard") {
            if(level==L"beginner"){initial_=25;clear_=80;awards_={1,1,.5,-2.86,-4.75};}
            if(level==L"normal"){initial_=60;clear_=0;failAtZero_=true;awards_={.79,.79,.2,-3.6,-6};}
            if(level==L"hyper"){initial_=40;clear_=100;awards_={1,0,0,-15,-25};}
            if(level==L"danger")awards_={0,-.02,-100,-100,-100};
        }
        reset();
    }
    void reset() {value_=authored_?initial_:(mode_==GrooveGaugeMode::Hard?100.0:20.0);failed_=false;}
    void apply(TimingJudgement judgement,double weight=1) {
        if (failed_) return;
        const double noteGain = total_ / static_cast<double>(std::max<size_t>(1, noteCount_));
        double delta = 0.0;
        switch (judgement) {
        case TimingJudgement::PGreat:
        case TimingJudgement::Great:
            delta = mode_ == GrooveGaugeMode::Hard ? 0.16 : noteGain;
            break;
        case TimingJudgement::Good:
            delta = mode_ == GrooveGaugeMode::Hard ? 0.0 : noteGain / 2.0;
            break;
        case TimingJudgement::Bad:
            delta = mode_ == GrooveGaugeMode::Hard ? -5.0 : mode_ == GrooveGaugeMode::Easy ? -1.6 : -2.0;
            break;
        case TimingJudgement::Poor:
            delta = mode_ == GrooveGaugeMode::Hard ? -9.0 : mode_ == GrooveGaugeMode::Easy ? -4.8 : -6.0;
            break;
        }
        if(authored_) {delta=awards_[static_cast<size_t>(judgement)];if(delta>0)delta*=noteGain;}
        value_=std::clamp(value_+delta*weight,0.0,100.0);
        failed_=(authored_?failAtZero_:mode_==GrooveGaugeMode::Hard) && value_<=0;
    }
    void damage(double amount) { value_=std::max(0.0,value_-std::max(0.0,amount)); failed_=(authored_?failAtZero_:mode_==GrooveGaugeMode::Hard) && value_<=0; }
    void holdTick(bool pressed) {apply(pressed?TimingJudgement::PGreat:TimingJudgement::Poor,0.5);}
    double value() const { return value_; }
    GrooveGaugeMode mode() const { return mode_; }
    size_t noteCount() const { return noteCount_; }
    bool depleted() const { return failed_; }
    bool clearConditionMet() const {
        return authored_?!failed_ && value_>=clear_:mode_==GrooveGaugeMode::Hard?value_>0:value_>=80;
    }
};
