#pragma once

namespace nexus {

class App {
public:
    int run(int argc, char* argv[]);

private:
    int runGui();
    void ensureDataDirectories() const;
};

}  // namespace nexus
