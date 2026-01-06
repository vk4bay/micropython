#include "py/mpconfig.h"
#include "py/runtime.h"
#include "py/obj.h"
#include "py/mphal.h"
#include "core1_api.h"
#include "esp_log.h"
#include <string.h>
#include <inttypes.h>

static const char *TAG = "MODCORE1";

// Include the core1_api implementation directly
// This ensures all functions are in the same compilation unit
#include "core1_api.c"

// Module-level exception
MP_DEFINE_EXCEPTION(Core1Error, Exception)
MP_DEFINE_EXCEPTION(Core1TimeoutError, Core1Error)
MP_DEFINE_EXCEPTION(Core1QueueFullError, Core1Error)

// Callback queue for deferred execution in MicroPython thread
#define CALLBACK_QUEUE_SIZE 16

typedef struct {
    mp_obj_t callback;
    core1_response_t response;  // Store raw response data, not Python objects
    bool is_timeout;
} callback_item_t;

static callback_item_t callback_queue[CALLBACK_QUEUE_SIZE];
static int callback_queue_head = 0;
static int callback_queue_tail = 0;

// Queue put scheduling for Peter Hinch queue integration
#define QUEUE_PUT_QUEUE_SIZE 16
#define MAX_QUEUED_EVENTS 16
#define MAX_QUEUE_PUT_RETRIES 10

typedef struct {
    mp_obj_t queue_obj;      // Peter Hinch Queue object
    mp_obj_t event_obj;      // Event object to put in queue
    uint8_t retry_count;     // Number of failed attempts
} queue_put_item_t;

static queue_put_item_t queue_put_queue[QUEUE_PUT_QUEUE_SIZE];
static int queue_put_head = 0;
static int queue_put_tail = 0;

// GC protection: keep event objects alive until queued
static mp_obj_t queued_events[MAX_QUEUED_EVENTS];
static int queued_events_count = 0;

// Event object type
typedef struct {
    mp_obj_base_t base;
    uint32_t sequence;
    mp_obj_t result;
    mp_obj_t error;
    bool ready;
    mp_obj_t queue_obj;  // Optional Peter Hinch queue
    // Store raw response for deferred conversion
    core1_response_t raw_response;
    bool has_raw_response;
} core1_event_obj_t;

// Forward declaration of event type
const mp_obj_type_t core1_event_type;

// Forward declarations
static mp_obj_t core1_init_mp(void);
static mp_obj_t core1_call_blocking(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args);
static void core1_process_queue_puts(void);

