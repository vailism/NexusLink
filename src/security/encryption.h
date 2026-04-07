#pragma once

#include <string>

namespace nexus::security {

std::string xorCipher(const std::string& input, const std::string& key);
const std::string& defaultKey();

}  // namespace nexus::security
