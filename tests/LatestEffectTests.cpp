#include "LatestEffectRequest.h"

#include <iostream>

int main() {
    LatestEffectRequest request;
    request.submit(L"levelchange");
    request.submit(L"click");
    if (request.take() != L"click") {
        std::cerr << "The last effect request must replace earlier pending requests.\n";
        return 1;
    }
    if (!request.take().empty()) {
        std::cerr << "Taking an effect request must clear the pending request.\n";
        return 1;
    }
    request.submit(L"click");
    request.clear();
    if (!request.take().empty()) return 1;
    std::cout << "LatestEffectRequest tests passed.\n";
    return 0;
}
