#include <windows.h>

#include <cctype>
#include <cstdint>
#include <charconv>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "steam.h"

namespace {

bool IsDecimal(std::string_view value) {
    if (value.empty()) return false;
    for (char ch : value) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) return false;
    }
    return true;
}

std::optional<uint32_t> ParseAppId(const std::string& value) {
    if (!IsDecimal(value)) return std::nullopt;

    unsigned long parsed{0};
    try {
        size_t consumed{0};
        parsed = std::stoul(value, &consumed, 10);
        if (consumed != value.size() || parsed > 0xFFFFFFFFul) return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }

    return static_cast<uint32_t>(parsed);
}

std::optional<uint32_t> ReadAppIdFromConsole() {
    std::cout << "AppID: ";

    std::string input;
    if (!std::getline(std::cin, input)) return std::nullopt;
    return ParseAppId(input);
}

std::optional<std::string> QueryRegistryString(HKEY root, const char* subKey, const char* valueName) {
    HKEY key{nullptr};
    if (RegOpenKeyExA(root, subKey, 0, KEY_READ | KEY_WOW64_32KEY, &key) != ERROR_SUCCESS) {
        return std::nullopt;
    }

    DWORD valueType{0};
    DWORD valueSize{0};
    LSTATUS status{RegQueryValueExA(key, valueName, nullptr, &valueType, nullptr, &valueSize)};
    if (status != ERROR_SUCCESS || valueType != REG_SZ || valueSize == 0) {
        RegCloseKey(key);
        return std::nullopt;
    }

    std::string value(valueSize, '\0');
    status = RegQueryValueExA(
        key,
        valueName,
        nullptr,
        nullptr,
        reinterpret_cast<LPBYTE>(value.data()),
        &valueSize);
    RegCloseKey(key);

    if (status != ERROR_SUCCESS) return std::nullopt;
    if (!value.empty() && value.back() == '\0') value.pop_back();
    return value;
}

std::optional<std::string> FindSteamInstallPath() {
    constexpr const char* kSteamKey{"Software\\Valve\\Steam"};

    if (auto path{QueryRegistryString(HKEY_CURRENT_USER, kSteamKey, "SteamPath")}) {
        std::cout << "Found SteamPath in HKEY_CURRENT_USER: " << *path << "\n";
        return path;
    }

    return std::nullopt;
}

std::string JoinPath(std::string base, const char* name) {
    for (char& ch : base) {
        if (ch == '/') ch = '\\';
    }
    if (!base.empty() && base.back() != '\\') base += '\\';
    base += name;
    return base;
}

std::string NormalizeDir(std::string dir) {
    for (char& ch : dir) {
        if (ch == '/') ch = '\\';
    }
    if (!dir.empty() && dir.back() == '\\') dir.pop_back();
    return dir;
}

HMODULE LoadSteamClient64(std::string& loadedPath) {
    auto steamPath{FindSteamInstallPath()};
    if (!steamPath) {
        std::cerr << "Failed to find Steam install path in registry.\n";
        return nullptr;
    }

    const std::string steamDir{NormalizeDir(*steamPath)};
    loadedPath = JoinPath(*steamPath, "steamclient64.dll");

    SetDllDirectoryA(steamDir.c_str());
    HMODULE module{LoadLibraryExA(loadedPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH)};
    if (!module) {
        std::cerr << "Failed to load " << loadedPath << " (GetLastError=" << GetLastError() << ").\n";
        return nullptr;
    }

    return module;
}

ISteamClient* CreateSteamClient(HMODULE module) {
    auto createInterface{reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module, "CreateInterface"))};
    if (!createInterface) {
        std::cerr << "steamclient64.dll has no CreateInterface export.\n";
        return nullptr;
    }

    int returnCode{0};
    auto* client{reinterpret_cast<ISteamClient*>(createInterface(kSteamClientInterfaceVersion, &returnCode))};
    if (!client) {
        std::cerr << "CreateInterface(" << kSteamClientInterfaceVersion
                  << ") failed (returnCode=" << returnCode << ").\n";
        return nullptr;
    }
    return client;
}

bool OpenSession(ISteamClient* client, HSteamPipe& pipe, HSteamUser& user) {
    pipe = client->CreateSteamPipe();
    if (!pipe) {
        std::cerr << "CreateSteamPipe failed. Is Steam running?\n";
        return false;
    }

    user = client->ConnectToGlobalUser(pipe);
    if (!user) {
        std::cerr << "ConnectToGlobalUser failed. Is a user logged in?\n";
        client->BReleaseSteamPipe(pipe);
        pipe = 0;
        return false;
    }

    return true;
}

