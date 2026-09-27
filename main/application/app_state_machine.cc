#include "app_state_machine.h"

#include <esp_log.h>

namespace jc1060p470 {

static const char* TAG = "jc1060_app";

const char* AppStateName(AppState state) {
    switch (state) {
        case AppState::PowerOn: return "POWER_ON";
        case AppState::WaitCamera: return "WAIT_CAMERA";
        case AppState::PromptDetected: return "PROMPT_DETECTED";
        case AppState::Setup: return "SETUP";
        case AppState::Uboot: return "UBOOT";
        case AppState::Finished: return "FINISHED";
        case AppState::Error: return "ERROR";
        default: return "UNKNOWN";
    }
}

AppStateMachine::AppStateMachine()
    : event_queue_(nullptr), task_handle_(nullptr), state_(AppState::PowerOn) {}

AppStateMachine::~AppStateMachine() {
    if (task_handle_ != nullptr) {
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }
    if (event_queue_ != nullptr) {
        vQueueDelete(event_queue_);
        event_queue_ = nullptr;
    }
}

bool AppStateMachine::Start() {
    if (event_queue_ != nullptr || task_handle_ != nullptr) {
        return false;
    }

    event_queue_ = xQueueCreate(8, sizeof(AppEvent));
    if (event_queue_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create application event queue");
        return false;
    }

    if (xTaskCreate(
            &AppStateMachine::TaskEntry,
            "app_state",
            4096,
            this,
            5,
            &task_handle_) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create application state task");
        vQueueDelete(event_queue_);
        event_queue_ = nullptr;
        return false;
    }

    return true;
}

bool AppStateMachine::PostEvent(const AppEvent& event) {
    if (event_queue_ == nullptr) {
        return false;
    }

    return xQueueSend(event_queue_, &event, pdMS_TO_TICKS(100)) == pdTRUE;
}

AppState AppStateMachine::GetState() const {
    return state_;
}

void AppStateMachine::TaskEntry(void* arg) {
    static_cast<AppStateMachine*>(arg)->TaskLoop();
}

void AppStateMachine::TaskLoop() {
    ESP_LOGI(TAG, "State machine started in %s", AppStateName(state_));

    AppEvent event{AppEventType::PowerOn, 0};
    HandleEvent(event);

    while (true) {
        if (xQueueReceive(event_queue_, &event, portMAX_DELAY) == pdTRUE) {
            HandleEvent(event);
        }
    }
}

void AppStateMachine::HandleEvent(const AppEvent& event) {
    switch (state_) {
        case AppState::PowerOn:
            if (event.type == AppEventType::PowerOn) {
                TransitionTo(AppState::WaitCamera);
            }
            break;

        case AppState::WaitCamera:
            if (event.type == AppEventType::CameraReady) {
                ESP_LOGI(TAG, "Camera is ready; waiting for prompt detection");
            } else if (event.type == AppEventType::PromptDetected) {
                TransitionTo(AppState::PromptDetected);
            }
            break;

        case AppState::PromptDetected:
            if (event.type == AppEventType::PromptDetected) {
                TransitionTo(AppState::Setup);
            }
            break;

        case AppState::Setup:
            if (event.type == AppEventType::SetupOk) {
                TransitionTo(AppState::Uboot);
            } else if (event.type == AppEventType::SetupError) {
                TransitionTo(AppState::Error);
            }
            break;

        case AppState::Uboot:
            if (event.type == AppEventType::UbootOk) {
                TransitionTo(AppState::Finished);
            } else if (event.type == AppEventType::UbootError) {
                TransitionTo(AppState::Error);
            }
            break;

        case AppState::Finished:
        case AppState::Error:
            if (event.type == AppEventType::Reset) {
                TransitionTo(AppState::PowerOn);
                HandleEvent({AppEventType::PowerOn, 0});
            }
            break;
    }
}

void AppStateMachine::TransitionTo(AppState next) {
    if (state_ == next) {
        return;
    }

    ESP_LOGI(TAG, "State: %s -> %s", AppStateName(state_), AppStateName(next));
    state_ = next;
}

}  // namespace jc1060p470
