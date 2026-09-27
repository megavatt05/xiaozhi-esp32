#pragma once

#include "app_state_machine.h"

namespace jc1060p470 {

class CamRxTask {
public:
    CamRxTask();
    ~CamRxTask();

    bool Start(AppStateMachine* state_machine);

private:
    static void TaskEntry(void* arg);
    void TaskLoop();

    AppStateMachine* state_machine_;
    TaskHandle_t task_handle_;
};

}  // namespace jc1060p470
