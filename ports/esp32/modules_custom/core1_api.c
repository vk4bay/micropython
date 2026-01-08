#include "core1_api.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>
#include <inttypes.h>

// Define TAG as macro if not already defined
// When included in modcore1.c, TAG will already be defined
#ifndef TAG
#define TAG "CORE1_API"
#endif

static core1_state_t g_core1_state = {0};

// Initialize the core1 system
void core1_init(void) {
    if (g_core1_state.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return;
    }

    ESP_LOGI(TAG, "[INIT] Initializing Core1 API...");
    
    // Initialize state flags
    g_core1_state.system_state = CORE1_STATE_INITIALIZED;
    g_core1_state.shutdown_requested = false;
    g_core1_state.monitor_stop_requested = false;
    g_core1_state.monitoring = false;
    g_core1_state.core1_task_exited = false;
    g_core1_state.monitor_task_exited = false;

    // Create queues
    g_core1_state.cmd_queue = xQueueCreate(CORE1_CMD_QUEUE_SIZE, sizeof(core1_command_t));
    g_core1_state.resp_queue = xQueueCreate(CORE1_RESP_QUEUE_SIZE, sizeof(core1_response_t));
    
    if (!g_core1_state.cmd_queue || !g_core1_state.resp_queue) {
        ESP_LOGE(TAG, "[INIT] Failed to create queues");
        g_core1_state.system_state = CORE1_STATE_ERROR;
        return;
    }

    // Initialize pending array
    memset(g_core1_state.pending, 0, sizeof(g_core1_state.pending));
    g_core1_state.sequence_counter = 1;

    // Create Core 1 task pinned to Core 1
    BaseType_t ret = xTaskCreatePinnedToCore(
        core1_task_main,
        "core1_task",
        CORE1_TASK_STACK_SIZE,
        NULL,
        CORE1_TASK_PRIORITY,
        &g_core1_state.core1_task,
        1  // Pin to Core 1
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "[INIT] Failed to create Core 1 task");
        g_core1_state.system_state = CORE1_STATE_ERROR;
        return;
    }

    g_core1_state.initialized = true;
    ESP_LOGI(TAG, "[INIT] Core1 API initialized successfully");
}

// Get next sequence number (atomic)
uint32_t core1_get_next_sequence(void) {
    // Simple increment, wraps naturally at uint32 max
    return __atomic_fetch_add(&g_core1_state.sequence_counter, 1, __ATOMIC_SEQ_CST);
}

// Hash function for sequence number to pending array index
static inline int core1_hash_sequence(uint32_t seq) {
    return seq % CORE1_MAX_PENDING;
}

// Register a pending command
int core1_register_pending(uint32_t seq, core1_response_mode_t mode,
                          void* callback_ref, void* event_ref, uint32_t timeout_ms) {
    // Hash-based insertion with linear probing
    int idx = core1_hash_sequence(seq);
    int start_idx = idx;
    
    do {
        if (!g_core1_state.pending[idx].active) {
            g_core1_state.pending[idx].sequence = seq;
            g_core1_state.pending[idx].mode = mode;
            g_core1_state.pending[idx].callback_ref = callback_ref;
            g_core1_state.pending[idx].event_ref = event_ref;
            
            // Calculate absolute deadline
            if (timeout_ms > 0) {
                g_core1_state.pending[idx].deadline_us = esp_timer_get_time() + (timeout_ms * 1000ULL);
            } else {
                g_core1_state.pending[idx].deadline_us = UINT64_MAX;  // No timeout
            }
            
            g_core1_state.pending[idx].active = true;
            return idx;
        }
        
        // Linear probing: move to next slot
        idx = (idx + 1) % CORE1_MAX_PENDING;
    } while (idx != start_idx);
    
    ESP_LOGW(TAG, "No free pending slots");
    return -1;
}

