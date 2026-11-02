#include "Hooks_CallBack.h"
#include "HookMacros.h"
#include "dllmain.h"
#include <atomic>

namespace {
    static std::atomic<HSteamPipe> g_activePipe{0};
    static std::atomic<HSteamUser> g_activeUser{0};

    HOOK_FUNC(SendCallbackToPipe, bool, void* pSteamEngine, HSteamPipe hSteamPipe,
            HSteamUser iClientUser, int iCallback, void* pCallbackData, int cubCallbackData) {
        if (hSteamPipe != 0) g_activePipe.store(hSteamPipe, std::memory_order_relaxed);
        if (iClientUser != 0) g_activeUser.store(iClientUser, std::memory_order_relaxed);

        return oSendCallbackToPipe(pSteamEngine, hSteamPipe, iClientUser,
                                    iCallback, pCallbackData, cubCallbackData);
    }
}

namespace Hooks_CallBack {
    void Install() {
        HOOK_BEGIN();
        INSTALL_HOOK_C(SendCallbackToPipe);
        HOOK_END();
    }

    void Uninstall() {
        UNHOOK_BEGIN();
        UNINSTALL_HOOK_C(SendCallbackToPipe);
        UNHOOK_END();
    }

    HSteamPipe GetActivePipe() {
        return g_activePipe.load(std::memory_order_relaxed);
    }

    HSteamUser GetActiveUser() {
        return g_activeUser.load(std::memory_order_relaxed);
    }
}
