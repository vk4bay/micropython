#include "py/obj.h"
#include "py/runtime.h"
#include "py/mphal.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "FT6336";

// FT6336 I2C Address and Registers
#define FT6336_ADDR           0x38
#define FT6336_REG_NUM_TOUCHES 0x02
#define FT6336_REG_TOUCH1_XH  0x03
#define FT6336_REG_TOUCH1_XL  0x04
#define FT6336_REG_TOUCH1_YH  0x05
#define FT6336_REG_TOUCH1_YL  0x06
#define FT6336_REG_CHIPID     0xA3
#define FT6336_REG_FIRMID     0xA6

// Orientation definitions (must match ili9488.c)
#define ORIENTATION_PORTRAIT       0  // 0 degrees (320x480)
#define ORIENTATION_LANDSCAPE      1  // 90 degrees (480x320)
#define ORIENTATION_PORTRAIT_INV   2  // 180 degrees (320x480)
#define ORIENTATION_LANDSCAPE_INV  3  // 270 degrees (480x320)

// Physical touch panel dimensions (matches ILI9488 physical dimensions)
#define TOUCH_PHYS_WIDTH  320
#define TOUCH_PHYS_HEIGHT 480

// Swipe detection parameters
#define SWIPE_THRESHOLD 50      // Minimum distance to consider a swipe
#define EDGE_THRESHOLD 30       // Maximum distance from edge to start swipe

// Global I2C configuration
static i2c_port_t i2c_port = I2C_NUM_0;
static bool initialized = false;

// Orientation support (set once at init)
static uint8_t current_orientation = ORIENTATION_PORTRAIT;
static int display_width = TOUCH_PHYS_WIDTH;
static int display_height = TOUCH_PHYS_HEIGHT;

// Swipe tracking
static bool swipe_tracking = false;
static int swipe_start_x = 0;
static int swipe_start_y = 0;

// Interrupt support
static int int_pin = -1;
static volatile bool touch_event_flag = false;
static SemaphoreHandle_t touch_semaphore = NULL;
static mp_obj_t touch_callback = mp_const_none;

// ISR handler for touch interrupt
static void IRAM_ATTR touch_isr_handler(void *arg) {
    touch_event_flag = true;
    
    // Signal semaphore for blocking wait
    if (touch_semaphore != NULL) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(touch_semaphore, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    }
}

// Read register with error handling
static esp_err_t ft6336_read_reg(uint8_t reg, uint8_t *data) {
    if (!initialized) {
        return ESP_FAIL;
    }
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        return ESP_FAIL;
    }
    
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (FT6336_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (FT6336_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, data, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    
    return ret;
}

// Transform touch coordinates based on orientation
static void transform_touch_coordinates(int raw_x, int raw_y, int *out_x, int *out_y) {
    // Raw coordinates are in physical panel coordinates (0-319 X, 0-479 Y)
    // We need to transform them to logical coordinates based on orientation
    
    switch (current_orientation) {
        case ORIENTATION_PORTRAIT:
            // 0 degrees: No transformation needed
            // Logical: 320x480, same as physical
            *out_x = raw_x;
            *out_y = raw_y;
            break;
            
        case ORIENTATION_LANDSCAPE:
            // 90 degrees clockwise
            // Logical: 480x320
            // X_logical = Y_physical
            // Y_logical = (WIDTH_physical - 1) - X_physical
            *out_x = raw_y;
            *out_y = (TOUCH_PHYS_WIDTH - 1) - raw_x;
            break;
            
        case ORIENTATION_PORTRAIT_INV:
            // 180 degrees
            // Logical: 320x480
            // X_logical = (WIDTH_physical - 1) - X_physical
            // Y_logical = (HEIGHT_physical - 1) - Y_physical
            *out_x = (TOUCH_PHYS_WIDTH - 1) - raw_x;
            *out_y = (TOUCH_PHYS_HEIGHT - 1) - raw_y;
            break;
            
        case ORIENTATION_LANDSCAPE_INV:
            // 270 degrees clockwise (90 counter-clockwise)
            // Logical: 480x320
            // X_logical = (HEIGHT_physical - 1) - Y_physical
            // Y_logical = X_physical
            *out_x = (TOUCH_PHYS_HEIGHT - 1) - raw_y;
            *out_y = raw_x;
            break;
            
        default:
            // Default to portrait (no transformation)
            *out_x = raw_x;
            *out_y = raw_y;
            break;
    }
}

// Deinitialize touch controller
static mp_obj_t ft6336_deinit(void) {
    if (initialized) {
        ESP_LOGI(TAG, "Deinitializing FT6336...");
        
        // Remove interrupt handler if configured
        if (int_pin >= 0) {
            gpio_isr_handler_remove(int_pin);
            gpio_reset_pin(int_pin);
            int_pin = -1;
        }
        
        // Delete semaphore if created
        if (touch_semaphore != NULL) {
            vSemaphoreDelete(touch_semaphore);
            touch_semaphore = NULL;
        }
        
        esp_err_t ret = i2c_driver_delete(i2c_port);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "I2C driver removed successfully");
            initialized = false;
        } else {
            ESP_LOGW(TAG, "Failed to remove I2C driver: %d", ret);
            initialized = false;
        }
        
        touch_callback = mp_const_none;
        current_orientation = ORIENTATION_PORTRAIT;
        display_width = TOUCH_PHYS_WIDTH;
        display_height = TOUCH_PHYS_HEIGHT;
        swipe_tracking = false;
    }
    
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(ft6336_deinit_obj, ft6336_deinit);

