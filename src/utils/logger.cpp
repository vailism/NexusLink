#include "logger.h"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <time.h>
#endif

namespace nexus {

std::mutex Logger::mutex_;
std::ofstream Logger::logFile_;
bool Logger::initialized_ = false;

namespace {

std::string buildTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto nowTimeT = std::chrono::system_clock::to_time_t(now);
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % std::chrono::milliseconds(1000);

    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &nowTimeT);
#else
    localtime_r(&nowTimeT, &tmBuf);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tmBuf, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

}  // namespace

void Logger::init(const std::string& logPath) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        return;
    }

    std::filesystem::create_directories(std::filesystem::path(logPath).parent_path());
    logFile_.open(logPath, std::ios::app);
    initialized_ = true;
}

void Logger::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (logFile_.is_open()) {
        logFile_.flush();
        logFile_.close();
    }
    initialized_ = false;
}

void Logger::info(const std::string& message) {
    write("INFO", message);
}

void Logger::error(const std::string& message) {
    write("ERROR", message);
}

void Logger::write(const std::string& level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);

    const std::string line = "[" + buildTimestamp() + "] [" + level + "] [tid=" +
                             std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + "] " +
                             message;
    std::cout << line << std::endl;
    if (logFile_.is_open()) {
        logFile_ << line << std::endl;
        logFile_.flush();
    }
}

}  // namespace nexus