// Module initialization
static mp_obj_t core1_init_mp(void) {
    core1_init();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(core1_init_obj, core1_init_mp);

// Blocking call implementation
static mp_obj_t core1_call_blocking(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_cmd_id, ARG_timeout, ARG_data };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_cmd_id, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_timeout, MP_ARG_INT, {.u_int = 5000} },  // Default 5 second timeout
        { MP_QSTR_data, MP_ARG_OBJ, {.u_obj = mp_const_none} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // Extract arguments
    uint16_t cmd_id = args[ARG_cmd_id].u_int;
    uint32_t timeout_ms = args[ARG_timeout].u_int;
    mp_obj_t data_obj = args[ARG_data].u_obj;

    // Build command
    core1_command_t cmd = {
        .cmd_id = cmd_id,
        .sequence = core1_get_next_sequence(),
        .mode = CORE1_MODE_BLOCKING,
        .timeout_ms = timeout_ms,
        .callback_ref = NULL,
        .event_ref = NULL
    };

    // Marshall data into payload
    memset(cmd.payload, 0, CORE1_MAX_PAYLOAD_SIZE);
    if (data_obj != mp_const_none) {
        if (mp_obj_is_int(data_obj)) {
            int32_t val = mp_obj_get_int(data_obj);
            memcpy(cmd.payload, &val, sizeof(val));
        } else if (mp_obj_is_str(data_obj)) {
            const char* str = mp_obj_str_get_str(data_obj);
            strncpy((char*)cmd.payload, str, CORE1_MAX_PAYLOAD_SIZE - 1);
        } else if (mp_obj_is_type(data_obj, &mp_type_bytes)) {
            mp_buffer_info_t bufinfo;
            mp_get_buffer_raise(data_obj, &bufinfo, MP_BUFFER_READ);
            size_t copy_len = bufinfo.len < CORE1_MAX_PAYLOAD_SIZE ? bufinfo.len : CORE1_MAX_PAYLOAD_SIZE;
            memcpy(cmd.payload, bufinfo.buf, copy_len);
        }
    }

    // Register as pending (for timeout tracking)
    int pending_idx = core1_register_pending(cmd.sequence, CORE1_MODE_BLOCKING, NULL, NULL, timeout_ms);
    if (pending_idx < 0) {
        mp_raise_msg(&mp_type_Core1QueueFullError, MP_ERROR_TEXT("Too many pending commands"));
    }

    ESP_LOGI(TAG, "[BLOCKING] Registered seq=%" PRIu32 " in slot %d", cmd.sequence, pending_idx);

    // Send command
    core1_state_t* state = core1_get_state();
    if (xQueueSend(state->cmd_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        core1_clear_pending(cmd.sequence);
        mp_raise_msg(&mp_type_Core1QueueFullError, MP_ERROR_TEXT("Command queue full"));
    }

    ESP_LOGI(TAG, "[BLOCKING] Sent seq=%" PRIu32 ", waiting for response...", cmd.sequence);

    // Wait for response - release GIL during wait
    core1_response_t resp;
    bool response_received = false;
    
    MP_THREAD_GIL_EXIT();
    TickType_t wait_ticks = pdMS_TO_TICKS(timeout_ms);
    
    // Keep checking for our specific response
    TickType_t start_ticks = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start_ticks) < wait_ticks) {
        if (xQueueReceive(state->resp_queue, &resp, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (resp.sequence == cmd.sequence) {
                response_received = true;
                ESP_LOGI(TAG, "[BLOCKING] Got matching response for seq=%" PRIu32, cmd.sequence);
                break;
            } else {
                ESP_LOGD(TAG, "[BLOCKING] Got wrong seq=%" PRIu32 ", expected %" PRIu32 ", putting back", 
                         resp.sequence, cmd.sequence);
                // Wrong sequence, put it back for monitor task
                xQueueSendToFront(state->resp_queue, &resp, 0);
                vTaskDelay(pdMS_TO_TICKS(1)); // Brief delay to avoid tight loop
            }
        }
    }
    MP_THREAD_GIL_ENTER();

    // Clear pending
    core1_clear_pending(cmd.sequence);

    if (!response_received) {
        ESP_LOGW(TAG, "[BLOCKING] TIMEOUT waiting for seq=%" PRIu32, cmd.sequence);
        mp_raise_msg(&mp_type_Core1TimeoutError, MP_ERROR_TEXT("Command timed out"));
    }

    ESP_LOGI(TAG, "[BLOCKING] Completed seq=%" PRIu32 " with status=%d", cmd.sequence, resp.status);

    if (resp.status != CORE1_OK) {
        mp_raise_msg_varg(&mp_type_Core1Error, MP_ERROR_TEXT("Core1 error: %d"), resp.status);
    }

    // Convert response payload to Python object
    // For now, return as bytes
    return mp_obj_new_bytes(resp.payload, CORE1_MAX_PAYLOAD_SIZE);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(core1_call_blocking_obj, 0, core1_call_blocking);

// Schedule callback from C (called from monitor task - no Python objects!)
void core1_schedule_callback(void* callback_ref, core1_response_t* resp) {
    mp_obj_t callback = (mp_obj_t)callback_ref;
    
    // Add to queue - store raw response data
    int next = (callback_queue_tail + 1) % CALLBACK_QUEUE_SIZE;
    if (next != callback_queue_head) {
        callback_queue[callback_queue_tail].callback = callback;
        callback_queue[callback_queue_tail].response = *resp;  // Copy response data
        callback_queue[callback_queue_tail].is_timeout = false;
        callback_queue_tail = next;
    }
}

void core1_schedule_callback_timeout(void* callback_ref) {
    mp_obj_t callback = (mp_obj_t)callback_ref;
    
    int next = (callback_queue_tail + 1) % CALLBACK_QUEUE_SIZE;
    if (next != callback_queue_head) {
        callback_queue[callback_queue_tail].callback = callback;
        callback_queue[callback_queue_tail].is_timeout = true;
        callback_queue_tail = next;
    }
}

// Process pending callbacks (called from MicroPython)
static mp_obj_t core1_process_callbacks(void) {
    int processed = 0;
    
    ESP_LOGI(TAG, "[CALLBACK] process_callbacks called, head=%d tail=%d", 
             callback_queue_head, callback_queue_tail);
    
    while (callback_queue_head != callback_queue_tail) {
        callback_item_t* item = &callback_queue[callback_queue_head];
        
        ESP_LOGI(TAG, "[CALLBACK] Processing queued callback, is_timeout=%d", item->is_timeout);
        
        // Create Python objects NOW (in MicroPython thread context)
        mp_obj_t result;
        mp_obj_t error;
        
        if (item->is_timeout) {
            result = mp_const_none;
            error = mp_obj_new_int(CORE1_ERROR_TIMEOUT);
        } else {
            result = mp_obj_new_bytes(item->response.payload, CORE1_MAX_PAYLOAD_SIZE);
            error = (item->response.status == CORE1_OK) ? mp_const_none : mp_obj_new_int(item->response.status);
        }
        
        // Call callback
        mp_obj_t args[2] = { result, error };
        mp_call_function_n_kw(item->callback, 2, 0, args);
        
        callback_queue_head = (callback_queue_head + 1) % CALLBACK_QUEUE_SIZE;
        processed++;
    }
    
    ESP_LOGI(TAG, "[CALLBACK] Processed %d callbacks", processed);
    
    // Process Peter Hinch queue puts
    core1_process_queue_puts();
    
    return mp_obj_new_int(processed);
}

// Process pending queue puts (called from process_callbacks)
static void core1_process_queue_puts(void) {
    int items_to_process = (queue_put_tail - queue_put_head + QUEUE_PUT_QUEUE_SIZE) % QUEUE_PUT_QUEUE_SIZE;
    
    if (items_to_process == 0) {
        return;  // Nothing to process
    }
    
    ESP_LOGI(TAG, "[QUEUE] Processing %d queued events", items_to_process);
    
    // Process each item (may skip failed items to try again later)
    for (int i = 0; i < items_to_process; i++) {
        queue_put_item_t* item = &queue_put_queue[queue_put_head];
        bool success = false;
        
        // Try to put event in queue
        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            // Call queue.put_nowait(event)
            mp_obj_t args[1] = { item->event_obj };
            mp_obj_t put_method = mp_load_attr(item->queue_obj, MP_QSTR_put_nowait);
            mp_call_function_n_kw(put_method, 1, 0, args);
            
            success = true;
            nlr_pop();
            
            ESP_LOGI(TAG, "[QUEUE] Successfully put event in queue");
        } else {
            // Exception occurred (likely queue full)
            mp_obj_t exc = MP_OBJ_FROM_PTR(nlr.ret_val);
            
            item->retry_count++;
            
            if (item->retry_count >= MAX_QUEUE_PUT_RETRIES) {
                // Give up after max retries
                mp_printf(&mp_plat_print, 
                    "Warning: Failed to put event in queue after %d retries - result available via polling\n",
                    MAX_QUEUE_PUT_RETRIES);
                ESP_LOGW(TAG, "[QUEUE] Giving up on event after %d retries", MAX_QUEUE_PUT_RETRIES);
                success = true;  // Remove from queue (give up)
            } else {
                ESP_LOGD(TAG, "[QUEUE] Queue put failed (attempt %d/%d), will retry", 
                         item->retry_count, MAX_QUEUE_PUT_RETRIES);
            }
        }
        
        if (success) {
            // Remove from queue_put_queue
            queue_put_head = (queue_put_head + 1) % QUEUE_PUT_QUEUE_SIZE;
            
            // Remove from GC protection list
            for (int j = 0; j < queued_events_count; j++) {
                if (queued_events[j] == item->event_obj) {
                    // Replace with last item and decrement count
                    queued_events[j] = queued_events[--queued_events_count];
                    ESP_LOGD(TAG, "[QUEUE] Removed event from GC protection, %d remaining", 
                             queued_events_count);
                    break;
                }
            }
        } else {
            // Move to next item (will retry this one next time)
            queue_put_head = (queue_put_head + 1) % QUEUE_PUT_QUEUE_SIZE;
            // Put it back at the end
            int next_tail = (queue_put_tail + 1) % QUEUE_PUT_QUEUE_SIZE;
            if (next_tail != queue_put_head) {
                queue_put_queue[queue_put_tail] = *item;
                queue_put_tail = next_tail;
            }
        }
    }
}
static MP_DEFINE_CONST_FUN_OBJ_0(core1_process_callbacks_obj, core1_process_callbacks);