// Initialize touch controller with optional orientation
static mp_obj_t ft6336_init(size_t n_args, const mp_obj_t *args) {
    // args: i2c_port, sda_pin, scl_pin, [freq], [orientation]
    i2c_port = mp_obj_get_int(args[0]);
    int sda_pin = mp_obj_get_int(args[1]);
    int scl_pin = mp_obj_get_int(args[2]);
    int freq = (n_args > 3) ? mp_obj_get_int(args[3]) : 100000;
    
    // Optional orientation parameter (default to portrait)
    if (n_args >= 5) {
        current_orientation = mp_obj_get_int(args[4]);
        if (current_orientation > ORIENTATION_LANDSCAPE_INV) {
            current_orientation = ORIENTATION_PORTRAIT;
        }
    } else {
        current_orientation = ORIENTATION_PORTRAIT;
    }
    
    // Set logical display dimensions based on orientation
    if (current_orientation == ORIENTATION_LANDSCAPE || 
        current_orientation == ORIENTATION_LANDSCAPE_INV) {
        display_width = TOUCH_PHYS_HEIGHT;   // 480
        display_height = TOUCH_PHYS_WIDTH;   // 320
    } else {
        display_width = TOUCH_PHYS_WIDTH;    // 320
        display_height = TOUCH_PHYS_HEIGHT;  // 480
    }
    
    ESP_LOGI(TAG, "Initializing FT6336: I2C%d, SDA=%d, SCL=%d, freq=%d, orientation=%d (%dx%d)", 
             i2c_port, sda_pin, scl_pin, freq, current_orientation, display_width, display_height);
    
    // If already initialized, deinitialize first
    if (initialized) {
        ESP_LOGW(TAG, "Already initialized, deinitializing first...");
        ft6336_deinit();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Try to delete driver in case it wasn't cleaned up properly
    i2c_driver_delete(i2c_port);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Configure I2C
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda_pin,
        .scl_io_num = scl_pin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = freq,
        .clk_flags = 0,
    };
    
    esp_err_t ret = i2c_param_config(i2c_port, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %d", ret);
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("I2C config failed"));
    }
    
    ret = i2c_driver_install(i2c_port, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %d", ret);
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("I2C driver install failed"));
    }
    
    initialized = true;
    swipe_tracking = false;
    
    // Verify chip ID
    uint8_t chip_id = 0;
    uint8_t firm_id = 0;
    
    ret = ft6336_read_reg(FT6336_REG_CHIPID, &chip_id);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read chip ID: %d", ret);
        chip_id = 0;
    }
    
    ret = ft6336_read_reg(FT6336_REG_FIRMID, &firm_id);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read firmware ID: %d", ret);
        firm_id = 0;
    }
    
    ESP_LOGI(TAG, "FT6336 initialized. Chip ID: 0x%02X, Firmware ID: 0x%02X", chip_id, firm_id);
    
    // Return tuple with chip info
    mp_obj_t tuple[2] = {
        mp_obj_new_int(chip_id),
        mp_obj_new_int(firm_id)
    };
    return mp_obj_new_tuple(2, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ft6336_init_obj, 3, 5, ft6336_init);

