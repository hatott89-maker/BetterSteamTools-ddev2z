// version.dll Dynamic Proxy Loader for OpenSteamTool / BetterSteamTools
#include <windows.h>
#include <string>

static HMODULE g_hRealVersion = nullptr;

typedef BOOL  (WINAPI *GetFileVersionInfoA_t)(LPCSTR, DWORD, DWORD, LPVOID);
typedef BOOL  (WINAPI *GetFileVersionInfoByHandle_t)(int, HANDLE, LPVOID, DWORD*);
typedef BOOL  (WINAPI *GetFileVersionInfoExA_t)(DWORD, LPCSTR, DWORD, DWORD, LPVOID);
typedef BOOL  (WINAPI *GetFileVersionInfoExW_t)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
typedef DWORD (WINAPI *GetFileVersionInfoSizeA_t)(LPCSTR, LPDWORD);
typedef DWORD (WINAPI *GetFileVersionInfoSizeExA_t)(DWORD, LPCSTR, LPDWORD);
typedef DWORD (WINAPI *GetFileVersionInfoSizeExW_t)(DWORD, LPCWSTR, LPDWORD);
typedef DWORD (WINAPI *GetFileVersionInfoSizeW_t)(LPCWSTR, LPDWORD);
typedef BOOL  (WINAPI *GetFileVersionInfoW_t)(LPCWSTR, DWORD, DWORD, LPVOID);
typedef DWORD (WINAPI *VerFindFileA_t)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT, LPSTR, PUINT);
typedef DWORD (WINAPI *VerFindFileW_t)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT, LPWSTR, PUINT);
typedef DWORD (WINAPI *VerInstallFileA_t)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT);
typedef DWORD (WINAPI *VerInstallFileW_t)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT);
typedef DWORD (WINAPI *VerLanguageNameA_t)(DWORD, LPSTR, DWORD);
typedef DWORD (WINAPI *VerLanguageNameW_t)(DWORD, LPWSTR, DWORD);
typedef BOOL  (WINAPI *VerQueryValueA_t)(LPCVOID, LPCSTR, LPVOID*, PUINT);
typedef BOOL  (WINAPI *VerQueryValueW_t)(LPCVOID, LPCWSTR, LPVOID*, PUINT);

static GetFileVersionInfoA_t         o_GetFileVersionInfoA = nullptr;
static GetFileVersionInfoByHandle_t   o_GetFileVersionInfoByHandle = nullptr;
static GetFileVersionInfoExA_t       o_GetFileVersionInfoExA = nullptr;
static GetFileVersionInfoExW_t       o_GetFileVersionInfoExW = nullptr;
static GetFileVersionInfoSizeA_t     o_GetFileVersionInfoSizeA = nullptr;
static GetFileVersionInfoSizeExA_t   o_GetFileVersionInfoSizeExA = nullptr;
static GetFileVersionInfoSizeExW_t   o_GetFileVersionInfoSizeExW = nullptr;
static GetFileVersionInfoSizeW_t     o_GetFileVersionInfoSizeW = nullptr;
static GetFileVersionInfoW_t         o_GetFileVersionInfoW = nullptr;
static VerFindFileA_t                o_VerFindFileA = nullptr;
static VerFindFileW_t                o_VerFindFileW = nullptr;
static VerInstallFileA_t             o_VerInstallFileA = nullptr;
static VerInstallFileW_t             o_VerInstallFileW = nullptr;
static VerLanguageNameA_t            o_VerLanguageNameA = nullptr;
static VerLanguageNameW_t            o_VerLanguageNameW = nullptr;
static VerQueryValueA_t              o_VerQueryValueA = nullptr;
static VerQueryValueW_t              o_VerQueryValueW = nullptr;