// Async call with callback
static mp_obj_t core1_call_async(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_cmd_id, ARG_callback, ARG_timeout, ARG_data };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_cmd_id, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_callback, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_timeout, MP_ARG_INT, {.u_int = 5000} },
        { MP_QSTR_data, MP_ARG_OBJ, {.u_obj = mp_const_none} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    uint16_t cmd_id = args[ARG_cmd_id].u_int;
    mp_obj_t callback = args[ARG_callback].u_obj;
    uint32_t timeout_ms = args[ARG_timeout].u_int;
    mp_obj_t data_obj = args[ARG_data].u_obj;

    // Verify callback is callable
    if (!mp_obj_is_callable(callback)) {
        mp_raise_TypeError(MP_ERROR_TEXT("callback must be callable"));
    }

    // Build command
    core1_command_t cmd = {
        .cmd_id = cmd_id,
        .sequence = core1_get_next_sequence(),
        .mode = CORE1_MODE_CALLBACK,
        .timeout_ms = timeout_ms,
        .callback_ref = callback,
        .event_ref = NULL
    };

    // Marshall data
    memset(cmd.payload, 0, CORE1_MAX_PAYLOAD_SIZE);
    if (data_obj != mp_const_none) {
        if (mp_obj_is_int(data_obj)) {
            int32_t val = mp_obj_get_int(data_obj);
            memcpy(cmd.payload, &val, sizeof(val));
        } else if (mp_obj_is_str(data_obj)) {
            const char* str = mp_obj_str_get_str(data_obj);
            strncpy((char*)cmd.payload, str, CORE1_MAX_PAYLOAD_SIZE - 1);
        } else if (mp_obj_is_type(data_obj, &mp_type_bytes)) {
            mp_buffer_info_t bufinfo;
            mp_get_buffer_raise(data_obj, &bufinfo, MP_BUFFER_READ);
            size_t copy_len = bufinfo.len < CORE1_MAX_PAYLOAD_SIZE ? bufinfo.len : CORE1_MAX_PAYLOAD_SIZE;
            memcpy(cmd.payload, bufinfo.buf, copy_len);
        }
    }

    // Register as pending
    int pending_idx = core1_register_pending(cmd.sequence, CORE1_MODE_CALLBACK, 
                                             (void*)callback, NULL, timeout_ms);
    if (pending_idx < 0) {
        mp_raise_msg(&mp_type_Core1QueueFullError, MP_ERROR_TEXT("Too many pending commands"));
    }

    ESP_LOGI(TAG, "[CALLBACK] Registered seq=%" PRIu32 " in slot %d", cmd.sequence, pending_idx);

    // Send command
    core1_state_t* state = core1_get_state();
    if (xQueueSend(state->cmd_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        core1_clear_pending(cmd.sequence);
        mp_raise_msg(&mp_type_Core1QueueFullError, MP_ERROR_TEXT("Command queue full"));
    }

    ESP_LOGI(TAG, "[CALLBACK] Sent seq=%" PRIu32, cmd.sequence);

    // Return sequence number
    return mp_obj_new_int(cmd.sequence);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(core1_call_async_obj, 0, core1_call_async);

// Start monitoring
static mp_obj_t core1_start_monitoring_mp(void) {
    core1_start_monitoring();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(core1_start_monitoring_obj, core1_start_monitoring_mp);

// Set log level (ESP_LOG_NONE=0, ERROR=1, WARN=2, INFO=3, DEBUG=4, VERBOSE=5)
static mp_obj_t core1_set_log_level_mp(mp_obj_t level_obj) {
    int level = mp_obj_get_int(level_obj);
    core1_set_log_level(level);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(core1_set_log_level_obj, core1_set_log_level_mp);

// Event object implementation

// Create new event object
static mp_obj_t core1_event_make_new(const mp_obj_type_t *type, size_t n_args, 
                                     size_t n_kw, const mp_obj_t *args) {
    core1_event_obj_t *self = m_new_obj(core1_event_obj_t);
    self->base.type = &core1_event_type;
    self->sequence = 0;
    self->result = mp_const_none;
    self->error = mp_const_none;
    self->ready = false;
    self->queue_obj = mp_const_none;
    self->has_raw_response = false;
    memset(&self->raw_response, 0, sizeof(self->raw_response));
    return MP_OBJ_FROM_PTR(self);
}

// Check if event is ready
static mp_obj_t core1_event_is_ready(mp_obj_t self_in) {
    core1_event_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_bool(self->ready);
}
static MP_DEFINE_CONST_FUN_OBJ_1(core1_event_is_ready_obj, core1_event_is_ready);

// Get result (blocking until ready)
static mp_obj_t core1_event_get_result(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_timeout };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_timeout, MP_ARG_INT, {.u_int = 0} },
    };
    
    // Parse args
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    
    core1_event_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    uint32_t timeout_ms = args[ARG_timeout].u_int;
    
    if (!timeout_ms) {
        // No timeout, return immediately
        if (!self->ready) {
            mp_raise_msg(&mp_type_Core1Error, MP_ERROR_TEXT("Result not ready"));
        }
    } else {
        // Wait with timeout
        uint32_t start = mp_hal_ticks_ms();
        while (!self->ready) {
            if (mp_hal_ticks_ms() - start > timeout_ms) {
                mp_raise_msg(&mp_type_Core1TimeoutError, MP_ERROR_TEXT("Timeout waiting for result"));
            }
            mp_hal_delay_ms(10);
        }
    }
    
    // Convert raw response to Python objects if not done yet
    if (self->has_raw_response) {
        self->result = mp_obj_new_bytes(self->raw_response.payload, CORE1_MAX_PAYLOAD_SIZE);
        self->error = (self->raw_response.status == CORE1_OK) ? mp_const_none : mp_obj_new_int(self->raw_response.status);
        self->has_raw_response = false;  // Mark as converted
    }
    
    if (self->error != mp_const_none) {
        mp_raise_msg_varg(&mp_type_Core1Error, MP_ERROR_TEXT("Core1 error: %d"), mp_obj_get_int(self->error));
    }
    
    return self->result;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(core1_event_get_result_obj, 1, core1_event_get_result);

// Get sequence number
static mp_obj_t core1_event_get_sequence(mp_obj_t self_in) {
    core1_event_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(self->sequence);
}
static MP_DEFINE_CONST_FUN_OBJ_1(core1_event_get_sequence_obj, core1_event_get_sequence);

// Event object locals dict
static const mp_rom_map_elem_t core1_event_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_is_ready), MP_ROM_PTR(&core1_event_is_ready_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_result), MP_ROM_PTR(&core1_event_get_result_obj) },
    { MP_ROM_QSTR(MP_QSTR_sequence), MP_ROM_PTR(&core1_event_get_sequence_obj) },
};
static MP_DEFINE_CONST_DICT(core1_event_locals_dict, core1_event_locals_dict_table);

