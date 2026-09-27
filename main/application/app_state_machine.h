#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "app_events.h"
#include "app_state.h"

namespace jc1060p470 {

class AppStateMachine {
public:
    AppStateMachine();
    ~AppStateMachine();

    bool Start();
    bool PostEvent(const AppEvent& event);
    AppState GetState() const;

private:
    static void TaskEntry(void* arg);
    void TaskLoop();
    void HandleEvent(const AppEvent& event);
    void TransitionTo(AppState next);

    QueueHandle_t event_queue_;
    TaskHandle_t task_handle_;
    AppState state_;
};

}  // namespace jc1060p470