static void LoadRealVersion() {
    if (g_hRealVersion) return;

    char sysDir[MAX_PATH];
    if (!GetSystemDirectoryA(sysDir, MAX_PATH)) return;
    std::string realPath = std::string(sysDir) + "\\version.dll";

    g_hRealVersion = LoadLibraryA(realPath.c_str());
    if (g_hRealVersion) {
        o_GetFileVersionInfoA        = (GetFileVersionInfoA_t)GetProcAddress(g_hRealVersion, "GetFileVersionInfoA");
        o_GetFileVersionInfoByHandle = (GetFileVersionInfoByHandle_t)GetProcAddress(g_hRealVersion, "GetFileVersionInfoByHandle");
        o_GetFileVersionInfoExA      = (GetFileVersionInfoExA_t)GetProcAddress(g_hRealVersion, "GetFileVersionInfoExA");
        o_GetFileVersionInfoExW      = (GetFileVersionInfoExW_t)GetProcAddress(g_hRealVersion, "GetFileVersionInfoExW");
        o_GetFileVersionInfoSizeA    = (GetFileVersionInfoSizeA_t)GetProcAddress(g_hRealVersion, "GetFileVersionInfoSizeA");
        o_GetFileVersionInfoSizeExA  = (GetFileVersionInfoSizeExA_t)GetProcAddress(g_hRealVersion, "GetFileVersionInfoSizeExA");
        o_GetFileVersionInfoSizeExW  = (GetFileVersionInfoSizeExW_t)GetProcAddress(g_hRealVersion, "GetFileVersionInfoSizeExW");
        o_GetFileVersionInfoSizeW    = (GetFileVersionInfoSizeW_t)GetProcAddress(g_hRealVersion, "GetFileVersionInfoSizeW");
        o_GetFileVersionInfoW        = (GetFileVersionInfoW_t)GetProcAddress(g_hRealVersion, "GetFileVersionInfoW");
        o_VerFindFileA               = (VerFindFileA_t)GetProcAddress(g_hRealVersion, "VerFindFileA");
        o_VerFindFileW               = (VerFindFileW_t)GetProcAddress(g_hRealVersion, "VerFindFileW");
        o_VerInstallFileA            = (VerInstallFileA_t)GetProcAddress(g_hRealVersion, "VerInstallFileA");
        o_VerInstallFileW            = (VerInstallFileW_t)GetProcAddress(g_hRealVersion, "VerInstallFileW");
        o_VerLanguageNameA           = (VerLanguageNameA_t)GetProcAddress(g_hRealVersion, "VerLanguageNameA");
        o_VerLanguageNameW           = (VerLanguageNameW_t)GetProcAddress(g_hRealVersion, "VerLanguageNameW");
        o_VerQueryValueA             = (VerQueryValueA_t)GetProcAddress(g_hRealVersion, "VerQueryValueA");
        o_VerQueryValueW             = (VerQueryValueW_t)GetProcAddress(g_hRealVersion, "VerQueryValueW");
    }
}

static BOOL OpenSteamToolLoad() {
    char exePath[MAX_PATH];
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH)) {
        const char* exeName = strrchr(exePath, '\\');
        exeName = exeName ? exeName + 1 : exePath;
        if (_stricmp(exeName, "steamcmd.exe") != 0 && _stricmp(exeName, "steam.exe") != 0) {
            return TRUE;
        }
    }
    return LoadLibraryA("OpenSteamTool.dll") != NULL;
}

