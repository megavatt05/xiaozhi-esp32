#pragma once

#include <stdint.h>

namespace jc1060p470 {

enum class AppEventType : uint8_t {
    PowerOn = 0,
    CameraReady,
    PromptDetected,
    SetupOk,
    SetupError,
    UbootOk,
    UbootError,
    Reset,
};

struct AppEvent {
    AppEventType type;
    int32_t value;
};

}  // namespace jc1060p470