// Clear a pending command
void core1_clear_pending(uint32_t seq) {
    // Hash-based lookup with linear probing
    int idx = core1_hash_sequence(seq);
    int start_idx = idx;
    
    do {
        if (g_core1_state.pending[idx].active) {
            if (g_core1_state.pending[idx].sequence == seq) {
                g_core1_state.pending[idx].active = false;
                g_core1_state.pending[idx].callback_ref = NULL;
                g_core1_state.pending[idx].event_ref = NULL;
                return;
            }
        } else {
            // Empty slot means sequence doesn't exist (stop probing)
            return;
        }
        
        // Linear probing: move to next slot
        idx = (idx + 1) % CORE1_MAX_PENDING;
    } while (idx != start_idx);
}

// Find a pending command by sequence
pending_command_t* core1_find_pending(uint32_t seq) {
    // Hash-based lookup with linear probing
    int idx = core1_hash_sequence(seq);
    int start_idx = idx;
    
    do {
        if (g_core1_state.pending[idx].active) {
            if (g_core1_state.pending[idx].sequence == seq) {
                return &g_core1_state.pending[idx];
            }
        } else {
            // Empty slot means sequence doesn't exist (stop probing)
            return NULL;
        }
        
        // Linear probing: move to next slot
        idx = (idx + 1) % CORE1_MAX_PENDING;
    } while (idx != start_idx);
    
    return NULL;
}




