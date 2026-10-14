#include "../setup/startup.h"
#include <cassert>

int main() {
    using namespace fcitx::setup;
    const auto command = loginStartupCommand(L"C:\\Program Files\\Fcitx5");
    assert(command ==
           L"\"C:\\Program Files\\Fcitx5\\bin\\Fcitx5.exe\" --login-start");
    assert(loginStartupCommand(L"relative").empty());
    assert(loginStartupCommand(L"C:\\bad\"path").empty());
    auto change = planStartupChange(command, L"", true);
    assert(!change.error && change.write && !change.remove);
    change = planStartupChange(command, command, true);
    assert(!change.error && !change.write && !change.remove);
    change = planStartupChange(command, command, false);
    assert(!change.error && !change.write && change.remove);
    change = planStartupChange(command, L"foreign executable", true);
    assert(change.error == ERROR_ALREADY_EXISTS && !change.write &&
           !change.remove);
    change = planStartupChange(command, L"foreign executable", false);
    assert(!change.error && !change.write && !change.remove);
}
