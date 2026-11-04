#include "ResultFade.h"
#include <cmath>
#include <stdexcept>
#include <iostream>
int main() {
    using namespace std::chrono_literals;
    ResultFade fade; const auto start = ResultFade::Clock::time_point{};
    const auto check=[](bool value){if(!value)throw std::runtime_error("Result fade boundary failure");};
    check(!fade.active() && fade.darkness(start)==0);
    fade.start(start);
    check(fade.outgoing(start) && fade.darkness(start)==0);
    check(std::abs(fade.darkness(start+100ms)-0.5)<1e-9);
    check(!fade.outgoing(start+200ms) && fade.darkness(start+200ms)==1);
    check(std::abs(fade.darkness(start+325ms)-0.5)<1e-9);
    check(!fade.finished(start+449ms) && fade.finished(start+450ms));
    check(fade.darkness(start+450ms)==0 && fade.darkness(start+1s)==0);
    fade.reset();check(!fade.active() && !fade.finished(start+1s));
    fade.start(start+1s);check(fade.outgoing(start+1s) && fade.darkness(start+1s)==0);
    std::cout<<"Result fade: outgoing/incoming boundaries, reset and restart passed\n";
}
