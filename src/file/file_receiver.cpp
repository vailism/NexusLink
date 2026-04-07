#include "file_manager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "../network/connection.h"
#include "../utils/logger.h"

namespace nexus::file {

namespace {

bool parseHeader(const std::string& headerMessage, FileHeader& outHeader) {
    if (headerMessage.rfind(FileManager::kFileBeginPrefix, 0) != 0) {
        return false;
    }

    const std::string payload = headerMessage.substr(std::char_traits<char>::length(FileManager::kFileBeginPrefix));
    const std::size_t sep = payload.rfind('|');
    if (sep == std::string::npos || sep == 0 || sep + 1 >= payload.size()) {
        return false;
    }

    outHeader.fileName = payload.substr(0, sep);
    try {
        outHeader.fileSize = static_cast<std::uint64_t>(std::stoull(payload.substr(sep + 1)));
    } catch (...) {
        return false;
    }

    return true;
}

std::string safeFileName(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char ch : input) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' ||
            ch == '_' || ch == '-') {
            out.push_back(ch);
        } else {
            out.push_back('_');
        }
    }
    if (out.empty()) {
        out = "file.bin";
    }
    return out;
}

}  // namespace

bool receiveFileFromHeader(
    network::Connection& connection,
    const std::string& headerMessage,
    const std::string& outputDirectory,
    const std::string& receiverTag,
    std::string& outSavedPath) {
    FileHeader header;
    if (!parseHeader(headerMessage, header)) {
        Logger::error(receiverTag + " invalid file header received.");
        return false;
    }

    std::filesystem::create_directories(outputDirectory);

    const std::string baseName = safeFileName(header.fileName);
    const std::filesystem::path outputPath = std::filesystem::path(outputDirectory) / baseName;
    outSavedPath = outputPath.string();

    std::ofstream output(outSavedPath, std::ios::binary);
    if (!output.is_open()) {
        Logger::error(receiverTag + " failed to open output file: " + outSavedPath);
        return false;
    }

    std::uint64_t receivedBytes = 0;
    while (receivedBytes < header.fileSize) {
        std::string chunk;
        if (!connection.receiveMessage(chunk)) {
            Logger::error(receiverTag + " failed while receiving file chunk.");
            return false;
        }

        const std::uint64_t remaining = header.fileSize - receivedBytes;
        const std::size_t writeSize =
            static_cast<std::size_t>(std::min<std::uint64_t>(remaining, static_cast<std::uint64_t>(chunk.size())));

        output.write(chunk.data(), static_cast<std::streamsize>(writeSize));
        if (!output.good()) {
            Logger::error(receiverTag + " failed while writing to: " + outSavedPath);
            return false;
        }

        receivedBytes += static_cast<std::uint64_t>(writeSize);
        const std::uint64_t percent = (header.fileSize == 0) ? 100 : (receivedBytes * 100 / header.fileSize);
        Logger::info(receiverTag + " receiving " + baseName + " " + std::to_string(percent) + "% (" +
                     std::to_string(receivedBytes) + "/" + std::to_string(header.fileSize) + " bytes)");
    }

    return true;
}

bool isFileHeader(const std::string& message) {
    return message.rfind(FileManager::kFileBeginPrefix, 0) == 0;
}

}  // namespace nexus::file
