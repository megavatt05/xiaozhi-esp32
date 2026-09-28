#include "cam_rx_task.h"

#include "board.h"
#include "camera.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace jc1060p470 {

static const char* TAG = "cam_rx";

CamRxTask::CamRxTask() : state_machine_(nullptr), task_handle_(nullptr) {}

CamRxTask::~CamRxTask() {
    if (task_handle_ != nullptr) {
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }
}

bool CamRxTask::Start(AppStateMachine* state_machine) {
    if (state_machine == nullptr || task_handle_ != nullptr) {
        return false;
    }

    state_machine_ = state_machine;

    if (xTaskCreate(
            &CamRxTask::TaskEntry,
            "cam_rx",
            8192,
            this,
            4,
            &task_handle_) != pdPASS) {
        state_machine_ = nullptr;
        return false;
    }

    return true;
}

void CamRxTask::TaskEntry(void* arg) {
    static_cast<CamRxTask*>(arg)->TaskLoop();
}

void CamRxTask::TaskLoop() {
    ESP_LOGI(TAG, "Camera RX task started");

    bool camera_ready_reported = false;
    int64_t last_capture_error_log_us = 0;

    while (true) {
        Camera* camera = Board::GetInstance().GetCamera();

        if (camera == nullptr) {
            ESP_LOGE(TAG, "Board camera is not initialized; retrying");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (camera->Capture()) {
            if (!camera_ready_reported) {
                camera_ready_reported = true;
                state_machine_->PostEvent({AppEventType::CameraReady, 0});
                ESP_LOGI(TAG, "Camera capture is working");
            }

            // Prompt detection is intentionally not guessed here.
            // A future detector must post AppEventType::PromptDetected.
            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            // Пока /dev/video0 не создан, EspVideo::Capture() может возвращать
            // ошибку на каждой попытке. Не превращаем это в бесконечный поток
            // логов: продолжаем проверять камеру, но редко сообщаем об ошибке.
            int64_t now_us = esp_timer_get_time();
            if (now_us - last_capture_error_log_us >= 10000000) {
                last_capture_error_log_us = now_us;
                ESP_LOGW(TAG, "Camera capture failed; retrying (next retry in 5s)");
            }
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
}

}  // namespace jc1060p470