// Core 1 task main loop
void core1_task_main(void *pvParameters) {
    ESP_LOGI(TAG, "[CORE1] Task started on core %d", xPortGetCoreID());
    
    core1_command_t cmd;
    core1_response_t resp;
    
    while (1) {
        // Check if shutdown requested
        if (g_core1_state.shutdown_requested) {
            ESP_LOGI(TAG, "[CORE1] Shutdown requested, exiting task");
            break;
        }
        
        // Wait for command (with timeout to check shutdown flag)
        if (xQueueReceive(g_core1_state.cmd_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
            ESP_LOGI(TAG, "[CORE1] Received command: id=0x%04x, seq=%" PRIu32 ", mode=%d", 
                     cmd.cmd_id, cmd.sequence, cmd.mode);
            
            // Prepare response
            resp.sequence = cmd.sequence;
            resp.status = CORE1_OK;
            memset(resp.payload, 0, sizeof(resp.payload));
            
            // Dispatch command
            switch (cmd.cmd_id) {
                case CMD_ECHO: {
                    // Echo back the payload
                    memcpy(resp.payload, cmd.payload, CORE1_MAX_PAYLOAD_SIZE);
                    break;
                }
                
                case CMD_ADD: {
                    // Add two integers
                    int32_t a = *(int32_t*)&cmd.payload[0];
                    int32_t b = *(int32_t*)&cmd.payload[4];
                    int32_t result = a + b;
                    memcpy(resp.payload, &result, sizeof(result));
                    break;
                }
                
                case CMD_DELAY: {
                    // Delay for specified milliseconds
                    uint32_t delay_ms = *(uint32_t*)&cmd.payload[0];
                    uint32_t timeout_ms = cmd.timeout_ms;
                    
                    // Use the smaller of requested delay and timeout
                    // This prevents Core 1 from blocking longer than the timeout
                    uint32_t actual_delay = delay_ms;
                    if (timeout_ms > 0 && timeout_ms < delay_ms) {
                        actual_delay = timeout_ms;
                        ESP_LOGW(TAG, "[CORE1] Delay shortened from %" PRIu32 " to %" PRIu32 " (timeout)", 
                                 delay_ms, actual_delay);
                    }
                    
                    vTaskDelay(pdMS_TO_TICKS(actual_delay));
                    
                    // If we had to shorten the delay, indicate timeout
                    if (actual_delay < delay_ms) {
                        resp.status = CORE1_ERROR_TIMEOUT;
                    }
                    break;
                }
                
                case CMD_STATUS: {
                    // Return status information
                    uint32_t free_heap = esp_get_free_heap_size();
                    memcpy(resp.payload, &free_heap, sizeof(free_heap));
                    break;
                }
                
                default:
                    ESP_LOGW(TAG, "Unknown command: 0x%04x", cmd.cmd_id);
                    resp.status = CORE1_ERROR_INVALID_CMD;
                    break;
            }
            
            // Send response
            ESP_LOGI(TAG, "[CORE1] Sending response for seq=%" PRIu32 " with status=%d", 
                     resp.sequence, resp.status);
            if (xQueueSend(g_core1_state.resp_queue, &resp, pdMS_TO_TICKS(100)) != pdTRUE) {
                ESP_LOGE(TAG, "[CORE1] Failed to send response for seq=%" PRIu32, resp.sequence);
            } else {
                ESP_LOGI(TAG, "[CORE1] Response sent successfully for seq=%" PRIu32, resp.sequence);
            }
        }
    }
    
    ESP_LOGI(TAG, "[CORE1] Task exiting cleanly");
    g_core1_state.core1_task_exited = true;
    // Just return - FreeRTOS will clean up the task
}

// Response monitor task (runs on Core 0)
void core1_monitor_task(void *pvParameters) {
    ESP_LOGI(TAG, "[MONITOR] Task started on core %d", xPortGetCoreID());
    
    core1_response_t resp;
    core1_state_t* state = core1_get_state();
    
    while (1) {
        // Check if stop requested
        if (state->monitor_stop_requested) {
            ESP_LOGI(TAG, "[MONITOR] Stop requested, exiting task");
            break;
        }
        
        // Check for responses
        if (xQueueReceive(state->resp_queue, &resp, pdMS_TO_TICKS(10)) == pdTRUE) {
            ESP_LOGI(TAG, "[MONITOR] Received response for seq=%" PRIu32, resp.sequence);
            
            // Find pending command
            pending_command_t* pending = core1_find_pending(resp.sequence);
            if (pending) {
                ESP_LOGI(TAG, "[MONITOR] Found pending seq=%" PRIu32 " with mode=%d", 
                         resp.sequence, pending->mode);
                
                if (pending->mode == CORE1_MODE_CALLBACK && pending->callback_ref) {
                    ESP_LOGI(TAG, "[MONITOR] Scheduling callback for seq=%" PRIu32, resp.sequence);
                    // Schedule callback execution
                    core1_schedule_callback(pending->callback_ref, &resp);
                    core1_clear_pending(resp.sequence);
                } else if (pending->mode == CORE1_MODE_EVENT && pending->event_ref) {
                    ESP_LOGI(TAG, "[MONITOR] Signaling event for seq=%" PRIu32, resp.sequence);
                    // Signal event/queue
                    core1_signal_event(pending->event_ref, &resp);
                    core1_clear_pending(resp.sequence);
                } else if (pending->mode == CORE1_MODE_BLOCKING) {
                    ESP_LOGI(TAG, "[MONITOR] Putting BLOCKING seq=%" PRIu32 " back in queue", resp.sequence);
                    // Put back in queue for blocking call to receive
                    xQueueSendToFront(state->resp_queue, &resp, 0);
                }
            } else {
                ESP_LOGW(TAG, "[MONITOR] No pending command found for seq=%" PRIu32, resp.sequence);
            }
        }
        
        // Check for timeouts
        uint64_t now = esp_timer_get_time();
        for (int i = 0; i < CORE1_MAX_PENDING; i++) {
            if (state->pending[i].active && now >= state->pending[i].deadline_us) {
                ESP_LOGW(TAG, "[MONITOR] Command seq=%" PRIu32 " timed out", state->pending[i].sequence);
                
                if (state->pending[i].mode == CORE1_MODE_CALLBACK && state->pending[i].callback_ref) {
                    core1_schedule_callback_timeout(state->pending[i].callback_ref);
                } else if (state->pending[i].mode == CORE1_MODE_EVENT && state->pending[i].event_ref) {
                    core1_signal_event_timeout(state->pending[i].event_ref);
                }
                
                core1_clear_pending(state->pending[i].sequence);
            }
        }
    }
    
    ESP_LOGI(TAG, "[MONITOR] Task exiting cleanly");
    g_core1_state.monitor_task_exited = true;
    // Just return - FreeRTOS will clean up the task
}

// Start monitoring
void core1_start_monitoring(void) {
    core1_state_t* state = core1_get_state();
    if (state->monitoring) {
        return;
    }
    
    BaseType_t ret = xTaskCreatePinnedToCore(
        core1_monitor_task,
        "core1_monitor",
        3072,
        NULL,
        4,  // Lower priority than main task
        &state->monitor_task,
        0  // Pin to Core 0
    );
    
    if (ret == pdPASS) {
        state->monitoring = true;
        ESP_LOGI(TAG, "Monitoring started");
    }
}

// Getter for global state (for MicroPython access)
core1_state_t* core1_get_state(void) {
    return &g_core1_state;
}

// Set log level for debugging
void core1_set_log_level(int level) {
    esp_log_level_set("CORE1_API", level);
    esp_log_level_set("MODCORE1", level);
    ESP_LOGI(TAG, "Log level set to %d", level);
}

// Stop monitoring task
void core1_stop_monitoring(uint32_t timeout_ms) {
    ESP_LOGI(TAG, "[SHUTDOWN] Stopping monitor task...");
    
    if (!g_core1_state.monitoring || !g_core1_state.monitor_task) {
        ESP_LOGW(TAG, "[SHUTDOWN] Monitor task not running");
        return;
    }
    
    // Set stop flag
    g_core1_state.monitor_stop_requested = true;
    g_core1_state.monitor_task_exited = false;
    
    // Wait for task to exit
    uint32_t elapsed = 0;
    const uint32_t check_interval = 50;
    
    while (elapsed < timeout_ms) {
        // Check if task has set the exit flag
        if (g_core1_state.monitor_task_exited) {
            ESP_LOGI(TAG, "[SHUTDOWN] Monitor task exited naturally");
            // Small delay to ensure FreeRTOS has cleaned up the task
            vTaskDelay(pdMS_TO_TICKS(50));
            break;
        }
        
        vTaskDelay(pdMS_TO_TICKS(check_interval));
        elapsed += check_interval;
    }
    
    // Force delete if still running
    if (!g_core1_state.monitor_task_exited) {
        ESP_LOGW(TAG, "[SHUTDOWN] Monitor task did not exit, force deleting");
        vTaskDelete(g_core1_state.monitor_task);
    }
    
    g_core1_state.monitor_task = NULL;
    g_core1_state.monitoring = false;
    g_core1_state.monitor_stop_requested = false;
    
    ESP_LOGI(TAG, "[SHUTDOWN] Monitor task stopped");
}

// Shutdown Core1 system
void core1_shutdown(uint32_t timeout_ms, bool force) {
    ESP_LOGI(TAG, "[SHUTDOWN] Starting shutdown (timeout=%" PRIu32 " ms, force=%d)", timeout_ms, force);
    
    if (!g_core1_state.initialized) {
        ESP_LOGW(TAG, "[SHUTDOWN] System not initialized");
        return;
    }
    
    if (g_core1_state.system_state == CORE1_STATE_SHUTTING_DOWN) {
        ESP_LOGW(TAG, "[SHUTDOWN] Shutdown already in progress");
        return;
    }
    
    g_core1_state.system_state = CORE1_STATE_SHUTTING_DOWN;
    g_core1_state.shutdown_requested = true;
    
    // Step 1: Stop monitor task first (so it stops consuming responses)
    if (g_core1_state.monitoring) {
        ESP_LOGI(TAG, "[SHUTDOWN] Step 1: Stopping monitor task");
        core1_stop_monitoring(timeout_ms / 2);  // Use half timeout for monitor
    }
    
    // Step 2: Stop Core 1 task
    ESP_LOGI(TAG, "[SHUTDOWN] Step 2: Stopping Core 1 task");
    
    if (g_core1_state.core1_task) {
        if (force) {
            ESP_LOGW(TAG, "[SHUTDOWN] Force mode: Deleting Core 1 task immediately");
            vTaskDelete(g_core1_state.core1_task);
        } else {
            // Graceful: wait for task to exit
            g_core1_state.core1_task_exited = false;
            uint32_t elapsed = 0;
            const uint32_t check_interval = 50;
            
            while (elapsed < timeout_ms) {
                if (g_core1_state.core1_task_exited) {
                    ESP_LOGI(TAG, "[SHUTDOWN] Core 1 task exited naturally");
                    // Small delay to ensure FreeRTOS has cleaned up the task
                    vTaskDelay(pdMS_TO_TICKS(100));
                    break;
                }
                
                vTaskDelay(pdMS_TO_TICKS(check_interval));
                elapsed += check_interval;
            }
            
            // Force delete if still running
            if (!g_core1_state.core1_task_exited) {
                ESP_LOGW(TAG, "[SHUTDOWN] Core 1 task did not exit, force deleting");
                vTaskDelete(g_core1_state.core1_task);
            }
        }
        
        g_core1_state.core1_task = NULL;
    }
    
    // Step 3: Clean up queues
    ESP_LOGI(TAG, "[SHUTDOWN] Step 3: Cleaning up queues");
    
    if (g_core1_state.cmd_queue) {
        // Drain command queue
        core1_command_t cmd;
        int drained = 0;
        while (xQueueReceive(g_core1_state.cmd_queue, &cmd, 0) == pdTRUE) {
            drained++;
        }
        if (drained > 0) {
            ESP_LOGW(TAG, "[SHUTDOWN] Drained %d commands from command queue", drained);
        }
        
        vQueueDelete(g_core1_state.cmd_queue);
        g_core1_state.cmd_queue = NULL;
    }
    
    if (g_core1_state.resp_queue) {
        // Drain response queue
        core1_response_t resp;
        int drained = 0;
        while (xQueueReceive(g_core1_state.resp_queue, &resp, 0) == pdTRUE) {
            drained++;
        }
        if (drained > 0) {
            ESP_LOGW(TAG, "[SHUTDOWN] Drained %d responses from response queue", drained);
        }
        
        vQueueDelete(g_core1_state.resp_queue);
        g_core1_state.resp_queue = NULL;
    }
    
    // Step 4: Clear pending commands
    ESP_LOGI(TAG, "[SHUTDOWN] Step 4: Clearing pending commands");
    int pending_count = 0;
    for (int i = 0; i < CORE1_MAX_PENDING; i++) {
        if (g_core1_state.pending[i].active) {
            pending_count++;
            g_core1_state.pending[i].active = false;
            g_core1_state.pending[i].callback_ref = NULL;
            g_core1_state.pending[i].event_ref = NULL;
        }
    }
    if (pending_count > 0) {
        ESP_LOGW(TAG, "[SHUTDOWN] Cleared %d pending commands", pending_count);
    }
    
    // Step 5: Reset state
    ESP_LOGI(TAG, "[SHUTDOWN] Step 5: Resetting state");
    g_core1_state.sequence_counter = 1;
    g_core1_state.initialized = false;
    g_core1_state.shutdown_requested = false;
    g_core1_state.system_state = CORE1_STATE_UNINITIALIZED;
    
    ESP_LOGI(TAG, "[SHUTDOWN] Shutdown complete");
}

// Get system state
core1_system_state_t core1_get_system_state(void) {
    return g_core1_state.system_state;
}

// Check if initialized
bool core1_is_initialized(void) {
    return g_core1_state.initialized;
}