// Get current orientation
static mp_obj_t ft6336_get_orientation(void) {
    return mp_obj_new_int(current_orientation);
}
static MP_DEFINE_CONST_FUN_OBJ_0(ft6336_get_orientation_obj, ft6336_get_orientation);

// Initialize interrupt support
static mp_obj_t ft6336_init_interrupt(mp_obj_t int_pin_obj) {
    if (!initialized) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Touch not initialized"));
    }
    
    int_pin = mp_obj_get_int(int_pin_obj);
    
    ESP_LOGI(TAG, "Initializing interrupt on pin %d", int_pin);
    
    // Configure GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << int_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE  // FT6336 INT is active low
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %d", ret);
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("GPIO config failed"));
    }
    
    // Create semaphore if not already created
    if (touch_semaphore == NULL) {
        touch_semaphore = xSemaphoreCreateBinary();
        if (touch_semaphore == NULL) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Failed to create semaphore"));
        }
    }
    
    // Install ISR service if not already installed
    gpio_install_isr_service(0);
    
    // Add ISR handler
    ret = gpio_isr_handler_add(int_pin, touch_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ISR handler add failed: %d", ret);
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("ISR handler add failed"));
    }
    
    touch_event_flag = false;
    
    ESP_LOGI(TAG, "Interrupt initialized successfully");
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(ft6336_init_interrupt_obj, ft6336_init_interrupt);

// Check if touch event occurred (non-blocking)
static mp_obj_t ft6336_event_occurred(void) {
    bool event = touch_event_flag;
    touch_event_flag = false;  // Clear flag
    return mp_obj_new_bool(event);
}
static MP_DEFINE_CONST_FUN_OBJ_0(ft6336_event_occurred_obj, ft6336_event_occurred);

// Wait for touch event (blocking with timeout)
static mp_obj_t ft6336_wait_for_event(size_t n_args, const mp_obj_t *args) {
    if (!initialized || int_pin < 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Interrupt not initialized"));
    }
    
    int timeout_ms = (n_args > 0) ? mp_obj_get_int(args[0]) : -1;  // -1 = wait forever
    
    touch_event_flag = false;  // Clear flag before waiting
    
    TickType_t ticks_to_wait = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    
    // Wait for semaphore
    if (xSemaphoreTake(touch_semaphore, ticks_to_wait) == pdTRUE) {
        return mp_const_true;  // Event occurred
    } else {
        return mp_const_false; // Timeout
    }
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ft6336_wait_for_event_obj, 0, 1, ft6336_wait_for_event);

