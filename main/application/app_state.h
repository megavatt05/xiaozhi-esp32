#pragma once

#include <stdint.h>

namespace jc1060p470 {

enum class AppState : uint8_t {
    PowerOn = 0,
    WaitCamera,
    PromptDetected,
    Setup,
    Uboot,
    Finished,
    Error,
};

const char* AppStateName(AppState state);

}  // namespace jc1060p470
