#include "gui.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#endif

#ifdef NEXUSLINK_ENABLE_GUI

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#ifdef _WIN32
#include <winsock2.h>
#include <GL/gl.h>
#else
#include <OpenGL/gl3.h>
#endif
#include <GLFW/glfw3.h>

#include "../chat/chat.h"
#include "../file/file_manager.h"
#include "../network/connection.h"
#include "../network/discovery.h"
#include "../network/socket.h"
#include "../utils/logger.h"

namespace nexus::ui {

namespace {

struct UiMessage {
    std::string sender;
    std::string body;
    std::string timestamp;
    bool outgoing = false;
    bool system = false;
};

struct DiscoveredDevice {
    std::string ip;
    int port = 0;
    std::string displayName;
    std::chrono::steady_clock::time_point lastSeen;
};

struct DroppedFileState {
    std::mutex mutex;
    std::vector<std::string> files;
};

void onFilesDropped(GLFWwindow* window, int count, const char** paths) {
    if (window == nullptr || count <= 0 || paths == nullptr) {
        return;
    }

    auto* state = static_cast<DroppedFileState*>(glfwGetWindowUserPointer(window));
    if (state == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(state->mutex);
    for (int i = 0; i < count; ++i) {
        if (paths[i] != nullptr) {
            state->files.emplace_back(paths[i]);
        }
    }
}

std::string nowClockString() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tmBuf, "%H:%M");
    return oss.str();
}

std::string detectLocalIpv4() {
#ifdef _WIN32
    return "127.0.0.1";
#else
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1 || ifaddr == nullptr) {
        return "127.0.0.1";
    }

    std::string bestIp = "127.0.0.1";
    for (struct ifaddrs* it = ifaddr; it != nullptr; it = it->ifa_next) {
        if (it->ifa_addr == nullptr) {
            continue;
        }
        if (it->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if ((it->ifa_flags & IFF_UP) == 0 || (it->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }

        char addrBuf[INET_ADDRSTRLEN] = {0};
        auto* sa = reinterpret_cast<sockaddr_in*>(it->ifa_addr);
        const char* res = inet_ntop(AF_INET, &(sa->sin_addr), addrBuf, INET_ADDRSTRLEN);
        if (res != nullptr) {
            bestIp = addrBuf;
            break;
        }
    }

    freeifaddrs(ifaddr);
    return bestIp;
#endif
}

UiMessage parseUiMessage(const std::string& raw) {
    UiMessage msg;
    msg.timestamp = nowClockString();
    msg.body = raw;
    msg.sender = "System";
    msg.system = true;

    if (raw.rfind("[You:", 0) == 0) {
        const std::size_t close = raw.find(']');
        if (close != std::string::npos && close > 5) {
            msg.sender = raw.substr(5, close - 5);
            msg.body = (close + 2 <= raw.size()) ? raw.substr(close + 2) : "";
        } else {
            msg.sender = "You";
            msg.body = raw.substr(5);
        }
        msg.outgoing = true;
        msg.system = false;
        return msg;
    }
    if (raw.rfind("[Peer] ", 0) == 0) {
        std::string payload = raw.substr(7);
        constexpr const char* kServerEchoPrefix = "Server echo: ";
        if (payload.rfind(kServerEchoPrefix, 0) == 0) {
            payload = payload.substr(std::strlen(kServerEchoPrefix));
        }

        constexpr const char* kChatPrefix = "CHAT:";
        if (payload.rfind(kChatPrefix, 0) == 0) {
            const std::string chatPayload = payload.substr(std::strlen(kChatPrefix));
            const std::size_t sep = chatPayload.find(':');
            if (sep != std::string::npos) {
                msg.sender = chatPayload.substr(0, sep);
                msg.body = chatPayload.substr(sep + 1);
            } else {
                msg.sender = "Peer";
                msg.body = payload;
            }
        } else {
            msg.sender = "Server";
            msg.body = payload;
        }
        msg.system = false;
        return msg;
    }
    if (raw.rfind("[System] ", 0) == 0) {
        msg.sender = "System";
        msg.body = raw.substr(9);
        msg.system = true;
        return msg;
    }
    if (raw.rfind("[UI] ", 0) == 0) {
        msg.sender = "UI";
        msg.body = raw.substr(5);
        msg.system = true;
        return msg;
    }
    return msg;
}

std::string initialForUser(const std::string& username) {
    for (char ch : username) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) {
            std::string out(1, static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
            return out;
        }
    }
    return "?";
}

ImU32 avatarColorForUser(const std::string& username) {
    const std::size_t h = std::hash<std::string>{}(username);
    const float hue = static_cast<float>(h % 360) / 360.0f;
    ImVec4 c = ImColor::HSV(hue, 0.55f, 0.85f);
    return ImGui::ColorConvertFloat4ToU32(c);
}

std::string encodeHistoryLine(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string decodeHistoryLine(const std::string& line) {
    std::string out;
    out.reserve(line.size());
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1 < line.size()) {
            if (line[i + 1] == 'n') {
                out.push_back('\n');
                ++i;
                continue;
            }
            if (line[i + 1] == 'r') {
                out.push_back('\r');
                ++i;
                continue;
            }
        }
        out.push_back(line[i]);
    }
    return out;
}

