#include "encryption.h"

namespace nexus::security {

std::string xorCipher(const std::string& input, const std::string& key) {
    if (key.empty()) {
        return input;
    }

    std::string output = input;
    for (std::size_t i = 0; i < input.size(); ++i) {
        output[i] = static_cast<char>(input[i] ^ key[i % key.size()]);
    }
    return output;
}

const std::string& defaultKey() {
    static const std::string key = "NexusLink_XOR_v1";
    return key;
}

}  // namespace nexus::security
