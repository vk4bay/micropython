#ifndef CORE1_API_H
#define CORE1_API_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// Configuration
#define CORE1_CMD_QUEUE_SIZE 16
#define CORE1_RESP_QUEUE_SIZE 16
#define CORE1_MAX_PENDING 32
#define CORE1_TASK_STACK_SIZE 4096
#define CORE1_TASK_PRIORITY 5
#define CORE1_MAX_PAYLOAD_SIZE 128

// Response modes
typedef enum {
    CORE1_MODE_BLOCKING = 0,
    CORE1_MODE_CALLBACK = 1,
    CORE1_MODE_EVENT = 2
} core1_response_mode_t;

// Command IDs
typedef enum {
    CMD_ECHO = 0x0001,
    CMD_ADD = 0x0002,
    CMD_GPIO_SET = 0x0010,
    CMD_GPIO_READ = 0x0011,
    CMD_DELAY = 0x0020,
    CMD_STATUS = 0x00FF
} core1_command_id_t;

// Status codes
typedef enum {
    CORE1_OK = 0,
    CORE1_ERROR_TIMEOUT = -1,
    CORE1_ERROR_QUEUE_FULL = -2,
    CORE1_ERROR_INVALID_CMD = -3,
    CORE1_ERROR_INVALID_PARAM = -4,
    CORE1_ERROR_CORE1_BUSY = -5,
    CORE1_ERROR_NO_RESPONSE = -6
} core1_status_t;

// Command structure
typedef struct {
    uint16_t cmd_id;
    uint32_t sequence;
    core1_response_mode_t mode;
    uint32_t timeout_ms;
    void* callback_ref;  // For Python callback object
    void* event_ref;     // For event/queue object
    uint8_t payload[CORE1_MAX_PAYLOAD_SIZE];
} core1_command_t;

// Response structure
typedef struct {
    uint32_t sequence;
    core1_status_t status;
    uint8_t payload[CORE1_MAX_PAYLOAD_SIZE];
} core1_response_t;

// Pending command tracking
typedef struct {
    uint32_t sequence;
    core1_response_mode_t mode;
    void* callback_ref;
    void* event_ref;
    uint64_t deadline_us;  // Absolute timestamp for timeout
    bool active;
} pending_command_t;

// Global state
typedef struct {
    QueueHandle_t cmd_queue;
    QueueHandle_t resp_queue;
    TaskHandle_t core1_task;
    TaskHandle_t monitor_task;
    uint32_t sequence_counter;
    pending_command_t pending[CORE1_MAX_PENDING];
    bool initialized;
    bool monitoring;
} core1_state_t;

// Function declarations
void core1_init(void);
void core1_task_main(void *pvParameters);
uint32_t core1_get_next_sequence(void);
int core1_register_pending(uint32_t seq, core1_response_mode_t mode, 
                           void* callback_ref, void* event_ref, uint32_t timeout_ms);
void core1_clear_pending(uint32_t seq);
pending_command_t* core1_find_pending(uint32_t seq);
core1_state_t* core1_get_state(void);
void core1_start_monitoring(void);
void core1_schedule_callback(void* callback_ref, core1_response_t* resp);
void core1_schedule_callback_timeout(void* callback_ref);
void core1_signal_event(void* event_ref, core1_response_t* resp);
void core1_signal_event_timeout(void* event_ref);
void core1_set_log_level(int level);

#endif // CORE1_API_H