std::optional<std::vector<uint8_t>> ExtractAppOwnershipTicket(
    ISteamClient* client, HSteamPipe pipe, HSteamUser user, uint32_t appId) {
    auto* appTicket{reinterpret_cast<ISteamAppTicket*>(
        client->GetISteamGenericInterface(user, pipe, kSteamAppTicketInterfaceVersion))};
    if (!appTicket) {
        std::cerr << "GetISteamGenericInterface(" << kSteamAppTicketInterfaceVersion
                  << ") returned null.\n";
        return std::nullopt;
    }

    std::vector<uint8_t> buffer(4096);
    uint32_t appIdOffset{0};
    uint32_t steamIdOffset{0};
    uint32_t signatureOffset{0};
    uint32_t signatureSize{0};
    const uint32_t written{appTicket->GetAppOwnershipTicketData(
        appId,
        buffer.data(),
        static_cast<uint32_t>(buffer.size()),
        &appIdOffset,
        &steamIdOffset,
        &signatureOffset,
        &signatureSize)};

    if (written == 0 || written > buffer.size()) {
        std::cerr << "GetAppOwnershipTicketData returned no ticket for AppID " << appId
                  << " (own the app and have it cached locally?).\n";
        return std::nullopt;
    }

    buffer.resize(written);
    std::cout << "Ownership ticket " << written << " bytes"
              << " (appIdOffset=" << appIdOffset
              << " steamIdOffset=" << steamIdOffset
              << " signatureOffset=" << signatureOffset
              << " signatureSize=" << signatureSize << ")\n";
    return buffer;
}

std::optional<std::vector<uint8_t>> ExtractEncryptedAppTicket(
    ISteamClient* client, HSteamPipe pipe, HSteamUser user, uint32_t appId) {
    auto* utils{client->GetISteamUtils(pipe, kSteamUtilsInterfaceVersion)};
    auto* steamUser{client->GetISteamUser(user, pipe, kSteamUserInterfaceVersion)};
    if (!utils || !steamUser) {
        std::cerr << "GetISteamUtils/GetISteamUser returned null.\n";
        return std::nullopt;
    }

    const SteamAPICall_t hCall{steamUser->RequestEncryptedAppTicket(nullptr, 0)};
    if (!hCall) {
        std::cerr << "RequestEncryptedAppTicket failed to start for AppID " << appId << ".\n";
        return std::nullopt;
    }

    constexpr int kMaxWaitMs{15000};
    constexpr int kStepMs{50};
    bool failed{false};
    int waited{0};
    while (!utils->IsAPICallCompleted(hCall, &failed)) {
        if (waited >= kMaxWaitMs) {
            std::cerr << "Timed out waiting for EncryptedAppTicketResponse_t.\n";
            return std::nullopt;
        }
        Sleep(kStepMs);
        waited += kStepMs;
    }

    EncryptedAppTicketResponse_t response{};
    const bool gotResult{utils->GetAPICallResult(
        hCall,
        &response,
        sizeof(response),
        EncryptedAppTicketResponse_t::k_iCallback,
        &failed)};
    if (!gotResult || failed) {
        std::cerr << "GetAPICallResult failed for EncryptedAppTicketResponse_t.\n";
        return std::nullopt;
    }
    if (response.m_eResult != k_EResultOK) {
        std::cerr << "RequestEncryptedAppTicket returned EResult "
                  << static_cast<int>(response.m_eResult) << ".\n";
        return std::nullopt;
    }

    std::vector<uint8_t> buffer(4096);
    uint32_t bytesWritten{0};
    if (!steamUser->GetEncryptedAppTicket(
            buffer.data(), static_cast<int>(buffer.size()), &bytesWritten)
        || bytesWritten == 0) {
        std::cerr << "GetEncryptedAppTicket returned false or zero bytes.\n";
        return std::nullopt;
    }

    buffer.resize(bytesWritten);
    std::cout << "Encrypted ticket " << bytesWritten << " bytes\n";
    return buffer;
}

void PrintHex(std::string_view label, const std::vector<uint8_t>& data) {
    static const char kHex[]{"0123456789abcdef"};
    std::cout << label << " (" << data.size() << " bytes):\n";

    for (size_t offset = 0; offset < data.size(); offset += 16) {
        std::string line;
        line.reserve(80);

        for (int shift = 28; shift >= 0; shift -= 4) {
            line += kHex[(offset >> shift) & 0xF];
        }
        line += "  ";

        std::string ascii;
        ascii.reserve(16);

        for (size_t col = 0; col < 16; ++col) {
            const size_t index = offset + col;
            if (index < data.size()) {
                const uint8_t byte = data[index];
                line += kHex[byte >> 4];
                line += kHex[byte & 0xF];
                line += ' ';
                ascii += (byte >= 0x20 && byte < 0x7F) ? static_cast<char>(byte) : '.';
            } else {
                line += "   ";
            }
            if (col == 7) line += ' ';
        }

        std::cout << line << " " << ascii << "\n";
    }
}

