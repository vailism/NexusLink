#include "app.h"

#include <filesystem>
#include "../ui/gui.h"
#include "../utils/logger.h"

namespace nexus {

int App::run(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    ensureDataDirectories();
    Logger::init("data/logs/nexuslink.log");

#ifdef NEXUSLINK_ENABLE_GUI
    const int code = runGui();
#else
#error "NEXUSLINK_ENABLE_GUI must be enabled"
#endif
    Logger::shutdown();
    return code;
}

int App::runGui() {
    ui::GuiApp gui;
    return gui.run();
}

void App::ensureDataDirectories() const {
    std::filesystem::create_directories("data/downloads");
    std::filesystem::create_directories("data/uploads");
    std::filesystem::create_directories("data/logs");
}

}  // namespace nexus
