#include "../src/SettingsController.h"
#include <iostream>
#include <stdexcept>
int main() {
    try {
        std::map<int, int> actual{{1012,240},{1013,4},{1040,50},{1042,100}};
        unsigned saves = 0, applications = 0;
        bms::SettingsController controller([&](int id) { return actual[id]; }, [&](int id, int value, std::wstring& error) {
            ++applications;
            if (id == 1042) { error = L"Simulated device rejection"; return false; }
            actual[id] = value; return true;
        }, [&] { ++saves; });
        auto require = [](bool value, const char* message) { if (!value) throw std::runtime_error(message); };
        require(controller.set(1040, 150).value == 100, "percentage clamping failed");
        require(controller.set(1262, 11).value == 11 && actual[1013] == 11, "summary did not resolve to the original setting");
        require(controller.set(1266,3).value==3&&actual[1049]==3,"summary accuracy limit not synchronized");
        require(!controller.set(1049,-1).applied&&!controller.set(1049,5).applied,"invalid accuracy limit accepted");
        const auto before = saves;
        require(!controller.set(1012, 123).applied && !controller.set(9999, 1).applied && saves == before, "invalid commands were saved");
        const auto rejected = controller.set(1042, 10);
        require(!rejected.applied && rejected.value == 100 && !rejected.error.empty() && saves == before, "device failure changed or persisted actual state");
        const auto oldApplications = applications;
        require(controller.enqueue(1012, 144) && actual[1012] == 240 && applications == oldApplications, "enqueue applied during event dispatch");
        const auto result = controller.drain();
        require(result.size() == 1 && result[0].applied && actual[1012] == 144, "queued command did not apply on drain");
        require(controller.enqueue(1012, 400), "queue failed"); controller.clearPending(); controller.drain();
        require(actual[1012] == 144, "cancelled queued command ran");
        const auto snapshot = controller.snapshot(); require(snapshot.at(1012) == 144 && snapshot.at(1013) == 11, "snapshot differs from actual state");
        for(int condition=0;condition<4;++condition)require(controller.set(1052,condition).applied&&controller.snapshot().at(1052)==condition,"timing hint condition failed");
        require(!controller.set(1052,-1).applied&&!controller.set(1052,4).applied,"invalid timing hint condition accepted");
        std::cout << "Settings controller: units, validation, summary mapping, failures, persistence and deferred commands passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