// Clear event flag
static mp_obj_t ft6336_clear_event(void) {
    touch_event_flag = false;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(ft6336_clear_event_obj, ft6336_clear_event);

// Check if initialized
static mp_obj_t ft6336_is_initialized(void) {
    return mp_obj_new_bool(initialized);
}
static MP_DEFINE_CONST_FUN_OBJ_0(ft6336_is_initialized_obj, ft6336_is_initialized);

// Check if interrupt is configured
static mp_obj_t ft6336_has_interrupt(void) {
    return mp_obj_new_bool(int_pin >= 0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(ft6336_has_interrupt_obj, ft6336_has_interrupt);

// Read touch data with coordinate transformation
static mp_obj_t ft6336_read_touch(void) {
    if (!initialized) {
        mp_obj_t tuple[3] = {
            mp_obj_new_bool(false),
            mp_obj_new_int(0),
            mp_obj_new_int(0)
        };
        return mp_obj_new_tuple(3, tuple);
    }
    
    // Read number of touches
    uint8_t num_touches = 0;
    esp_err_t ret = ft6336_read_reg(FT6336_REG_NUM_TOUCHES, &num_touches);
    
    if (ret != ESP_OK || num_touches == 0 || num_touches > 2) {
        // Touch released - reset swipe tracking
        swipe_tracking = false;
        
        mp_obj_t tuple[3] = {
            mp_obj_new_bool(false),
            mp_obj_new_int(0),
            mp_obj_new_int(0)
        };
        return mp_obj_new_tuple(3, tuple);
    }
    
    // Read touch coordinates
    uint8_t xh = 0, xl = 0, yh = 0, yl = 0;
    
    if (ft6336_read_reg(FT6336_REG_TOUCH1_XH, &xh) != ESP_OK ||
        ft6336_read_reg(FT6336_REG_TOUCH1_XL, &xl) != ESP_OK ||
        ft6336_read_reg(FT6336_REG_TOUCH1_YH, &yh) != ESP_OK ||
        ft6336_read_reg(FT6336_REG_TOUCH1_YL, &yl) != ESP_OK) {
        
        mp_obj_t tuple[3] = {
            mp_obj_new_bool(false),
            mp_obj_new_int(0),
            mp_obj_new_int(0)
        };
        return mp_obj_new_tuple(3, tuple);
    }
    
    // Combine bytes to get raw physical coordinates
    int raw_x = ((xh & 0x0F) << 8) | xl;
    int raw_y = ((yh & 0x0F) << 8) | yl;
    
    // Transform coordinates based on orientation
    int logical_x, logical_y;
    transform_touch_coordinates(raw_x, raw_y, &logical_x, &logical_y);
    
    // Track swipe start position
    if (!swipe_tracking) {
        swipe_start_x = logical_x;
        swipe_start_y = logical_y;
        swipe_tracking = true;
    }
    
    // Return tuple (touched, x, y) with transformed coordinates
    mp_obj_t tuple[3] = {
        mp_obj_new_bool(true),
        mp_obj_new_int(logical_x),
        mp_obj_new_int(logical_y)
    };
    return mp_obj_new_tuple(3, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_0(ft6336_read_touch_obj, ft6336_read_touch);

// Read raw touch data (untransformed physical coordinates)
static mp_obj_t ft6336_read_touch_raw(void) {
    if (!initialized) {
        mp_obj_t tuple[3] = {
            mp_obj_new_bool(false),
            mp_obj_new_int(0),
            mp_obj_new_int(0)
        };
        return mp_obj_new_tuple(3, tuple);
    }
    
    // Read number of touches
    uint8_t num_touches = 0;
    esp_err_t ret = ft6336_read_reg(FT6336_REG_NUM_TOUCHES, &num_touches);
    
    if (ret != ESP_OK || num_touches == 0 || num_touches > 2) {
        mp_obj_t tuple[3] = {
            mp_obj_new_bool(false),
            mp_obj_new_int(0),
            mp_obj_new_int(0)
        };
        return mp_obj_new_tuple(3, tuple);
    }
    
    // Read touch coordinates
    uint8_t xh = 0, xl = 0, yh = 0, yl = 0;
    
    if (ft6336_read_reg(FT6336_REG_TOUCH1_XH, &xh) != ESP_OK ||
        ft6336_read_reg(FT6336_REG_TOUCH1_XL, &xl) != ESP_OK ||
        ft6336_read_reg(FT6336_REG_TOUCH1_YH, &yh) != ESP_OK ||
        ft6336_read_reg(FT6336_REG_TOUCH1_YL, &yl) != ESP_OK) {
        
        mp_obj_t tuple[3] = {
            mp_obj_new_bool(false),
            mp_obj_new_int(0),
            mp_obj_new_int(0)
        };
        return mp_obj_new_tuple(3, tuple);
    }
    
    // Combine bytes to get raw physical coordinates (no transformation)
    int raw_x = ((xh & 0x0F) << 8) | xl;
    int raw_y = ((yh & 0x0F) << 8) | yl;
    
    // Return tuple (touched, x, y) with raw coordinates
    mp_obj_t tuple[3] = {
        mp_obj_new_bool(true),
        mp_obj_new_int(raw_x),
        mp_obj_new_int(raw_y)
    };
    return mp_obj_new_tuple(3, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_0(ft6336_read_touch_raw_obj, ft6336_read_touch_raw);

// Get number of touches
static mp_obj_t ft6336_get_touches(void) {
    if (!initialized) {
        return mp_obj_new_int(0);
    }
    
    uint8_t num_touches = 0;
    esp_err_t ret = ft6336_read_reg(FT6336_REG_NUM_TOUCHES, &num_touches);
    
    if (ret != ESP_OK) {
        return mp_obj_new_int(0);
    }
    
    return mp_obj_new_int(num_touches);
}
static MP_DEFINE_CONST_FUN_OBJ_0(ft6336_get_touches_obj, ft6336_get_touches);

// Helper function to read current touch without updating swipe tracking
static bool read_touch_internal(int *out_x, int *out_y) {
    if (!initialized) {
        return false;
    }
    
    // Read number of touches
    uint8_t num_touches = 0;
    esp_err_t ret = ft6336_read_reg(FT6336_REG_NUM_TOUCHES, &num_touches);
    
    if (ret != ESP_OK || num_touches == 0 || num_touches > 2) {
        return false;
    }
    
    // Read touch coordinates
    uint8_t xh = 0, xl = 0, yh = 0, yl = 0;
    
    if (ft6336_read_reg(FT6336_REG_TOUCH1_XH, &xh) != ESP_OK ||
        ft6336_read_reg(FT6336_REG_TOUCH1_XL, &xl) != ESP_OK ||
        ft6336_read_reg(FT6336_REG_TOUCH1_YH, &yh) != ESP_OK ||
        ft6336_read_reg(FT6336_REG_TOUCH1_YL, &yl) != ESP_OK) {
        return false;
    }
    
    // Combine bytes to get raw physical coordinates
    int raw_x = ((xh & 0x0F) << 8) | xl;
    int raw_y = ((yh & 0x0F) << 8) | yl;
    
    // Transform coordinates based on orientation
    transform_touch_coordinates(raw_x, raw_y, out_x, out_y);
    
    return true;
}

// Detect swipe from left edge
static mp_obj_t ft6336_swipe_from_left(void) {
    if (!initialized) {
        return mp_const_false;
    }
    
    int current_x, current_y;
    bool touched = read_touch_internal(&current_x, &current_y);
    
    if (!touched) {
        // Touch released - check if it was a valid swipe from left edge
        if (swipe_tracking && swipe_start_x <= EDGE_THRESHOLD) {
            swipe_tracking = false;
            return mp_const_true;
        }
        swipe_tracking = false;
        return mp_const_false;
    }
    
    // Start tracking on new touch
    if (!swipe_tracking) {
        swipe_start_x = current_x;
        swipe_start_y = current_y;
        swipe_tracking = true;
    }
    
    // Check if swipe started from left edge and moved right
    if (swipe_start_x <= EDGE_THRESHOLD && 
        (current_x - swipe_start_x) >= SWIPE_THRESHOLD) {
        swipe_tracking = false;  // Reset for next swipe
        return mp_const_true;
    }
    
    return mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_0(ft6336_swipe_from_left_obj, ft6336_swipe_from_left);

// Detect swipe from right edge
static mp_obj_t ft6336_swipe_from_right(void) {
    if (!initialized) {
        return mp_const_false;
    }
    
    int current_x, current_y;
    bool touched = read_touch_internal(&current_x, &current_y);
    
    if (!touched) {
        // Touch released - check if it was a valid swipe from right edge
        if (swipe_tracking && swipe_start_x >= (display_width - EDGE_THRESHOLD)) {
            swipe_tracking = false;
            return mp_const_true;
        }
        swipe_tracking = false;
        return mp_const_false;
    }
    
    // Start tracking on new touch
    if (!swipe_tracking) {
        swipe_start_x = current_x;
        swipe_start_y = current_y;
        swipe_tracking = true;
    }
    
    // Check if swipe started from right edge and moved left
    if (swipe_start_x >= (display_width - EDGE_THRESHOLD) && 
        (swipe_start_x - current_x) >= SWIPE_THRESHOLD) {
        swipe_tracking = false;  // Reset for next swipe
        return mp_const_true;
    }
    
    return mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_0(ft6336_swipe_from_right_obj, ft6336_swipe_from_right);

// Detect swipe from top edge
static mp_obj_t ft6336_swipe_from_top(void) {
    if (!initialized) {
        return mp_const_false;
    }
    
    int current_x, current_y;
    bool touched = read_touch_internal(&current_x, &current_y);
    
    if (!touched) {
        // Touch released - check if it was a valid swipe from top edge
        if (swipe_tracking && swipe_start_y <= EDGE_THRESHOLD) {
            swipe_tracking = false;
            return mp_const_true;
        }
        swipe_tracking = false;
        return mp_const_false;
    }
    
    // Start tracking on new touch
    if (!swipe_tracking) {
        swipe_start_x = current_x;
        swipe_start_y = current_y;
        swipe_tracking = true;
    }
    
    // Check if swipe started from top edge and moved down
    if (swipe_start_y <= EDGE_THRESHOLD && 
        (current_y - swipe_start_y) >= SWIPE_THRESHOLD) {
        swipe_tracking = false;  // Reset for next swipe
        return mp_const_true;
    }
    
    return mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_0(ft6336_swipe_from_top_obj, ft6336_swipe_from_top);

// Detect swipe from bottom edge
static mp_obj_t ft6336_swipe_from_bottom(void) {
    if (!initialized) {
        return mp_const_false;
    }
    
    int current_x, current_y;
    bool touched = read_touch_internal(&current_x, &current_y);
    
    if (!touched) {
        // Touch released - check if it was a valid swipe from bottom edge
        if (swipe_tracking && swipe_start_y >= (display_height - EDGE_THRESHOLD)) {
            swipe_tracking = false;
            return mp_const_true;
        }
        swipe_tracking = false;
        return mp_const_false;
    }
    
    // Start tracking on new touch
    if (!swipe_tracking) {
        swipe_start_x = current_x;
        swipe_start_y = current_y;
        swipe_tracking = true;
    }
    
    // Check if swipe started from bottom edge and moved up
    if (swipe_start_y >= (display_height - EDGE_THRESHOLD) && 
        (swipe_start_y - current_y) >= SWIPE_THRESHOLD) {
        swipe_tracking = false;  // Reset for next swipe
        return mp_const_true;
    }
    
    return mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_0(ft6336_swipe_from_bottom_obj, ft6336_swipe_from_bottom);

// Module globals
static const mp_rom_map_elem_t ft6336_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_ft6336) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&ft6336_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&ft6336_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_orientation), MP_ROM_PTR(&ft6336_get_orientation_obj) },
    { MP_ROM_QSTR(MP_QSTR_init_interrupt), MP_ROM_PTR(&ft6336_init_interrupt_obj) },
    { MP_ROM_QSTR(MP_QSTR_is_initialized), MP_ROM_PTR(&ft6336_is_initialized_obj) },
    { MP_ROM_QSTR(MP_QSTR_has_interrupt), MP_ROM_PTR(&ft6336_has_interrupt_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_touch), MP_ROM_PTR(&ft6336_read_touch_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_touch_raw), MP_ROM_PTR(&ft6336_read_touch_raw_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_touches), MP_ROM_PTR(&ft6336_get_touches_obj) },
    { MP_ROM_QSTR(MP_QSTR_event_occurred), MP_ROM_PTR(&ft6336_event_occurred_obj) },
    { MP_ROM_QSTR(MP_QSTR_wait_for_event), MP_ROM_PTR(&ft6336_wait_for_event_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear_event), MP_ROM_PTR(&ft6336_clear_event_obj) },
    { MP_ROM_QSTR(MP_QSTR_swipe_from_left), MP_ROM_PTR(&ft6336_swipe_from_left_obj) },
    { MP_ROM_QSTR(MP_QSTR_swipe_from_right), MP_ROM_PTR(&ft6336_swipe_from_right_obj) },
    { MP_ROM_QSTR(MP_QSTR_swipe_from_top), MP_ROM_PTR(&ft6336_swipe_from_top_obj) },
    { MP_ROM_QSTR(MP_QSTR_swipe_from_bottom), MP_ROM_PTR(&ft6336_swipe_from_bottom_obj) },
    { MP_ROM_QSTR(MP_QSTR_ORIENTATION_PORTRAIT), MP_ROM_INT(ORIENTATION_PORTRAIT) },
    { MP_ROM_QSTR(MP_QSTR_ORIENTATION_LANDSCAPE), MP_ROM_INT(ORIENTATION_LANDSCAPE) },
    { MP_ROM_QSTR(MP_QSTR_ORIENTATION_PORTRAIT_INV), MP_ROM_INT(ORIENTATION_PORTRAIT_INV) },
    { MP_ROM_QSTR(MP_QSTR_ORIENTATION_LANDSCAPE_INV), MP_ROM_INT(ORIENTATION_LANDSCAPE_INV) },
};
static MP_DEFINE_CONST_DICT(ft6336_module_globals, ft6336_module_globals_table);

const mp_obj_module_t ft6336_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&ft6336_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_ft6336, ft6336_module);

