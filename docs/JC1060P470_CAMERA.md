# JC1060P470C-I_W_Y — подключение и работа камеры OV02C10

## 1. Назначение

Документ фиксирует подключение камеры на Guition JC1060P470C-I_W_Y и путь от сенсора до кадра приложения.
Источник аппаратной и программной информации: рабочая ветка esp_brookesia_phone репозитория megavatt05/ESP32P4-JC1060P470C-I_W_Y.

Камера: OV02C10, интерфейс изображения MIPI-CSI, управление SCCB/I2C, ESP32-P4.
Основной проверочный режим: RAW10, 1288x728, 30 FPS, 1 lane.
Драйвер также содержит 1920x1080@30 FPS, включая 2-lane режим.

## 2. Распиновка

| Сигнал | GPIO | Назначение |
|---|---:|---|
| SCCB SCL | GPIO8 | I2C/SCCB clock |
| SCCB SDA | GPIO7 | I2C/SCCB data |
| RESET | -1 / NC | аппаратный reset не используется |
| PWDN | -1 / NC | power-down не используется |
| XCLK | -1 / NC | внешний XCLK не выводится |
| MIPI-CSI | CSI PHY | высокоскоростные линии изображения, не GPIO |

Параметры reference example: I2C port 0; SCCB default 100 kHz. В нашем board layer уже существует I2C bus на GPIO7/8, поэтому камера должна использовать существующий bus handle.

## 3. Общая схема

```text
OV02C10
   │
   ├── SCCB/I2C ── GPIO7/8 ── existing I2C bus
   │
   └── MIPI-CSI ── ESP32-P4 CSI PHY
                         │
                         ▼
                   esp_cam_sensor
                         │
                         ▼
                      esp_video
                         │
                         ▼
                        V4L2
                         │
                         ▼
                    frame buffer
                         │
                         ▼
                     cam_rx_task
```

## 4. Kconfig

Минимально необходимо:

```text
CONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE=y
CONFIG_ESP_VIDEO_ENABLE_DVP_VIDEO_DEVICE=n
CONFIG_CAMERA_OV02C10=y
```

Если используется ISP pipeline:

```text
CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=y
```

## 5. Инициализация

При использовании существующей I2C bus:

```cpp
esp_video_init_csi_config_t csi_config = {
    .sccb_config = {
        .init_sccb = false,
        .i2c_handle = i2c_bus_handle,
        .freq = 400000,
    },
    .reset_pin = GPIO_NUM_NC,
    .pwdn_pin = GPIO_NUM_NC,
};

esp_video_init_config_t video_config = {
    .csi = &csi_config,
};
```

init_sccb=false означает, что esp_video использует уже созданный I2C bus handle. Частота 400 kHz поддерживается стеком; reference Kconfig имеет default 100 kHz. В проекте должна использоваться одна согласованная настройка.

Board layer создаёт camera object:

```cpp
camera_ = new EspVideo(video_config);
```

и возвращает его через GetCamera().

## 6. Обнаружение OV02C10

esp_video -> CSI video device -> esp_cam_sensor -> OV02C10 driver -> SCCB register access -> sensor ID/configuration.

Драйвер OV02C10 уже содержит detection, SCCB read/write, MIPI format configuration, stream ON/OFF, exposure/gain и sensor modes. Собственный esp_camera driver писать не нужно.

## 7. Получение изображения

Рабочий reference использует V4L2:

```text
esp_video_init
    ↓
open(video device)
    ↓
VIDIOC_QUERYCAP
    ↓
VIDIOC_G_FMT
    ↓
VIDIOC_S_FMT
    ↓
VIDIOC_REQBUFS
    ↓
VIDIOC_QUERYBUF
    ↓
VIDIOC_QBUF
    ↓
VIDIOC_STREAMON
    ↓
VIDIOC_DQBUF
    ↓
frame callback
    ↓
VIDIOC_QBUF
```

После VIDIOC_DQBUF buf.index указывает на готовый capture buffer. Reference передаёт buffer pointer, index, width, height и размер в frame callback.

## 8. Буферы

Поддерживаются MMAP и USERPTR.

Для будущего prompt detector предпочтительно минимизировать memcpy: использовать capture buffer напрямую либо переиспользуемый рабочий буфер.

## 9. Управление потоком

Старт:

```cpp
int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
ioctl(video_fd, VIDIOC_STREAMON, &type);
```

Остановка:

```cpp
ioctl(video_fd, VIDIOC_STREAMOFF, &type);
```

Каждый полученный буфер после обработки должен быть возвращён через VIDIOC_QBUF.

## 10. Управление сенсором

Sensor controls должны проходить через esp_cam_sensor/video stack. Не следует писать OV02C10 registers напрямую из cam_rx_task без необходимости.

Поддерживаемые направления управления: exposure, gain, format, stream enable/disable и другие controls, предоставленные driver.

## 11. Архитектура проекта

```text
Board::GetCamera()
       │
       ▼
    EspVideo
       │
       ▼
   esp_video
       │
       ▼
     V4L2
       │
       ▼
  capture frame
       │
       ▼
  cam_rx_task
       │
       ├── frame processing
       │
       ▼
  prompt detector
       │
       ▼
 PromptDetected
       │
       ▼
 STATE_SETUP
```

CameraReady означает только первый успешный capture. Он не означает обнаружение prompt.

## 12. Диагностика

Если OV02C10 не обнаруживается: проверить питание, GPIO7 SDA, GPIO8 SCL, pull-up, I2C frequency и sensor ID.

Если I2C работает, но кадров нет: проверить MIPI-CSI video device, OV02C10 mode, количество lanes, RAW10 format, sensor clocking, CSI PHY и ISP configuration.

Для V4L2 логировать каждый этап: open, QUERYCAP, G_FMT, S_FMT, REQBUFS, QUERYBUF, QBUF, STREAMON, DQBUF.

## 13. Контрольная последовательность

```text
Power ON
  ↓
I2C bus GPIO7/8 ready
  ↓
esp_video_init
  ↓
OV02C10 detect
  ↓
CSI ready
  ↓
video device open
  ↓
G_FMT / S_FMT
  ↓
REQBUFS / QUERYBUF / QBUF
  ↓
STREAMON
  ↓
DQBUF
  ↓
first valid frame
  ↓
CameraReady
  ↓
frame processing
  ↓
PromptDetected
  ↓
STATE_SETUP
```

## 14. Reference files

Рабочая реализация находится в ветке esp_brookesia_phone:

```text
components/apps/camera/Camera.cpp
components/apps/camera/Camera.hpp
components/apps/camera/app_video.c
components/apps/camera/app_video.h
components/apps/camera/app_camera_pipeline.cpp
components/espressif__esp_cam_sensor/sensors/ov02c10/
components/espressif__esp_video/
```

Документ является частью проекта и должен обновляться при изменении camera board configuration или capture pipeline.