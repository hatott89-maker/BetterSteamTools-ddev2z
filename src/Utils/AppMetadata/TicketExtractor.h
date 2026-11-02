#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace TicketExtractor {

    struct TicketResult {
        bool success = false;
        std::string errorMessage;
        std::string ownershipHex;
        std::string encryptedHex;
        std::string debugDetails;
    };

    TicketResult ExtractTickets(uint32_t appId);

} // namespace TicketExtractor