void appendChatHistory(const std::string& raw) {
    static std::mutex historyMutex;
    std::lock_guard<std::mutex> lock(historyMutex);
    std::filesystem::create_directories("data/logs");
    std::ofstream out("data/logs/chat.txt", std::ios::app);
    if (out.is_open()) {
        out << encodeHistoryLine(raw) << '\n';
    }
}

std::vector<std::string> loadChatHistory() {
    std::vector<std::string> lines;
    std::ifstream in("data/logs/chat.txt");
    if (!in.is_open()) {
        return lines;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) {
            lines.push_back(decodeHistoryLine(line));
        }
    }
    return lines;
}

std::string loadSavedUsername() {
    std::ifstream in("data/logs/username.txt");
    std::string line;
    if (!in.is_open() || !std::getline(in, line)) {
        return "";
    }
    return line;
}

void saveUsername(const std::string& username) {
    std::filesystem::create_directories("data/logs");
    std::ofstream out("data/logs/username.txt", std::ios::trunc);
    if (out.is_open()) {
        out << username << '\n';
    }
}

}  // namespace

int GuiApp::run() {
    std::cout << "Launching GUI..." << std::endl;

    if (!network::initializeSockets()) {
        Logger::error("Socket initialization failed for GUI mode.");
        std::cerr << "[GUI] network::initializeSockets failed" << std::endl;
        return 1;
    }

    if (!glfwInit()) {
        const char* desc = nullptr;
        int err = glfwGetError(&desc);
        std::cerr << "[GUI] glfwInit failed, error=" << err << " desc=" << (desc ? desc : "<none>") << std::endl;
        network::cleanupSockets();
        return 1;
    }
    std::cout << "[GUI] glfwInit ok" << std::endl;

    const char* glslVersion = "#version 130";
#ifdef __APPLE__
    glslVersion = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#elif defined(_WIN32)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    GLFWwindow* window = glfwCreateWindow(1200, 760, "NexusLink", nullptr, nullptr);
    if (window == nullptr) {
        const char* desc = nullptr;
        int err = glfwGetError(&desc);
        std::cerr << "[GUI] glfwCreateWindow failed, error=" << err << " desc=" << (desc ? desc : "<none>")
                  << std::endl;
        glfwTerminate();
        network::cleanupSockets();
        return 1;
    }
    std::cout << "[GUI] window created" << std::endl;

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    DroppedFileState droppedFileState;
    glfwSetWindowUserPointer(window, &droppedFileState);
    glfwSetDropCallback(window, onFilesDropped);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    std::cout << "[GUI] ImGui context created" << std::endl;
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 14.0f;
    style.FrameRounding = 10.0f;
    style.GrabRounding = 8.0f;
    style.ChildRounding = 12.0f;
    style.WindowPadding = ImVec2(14.0f, 12.0f);
    style.FramePadding = ImVec2(10.0f, 8.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.08f, 0.11f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.11f, 0.15f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.14f, 0.19f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.17f, 0.18f, 0.24f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.21f, 0.28f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.16f, 0.31f, 0.50f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.37f, 0.58f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.13f, 0.27f, 0.44f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.16f, 0.31f, 0.50f, 0.85f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.20f, 0.37f, 0.58f, 0.90f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.13f, 0.27f, 0.44f, 0.95f);

    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
        std::cerr << "[GUI] ImGui_ImplGlfw_InitForOpenGL failed" << std::endl;
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        network::cleanupSockets();
        return 1;
    }

    if (!ImGui_ImplOpenGL3_Init(glslVersion)) {
        std::cerr << "[GUI] ImGui_ImplOpenGL3_Init failed for GLSL " << glslVersion << std::endl;
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        network::cleanupSockets();
        return 1;
    }
    std::cout << "[GUI] ImGui backends initialized" << std::endl;

    std::array<char, 64> ipInput{};
    std::snprintf(ipInput.data(), ipInput.size(), "127.0.0.1");
    std::array<char, 16> portInput{};
    std::snprintf(portInput.data(), portInput.size(), "4040");
    const std::string localIp = detectLocalIpv4();

    std::array<char, 128> chatInput{};
    std::array<char, 64> usernameInput{};
    const std::string savedUsername = loadSavedUsername();
    if (!savedUsername.empty()) {
        std::snprintf(usernameInput.data(), usernameInput.size(), "%s", savedUsername.c_str());
    }
    bool usernameReady = false;
    std::string currentUsername;
    std::string selectedFile = "No file selected";

    std::atomic<bool> isConnected{false};
    std::atomic<bool> serverRunning{false};
    std::atomic<bool> receiveLoopRunning{false};
    std::atomic<bool> fileTransferRunning{false};
    std::atomic<float> fileProgress{0.0f};
    std::mutex messagesMutex;
    std::vector<UiMessage> pendingMessages;

    auto addMessage = [&](std::vector<UiMessage>& target, const std::string& raw, bool persist = true) {
        target.push_back(parseUiMessage(raw));
        if (persist) {
            appendChatHistory(raw);
        }
    };

    auto pushAsyncMessage = [&](const std::string& message) {
        std::lock_guard<std::mutex> lock(messagesMutex);
        addMessage(pendingMessages, message);
    };

    std::vector<UiMessage> messages;
    const std::vector<std::string> historyLines = loadChatHistory();
    for (const auto& raw : historyLines) {
        addMessage(messages, raw, false);
    }
    addMessage(messages, "[System] NexusLink UI initialized");
    addMessage(messages, "[System] Chat backend integration active");

    std::mutex connectionMutex;
    std::shared_ptr<network::Connection> activeConnection;
    std::thread receiveThread;
    std::thread fileSendThread;

    std::mutex typingMutex;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> typingUsers;
    std::chrono::steady_clock::time_point lastTypingSignalAt = std::chrono::steady_clock::time_point::min();

    std::mutex discoveryMutex;
    std::vector<DiscoveredDevice> discoveredDevices;
    network::DiscoveryListener discoveryListener;

    std::thread localServerThread;
    std::atomic<int> nextClientId{1};

    auto startEmbeddedServer = [&]() {
        if (serverRunning.load()) {
            return;
        }

        int port = 4040;
        try {
            port = std::stoi(portInput.data());
        } catch (...) {
            pushAsyncMessage("[System] Invalid port. Using 4040.");
            port = 4040;
            std::snprintf(portInput.data(), portInput.size(), "4040");
        }

        serverRunning.store(true);
        localServerThread = std::thread([&, port]() {
            network::TcpSocket listener;
            if (!listener.create() || !listener.bindAndListen(port)) {
                pushAsyncMessage("[System] Failed to start local server on port " + std::to_string(port));
                serverRunning.store(false);
                return;
            }

            pushAsyncMessage("[System] Local server listening on port " + std::to_string(port));

            while (serverRunning.load()) {
                network::TcpSocket clientSocket = listener.acceptClient();
                if (!clientSocket.valid()) {
                    if (serverRunning.load()) {
                        pushAsyncMessage("[System] Local server accept failed");
                    }
                    continue;
                }

                if (!serverRunning.load()) {
                    break;
                }

                const int clientId = nextClientId.fetch_add(1);
                pushAsyncMessage("[System] Accepted client #" + std::to_string(clientId));

                std::thread([clientId, socket = std::move(clientSocket)]() mutable {
                    network::Connection connection(std::move(socket));
                    chat::ChatService chatService;
                    chatService.runServerSession(connection, clientId);
                }).detach();
            }
        });
    };

    auto connectClient = [&]() {
        if (isConnected.load()) {
            return;
        }

        int port = 4040;
        try {
            port = std::stoi(portInput.data());
        } catch (...) {
            addMessage(messages, "[System] Invalid port");
            return;
        }

        network::TcpSocket socket;
        if (!socket.create() || !socket.connectTo(ipInput.data(), port)) {
            addMessage(
                messages,
                "[System] Connection failed to " + std::string(ipInput.data()) + ":" + std::to_string(port));
            return;
        }

        {
            std::lock_guard<std::mutex> lock(connectionMutex);
            activeConnection = std::make_shared<network::Connection>(std::move(socket));
        }

        isConnected.store(true);
        receiveLoopRunning.store(true);
        addMessage(messages, "[System] Connected to " + std::string(ipInput.data()) + ":" + std::to_string(port));

        if (receiveThread.joinable()) {
            receiveThread.join();
        }

        receiveThread = std::thread([&]() {
            std::shared_ptr<network::Connection> connection;
            {
                std::lock_guard<std::mutex> lock(connectionMutex);
                connection = activeConnection;
            }

            while (receiveLoopRunning.load() && connection && connection->valid()) {
                std::string incoming;
                if (!connection->receiveMessage(incoming)) {
                    break;
                }

                std::string signalPayload = incoming;
                constexpr const char* kServerEchoPrefix = "Server echo: ";
                if (signalPayload.rfind(kServerEchoPrefix, 0) == 0) {
                    signalPayload = signalPayload.substr(std::strlen(kServerEchoPrefix));
                }

                constexpr const char* kTypingPrefix = "TYPING:";
                if (signalPayload.rfind(kTypingPrefix, 0) == 0) {
                    const std::string typingUser = signalPayload.substr(std::strlen(kTypingPrefix));
                    if (!typingUser.empty() && typingUser != currentUsername) {
                        std::lock_guard<std::mutex> lock(typingMutex);
                        typingUsers[typingUser] = std::chrono::steady_clock::now();
                    }
                    continue;
                }

                pushAsyncMessage("[Peer] " + incoming);
            }

            receiveLoopRunning.store(false);
            isConnected.store(false);
            pushAsyncMessage("[System] Disconnected");
        });
    };

    auto disconnectClient = [&]() {
        const bool wasConnected = isConnected.load();

        std::shared_ptr<network::Connection> connection;
        {
            std::lock_guard<std::mutex> lock(connectionMutex);
            connection = activeConnection;
        }

        if (wasConnected && connection) {
            connection->sendMessage("/quit");
        }

        receiveLoopRunning.store(false);
        if (receiveThread.joinable()) {
            receiveThread.join();
        }

        {
            std::lock_guard<std::mutex> lock(connectionMutex);
            activeConnection.reset();
        }

        isConnected.store(false);
        if (wasConnected) {
            addMessage(messages, "[System] Disconnected");
        }
    };

    auto sendChatMessage = [&](const std::string& text) {
        if (text.empty()) {
            return;
        }

        if (!usernameReady || currentUsername.empty()) {
            addMessage(messages, "[System] Set username first");
            return;
        }

        if (!isConnected.load()) {
            addMessage(messages, "[System] Not connected");
            return;
        }

        std::shared_ptr<network::Connection> connection;
        {
            std::lock_guard<std::mutex> lock(connectionMutex);
            connection = activeConnection;
        }

        if (!connection || !connection->valid()) {
            addMessage(messages, "[System] Connection is not valid");
            return;
        }

        const std::string packet = "CHAT:" + currentUsername + ":" + text;
        if (!connection->sendMessage(packet)) {
            addMessage(messages, "[System] Failed to send message");
            return;
        }

        addMessage(messages, "[You:" + currentUsername + "] " + text);
    };

    auto sendTypingSignal = [&]() {
        if (!usernameReady || currentUsername.empty() || !isConnected.load()) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (lastTypingSignalAt != std::chrono::steady_clock::time_point::min() &&
            (now - lastTypingSignalAt) < std::chrono::milliseconds(450)) {
            return;
        }

        std::shared_ptr<network::Connection> connection;
        {
            std::lock_guard<std::mutex> lock(connectionMutex);
            connection = activeConnection;
        }

        if (!connection || !connection->valid()) {
            return;
        }

        if (connection->sendMessage("TYPING:" + currentUsername)) {
            lastTypingSignalAt = now;
        }
    };

    auto startFileSend = [&](const std::string& filePath, const std::string& displayName) {
        if (fileTransferRunning.load()) {
            addMessage(messages, "[System] File transfer already in progress");
            return;
        }
        if (!isConnected.load()) {
            addMessage(messages, "[System] Connect before sending a file");
            return;
        }

        std::shared_ptr<network::Connection> connection;
        {
            std::lock_guard<std::mutex> lock(connectionMutex);
            connection = activeConnection;
        }

        if (!connection || !connection->valid()) {
            addMessage(messages, "[System] Connection is not valid");
            return;
        }

        if (fileSendThread.joinable()) {
            fileSendThread.join();
        }

        fileTransferRunning.store(true);
        fileProgress.store(0.0f);
        selectedFile = displayName;
        addMessage(messages, "[System] Sending file: " + displayName);

        fileSendThread = std::thread([&, connection, filePath, displayName]() {
            const bool ok = file::sendFile(*connection, filePath, "Client", [&](std::uint64_t sent, std::uint64_t total) {
                if (total == 0) {
                    fileProgress.store(1.0f);
                    return;
                }
                fileProgress.store(static_cast<float>(sent) / static_cast<float>(total));
            });

            if (ok) {
                fileProgress.store(1.0f);
                pushAsyncMessage("[System] File sent: " + displayName);
            } else {
                pushAsyncMessage("[System] File send failed: " + displayName);
            }
            fileTransferRunning.store(false);
        });
    };

    if (!discoveryListener.start([&](const std::string& ip, int port) {
            std::lock_guard<std::mutex> lock(discoveryMutex);
            const auto now = std::chrono::steady_clock::now();
            for (auto& device : discoveredDevices) {
                if (device.ip == ip && device.port == port) {
                    device.lastSeen = now;
                    return;
                }
            }
            std::string label = "NexusLink Node";
            if (ip == localIp) {
                label = "This Device";
            }
            discoveredDevices.push_back(DiscoveredDevice{ip, port, label, now});
        })) {
        addMessage(messages, "[System] Discovery listener failed to start on UDP 50000");
    } else {
        addMessage(messages, "[System] Discovery listener active (UDP 50000)");
    }

    auto drawChatBubble = [&](const UiMessage& msg, int index, bool groupedWithPrevious) {
        const std::string& sender = msg.sender;
        const std::string& body = msg.body;
        const bool isOutgoing = msg.outgoing;

        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const float maxBubbleWidth = std::max(220.0f, availableWidth * 0.72f);
        const ImVec2 textSize = ImGui::CalcTextSize(body.c_str(), nullptr, false, maxBubbleWidth - 26.0f);
        const float bubbleWidth = std::min(maxBubbleWidth, std::max(180.0f, textSize.x + 26.0f));
        const float bubbleHeight = textSize.y + 46.0f;

        if (!groupedWithPrevious) {
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
        }

        if (isOutgoing) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, availableWidth - bubbleWidth - 6.0f));
        }

        ImVec4 bubbleColor = ImVec4(0.17f, 0.19f, 0.26f, 1.0f);
        ImVec4 senderColor = ImVec4(0.58f, 0.65f, 0.82f, 1.0f);
        if (isOutgoing) {
            bubbleColor = ImVec4(0.16f, 0.36f, 0.60f, 1.0f);
            senderColor = ImVec4(0.86f, 0.93f, 1.0f, 1.0f);
        } else if (msg.system) {
            bubbleColor = ImVec4(0.25f, 0.20f, 0.13f, 1.0f);
            senderColor = ImVec4(1.00f, 0.85f, 0.60f, 1.0f);
        }

        const std::string id = "bubble_" + std::to_string(index);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, bubbleColor);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14.0f);
        ImGui::BeginChild(id.c_str(), ImVec2(bubbleWidth, bubbleHeight), true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const ImVec2 headerStart = ImGui::GetCursorScreenPos();
        const float avatarRadius = 10.0f;
        const ImVec2 avatarCenter(headerStart.x + avatarRadius + 1.0f, headerStart.y + avatarRadius + 1.0f);
        ImGui::GetWindowDrawList()->AddCircleFilled(avatarCenter, avatarRadius, avatarColorForUser(sender));
        const std::string initial = initialForUser(sender);
        const ImVec2 initialSize = ImGui::CalcTextSize(initial.c_str());
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(avatarCenter.x - initialSize.x * 0.5f, avatarCenter.y - initialSize.y * 0.5f),
            IM_COL32(250, 250, 250, 255),
            initial.c_str());
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 26.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 0.76f, 0.84f, 1.0f));
        ImGui::TextUnformatted(msg.timestamp.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), bubbleWidth - ImGui::CalcTextSize(sender.c_str()).x - 14.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, senderColor);
        ImGui::TextUnformatted(sender.c_str());
        ImGui::PopStyleColor();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + bubbleWidth - 18.0f);
        ImGui::TextUnformatted(body.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    };

    std::uint64_t frameCount = 0;
    while (!glfwWindowShouldClose(window)) {
        ++frameCount;
        if (frameCount == 1 || frameCount % 600 == 0) {
            std::cout << "[GUI] render loop active, frame=" << frameCount << std::endl;
        }
        glfwPollEvents();

        {
            std::vector<std::string> droppedFiles;
            {
                std::lock_guard<std::mutex> lock(droppedFileState.mutex);
                droppedFiles.swap(droppedFileState.files);
            }

            for (const auto& droppedPath : droppedFiles) {
                const std::filesystem::path p(droppedPath);
                if (!std::filesystem::exists(p) || !std::filesystem::is_regular_file(p)) {
                    addMessage(messages, "[System] Dropped file is invalid");
                    continue;
                }

                const std::string fileName = p.filename().string();
                addMessage(messages, "[UI] Dropped file: " + fileName);
                startFileSend(p.string(), fileName);
            }
        }

        {
            std::lock_guard<std::mutex> lock(messagesMutex);
            for (const UiMessage& pending : pendingMessages) {
                messages.push_back(pending);
            }
            pendingMessages.clear();
        }

        {
            std::lock_guard<std::mutex> lock(discoveryMutex);
            const auto cutoff = std::chrono::steady_clock::now() - std::chrono::seconds(6);
            discoveredDevices.erase(
                std::remove_if(discoveredDevices.begin(),
                               discoveredDevices.end(),
                               [&](const DiscoveredDevice& d) { return d.lastSeen < cutoff; }),
                discoveredDevices.end());
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoSavedSettings;

        ImGui::Begin("NexusLinkRoot", nullptr, flags);

        if (!usernameReady) {
            ImGui::OpenPopup("Set Username");
        }

        if (ImGui::BeginPopupModal("Set Username", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Enter your username to start.");
            ImGui::SetNextItemWidth(280.0f);
            ImGui::InputText("##username", usernameInput.data(), usernameInput.size());

            if (ImGui::Button("Continue", ImVec2(120.0f, 0.0f))) {
                std::string picked = usernameInput.data();
                while (!picked.empty() && (picked.back() == ' ' || picked.back() == '\t')) {
                    picked.pop_back();
                }
                std::size_t start = 0;
                while (start < picked.size() && (picked[start] == ' ' || picked[start] == '\t')) {
                    ++start;
                }
                picked = picked.substr(start);
                if (!picked.empty()) {
                    currentUsername = picked;
                    usernameReady = true;
                    saveUsername(currentUsername);
                    addMessage(messages, "[System] Logged in as " + currentUsername);
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Guest", ImVec2(120.0f, 0.0f))) {
                currentUsername = "Guest";
                std::snprintf(usernameInput.data(), usernameInput.size(), "%s", currentUsername.c_str());
                usernameReady = true;
                saveUsername(currentUsername);
                addMessage(messages, "[System] Logged in as " + currentUsername);
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        ImGui::BeginChild("Sidebar", ImVec2(330.0f, 0.0f), true);
        ImGui::TextUnformatted("NexusLink");
        ImGui::TextUnformatted("LAN Cloud + Chat");
        if (usernameReady) {
            ImGui::TextColored(ImVec4(0.80f, 0.92f, 0.95f, 1.0f), "User: %s", currentUsername.c_str());
        }
        ImGui::Separator();

        if (ImGui::Button("Start Server", ImVec2(-1.0f, 0.0f))) {
            startEmbeddedServer();
        }
        if (ImGui::Button("Connect as Client", ImVec2(-1.0f, 0.0f))) {
            connectClient();
        }

        ImGui::SeparatorText("Connection");
        ImGui::TextUnformatted("Your IP");
        ImGui::TextColored(ImVec4(0.62f, 0.86f, 0.98f, 1.0f), "%s", localIp.c_str());
        if (ImGui::Button("Use My IP", ImVec2(-1.0f, 0.0f))) {
            std::snprintf(ipInput.data(), ipInput.size(), "%s", localIp.c_str());
        }
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("IP Address", ipInput.data(), ipInput.size());
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("Port", portInput.data(), portInput.size());
        if (!isConnected.load()) {
            if (ImGui::Button("Connect", ImVec2(-1.0f, 0.0f))) {
                connectClient();
            }
        } else {
            if (ImGui::Button("Disconnect", ImVec2(-1.0f, 0.0f))) {
                disconnectClient();
            }
        }

        ImVec4 statusColor =
            isConnected.load() ? ImVec4(0.2f, 0.9f, 0.4f, 1.0f) : ImVec4(0.95f, 0.35f, 0.35f, 1.0f);
        ImGui::TextUnformatted("Status");
        ImGui::SameLine();
        ImGui::TextColored(statusColor, isConnected.load() ? "Online" : "Offline");

        ImGui::SeparatorText("Available Devices");
        std::vector<DiscoveredDevice> discoveredSnapshot;
        {
            std::lock_guard<std::mutex> lock(discoveryMutex);
            discoveredSnapshot = discoveredDevices;
        }

        if (discoveredSnapshot.empty()) {
            ImGui::TextUnformatted("No devices discovered yet...");
        } else {
            for (const auto& device : discoveredSnapshot) {
                const auto ageSec = std::chrono::duration_cast<std::chrono::seconds>(
                                        std::chrono::steady_clock::now() - device.lastSeen)
                                        .count();
                const std::string rowLabel = "[" + device.ip + "] " + device.displayName +
                                             "  •  Port " + std::to_string(device.port) +
                                             "  •  Seen " + std::to_string(ageSec) + "s ago";
                ImGui::TextWrapped("%s", rowLabel.c_str());
                const std::string btnLabel = "Connect##" + device.ip + ":" + std::to_string(device.port);
                if (ImGui::Button(btnLabel.c_str(), ImVec2(-1.0f, 0.0f))) {
                    std::snprintf(ipInput.data(), ipInput.size(), "%s", device.ip.c_str());
                    std::snprintf(portInput.data(), portInput.size(), "%d", device.port);
                    connectClient();
                }
            }
        }

        ImGui::SeparatorText("File Transfer");
        if (ImGui::Button("Select File", ImVec2(-1.0f, 0.0f))) {
            ImGui::OpenPopup("FilePickerPopup");
        }

        if (ImGui::BeginPopup("FilePickerPopup")) {
            bool hasAnyFile = false;
            const std::filesystem::path uploads(file::FileManager::uploadsDir());
            if (std::filesystem::exists(uploads)) {
                for (const auto& entry : std::filesystem::directory_iterator(uploads)) {
                    if (!entry.is_regular_file()) {
                        continue;
                    }
                    hasAnyFile = true;
                    const std::string fileName = entry.path().filename().string();
                    if (ImGui::Selectable(fileName.c_str())) {
                        selectedFile = fileName;
                        fileProgress.store(0.0f);
                        addMessage(messages, "[UI] Selected file: " + selectedFile);
                    }
                }
            }

            if (!hasAnyFile) {
                ImGui::TextUnformatted("No files found in data/uploads");
            }
            ImGui::EndPopup();
        }

        if (ImGui::Button("Send Selected File", ImVec2(-1.0f, 0.0f))) {
            if (selectedFile == "No file selected") {
                addMessage(messages, "[System] Select a file first");
            } else {
                const std::filesystem::path filePath = std::filesystem::path(file::FileManager::uploadsDir()) / selectedFile;
                startFileSend(filePath.string(), selectedFile);
            }
        }
        ImGui::TextUnformatted("Selected");
        ImGui::TextWrapped("%s", selectedFile.c_str());
        const char* progressLabel = fileTransferRunning.load() ? "Sending..." : "Transfer Progress";
        ImGui::ProgressBar(fileProgress.load(), ImVec2(-1.0f, 0.0f), progressLabel);
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("ChatWorkspace", ImVec2(0.0f, 0.0f), true);
        ImGui::TextUnformatted("Team Chat");
        ImGui::Separator();

        std::vector<std::string> activeTypingUsers;
        {
            std::lock_guard<std::mutex> lock(typingMutex);
            const auto cutoff = std::chrono::steady_clock::now() - std::chrono::seconds(2);
            for (auto it = typingUsers.begin(); it != typingUsers.end();) {
                if (it->second < cutoff) {
                    it = typingUsers.erase(it);
                } else {
                    activeTypingUsers.push_back(it->first);
                    ++it;
                }
            }
        }

        if (!activeTypingUsers.empty()) {
            std::string typingLabel;
            for (std::size_t i = 0; i < activeTypingUsers.size(); ++i) {
                typingLabel += activeTypingUsers[i];
                if (i + 1 < activeTypingUsers.size()) {
                    typingLabel += ", ";
                }
            }
            typingLabel += (activeTypingUsers.size() == 1) ? " is typing..." : " are typing...";
            ImGui::TextColored(ImVec4(0.70f, 0.86f, 0.98f, 1.0f), "%s", typingLabel.c_str());
        }

        ImGui::BeginChild("ChatScroll", ImVec2(0.0f, -72.0f), false,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar);
        for (int i = 0; i < static_cast<int>(messages.size()); ++i) {
            bool grouped = false;
            if (i > 0) {
                grouped = (messages[i - 1].sender == messages[i].sender) &&
                          (messages[i - 1].outgoing == messages[i].outgoing) &&
                          (messages[i - 1].system == messages[i].system);
            }
            drawChatBubble(messages[i], i, grouped);
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        ImGui::SetNextItemWidth(-110.0f);
        const bool inputChanged = ImGui::InputText("##chat_input", chatInput.data(), chatInput.size());
        if (inputChanged && chatInput[0] != '\0') {
            sendTypingSignal();
        }
        ImGui::SameLine();
        if (ImGui::Button("Send", ImVec2(96.0f, 0.0f))) {
            if (chatInput[0] != '\0') {
                sendChatMessage(chatInput.data());
                chatInput[0] = '\0';
            }
        }
        ImGui::EndChild();

        ImGui::End();

        ImGui::Render();
        int displayW = 0;
        int displayH = 0;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    std::cout << "[GUI] render loop ended" << std::endl;

    disconnectClient();
    discoveryListener.stop();
    if (fileSendThread.joinable()) {
        fileSendThread.join();
    }
    serverRunning.store(false);
    if (localServerThread.joinable()) {
        int shutdownPort = 4040;
        try {
            shutdownPort = std::stoi(portInput.data());
        } catch (...) {
            shutdownPort = 4040;
        }

        network::TcpSocket wakeSocket;
        if (wakeSocket.create()) {
            wakeSocket.connectTo("127.0.0.1", shutdownPort);
        }

        localServerThread.join();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    network::cleanupSockets();
    std::cout << "[GUI] GuiApp::run finished" << std::endl;
    return 0;
}

}  // namespace nexus::ui

#else

#error "NEXUSLINK_ENABLE_GUI must be defined for GuiApp"

#endif