std::string ToHexString(const std::vector<uint8_t>& data) {
    static const char kHex[]{"0123456789abcdef"};
    std::string out;
    out.reserve(data.size() * 2);
    for (uint8_t byte : data) {
        out += kHex[byte >> 4];
        out += kHex[byte & 0xF];
    }
    return out;
}

std::vector<uint8_t> HexToBytes(std::string_view hex) {
    std::vector<uint8_t> bytes;
    if (hex.size() % 2 != 0) return bytes;
    bytes.reserve(hex.size() / 2);
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < hex.size(); i += 2) {
        int hi = hexVal(hex[i]);
        int lo = hexVal(hex[i + 1]);
        if (hi < 0 || lo < 0) return {};
        bytes.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return bytes;
}

bool WriteBinaryFile(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) {
        std::cerr << "Failed to create " << path << ".\n";
        return false;
    }
    output.write(reinterpret_cast<const char*>(data.data()),
                 static_cast<std::streamsize>(data.size()));
    if (!output) {
        std::cerr << "Failed to write " << path << ".\n";
        return false;
    }
    return true;
}

std::string TicketLine(const char* name, const std::optional<std::vector<uint8_t>>& ticket) {
    if (!ticket) return std::string{name} + ":\n";
    return std::string{name} + ":" + ToHexString(*ticket) + "\n";
}

