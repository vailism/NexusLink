#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace nexus::network {
class Connection;
}

namespace nexus::file {

class FileManager {
public:
    static constexpr std::size_t kChunkSize = 4096;
    static constexpr const char* kFileBeginPrefix = "__NL_FILE_BEGIN__|";
    static std::string uploadsDir() { return "data/uploads"; }
    static std::string downloadsDir() { return "data/downloads"; }
};

struct FileHeader {
    std::string fileName;
    std::uint64_t fileSize = 0;
};

bool sendFile(network::Connection& connection, const std::string& filePath, const std::string& senderTag);
bool sendFile(
    network::Connection& connection,
    const std::string& filePath,
    const std::string& senderTag,
    const std::function<void(std::uint64_t, std::uint64_t)>& onProgress);
bool receiveFileFromHeader(
    network::Connection& connection,
    const std::string& headerMessage,
    const std::string& outputDirectory,
    const std::string& receiverTag,
    std::string& outSavedPath);
bool isFileHeader(const std::string& message);

}  // namespace nexus::file
