#pragma once

#include <string>

#include "../network/connection.h"

namespace nexus::chat {

class ChatService {
public:
    void runServerSession(network::Connection& connection, int clientId) const;
};

}  // namespace nexus::chat