bool WriteOutputs(uint32_t appId,
                  const std::optional<std::vector<uint8_t>>& ownership,
                  const std::optional<std::vector<uint8_t>>& encrypted) {
    const std::string dir{std::to_string(appId)};
    if (!CreateDirectoryA(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        std::cerr << "Failed to create directory " << dir
                  << " (GetLastError=" << GetLastError() << ").\n";
        return false;
    }

    const std::string text{
        "appid:" + std::to_string(appId) + "\n"
        + TicketLine("appticket", ownership)
        + TicketLine("eticket", encrypted)};

    const std::string textPath{JoinPath(dir, "tickets.txt")};
    std::ofstream summary{textPath, std::ios::trunc};
    if (!summary || !(summary << text)) {
        std::cerr << "Failed to write " << textPath << ".\n";
        return false;
    }

    std::cout << "Successfully saved to directory: " << dir << "\\\n";
    return true;
}

// ── JSON Helpers for IPC Communication ─────────────────────────
size_t FindValueStart(std::string_view body, std::string_view key) {
    std::string needle = "\"" + std::string(key) + "\"";
    size_t k = body.find(needle);
    if (k == std::string_view::npos) return std::string_view::npos;
    size_t colon = body.find(':', k + needle.size());
    if (colon == std::string_view::npos) return std::string_view::npos;
    size_t v = colon + 1;
    while (v < body.size() && isspace(static_cast<unsigned char>(body[v]))) {
        v++;
    }
    return v < body.size() ? v : std::string_view::npos;
}

bool JsonExtractString(std::string_view body, std::string_view key, std::string& out) {
    size_t v = FindValueStart(body, key);
    if (v == std::string_view::npos || body[v] != '"') return false;
    size_t q1 = v;
    size_t q2 = body.find('"', q1 + 1);
    if (q2 == std::string_view::npos) return false;
    out = std::string(body.substr(q1 + 1, q2 - q1 - 1));
    return true;
}

bool JsonExtractUInt64(std::string_view body, std::string_view key, uint64_t& out) {
    size_t v = FindValueStart(body, key);
    if (v == std::string_view::npos) return false;
    if (body[v] == '"') {
        size_t q2 = body.find('"', v + 1);
        if (q2 == std::string_view::npos) return false;
        std::string_view numStr = body.substr(v + 1, q2 - v - 1);
        auto [p, ec] = std::from_chars(numStr.data(), numStr.data() + numStr.size(), out);
        return (ec == std::errc{});
    }
    if (isdigit(static_cast<unsigned char>(body[v]))) {
        size_t end = v;
        while (end < body.size() && isdigit(static_cast<unsigned char>(body[end]))) {
            end++;
        }
        std::string_view numStr = body.substr(v, end - v);
        auto [p, ec] = std::from_chars(numStr.data(), numStr.data() + numStr.size(), out);
        return (ec == std::errc{});
    }
    return false;
}

bool TryExtractViaOpenSteamToolIpc(uint32_t appId) {
    constexpr const char* kPipeName = "\\\\.\\pipe\\OpenSteamTool_IPC";
    const std::string req = "{\"action\":\"extract_tickets\",\"appId\":" + std::to_string(appId) + "}";

    std::vector<char> respBuf(131072, 0);
    DWORD bytesRead = 0;

    bool connected = false;
    for (int retry = 0; retry < 10; ++retry) {
        if (CallNamedPipeA(kPipeName, const_cast<char*>(req.data()), static_cast<DWORD>(req.size()),
                            respBuf.data(), static_cast<DWORD>(respBuf.size()) - 1, &bytesRead, 5000)) {
            connected = true;
            break;
        }
        if (GetLastError() == ERROR_FILE_NOT_FOUND) {
            Sleep(500);
            continue;
        }
        break;
    }

    if (!connected) {
        return false;
    }

    std::string resp(respBuf.data(), bytesRead);
    std::cout << "[IPC] Connected to OpenSteamTool_IPC (steamcmd/steam host process)\n";

    uint64_t code = 0;
    if (!JsonExtractUInt64(resp, "code", code) || code != 200) {
        std::string msg = "Unknown error";
        JsonExtractString(resp, "message", msg);
        std::cerr << "[IPC] Request failed: " << msg << "\n";
        return true;
    }

    std::string ownershipHex, encryptedHex, debugInfo;
    JsonExtractString(resp, "ownership", ownershipHex);
    JsonExtractString(resp, "encrypted", encryptedHex);
    JsonExtractString(resp, "debug", debugInfo);

    std::optional<std::vector<uint8_t>> ownership;
    std::optional<std::vector<uint8_t>> encrypted;

    if (!ownershipHex.empty()) {
        auto b = HexToBytes(ownershipHex);
        if (!b.empty()) {
            ownership = std::move(b);
            PrintHex("Ownership ticket", *ownership);
        }
    } else {
        std::cout << "[IPC] Ownership ticket not available yet (" << debugInfo << ")\n";
    }

    if (!encryptedHex.empty()) {
        auto b = HexToBytes(encryptedHex);
        if (!b.empty()) {
            encrypted = std::move(b);
            PrintHex("Encrypted ticket", *encrypted);
        }
    } else {
        std::cout << "[IPC] Encrypted ticket not obtained (" << debugInfo << ")\n";
    }

    WriteOutputs(appId, ownership, encrypted);
    return true;
}

void WaitForExit(bool wait) {
    if (!wait) return;
    std::cout << "\nPress Enter to exit...";
    std::string dummy;
    std::getline(std::cin, dummy);
}

#if defined(_WIN64)
int Run(int argc, char** argv) {
    std::optional<uint32_t> appId;
    if (argc >= 2) {
        appId = ParseAppId(argv[1]);
        if (!appId) {
            std::cerr << "Invalid AppID: " << argv[1] << "\n";
            return 1;
        }
    } else {
        appId = ReadAppIdFromConsole();
        if (!appId) {
            std::cerr << "Invalid AppID.\n";
            return 1;
        }
    }

    // 1. Try extracting via OpenSteamTool_IPC (supports steamcmd and hooked Steam)
    if (TryExtractViaOpenSteamToolIpc(*appId)) {
        return 0;
    }

    std::cout << "OpenSteamTool_IPC not found. Falling back to native Steam client connection...\n";

    // 2. Fallback to native Steam client connection
    const std::string appIdStr{std::to_string(*appId)};
    SetEnvironmentVariableA("SteamAppId", appIdStr.c_str());
    SetEnvironmentVariableA("SteamGameId", appIdStr.c_str());

    std::string steamClientPath;
    HMODULE steamClient{LoadSteamClient64(steamClientPath)};
    if (!steamClient) return 1;

    std::cout << "Loaded " << steamClientPath << "\n";

    ISteamClient* client{CreateSteamClient(steamClient)};
    if (!client) {
        FreeLibrary(steamClient);
        return 1;
    }

    HSteamPipe pipe{0};
    HSteamUser user{0};
    if (!OpenSession(client, pipe, user)) {
        FreeLibrary(steamClient);
        return 1;
    }

    if (auto* utils{client->GetISteamUtils(pipe, kSteamUtilsInterfaceVersion)}) {
        std::cout << "ConnectedUniverse=" << static_cast<int>(utils->GetConnectedUniverse())
                  << " ClientAppID=" << utils->GetAppID() << "\n";
    }

    auto ownership{ExtractAppOwnershipTicket(client, pipe, user, *appId)};
    if (ownership) PrintHex("Ownership ticket", *ownership);

    auto encrypted{ExtractEncryptedAppTicket(client, pipe, user, *appId)};
    if (encrypted) PrintHex("Encrypted ticket", *encrypted);

    const bool ok{WriteOutputs(*appId, ownership, encrypted)};

    client->BReleaseSteamPipe(pipe);
    FreeLibrary(steamClient);
    return ok ? 0 : 1;
}
#endif

} // namespace

int main(int argc, char** argv) {
#if !defined(_WIN64)
    std::cerr << "extract_tickets must be built as a 64-bit Windows executable.\n";
    WaitForExit(argc < 2);
    return 1;
#else
    const int rc{Run(argc, argv)};
    WaitForExit(argc < 2);
    return rc;
#endif
}
