#include "SoulMod/YYToolkitAdapter.hpp"

#include <iostream>

namespace {

int failures = 0;

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void Test_binding_requires_an_initialized_aurie_dispatcher() {
    SoulMod::YYToolkitAdapter& adapter = SoulMod::YYToolkitAdapter::Instance();
    adapter.Shutdown();

    const bool initialized = adapter.Initialize();

    Require(!initialized, "plain CTest process must not initialize without Aurie dispatcher");
    Require(!adapter.IsYYToolkitBound(), "failed initialization must not claim YYToolkit binding");
    Require(adapter.GetBindingStatus() == SoulMod::YYToolkitBindingStatus::AurieDispatcherUnavailable,
        "failed initialization must expose Aurie dispatcher unavailability");
}

}

int main() {
    Test_binding_requires_an_initialized_aurie_dispatcher();
    if (failures != 0) {
        std::cerr << "RESULT: FAIL (" << failures << " assertion(s) failed)\n";
        return 1;
    }
    std::cout << "RESULT: PASS (YYToolkit binding precondition)\n";
    return 0;
}
