#include "TicketExtractor.h"
#include "dllmain.h"
#include "Hook/Hooks_CallBack.h"
#include "Hook/Hooks_Misc.h"
#include "Utils/Logging/Log.h"

#include <windows.h>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>

namespace {

typedef int32 HSteamPipe;
typedef int32 HSteamUser;
typedef uint32 AppId_t;
typedef uint64 SteamAPICall_t;

class ISteamUtils {
public:
    virtual uint32 GetSecondsSinceAppActive() = 0;
    virtual uint32 GetSecondsSinceComputerActive() = 0;
    virtual int GetConnectedUniverse() = 0;
    virtual uint32 GetServerRealTime() = 0;
    virtual const char* GetIPCountry() = 0;
    virtual bool GetImageSize(int iImage, uint32* pnWidth, uint32* pnHeight) = 0;
    virtual bool GetImageRGBA(int iImage, uint8* pubDest, int nDestBufferSize) = 0;
    virtual bool GetCSERIPPort(uint32* unIP, uint16* usPort) = 0;
    virtual uint8 GetCurrentBatteryPower() = 0;
    virtual uint32 GetAppID() = 0;
    virtual void SetOverlayNotificationPosition(int eNotificationPosition) = 0;
    virtual bool IsAPICallCompleted(SteamAPICall_t hSteamAPICall, bool* pbFailed) = 0;
    virtual int GetAPICallFailureReason(SteamAPICall_t hSteamAPICall) = 0;
    virtual bool GetAPICallResult(SteamAPICall_t hSteamAPICall, void* pCallback, int cubCallback, int iCallbackExpected, bool* pbFailed) = 0;
};

class ISteamUser {
public:
    virtual HSteamUser GetHSteamUser() = 0;
    virtual bool BLoggedOn() = 0;
    virtual uint64 GetSteamID() = 0;
    virtual int InitiateGameConnection_DEPRECATED(void*, int, uint64, uint32, uint16, bool) = 0;
    virtual void TerminateGameConnection_DEPRECATED(uint32, uint16) = 0;
    virtual void TrackAppUsageEvent(uint64, int, const char*) = 0;
    virtual bool GetUserDataFolder(char*, int) = 0;
    virtual void StartVoiceRecording() = 0;
    virtual void StopVoiceRecording() = 0;
    virtual int GetAvailableVoice(uint32*, uint32*, uint32) = 0;
    virtual int GetVoice(bool, void*, uint32, uint32*, bool, void*, uint32, uint32*, uint32) = 0;
    virtual int DecompressVoice(const void*, uint32, void*, uint32, uint32*, uint32) = 0;
    virtual uint32 GetVoiceOptimalSampleRate() = 0;
    virtual uint32 GetAuthSessionTicket(void*, int, uint32*, const void*) = 0;
    virtual uint32 GetAuthTicketForWebApi(const char*) = 0;
    virtual int BeginAuthSession(const void*, int, uint64) = 0;
    virtual void EndAuthSession(uint64) = 0;
    virtual void CancelAuthTicket(uint32) = 0;
    virtual int UserHasLicenseForApp(uint64, AppId_t) = 0;
    virtual bool BIsBehindNAT() = 0;
    virtual void AdvertiseGame(uint64, uint32, uint16) = 0;
    virtual SteamAPICall_t RequestEncryptedAppTicket(void* pDataToInclude, int cbDataToInclude) = 0;
    virtual bool GetEncryptedAppTicket(void* pTicket, int cbMaxTicket, uint32* pcbTicket) = 0;
};

class ISteamAppTicket {
public:
    virtual uint32 GetAppOwnershipTicketData(
        uint32 nAppID,
        void* pvBuffer,
        uint32 cubBuffer,
        uint32* piAppId,
        uint32* piSteamId,
        uint32* piSignature,
        uint32* pcbSignature) = 0;
};

class ISteamClient {
public:
    virtual HSteamPipe CreateSteamPipe() = 0;
    virtual bool BReleaseSteamPipe(HSteamPipe hSteamPipe) = 0;
    virtual HSteamUser ConnectToGlobalUser(HSteamPipe hSteamPipe) = 0;
    virtual HSteamUser CreateLocalUser(HSteamPipe* phSteamPipe, int eAccountType) = 0;
    virtual void ReleaseUser(HSteamPipe hSteamPipe, HSteamUser hUser) = 0;
    virtual ISteamUser* GetISteamUser(HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char* pchVersion) = 0;
    virtual void* GetISteamGameServer(HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char* pchVersion) = 0;
    virtual void SetLocalIPBinding(const void* unIP, uint16 usPort) = 0;
    virtual void* GetISteamFriends(HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char* pchVersion) = 0;
    virtual ISteamUtils* GetISteamUtils(HSteamPipe hSteamPipe, const char* pchVersion) = 0;
    virtual void* GetISteamMatchmaking(HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char* pchVersion) = 0;
    virtual void* GetISteamMatchmakingServers(HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char* pchVersion) = 0;
    virtual void* GetISteamGenericInterface(HSteamUser hSteamUser, HSteamPipe hSteamPipe, const char* pchVersion) = 0;
};

typedef void* (*CreateInterfaceFn)(const char* pName, int* pReturnCode);
typedef bool (*Steam_BLoggedOnFn)(HSteamPipe hSteamPipe, HSteamUser hUser);

std::string ToHex(const uint8* data, size_t len) {
    static const char hexChars[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        hex.push_back(hexChars[(data[i] >> 4) & 0x0F]);
        hex.push_back(hexChars[data[i] & 0x0F]);
    }
    return hex;
}

} // namespace

