#pragma once
#include "assets/wav.h"
#include <string>

namespace vyruz {
// Decodes only embedded SMP_/SND_ PCM. A bank without samples succeeds empty.
// On malformed input the output remains unchanged.
bool decodeGafSamples(const std::vector<uint8_t>& data,std::vector<Wave>& out,std::string& error);
}
