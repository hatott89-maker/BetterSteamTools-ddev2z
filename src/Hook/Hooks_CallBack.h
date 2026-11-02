#pragma once
#include "Steam/Types.h"

namespace Hooks_CallBack {
    // CallBack hook: handles various callback events.
    void Install();
    void Uninstall();

    HSteamPipe GetActivePipe();
    HSteamUser GetActiveUser();
}
