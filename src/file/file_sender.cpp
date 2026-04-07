#include "file_manager.h"

#include <filesystem>
#include <fstream>
#include <vector>

#include "../network/connection.h"
#include "../utils/logger.h"

namespace nexus::file {

bool sendFile(network::Connection& connection, const std::string& filePath, const std::string& senderTag) {
    return sendFile(connection, filePath, senderTag, {});
}

bool sendFile(
    network::Connection& connection,
    const std::string& filePath,
    const std::string& senderTag,
    const std::function<void(std::uint64_t, std::uint64_t)>& onProgress) {
    const std::filesystem::path path(filePath);
    if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
        Logger::error(senderTag + " file not found: " + filePath);
        return false;
    }

    const std::uint64_t totalSize = std::filesystem::file_size(path);
    const std::string fileName = path.filename().string();
    const std::string header =
        std::string(FileManager::kFileBeginPrefix) + fileName + "|" + std::to_string(totalSize);

    if (!connection.sendMessage(header)) {
        Logger::error(senderTag + " failed to send file header.");
        return false;
    }

    std::ifstream input(filePath, std::ios::binary);
    if (!input.is_open()) {
        Logger::error(senderTag + " failed to open file: " + filePath);
        return false;
    }

    std::vector<char> buffer(FileManager::kChunkSize);
    std::uint64_t sentBytes = 0;

    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize readBytes = input.gcount();
        if (readBytes <= 0) {
            break;
        }

        if (!connection.sendMessage(std::string(buffer.data(), static_cast<std::size_t>(readBytes)))) {
            Logger::error(senderTag + " failed while sending file chunk.");
            return false;
        }

        sentBytes += static_cast<std::uint64_t>(readBytes);
        const std::uint64_t percent = (totalSize == 0) ? 100 : (sentBytes * 100 / totalSize);
        Logger::info(senderTag + " sending " + fileName + " " + std::to_string(percent) + "% (" +
                     std::to_string(sentBytes) + "/" + std::to_string(totalSize) + " bytes)");

        if (onProgress) {
            onProgress(sentBytes, totalSize);
        }
    }

    return sentBytes == totalSize;
}

}  // namespace nexus::file