// Event object type (using MicroPython v1.20+ slot-based format)
MP_DEFINE_CONST_OBJ_TYPE(
    core1_event_type,
    MP_QSTR_Event,
    MP_TYPE_FLAG_NONE,
    make_new, core1_event_make_new,
    locals_dict, &core1_event_locals_dict
);

// Signal event from C (called from monitor task - store raw data)
void core1_signal_event(void* event_ref, core1_response_t* resp) {
    core1_event_obj_t* event = (core1_event_obj_t*)event_ref;
    
    // Store raw response - will be converted when get_result() is called
    event->raw_response = *resp;
    event->has_raw_response = true;
    event->ready = true;
    
    // If a Peter Hinch queue was provided, schedule putting the event in it
    if (event->queue_obj != mp_const_none) {
        int next = (queue_put_tail + 1) % QUEUE_PUT_QUEUE_SIZE;
        if (next != queue_put_head) {
            mp_obj_t event_obj = MP_OBJ_FROM_PTR(event);
            
            // Add to GC protection list
            if (queued_events_count < MAX_QUEUED_EVENTS) {
                queued_events[queued_events_count++] = event_obj;
            } else {
                ESP_LOGW(TAG, "[QUEUE] GC protection list full, dropping oldest event");
                // Drop oldest to make room
                queued_events[0] = event_obj;
            }
            
            // Schedule queue put
            queue_put_queue[queue_put_tail].queue_obj = event->queue_obj;
            queue_put_queue[queue_put_tail].event_obj = event_obj;
            queue_put_queue[queue_put_tail].retry_count = 0;
            queue_put_tail = next;
            
            ESP_LOGI(TAG, "[QUEUE] Scheduled queue put for event seq=%" PRIu32, event->sequence);
        } else {
            ESP_LOGW(TAG, "[QUEUE] Queue put queue full for event seq=%" PRIu32, event->sequence);
        }
    }
}