extern "C" {
    BOOL WINAPI GetFileVersionInfoA(LPCSTR pt, DWORD dw, DWORD len, LPVOID pb) {
        if (!o_GetFileVersionInfoA) LoadRealVersion();
        return o_GetFileVersionInfoA ? o_GetFileVersionInfoA(pt, dw, len, pb) : FALSE;
    }
    BOOL WINAPI GetFileVersionInfoByHandle(int h, HANDLE hf, LPVOID b, DWORD* l) {
        if (!o_GetFileVersionInfoByHandle) LoadRealVersion();
        return o_GetFileVersionInfoByHandle ? o_GetFileVersionInfoByHandle(h, hf, b, l) : FALSE;
    }
    BOOL WINAPI GetFileVersionInfoExA(DWORD fl, LPCSTR pt, DWORD dw, DWORD len, LPVOID pb) {
        if (!o_GetFileVersionInfoExA) LoadRealVersion();
        return o_GetFileVersionInfoExA ? o_GetFileVersionInfoExA(fl, pt, dw, len, pb) : FALSE;
    }
    BOOL WINAPI GetFileVersionInfoExW(DWORD fl, LPCWSTR pt, DWORD dw, DWORD len, LPVOID pb) {
        if (!o_GetFileVersionInfoExW) LoadRealVersion();
        return o_GetFileVersionInfoExW ? o_GetFileVersionInfoExW(fl, pt, dw, len, pb) : FALSE;
    }
    DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR pt, LPDWORD ph) {
        if (!o_GetFileVersionInfoSizeA) LoadRealVersion();
        return o_GetFileVersionInfoSizeA ? o_GetFileVersionInfoSizeA(pt, ph) : 0;
    }
    DWORD WINAPI GetFileVersionInfoSizeExA(DWORD fl, LPCSTR pt, LPDWORD ph) {
        if (!o_GetFileVersionInfoSizeExA) LoadRealVersion();
        return o_GetFileVersionInfoSizeExA ? o_GetFileVersionInfoSizeExA(fl, pt, ph) : 0;
    }
    DWORD WINAPI GetFileVersionInfoSizeExW(DWORD fl, LPCWSTR pt, LPDWORD ph) {
        if (!o_GetFileVersionInfoSizeExW) LoadRealVersion();
        return o_GetFileVersionInfoSizeExW ? o_GetFileVersionInfoSizeExW(fl, pt, ph) : 0;
    }
    DWORD WINAPI GetFileVersionInfoSizeW(LPCWSTR pt, LPDWORD ph) {
        if (!o_GetFileVersionInfoSizeW) LoadRealVersion();
        return o_GetFileVersionInfoSizeW ? o_GetFileVersionInfoSizeW(pt, ph) : 0;
    }
    BOOL WINAPI GetFileVersionInfoW(LPCWSTR pt, DWORD dw, DWORD len, LPVOID pb) {
        if (!o_GetFileVersionInfoW) LoadRealVersion();
        return o_GetFileVersionInfoW ? o_GetFileVersionInfoW(pt, dw, len, pb) : FALSE;
    }
    DWORD WINAPI VerFindFileA(DWORD fl, LPCSTR fn, LPCSTR ad, LPCSTR cd, LPSTR sd, PUINT sdl, LPSTR cd2, PUINT cdl) {
        if (!o_VerFindFileA) LoadRealVersion();
        return o_VerFindFileA ? o_VerFindFileA(fl, fn, ad, cd, sd, sdl, cd2, cdl) : 0;
    }
    DWORD WINAPI VerFindFileW(DWORD fl, LPCWSTR fn, LPCWSTR ad, LPCWSTR cd, LPWSTR sd, PUINT sdl, LPWSTR cd2, PUINT cdl) {
        if (!o_VerFindFileW) LoadRealVersion();
        return o_VerFindFileW ? o_VerFindFileW(fl, fn, ad, cd, sd, sdl, cd2, cdl) : 0;
    }
    DWORD WINAPI VerInstallFileA(DWORD fl, LPCSTR sf, LPCSTR df, LPCSTR sd, LPCSTR dd, LPCSTR cd, LPSTR tm, PUINT tml) {
        if (!o_VerInstallFileA) LoadRealVersion();
        return o_VerInstallFileA ? o_VerInstallFileA(fl, sf, df, sd, dd, cd, tm, tml) : 0;
    }
    DWORD WINAPI VerInstallFileW(DWORD fl, LPCWSTR sf, LPCWSTR df, LPCWSTR sd, LPCWSTR dd, LPCWSTR cd, LPWSTR tm, PUINT tml) {
        if (!o_VerInstallFileW) LoadRealVersion();
        return o_VerInstallFileW ? o_VerInstallFileW(fl, sf, df, sd, dd, cd, tm, tml) : 0;
    }
    DWORD WINAPI VerLanguageNameA(DWORD lid, LPSTR sz, DWORD cch) {
        if (!o_VerLanguageNameA) LoadRealVersion();
        return o_VerLanguageNameA ? o_VerLanguageNameA(lid, sz, cch) : 0;
    }
    DWORD WINAPI VerLanguageNameW(DWORD lid, LPWSTR sz, DWORD cch) {
        if (!o_VerLanguageNameW) LoadRealVersion();
        return o_VerLanguageNameW ? o_VerLanguageNameW(lid, sz, cch) : 0;
    }
    BOOL WINAPI VerQueryValueA(LPCVOID pb, LPCSTR sb, LPVOID* buf, PUINT len) {
        if (!o_VerQueryValueA) LoadRealVersion();
        return o_VerQueryValueA ? o_VerQueryValueA(pb, sb, buf, len) : FALSE;
    }
    BOOL WINAPI VerQueryValueW(LPCVOID pb, LPCWSTR sb, LPVOID* buf, PUINT len) {
        if (!o_VerQueryValueW) LoadRealVersion();
        return o_VerQueryValueW ? o_VerQueryValueW(pb, sb, buf, len) : FALSE;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, PVOID pvReserved) {
    if (dwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        LoadRealVersion();
        if (!OpenSteamToolLoad())
            return FALSE;
    }
    return TRUE;
}
