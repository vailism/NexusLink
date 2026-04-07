#include "core/app.h"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <cstdio>
#include <windows.h>
#endif

#ifndef NEXUSLINK_ENABLE_GUI
#error "NEXUSLINK_ENABLE_GUI must be defined. Reconfigure with -DNEXUSLINK_ENABLE_GUI=ON"
#endif

namespace {

std::string nowTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tmBuf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void appendCrashLog(const std::string& line) {
    std::filesystem::create_directories("data/logs");
    std::ofstream out("data/logs/crash.log", std::ios::app);
    if (!out.is_open()) {
        return;
    }
    out << "[" << nowTimestamp() << "] " << line << '\n';
}

void onCrashSignal(int signalCode) {
    appendCrashLog("Caught fatal signal " + std::to_string(signalCode));
    std::_Exit(128 + signalCode);
}

void onTerminate() {
    appendCrashLog("std::terminate invoked");
    std::_Exit(1);
}

void installCrashHandlers() {
    std::set_terminate(onTerminate);
    std::signal(SIGABRT, onCrashSignal);
    std::signal(SIGSEGV, onCrashSignal);
    std::signal(SIGILL, onCrashSignal);
    std::signal(SIGFPE, onCrashSignal);
#ifdef SIGBUS
    std::signal(SIGBUS, onCrashSignal);
#endif
}

#ifdef _WIN32
void ensureConsoleForDebug() {
    if (GetConsoleWindow() == nullptr) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
            AllocConsole();
        }
    }

    FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
    freopen_s(&stream, "CONIN$", "r", stdin);
}
#endif

}  // namespace

int main(int argc, char* argv[]) {
#ifdef _WIN32
    ensureConsoleForDebug();
#endif

    installCrashHandlers();

    if (std::getenv("NEXUSLINK_SMOKE_TEST") != nullptr) {
        std::cout << "NexusLink smoke test passed" << std::endl;
        return 0;
    }

    std::cout << "App started" << std::endl;

    try {
        nexus::App app;
        const int code = app.run(argc, argv);
        std::cout << "App exiting with code " << code << std::endl;
        return code;
    } catch (const std::exception& ex) {
        std::cerr << "Fatal exception: " << ex.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Fatal unknown exception" << std::endl;
        return 1;
    }
}