void core1_signal_event_timeout(void* event_ref) {
    core1_event_obj_t* event = (core1_event_obj_t*)event_ref;
    
    // Store timeout status in raw response
    event->raw_response.status = CORE1_ERROR_TIMEOUT;
    event->has_raw_response = true;
    event->ready = true;
    
    // If a Peter Hinch queue was provided, schedule putting the event in it
    if (event->queue_obj != mp_const_none) {
        int next = (queue_put_tail + 1) % QUEUE_PUT_QUEUE_SIZE;
        if (next != queue_put_head) {
            mp_obj_t event_obj = MP_OBJ_FROM_PTR(event);
            
            // Add to GC protection list
            if (queued_events_count < MAX_QUEUED_EVENTS) {
                queued_events[queued_events_count++] = event_obj;
            }
            
            // Schedule queue put
            queue_put_queue[queue_put_tail].queue_obj = event->queue_obj;
            queue_put_queue[queue_put_tail].event_obj = event_obj;
            queue_put_queue[queue_put_tail].retry_count = 0;
            queue_put_tail = next;
            
            ESP_LOGI(TAG, "[QUEUE] Scheduled queue put for timed-out event");
        }
    }
}

// Call with event
static mp_obj_t core1_call_event(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_cmd_id, ARG_timeout, ARG_data, ARG_queue };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_cmd_id, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_timeout, MP_ARG_INT, {.u_int = 5000} },
        { MP_QSTR_data, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_queue, MP_ARG_OBJ, {.u_obj = mp_const_none} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // Create event object
    core1_event_obj_t *event = m_new_obj(core1_event_obj_t);
    event->base.type = &core1_event_type;
    event->result = mp_const_none;
    event->error = mp_const_none;
    event->ready = false;
    event->queue_obj = args[ARG_queue].u_obj;
    event->has_raw_response = false;
    memset(&event->raw_response, 0, sizeof(event->raw_response));

    // Build command
    core1_command_t cmd = {
        .cmd_id = args[ARG_cmd_id].u_int,
        .sequence = core1_get_next_sequence(),
        .mode = CORE1_MODE_EVENT,
        .timeout_ms = args[ARG_timeout].u_int,
        .callback_ref = NULL,
        .event_ref = event
    };
    
    event->sequence = cmd.sequence;

    // Marshall data
    memset(cmd.payload, 0, CORE1_MAX_PAYLOAD_SIZE);
    mp_obj_t data_obj = args[ARG_data].u_obj;
    if (data_obj != mp_const_none) {
        if (mp_obj_is_int(data_obj)) {
            int32_t val = mp_obj_get_int(data_obj);
            memcpy(cmd.payload, &val, sizeof(val));
        } else if (mp_obj_is_str(data_obj)) {
            const char* str = mp_obj_str_get_str(data_obj);
            strncpy((char*)cmd.payload, str, CORE1_MAX_PAYLOAD_SIZE - 1);
        } else if (mp_obj_is_type(data_obj, &mp_type_bytes)) {
            mp_buffer_info_t bufinfo;
            mp_get_buffer_raise(data_obj, &bufinfo, MP_BUFFER_READ);
            size_t copy_len = bufinfo.len < CORE1_MAX_PAYLOAD_SIZE ? bufinfo.len : CORE1_MAX_PAYLOAD_SIZE;
            memcpy(cmd.payload, bufinfo.buf, copy_len);
        }
    }

    // Register as pending
    int pending_idx = core1_register_pending(cmd.sequence, CORE1_MODE_EVENT, 
                                             NULL, (void*)event, cmd.timeout_ms);
    if (pending_idx < 0) {
        mp_raise_msg(&mp_type_Core1QueueFullError, MP_ERROR_TEXT("Too many pending commands"));
    }

    ESP_LOGI(TAG, "[EVENT] Registered seq=%" PRIu32 " in slot %d", cmd.sequence, pending_idx);

    // Send command
    core1_state_t* state = core1_get_state();
    if (xQueueSend(state->cmd_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        core1_clear_pending(cmd.sequence);
        mp_raise_msg(&mp_type_Core1QueueFullError, MP_ERROR_TEXT("Command queue full"));
    }

    ESP_LOGI(TAG, "[EVENT] Sent seq=%" PRIu32, cmd.sequence);

    return MP_OBJ_FROM_PTR(event);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(core1_call_event_obj, 0, core1_call_event);

// Module globals table
static const mp_rom_map_elem_t core1_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_core1) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&core1_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_call), MP_ROM_PTR(&core1_call_blocking_obj) },
    { MP_ROM_QSTR(MP_QSTR_call_async), MP_ROM_PTR(&core1_call_async_obj) },
    { MP_ROM_QSTR(MP_QSTR_call_event), MP_ROM_PTR(&core1_call_event_obj) },
    { MP_ROM_QSTR(MP_QSTR_start_monitoring), MP_ROM_PTR(&core1_start_monitoring_obj) },
    { MP_ROM_QSTR(MP_QSTR_process_callbacks), MP_ROM_PTR(&core1_process_callbacks_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_log_level), MP_ROM_PTR(&core1_set_log_level_obj) },
    
    // Exceptions
    { MP_ROM_QSTR(MP_QSTR_Core1Error), MP_ROM_PTR(&mp_type_Core1Error) },
    { MP_ROM_QSTR(MP_QSTR_Core1TimeoutError), MP_ROM_PTR(&mp_type_Core1TimeoutError) },
    { MP_ROM_QSTR(MP_QSTR_Core1QueueFullError), MP_ROM_PTR(&mp_type_Core1QueueFullError) },
    
    // Event type
    { MP_ROM_QSTR(MP_QSTR_Event), MP_ROM_PTR(&core1_event_type) },
    
    // Command IDs
    { MP_ROM_QSTR(MP_QSTR_CMD_ECHO), MP_ROM_INT(CMD_ECHO) },
    { MP_ROM_QSTR(MP_QSTR_CMD_ADD), MP_ROM_INT(CMD_ADD) },
    { MP_ROM_QSTR(MP_QSTR_CMD_DELAY), MP_ROM_INT(CMD_DELAY) },
    { MP_ROM_QSTR(MP_QSTR_CMD_STATUS), MP_ROM_INT(CMD_STATUS) },
};
static MP_DEFINE_CONST_DICT(core1_module_globals, core1_module_globals_table);

// Module definition (using new MicroPython v1.20+ format)
const mp_obj_module_t core1_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&core1_module_globals,
};

// Register module (MP_REGISTER_MODULE works with new format)
MP_REGISTER_MODULE(MP_QSTR_core1, core1_module);