namespace TicketExtractor {

    TicketResult ExtractTickets(uint32_t appId) {
        TicketResult result{};

        if (!client_hModule) {
            result.errorMessage = "steamclient64.dll module handle is null";
            LOG_ERROR("TicketExtractor: {}", result.errorMessage);
            return result;
        }

        auto createInterface = reinterpret_cast<CreateInterfaceFn>(
            OSTPlatform::DynamicLibrary::GetSymbol(client_hModule, "CreateInterface"));
        if (!createInterface) {
            result.errorMessage = "steamclient64.dll does not export CreateInterface";
            LOG_ERROR("TicketExtractor: {}", result.errorMessage);
            return result;
        }

        auto steamBLoggedOn = reinterpret_cast<Steam_BLoggedOnFn>(
            OSTPlatform::DynamicLibrary::GetSymbol(client_hModule, "Steam_BLoggedOn"));

        ISteamClient* client = nullptr;
        const char* clientVersions[] = { "SteamClient023", "SteamClient021", "SteamClient020", "SteamClient017" };
        for (const char* ver : clientVersions) {
            int retCode = 0;
            client = reinterpret_cast<ISteamClient*>(createInterface(ver, &retCode));
            if (client) break;
        }

        if (!client) {
            result.errorMessage = "Failed to obtain ISteamClient interface";
            LOG_ERROR("TicketExtractor: {}", result.errorMessage);
            return result;
        }

        HSteamPipe pipe = Hooks_CallBack::GetActivePipe();
        HSteamUser user = Hooks_CallBack::GetActiveUser();

        // 1. Scan for active logged-on local/global sessions (critical for steamcmd)
        if (!pipe || !user) {
            LOG_INFO("TicketExtractor: Scanning for logged-on session in current process...");
            for (HSteamPipe p = 1; p <= 16; ++p) {
                for (HSteamUser u = 1; u <= 16; ++u) {
                    bool isLogged = false;
                    if (steamBLoggedOn && steamBLoggedOn(p, u)) {
                        isLogged = true;
                    } else {
                        ISteamUser* su = client->GetISteamUser(u, p, "SteamUser023");
                        if (!su) su = client->GetISteamUser(u, p, "SteamUser021");
                        if (su && su->BLoggedOn()) {
                            isLogged = true;
                        }
                    }

                    if (isLogged) {
                        pipe = p;
                        user = u;
                        LOG_INFO("TicketExtractor: Auto-detected active session: pipe={}, user={}", pipe, user);
                        break;
                    }
                }
                if (pipe && user) break;
            }
        }

        // 2. Fallback: try connecting to Global User (for Desktop Steam)
        if (!pipe || !user) {
            pipe = client->CreateSteamPipe();
            if (pipe) {
                user = client->ConnectToGlobalUser(pipe);
                if (user) {
                    LOG_INFO("TicketExtractor: Connected to global user via Desktop Steam: pipe={}, user={}", pipe, user);
                }
            }
        }

        // 3. Fallback default guess for steamcmd if login was completed but BLoggedOn is hesitant
        if (!pipe || !user) {
            // In typical steamcmd, pipe 1 and user 1 are always the primary local session
            ISteamUser* testUser = client->GetISteamUser(1, 1, "SteamUser023");
            if (!testUser) testUser = client->GetISteamUser(1, 1, "SteamUser021");
            if (testUser && testUser->GetSteamID() != 0) {
                pipe = 1;
                user = 1;
                LOG_INFO("TicketExtractor: Detected valid SteamID 0x{:016X} at pipe=1, user=1", testUser->GetSteamID());
            }
        }

        if (!pipe || !user) {
            result.errorMessage = "Steam user session not active. Ensure steamcmd/steam is logged in.";
            LOG_WARN("TicketExtractor: Failed to locate any active user session");
            return result;
        }

        LOG_INFO("TicketExtractor: Extracting tickets for appId={} using pipe={}, user={}", appId, pipe, user);

        // Dynamically override AppId across steamclient
        Hooks_Misc::SetOverrideAppId(appId);

        typedef void (*Breakpad_SteamSetAppIDFn)(uint32_t);
        auto setAppId = reinterpret_cast<Breakpad_SteamSetAppIDFn>(
            OSTPlatform::DynamicLibrary::GetSymbol(client_hModule, "Breakpad_SteamSetAppID"));
        if (setAppId) {
            setAppId(appId);
        }

        const std::string appIdStr = std::to_string(appId);
        SetEnvironmentVariableA("SteamAppId", appIdStr.c_str());
        SetEnvironmentVariableA("SteamGameId", appIdStr.c_str());

        struct CleanupScope {
            Breakpad_SteamSetAppIDFn fn;
            ~CleanupScope() {
                Hooks_Misc::SetOverrideAppId(0);
                if (fn) fn(0);
            }
        } scope{setAppId};

        std::string debugDetails;

        // 1. Ownership Ticket
        auto* appTicket = reinterpret_cast<ISteamAppTicket*>(
            client->GetISteamGenericInterface(user, pipe, "STEAMAPPTICKET_INTERFACE_VERSION001"));
        if (appTicket) {
            std::vector<uint8_t> buffer(4096);
            uint32_t appIdOffset = 0, steamIdOffset = 0, signatureOffset = 0, signatureSize = 0;
            uint32_t written = appTicket->GetAppOwnershipTicketData(
                appId, buffer.data(), static_cast<uint32_t>(buffer.size()),
                &appIdOffset, &steamIdOffset, &signatureOffset, &signatureSize);

            debugDetails += std::format("appTicket written={} bytes; ", written);

            if (written > 0 && written <= buffer.size()) {
                result.ownershipHex = ToHex(buffer.data(), written);
                LOG_INFO("TicketExtractor: Successfully extracted ownership ticket for appId {} ({} bytes)", appId, written);
            } else {
                LOG_WARN("TicketExtractor: GetAppOwnershipTicketData returned {} bytes for appId {}", written, appId);
            }
        } else {
            debugDetails += "appTicket interface is null; ";
            LOG_WARN("TicketExtractor: ISteamAppTicket interface returned null");
        }

        // 2. Encrypted App Ticket
        ISteamUtils* utils = client->GetISteamUtils(pipe, "SteamUtils010");
        if (!utils) utils = client->GetISteamUtils(pipe, "SteamUtils011");
        if (!utils) utils = client->GetISteamUtils(pipe, "SteamUtils009");
        ISteamUser* steamUser = client->GetISteamUser(user, pipe, "SteamUser023");
        if (!steamUser) steamUser = client->GetISteamUser(user, pipe, "SteamUser021");

        if (utils && steamUser) {
            SteamAPICall_t hCall = steamUser->RequestEncryptedAppTicket(nullptr, 0);
            debugDetails += std::format("hCall=0x{:X}; ", hCall);

            if (hCall) {
                constexpr int kMaxWaitMs = 15000;
                constexpr int kStepMs = 50;
                bool failed = false;
                int waited = 0;
                while (!utils->IsAPICallCompleted(hCall, &failed)) {
                    if (waited >= kMaxWaitMs) break;
                    Sleep(kStepMs);
                    waited += kStepMs;
                }

                debugDetails += std::format("waited={}ms failed={}; ", waited, failed);

                if (waited < kMaxWaitMs && !failed) {
                    EncryptedAppTicketResponse_t resp{};
                    bool gotResult = utils->GetAPICallResult(
                        hCall, &resp, sizeof(resp),
                        EncryptedAppTicketResponse_t::k_iCallback, &failed);

                    debugDetails += std::format("gotResult={} EResult={}; ", gotResult, static_cast<int>(resp.m_eResult));

                    if (gotResult && !failed && resp.m_eResult == k_EResultOK) {
                        std::vector<uint8_t> encBuf(4096);
                        uint32_t encWritten = 0;
                        if (steamUser->GetEncryptedAppTicket(encBuf.data(), static_cast<int>(encBuf.size()), &encWritten) && encWritten > 0) {
                            result.encryptedHex = ToHex(encBuf.data(), encWritten);
                            LOG_INFO("TicketExtractor: Successfully extracted encrypted ticket for appId {} ({} bytes)", appId, encWritten);
                        } else {
                            debugDetails += "GetEncryptedAppTicket returned 0 bytes; ";
                        }
                    } else {
                        LOG_WARN("TicketExtractor: RequestEncryptedAppTicket completed with result code {}", static_cast<int>(resp.m_eResult));
                    }
                } else {
                    int failReason = utils->GetAPICallFailureReason(hCall);
                    debugDetails += std::format("failReason={}; ", failReason);
                    LOG_WARN("TicketExtractor: Timed out waiting for encrypted ticket for appId {}", appId);
                }
            } else {
                LOG_WARN("TicketExtractor: RequestEncryptedAppTicket returned null call handle");
            }
        } else {
            debugDetails += std::format("utils={} steamUser={}; ", (void*)utils, (void*)steamUser);
        }

        result.debugDetails = debugDetails;

        if (result.ownershipHex.empty() && result.encryptedHex.empty()) {
            result.errorMessage = std::format("Neither ticket obtained ({})", debugDetails);
            return result;
        }

        result.success = true;
        return result;
    }

} // namespace TicketExtractor
