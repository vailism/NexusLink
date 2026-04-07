#include "chat.h"

#include "../file/file_manager.h"
#include "../utils/logger.h"

namespace nexus::chat {

void ChatService::runServerSession(network::Connection& connection, int clientId) const {
    const std::string clientTag = "Client #" + std::to_string(clientId);
    Logger::info(clientTag + " chat session started.");

    while (true) {
        std::string message;
        if (!connection.receiveMessage(message)) {
            Logger::error(clientTag + " receive failed. Closing session.");
            return;
        }

        if (file::isFileHeader(message)) {
            std::string savedPath;
            if (file::receiveFileFromHeader(connection, message, file::FileManager::downloadsDir(), clientTag, savedPath)) {
                const std::string response = "Server stored file at " + savedPath;
                connection.sendMessage(response);
                Logger::info(clientTag + " transfer complete: " + savedPath);
            } else {
                connection.sendMessage("Server failed to receive file.");
                Logger::error(clientTag + " transfer failed.");
            }
            continue;
        }

        Logger::info(clientTag + ": " + message);

        if (message == "/quit") {
            connection.sendMessage("Server closing session.");
            Logger::info(clientTag + " closed session.");
            return;
        }

        if (!connection.sendMessage("Server echo: " + message)) {
            Logger::error(clientTag + " send failed. Closing session.");
            return;
        }
    }
}

}  // namespace nexus::chat
