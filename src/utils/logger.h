#pragma once

#include <fstream>
#include <mutex>
#include <string>

namespace nexus {

class Logger {
public:
    static void init(const std::string& logPath);
    static void shutdown();
    static void info(const std::string& message);
    static void error(const std::string& message);

private:
    static void write(const std::string& level, const std::string& message);
    static std::mutex mutex_;
    static std::ofstream logFile_;
    static bool initialized_;
};

}  // namespace nexus
