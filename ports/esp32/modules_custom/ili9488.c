#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "py/obj.h"
#include "py/runtime.h"
#include "py/mphal.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"

static const char *TAG = "ILI9488";

// ILI9488 Commands
#define ILI9488_SWRESET 0x01
#define ILI9488_SLPOUT  0x11
#define ILI9488_DISPON  0x29
#define ILI9488_CASET   0x2A
#define ILI9488_PASET   0x2B
#define ILI9488_RAMWR   0x2C
#define ILI9488_MADCTL  0x36
#define ILI9488_PIXFMT  0x3A

// MADCTL bits
#define MADCTL_MY  0x80  // Row Address Order
#define MADCTL_MX  0x40  // Column Address Order
#define MADCTL_MV  0x20  // Row/Column Exchange
#define MADCTL_ML  0x10  // Vertical Refresh Order
#define MADCTL_BGR 0x08  // RGB-BGR Order
#define MADCTL_MH  0x04  // Horizontal Refresh Order

// Orientation definitions
#define ORIENTATION_PORTRAIT       0  // 0 degrees (320x480)
#define ORIENTATION_LANDSCAPE      1  // 90 degrees (480x320)
#define ORIENTATION_PORTRAIT_INV   2  // 180 degrees (320x480)
#define ORIENTATION_LANDSCAPE_INV  3  // 270 degrees (480x320)

// Display dimensions (physical)
#define ILI9488_PHYS_WIDTH  320
#define ILI9488_PHYS_HEIGHT 480

// DMA configuration
#define DMA_BUFFER_SIZE (4096 - 16)  // 4KB minus SPI transaction overhead to stay within limits

// Display initialization constants
#define ILI9488_PIXEL_FORMAT_18BIT 0x66  // 18-bit color mode: 6 bits R, 6 bits G, 6 bits B

// Update optimization thresholds
#define SMALL_UPDATE_THRESHOLD 512       // Bytes - use polling for smaller transfers
#define SMALL_UPDATE_ROWS 4              // Rows - use polling for fewer rows

// Special color value for "no fill"
#define COLOR_NONE 0xFFFFFFFF

// Global state
static spi_device_handle_t spi_device = NULL;
static int dc_pin = -1;
static int rst_pin = -1;
static uint8_t *framebuffer = NULL;
static uint8_t *dma_buffer = NULL;  // DMA-capable bounce buffer
static int line_thickness = 1;
static uint8_t current_orientation = ORIENTATION_PORTRAIT;
static int display_width = ILI9488_PHYS_WIDTH;
static int display_height = ILI9488_PHYS_HEIGHT;

// Helper: Send command (polling mode for control commands)
// Polling is used for commands since they're small and infrequent
static void ili9488_write_cmd(uint8_t cmd) {
    gpio_set_level(dc_pin, 0);  // DC low = command mode
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    spi_device_polling_transmit(spi_device, &t);
}

// Helper: Send data (polling mode for small control data)
// Used for configuration data and small transfers where DMA overhead isn't worth it
static void ili9488_write_data(uint8_t *data, size_t len) {
    if (len == 0) return;
    gpio_set_level(dc_pin, 1);  // DC high = data mode
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    spi_device_polling_transmit(spi_device, &t);
}

// Helper: Send large data using DMA (data must be in DMA-capable memory)
// DMA is faster for large transfers but requires internal SRAM buffer
static void ili9488_write_data_dma(const uint8_t *data, size_t len) {
    if (len == 0) return;
    
    gpio_set_level(dc_pin, 1);  // DC high = data mode
    
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    
    esp_err_t ret = spi_device_transmit(spi_device, &t);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DMA transfer failed: %d", ret);
    }
}

// Helper: Set address window
static void ili9488_set_window(int x0, int y0, int x1, int y1) {
    uint8_t data[4];

    ili9488_write_cmd(ILI9488_CASET);
    data[0] = x0 >> 8;
    data[1] = x0 & 0xFF;
    data[2] = x1 >> 8;
    data[3] = x1 & 0xFF;
    ili9488_write_data(data, 4);

    ili9488_write_cmd(ILI9488_PASET);
    data[0] = y0 >> 8;
    data[1] = y0 & 0xFF;
    data[2] = y1 >> 8;
    data[3] = y1 & 0xFF;
    ili9488_write_data(data, 4);

    ili9488_write_cmd(ILI9488_RAMWR);
}

// Helper: Set pixel in framebuffer (uses logical coordinates)
// Always performs bounds checking - safe but slower
static inline void set_pixel_fb(int x, int y, uint32_t color) {
    if (x >= 0 && x < display_width && y >= 0 && y < display_height && framebuffer) {
        int offset = (y * display_width + x) * 3;
        framebuffer[offset + 0] = (color >> 16) & 0xFF; // R
        framebuffer[offset + 1] = (color >> 8) & 0xFF;  // G
        framebuffer[offset + 2] = color & 0xFF;         // B
    }
}

// Helper: Set pixel without bounds checking - use only when coordinates are guaranteed valid
// This is faster for bulk operations where bounds are pre-validated
static inline void set_pixel_fb_unchecked(int x, int y, uint32_t color) {
    int offset = (y * display_width + x) * 3;
    framebuffer[offset + 0] = (color >> 16) & 0xFF; // R
    framebuffer[offset + 1] = (color >> 8) & 0xFF;  // G
    framebuffer[offset + 2] = color & 0xFF;         // B
}

// Helper: Get MADCTL value for orientation
static uint8_t get_madctl_for_orientation(uint8_t orientation) {
    uint8_t madctl = MADCTL_BGR;
    
    switch (orientation) {
        case ORIENTATION_PORTRAIT:
            madctl |= MADCTL_MX;
            break;
        case ORIENTATION_LANDSCAPE:
            madctl |= MADCTL_MV;
            break;
        case ORIENTATION_PORTRAIT_INV:
            madctl |= MADCTL_MY;
            break;
        case ORIENTATION_LANDSCAPE_INV:
            madctl |= MADCTL_MX | MADCTL_MY | MADCTL_MV;
            break;
        default:
            madctl |= MADCTL_MX;
            break;
    }
    
    return madctl;
}

// Initialize display with optional orientation parameter
static mp_obj_t ili9488_init(size_t n_args, const mp_obj_t *args) {
    mp_printf(&mp_plat_print, "ILI9488: Starting init\n");

    // args: spi_bus, dc_pin, rst_pin, cs_pin, [orientation]
    int spi_host = mp_obj_get_int(args[0]);
    dc_pin = mp_obj_get_int(args[1]);
    rst_pin = mp_obj_get_int(args[2]);
    int cs_pin = mp_obj_get_int(args[3]);
    
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
        display_width = ILI9488_PHYS_HEIGHT;
        display_height = ILI9488_PHYS_WIDTH;
    } else {
        display_width = ILI9488_PHYS_WIDTH;
        display_height = ILI9488_PHYS_HEIGHT;
    }

    mp_printf(&mp_plat_print, "ILI9488: SPI=%d DC=%d RST=%d CS=%d Orientation=%d (%dx%d)\n", 
              spi_host, dc_pin, rst_pin, cs_pin, current_orientation, 
              display_width, display_height);

    // Configure GPIO
    gpio_set_direction(dc_pin, GPIO_MODE_OUTPUT);
    gpio_set_direction(rst_pin, GPIO_MODE_OUTPUT);

    // Hardware reset
    gpio_set_level(rst_pin, 0);
    mp_hal_delay_ms(10);
    gpio_set_level(rst_pin, 1);
    mp_hal_delay_ms(120);

    // Configure SPI device - note: assumes SPI bus already initialized with DMA
    // The SPI bus should be initialized in your main code with spi_bus_initialize()
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 40000000,
        .mode = 0,
        .spics_io_num = cs_pin,
        .queue_size = 7,
        .flags = 0,
        .pre_cb = NULL,
        .post_cb = NULL
    };
    
    esp_err_t ret = spi_bus_add_device(spi_host, &devcfg, &spi_device);
    if (ret != ESP_OK) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Failed to add SPI device"));
    }

    // Initialize display
    ili9488_write_cmd(ILI9488_SWRESET);
    mp_hal_delay_ms(120);

    ili9488_write_cmd(ILI9488_SLPOUT);
    mp_hal_delay_ms(120);

    // Set color format to 18-bit (6-6-6 RGB)
    // This gives us 262K colors and works well with 3 bytes per pixel
    ili9488_write_cmd(ILI9488_PIXFMT);
    uint8_t pixfmt = ILI9488_PIXEL_FORMAT_18BIT;
    ili9488_write_data(&pixfmt, 1);

    // Configure MADCTL with selected orientation
    uint8_t madctl = get_madctl_for_orientation(current_orientation);
    ili9488_write_cmd(ILI9488_MADCTL);
    ili9488_write_data(&madctl, 1);
    mp_printf(&mp_plat_print, "ILI9488: MADCTL set to 0x%02X\n", madctl);

    ili9488_write_cmd(ILI9488_DISPON);
    mp_hal_delay_ms(100);

    // Allocate framebuffer in PSRAM
    if (framebuffer == NULL) {
        framebuffer = heap_caps_malloc(display_width * display_height * 3, 
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (framebuffer == NULL) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Failed to allocate framebuffer"));
        }
        // Initialize to black
        memset(framebuffer, 0, display_width * display_height * 3);
        mp_printf(&mp_plat_print, "ILI9488: Framebuffer allocated in PSRAM (%d bytes)\n",
                  display_width * display_height * 3);
    }
    
    // Allocate DMA bounce buffer in internal SRAM
    if (dma_buffer == NULL) {
        dma_buffer = heap_caps_malloc(DMA_BUFFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (dma_buffer == NULL) {
            heap_caps_free(framebuffer);
            framebuffer = NULL;
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Failed to allocate DMA buffer"));
        }
        mp_printf(&mp_plat_print, "ILI9488: DMA bounce buffer allocated in internal SRAM (%d bytes)\n",
                  DMA_BUFFER_SIZE);
    }

    mp_printf(&mp_plat_print, "ILI9488: Init complete with DMA support\n");
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ili9488_init_obj, 4, 5, ili9488_init);

// Deinitialize display
static mp_obj_t ili9488_deinit(void) {
    mp_printf(&mp_plat_print, "ILI9488: Deinitializing...\n");
    
    if (spi_device != NULL) {
        esp_err_t ret = spi_bus_remove_device(spi_device);
        if (ret == ESP_OK) {
            mp_printf(&mp_plat_print, "ILI9488: SPI device removed\n");
        } else {
            mp_printf(&mp_plat_print, "ILI9488: Failed to remove SPI device\n");
        }
        spi_device = NULL;
    }
    
    if (framebuffer != NULL) {
        heap_caps_free(framebuffer);
        framebuffer = NULL;
        mp_printf(&mp_plat_print, "ILI9488: Framebuffer freed\n");
    }
    
    if (dma_buffer != NULL) {
        heap_caps_free(dma_buffer);
        dma_buffer = NULL;
        mp_printf(&mp_plat_print, "ILI9488: DMA buffer freed\n");
    }
    
    display_width = ILI9488_PHYS_WIDTH;
    display_height = ILI9488_PHYS_HEIGHT;
    current_orientation = ORIENTATION_PORTRAIT;
    
    mp_printf(&mp_plat_print, "ILI9488: Deinitialization complete\n");
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(ili9488_deinit_obj, ili9488_deinit);

// Get current display dimensions
static mp_obj_t ili9488_get_width(void) {
    return mp_obj_new_int(display_width);
}
static MP_DEFINE_CONST_FUN_OBJ_0(ili9488_get_width_obj, ili9488_get_width);

static mp_obj_t ili9488_get_height(void) {
    return mp_obj_new_int(display_height);
}
static MP_DEFINE_CONST_FUN_OBJ_0(ili9488_get_height_obj, ili9488_get_height);

static mp_obj_t ili9488_get_orientation(void) {
    return mp_obj_new_int(current_orientation);
}
static MP_DEFINE_CONST_FUN_OBJ_0(ili9488_get_orientation_obj, ili9488_get_orientation);

static mp_obj_t ili9488_set_line_thickness(mp_obj_t thickness_obj) {
    int thickness = mp_obj_get_int(thickness_obj);
    if (thickness < 1) thickness = 1;
    if (thickness > 20) thickness = 20;
    line_thickness = thickness;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(ili9488_set_line_thickness_obj, ili9488_set_line_thickness);

static mp_obj_t ili9488_get_line_thickness(void) {
    return mp_obj_new_int(line_thickness);
}
static MP_DEFINE_CONST_FUN_OBJ_0(ili9488_get_line_thickness_obj, ili9488_get_line_thickness);

static mp_obj_t ili9488_mem_info(void) {
    size_t spiram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA);

    mp_printf(&mp_plat_print, "SPIRAM free: %d bytes\n", spiram_free);
    mp_printf(&mp_plat_print, "Internal RAM free: %d bytes\n", internal_free);
    mp_printf(&mp_plat_print, "DMA capable free: %d bytes\n", dma_free);
    mp_printf(&mp_plat_print, "Framebuffer needs: %d bytes\n", display_width * display_height * 3);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(ili9488_mem_info_obj, ili9488_mem_info);

// Partial update - optimized with DMA for larger regions
// Uses batched transfers to minimize SPI overhead
static mp_obj_t ili9488_update_region(size_t n_args, const mp_obj_t *args) {
    if (!framebuffer || !spi_device || !dma_buffer) return mp_const_none;

    int x = mp_obj_get_int(args[0]);
    int y = mp_obj_get_int(args[1]);
    int w = mp_obj_get_int(args[2]);
    int h = mp_obj_get_int(args[3]);

    // Clamp to screen bounds to prevent buffer overruns
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > display_width) w = display_width - x;
    if (y + h > display_height) h = display_height - y;
    
    if (w <= 0 || h <= 0) return mp_const_none;

    ili9488_set_window(x, y, x + w - 1, y + h - 1);

    size_t row_bytes = w * 3;
    
    // For small updates, use direct polling transfer to avoid DMA overhead
    if (row_bytes <= SMALL_UPDATE_THRESHOLD || h <= SMALL_UPDATE_ROWS) {
        for (int row = 0; row < h; row++) {
            int fb_offset = ((y + row) * display_width + x) * 3;
            ili9488_write_data((uint8_t *)framebuffer + fb_offset, row_bytes);
        }
        return mp_const_none;
    }
    
    // For larger updates, use DMA with bounce buffer and batching
    gpio_set_level(dc_pin, 1);  // Set DC high for data once
    
    size_t accumulated = 0;  // Track bytes in DMA buffer
    spi_transaction_t t = { 0 };
    
    for (int row = 0; row < h; row++) {
        int fb_offset = ((y + row) * display_width + x) * 3;
        
        // If row fits in remaining buffer space, accumulate it
        if (accumulated + row_bytes <= DMA_BUFFER_SIZE) {
            memcpy(dma_buffer + accumulated, framebuffer + fb_offset, row_bytes);
            accumulated += row_bytes;
        } else {
            // Buffer full - flush accumulated data first
            if (accumulated > 0) {
                t.length = accumulated * 8;
                t.tx_buffer = dma_buffer;
                spi_device_transmit(spi_device, &t);
                accumulated = 0;
            }
            
            // Handle current row
            if (row_bytes <= DMA_BUFFER_SIZE) {
                // Row fits in buffer - accumulate it
                memcpy(dma_buffer, framebuffer + fb_offset, row_bytes);
                accumulated = row_bytes;
            } else {
                // Row too large for buffer - send in chunks
                size_t offset = 0;
                while (offset < row_bytes) {
                    size_t chunk = (row_bytes - offset > DMA_BUFFER_SIZE) ? 
                                  DMA_BUFFER_SIZE : (row_bytes - offset);
                    memcpy(dma_buffer, framebuffer + fb_offset + offset, chunk);
                    
                    t.length = chunk * 8;
                    t.tx_buffer = dma_buffer;
                    spi_device_transmit(spi_device, &t);
                    
                    offset += chunk;
                }
            }
        }
    }
    
    // Flush any remaining accumulated data
    if (accumulated > 0) {
        t.length = accumulated * 8;
        t.tx_buffer = dma_buffer;
        spi_device_transmit(spi_device, &t);
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ili9488_update_region_obj, 4, 4, ili9488_update_region);

// Fill entire framebuffer with a solid color
// Optimized using logarithmic memcpy for ~10x speed improvement
static mp_obj_t ili9488_fill(mp_obj_t color_obj) {
    uint32_t color = mp_obj_get_int(color_obj);

    if (framebuffer) {
        uint8_t r = (color >> 16) & 0xFF;
        uint8_t g = (color >> 8) & 0xFF;
        uint8_t b = color & 0xFF;
        
        size_t total_bytes = display_width * display_height * 3;
        
        // Fill first pixel
        framebuffer[0] = r;
        framebuffer[1] = g;
        framebuffer[2] = b;
        
        // Use logarithmic doubling with memcpy - much faster than loop
        // Each iteration doubles the filled region until entire buffer is filled
        size_t filled = 3;
        while (filled < total_bytes) {
            size_t to_copy = (filled < total_bytes - filled) ? filled : (total_bytes - filled);
            memcpy(framebuffer + filled, framebuffer, to_copy);
            filled += to_copy;
        }
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(ili9488_fill_obj, ili9488_fill);

static mp_obj_t ili9488_pixel(size_t n_args, const mp_obj_t *args) {
    int x = mp_obj_get_int(args[0]);
    int y = mp_obj_get_int(args[1]);
    uint32_t color = mp_obj_get_int(args[2]);

    set_pixel_fb(x, y, color);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ili9488_pixel_obj, 3, 3, ili9488_pixel);

// Draw line (Bresenham's algorithm with thickness support)
static mp_obj_t ili9488_line(size_t n_args, const mp_obj_t *args) {
    int x0 = mp_obj_get_int(args[0]);
    int y0 = mp_obj_get_int(args[1]);
    int x1 = mp_obj_get_int(args[2]);
    int y1 = mp_obj_get_int(args[3]);
    uint32_t color = mp_obj_get_int(args[4]);

    if (!framebuffer) return mp_const_none;

    // Fast path for thickness 1: use standard Bresenham
    if (line_thickness == 1) {
        int dx = abs(x1 - x0);
        int dy = abs(y1 - y0);
        int sx = x0 < x1 ? 1 : -1;
        int sy = y0 < y1 ? 1 : -1;
        int err = dx - dy;

        while (1) {
            set_pixel_fb(x0, y0, color);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 < dx) { err += dx; y0 += sy; }
        }
        return mp_const_none;
    }
    
    // Thick line implementation using perpendicular scanline fill
    // This creates a filled quadrilateral representing the thick line
    int dx = x1 - x0;
    int dy = y1 - y0;
    float length = sqrt(dx * dx + dy * dy);
    
    // Degenerate case: zero-length line becomes a filled circle
    if (length < 0.1f) {
        int radius = line_thickness / 2;
        for (int j = -radius; j <= radius; j++) {
            for (int i = -radius; i <= radius; i++) {
                if (i*i + j*j <= radius*radius) {
                    set_pixel_fb(x0 + i, y0 + j, color);
                }
            }
        }
        return mp_const_none;
    }
    
    // Optimized special case: horizontal line
    if (dy == 0) {
        int min_x = x0 < x1 ? x0 : x1;
        int max_x = x0 < x1 ? x1 : x0;
        int half_thick = line_thickness / 2;
        
        // Clamp to screen bounds for unchecked pixel writes
        int start_y = y0 - half_thick;
        int end_y = y0 + half_thick;
        if (start_y < 0) start_y = 0;
        if (end_y >= display_height) end_y = display_height - 1;
        if (min_x < 0) min_x = 0;
        if (max_x >= display_width) max_x = display_width - 1;
        
        for (int y = start_y; y <= end_y; y++) {
            for (int x = min_x; x <= max_x; x++) {
                set_pixel_fb_unchecked(x, y, color);
            }
        }
        return mp_const_none;
    }
    
    // Optimized special case: vertical line
    if (dx == 0) {
        int min_y = y0 < y1 ? y0 : y1;
        int max_y = y0 < y1 ? y1 : y0;
        int half_thick = line_thickness / 2;
        
        // Clamp to screen bounds for unchecked pixel writes
        int start_x = x0 - half_thick;
        int end_x = x0 + half_thick;
        if (start_x < 0) start_x = 0;
        if (end_x >= display_width) end_x = display_width - 1;
        if (min_y < 0) min_y = 0;
        if (max_y >= display_height) max_y = display_height - 1;
        
        for (int x = start_x; x <= end_x; x++) {
            for (int y = min_y; y <= max_y; y++) {
                set_pixel_fb_unchecked(x, y, color);
            }
        }
        return mp_const_none;
    }
    
    // General case: compute perpendicular offset for thick line
    // Create a quadrilateral with 4 corners (c1, c2, c3, c4) representing the thick line
    float perp_x = -dy / length;  // Perpendicular unit vector
    float perp_y = dx / length;
    float half_thick = line_thickness / 2.0f;
    
    // Four corners of the thick line quadrilateral
    float c1x = x0 + perp_x * half_thick;
    float c1y = y0 + perp_y * half_thick;
    float c2x = x0 - perp_x * half_thick;
    float c2y = y0 - perp_y * half_thick;
    float c3x = x1 - perp_x * half_thick;
    float c3y = y1 - perp_y * half_thick;
    float c4x = x1 + perp_x * half_thick;
    float c4y = y1 + perp_y * half_thick;
    
    // Find bounding box for scanline algorithm
    int min_y = (int)c1y;
    int max_y = (int)c1y;
    if ((int)c2y < min_y) min_y = (int)c2y;
    if ((int)c3y < min_y) min_y = (int)c3y;
    if ((int)c4y < min_y) min_y = (int)c4y;
    if ((int)c2y > max_y) max_y = (int)c2y;
    if ((int)c3y > max_y) max_y = (int)c3y;
    if ((int)c4y > max_y) max_y = (int)c4y;
    min_y -= 1;
    max_y += 1;
    
    // Scanline fill algorithm: for each horizontal line, find intersections with quad edges
    for (int scan_y = min_y; scan_y <= max_y; scan_y++) {
        int intersections[4];
        int num_intersections = 0;
        
        // Check intersection with edge c1-c2
        if ((c1y <= scan_y && scan_y <= c2y) || (c2y <= scan_y && scan_y <= c1y)) {
            if (fabs(c2y - c1y) > 0.01f) {
                float t = (scan_y - c1y) / (c2y - c1y);
                intersections[num_intersections++] = (int)(c1x + t * (c2x - c1x));
            }
        }
        // Check intersection with edge c2-c3
        if ((c2y <= scan_y && scan_y <= c3y) || (c3y <= scan_y && scan_y <= c2y)) {
            if (fabs(c3y - c2y) > 0.01f) {
                float t = (scan_y - c2y) / (c3y - c2y);
                intersections[num_intersections++] = (int)(c2x + t * (c3x - c2x));
            }
        }
        // Check intersection with edge c3-c4
        if ((c3y <= scan_y && scan_y <= c4y) || (c4y <= scan_y && scan_y <= c3y)) {
            if (fabs(c4y - c3y) > 0.01f) {
                float t = (scan_y - c3y) / (c4y - c3y);
                intersections[num_intersections++] = (int)(c3x + t * (c4x - c3x));
            }
        }
        // Check intersection with edge c4-c1
        if ((c4y <= scan_y && scan_y <= c1y) || (c1y <= scan_y && scan_y <= c4y)) {
            if (fabs(c1y - c4y) > 0.01f) {
                float t = (scan_y - c4y) / (c1y - c4y);
                intersections[num_intersections++] = (int)(c4x + t * (c1x - c4x));
            }
        }
        
        // Simple bubble sort for small array (max 4 elements)
        for (int i = 0; i < num_intersections - 1; i++) {
            for (int j = 0; j < num_intersections - i - 1; j++) {
                if (intersections[j] > intersections[j + 1]) {
                    int temp = intersections[j];
                    intersections[j] = intersections[j + 1];
                    intersections[j + 1] = temp;
                }
            }
        }
        
        // Fill between leftmost and rightmost intersections
        if (num_intersections >= 2) {
            int min_x = intersections[0];
            int max_x = intersections[num_intersections - 1];
            for (int x = min_x; x <= max_x; x++) {
                set_pixel_fb(x, scan_y, color);
            }
        }
    }
    
    // Add rounded end caps to make line ends smooth
    int radius = line_thickness / 2;
    for (int j = -radius; j <= radius; j++) {
        for (int i = -radius; i <= radius; i++) {
            if (i*i + j*j <= radius*radius) {
                set_pixel_fb(x0 + i, y0 + j, color);
                set_pixel_fb(x1 + i, y1 + j, color);
            }
        }
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ili9488_line_obj, 5, 5, ili9488_line);

// Draw filled or outlined rectangle
static mp_obj_t ili9488_rect(size_t n_args, const mp_obj_t *args) {
    int x = mp_obj_get_int(args[0]);
    int y = mp_obj_get_int(args[1]);
    int w = mp_obj_get_int(args[2]);
    int h = mp_obj_get_int(args[3]);
    uint32_t color = mp_obj_get_int(args[4]);
    uint32_t fill_color = (n_args > 5) ? mp_obj_get_int(args[5]) : COLOR_NONE;

    if (!framebuffer) return mp_const_none;
    
    // Validate dimensions to prevent errors
    if (w <= 0 || h <= 0) return mp_const_none;

    // Fill interior if requested
    if (fill_color != COLOR_NONE) {
        // Clamp to screen bounds for unchecked writes
        int start_x = (x < 0) ? 0 : x;
        int start_y = (y < 0) ? 0 : y;
        int end_x = (x + w > display_width) ? display_width : x + w;
        int end_y = (y + h > display_height) ? display_height : y + h;
        
        for (int j = start_y; j < end_y; j++) {
            for (int i = start_x; i < end_x; i++) {
                set_pixel_fb_unchecked(i, j, fill_color);
            }
        }
    }

    // Draw outline (uses bounds-checked version)
    for (int i = 0; i < w; i++) {
        set_pixel_fb(x + i, y, color);           // Top edge
        set_pixel_fb(x + i, y + h - 1, color);   // Bottom edge
    }
    for (int i = 0; i < h; i++) {
        set_pixel_fb(x, y + i, color);           // Left edge
        set_pixel_fb(x + w - 1, y + i, color);   // Right edge
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ili9488_rect_obj, 5, 6, ili9488_rect);

// Draw filled or outlined circle
// Uses midpoint circle algorithm for outline, area fill for interior
static mp_obj_t ili9488_circle(size_t n_args, const mp_obj_t *args) {
    int x0 = mp_obj_get_int(args[0]);
    int y0 = mp_obj_get_int(args[1]);
    int r = mp_obj_get_int(args[2]);
    uint32_t color = mp_obj_get_int(args[3]);
    uint32_t fill_color = (n_args > 4) ? mp_obj_get_int(args[4]) : COLOR_NONE;

    if (!framebuffer) return mp_const_none;
    if (r <= 0) return mp_const_none;  // Validate radius

    // Fill interior if requested
    // Uses simple area algorithm - could be optimized with scanline
    if (fill_color != COLOR_NONE) {
        for (int y = -r; y <= r; y++) {
            for (int x = -r; x <= r; x++) {
                if (x * x + y * y <= r * r) {
                    set_pixel_fb(x0 + x, y0 + y, fill_color);
                }
            }
        }
    }

    // Draw outline using midpoint circle algorithm (Bresenham's circle)
    int x = r;
    int y = 0;
    int err = 0;

    while (x >= y) {
        // Draw 8 symmetric points
        set_pixel_fb(x0 + x, y0 + y, color);
        set_pixel_fb(x0 + y, y0 + x, color);
        set_pixel_fb(x0 - y, y0 + x, color);
        set_pixel_fb(x0 - x, y0 + y, color);
        set_pixel_fb(x0 - x, y0 - y, color);
        set_pixel_fb(x0 - y, y0 - x, color);
        set_pixel_fb(x0 + y, y0 - x, color);
        set_pixel_fb(x0 + x, y0 - y, color);

        if (err <= 0) {
            y += 1;
            err += 2 * y + 1;
        }
        if (err > 0) {
            x -= 1;
            err -= 2 * x + 1;
        }
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ili9488_circle_obj, 4, 5, ili9488_circle);

// Draw filled or outlined triangle
// Uses scanline conversion for fill, line drawing for outline
static mp_obj_t ili9488_triangle(size_t n_args, const mp_obj_t *args) {
    int x0 = mp_obj_get_int(args[0]);
    int y0 = mp_obj_get_int(args[1]);
    int x1 = mp_obj_get_int(args[2]);
    int y1 = mp_obj_get_int(args[3]);
    int x2 = mp_obj_get_int(args[4]);
    int y2 = mp_obj_get_int(args[5]);
    uint32_t color = mp_obj_get_int(args[6]);
    uint32_t fill_color = (n_args > 7) ? mp_obj_get_int(args[7]) : COLOR_NONE;

    if (!framebuffer) return mp_const_none;

    // Fill interior using scanline algorithm
    if (fill_color != COLOR_NONE) {
        // Sort vertices by Y coordinate (bubble sort for 3 elements)
        if (y0 > y1) { int t; t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
        if (y1 > y2) { int t; t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
        if (y0 > y1) { int t; t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }

        // Scanline fill algorithm
        for (int y = y0; y <= y2; y++) {
            int xa, xb;
            
            // Calculate X intersections for current scanline
            if (y < y1) {
                // Upper half of triangle
                // FIXED: Added zero-division protection
                if (y1 - y0 != 0) {
                    xa = x0 + (y - y0) * (x1 - x0) / (y1 - y0);
                } else {
                    xa = x0;
                }
                if (y2 - y0 != 0) {
                    xb = x0 + (y - y0) * (x2 - x0) / (y2 - y0);
                } else {
                    xb = x0;
                }
            } else {
                // Lower half of triangle
                // FIXED: Added zero-division protection
                if (y2 - y1 != 0) {
                    xa = x1 + (y - y1) * (x2 - x1) / (y2 - y1);
                } else {
                    xa = x1;
                }
                if (y2 - y0 != 0) {
                    xb = x0 + (y - y0) * (x2 - x0) / (y2 - y0);
                } else {
                    xb = x0;
                }
            }
            
            // Ensure xa is leftmost
            if (xa > xb) { int t = xa; xa = xb; xb = t; }
            
            // Fill scanline
            for (int x = xa; x <= xb; x++) {
                set_pixel_fb(x, y, fill_color);
            }
        }
    }

    // Draw outline by drawing three lines
    mp_obj_t line_args[5];
    line_args[4] = args[6];  // color
    
    // Line from vertex 0 to vertex 1
    line_args[0] = args[0]; line_args[1] = args[1];
    line_args[2] = args[2]; line_args[3] = args[3];
    ili9488_line(5, line_args);
    
    // Line from vertex 1 to vertex 2
    line_args[0] = args[2]; line_args[1] = args[3];
    line_args[2] = args[4]; line_args[3] = args[5];
    ili9488_line(5, line_args);
    
    // Line from vertex 2 to vertex 0
    line_args[0] = args[4]; line_args[1] = args[5];
    line_args[2] = args[0]; line_args[3] = args[1];
    ili9488_line(5, line_args);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ili9488_triangle_obj, 7, 8, ili9488_triangle);

// Sprite class - represents a movable graphic object with background preservation
typedef struct {
    mp_obj_base_t base;
    uint8_t *pixels;      // Sprite pixel data
    uint8_t *background;  // Saved background for restoration
    int width;
    int height;
    int x;                // Current position
    int y;
    int old_x;            // Previous position (for efficient updates)
    int old_y;
    bool visible;
    bool moved;
} sprite_obj_t;

const mp_obj_type_t sprite_type;

// Create a new sprite with specified dimensions
// NOTE: Currently no destructor - memory leak potential if many sprites created/destroyed
// TODO: Add __del__ method or use gc-tracked allocation
static mp_obj_t sprite_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    (void)type;
    mp_arg_check_num(n_args, n_kw, 2, 2, false);

    sprite_obj_t *self = m_new_obj(sprite_obj_t);
    self->base.type = &sprite_type;
    self->width = mp_obj_get_int(args[0]);
    self->height = mp_obj_get_int(args[1]);
    
    // Validate dimensions
    if (self->width <= 0 || self->height <= 0) {
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("Sprite dimensions must be positive"));
    }
    
    self->pixels = m_new(uint8_t, self->width * self->height * 3);
    self->background = m_new(uint8_t, self->width * self->height * 3);
    self->x = 0;
    self->y = 0;
    self->old_x = 0;
    self->old_y = 0;
    self->visible = false;
    self->moved = false;

    // Initialize to transparent (black = transparent)
    memset(self->pixels, 0, self->width * self->height * 3);
    memset(self->background, 0, self->width * self->height * 3);

    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t sprite_set_pixel(size_t n_args, const mp_obj_t *args) {
    sprite_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    int x = mp_obj_get_int(args[1]);
    int y = mp_obj_get_int(args[2]);
    uint32_t color = mp_obj_get_int(args[3]);

    if (x >= 0 && x < self->width && y >= 0 && y < self->height) {
        int offset = (y * self->width + x) * 3;
        self->pixels[offset + 0] = (color >> 16) & 0xFF;
        self->pixels[offset + 1] = (color >> 8) & 0xFF;
        self->pixels[offset + 2] = color & 0xFF;
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sprite_set_pixel_obj, 4, 4, sprite_set_pixel);

// Helper: Calculate the bounding box for sprite update when sprite moves
// Returns the minimal rectangle that encompasses both old and new sprite positions
static void calculate_sprite_update_region(sprite_obj_t *self, int new_x, int new_y,
                                          int *out_x, int *out_y, int *out_w, int *out_h) {
    // Find bounding box of old and new positions
    int min_x = (self->x < new_x) ? self->x : new_x;
    int min_y = (self->y < new_y) ? self->y : new_y;
    int max_x = ((self->x + self->width) > (new_x + self->width)) ? 
                (self->x + self->width) : (new_x + self->width);
    int max_y = ((self->y + self->height) > (new_y + self->height)) ? 
                (self->y + self->height) : (new_y + self->height);
    
    int update_w = max_x - min_x;
    int update_h = max_y - min_y;
    
    // Clamp to screen bounds
    if (min_x < 0) { update_w += min_x; min_x = 0; }
    if (min_y < 0) { update_h += min_y; min_y = 0; }
    if (min_x + update_w > display_width) update_w = display_width - min_x;
    if (min_y + update_h > display_height) update_h = display_height - min_y;
    
    *out_x = min_x;
    *out_y = min_y;
    *out_w = update_w;
    *out_h = update_h;
}

// Draw sprite at new position with optional auto-update to display
// If auto_update is true, immediately sends the changed region to the display via DMA
// Draw sprite at new position with optional auto-update to display
// If auto_update is true, immediately sends the changed region to the display via DMA
static mp_obj_t sprite_draw(size_t n_args, const mp_obj_t *args) {
    sprite_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    int new_x = mp_obj_get_int(args[1]);
    int new_y = mp_obj_get_int(args[2]);
    bool auto_update = (n_args > 3) ? mp_obj_is_true(args[3]) : false;

    if (!framebuffer) return mp_const_none;

    bool sprite_moved = self->visible && (self->x != new_x || self->y != new_y);

    // Step 1: Restore old background if sprite was visible
    if (self->visible) {
        for (int j = 0; j < self->height; j++) {
            for (int i = 0; i < self->width; i++) {
                int px = self->x + i;
                int py = self->y + j;
                if (px >= 0 && px < display_width && py >= 0 && py < display_height) {
                    int fb_offset = (py * display_width + px) * 3;
                    int bg_offset = (j * self->width + i) * 3;
                    framebuffer[fb_offset + 0] = self->background[bg_offset + 0];
                    framebuffer[fb_offset + 1] = self->background[bg_offset + 1];
                    framebuffer[fb_offset + 2] = self->background[bg_offset + 2];
                }
            }
        }
    }

    // Step 2: Save new background
    for (int j = 0; j < self->height; j++) {
        for (int i = 0; i < self->width; i++) {
            int px = new_x + i;
            int py = new_y + j;
            if (px >= 0 && px < display_width && py >= 0 && py < display_height) {
                int fb_offset = (py * display_width + px) * 3;
                int bg_offset = (j * self->width + i) * 3;
                self->background[bg_offset + 0] = framebuffer[fb_offset + 0];
                self->background[bg_offset + 1] = framebuffer[fb_offset + 1];
                self->background[bg_offset + 2] = framebuffer[fb_offset + 2];
            }
        }
    }

    // Step 3: Draw sprite at new position (black pixels are transparent)
    for (int j = 0; j < self->height; j++) {
        for (int i = 0; i < self->width; i++) {
            int px = new_x + i;
            int py = new_y + j;
            int sp_offset = (j * self->width + i) * 3;
            
            uint8_t r = self->pixels[sp_offset + 0];
            uint8_t g = self->pixels[sp_offset + 1];
            uint8_t b = self->pixels[sp_offset + 2];

            // Skip transparent pixels (black = transparent)
            if ((r != 0 || g != 0 || b != 0) && 
                px >= 0 && px < display_width && py >= 0 && py < display_height) {
                int fb_offset = (py * display_width + px) * 3;
                framebuffer[fb_offset + 0] = r;
                framebuffer[fb_offset + 1] = g;
                framebuffer[fb_offset + 2] = b;
            }
        }
    }

    // Step 4: Auto-update display if requested
    if (auto_update && spi_device && dma_buffer) {
        if (sprite_moved) {
            // Sprite moved - update the union of old and new positions
            int min_x, min_y, update_w, update_h;
            calculate_sprite_update_region(self, new_x, new_y, 
                                          &min_x, &min_y, &update_w, &update_h);
            
            if (update_w > 0 && update_h > 0) {
                ili9488_set_window(min_x, min_y, min_x + update_w - 1, min_y + update_h - 1);
                
                gpio_set_level(dc_pin, 1);
                size_t row_bytes = update_w * 3;
                spi_transaction_t t = { 0 };
                
                for (int row = 0; row < update_h; row++) {
                    int fb_offset = ((min_y + row) * display_width + min_x) * 3;
                    
                    if (row_bytes <= DMA_BUFFER_SIZE) {
                        memcpy(dma_buffer, framebuffer + fb_offset, row_bytes);
                        t.length = row_bytes * 8;
                        t.tx_buffer = dma_buffer;
                        spi_device_transmit(spi_device, &t);
                    } else {
                        // Fallback for very wide sprites
                        ili9488_write_data((uint8_t *)framebuffer + fb_offset, row_bytes);
                    }
                }
            }
        } else {
            // Sprite didn't move - just update its current position
            if (new_x >= 0 && new_y >= 0 && 
                new_x + self->width <= display_width && 
                new_y + self->height <= display_height) {
                ili9488_set_window(new_x, new_y, 
                                  new_x + self->width - 1, 
                                  new_y + self->height - 1);
                
                gpio_set_level(dc_pin, 1);
                size_t row_bytes = self->width * 3;
                spi_transaction_t t = { 0 };
                
                for (int row = 0; row < self->height; row++) {
                    int fb_offset = ((new_y + row) * display_width + new_x) * 3;
                    
                    if (row_bytes <= DMA_BUFFER_SIZE) {
                        memcpy(dma_buffer, framebuffer + fb_offset, row_bytes);
                        t.length = row_bytes * 8;
                        t.tx_buffer = dma_buffer;
                        spi_device_transmit(spi_device, &t);
                    } else {
                        ili9488_write_data((uint8_t *)framebuffer + fb_offset, row_bytes);
                    }
                }
            }
        }
    }

    // Update sprite state
    self->old_x = self->x;
    self->old_y = self->y;
    self->x = new_x;
    self->y = new_y;
    self->visible = true;
    self->moved = sprite_moved;

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sprite_draw_obj, 3, 4, sprite_draw);

// Hide sprite by restoring its background
static mp_obj_t sprite_hide(mp_obj_t self_in) {
    sprite_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (!framebuffer || !self->visible) return mp_const_none;

    // Restore background where sprite was
    for (int j = 0; j < self->height; j++) {
        for (int i = 0; i < self->width; i++) {
            int px = self->x + i;
            int py = self->y + j;
            if (px >= 0 && px < display_width && py >= 0 && py < display_height) {
                int fb_offset = (py * display_width + px) * 3;
                int bg_offset = (j * self->width + i) * 3;
                framebuffer[fb_offset + 0] = self->background[bg_offset + 0];
                framebuffer[fb_offset + 1] = self->background[bg_offset + 1];
                framebuffer[fb_offset + 2] = self->background[bg_offset + 2];
            }
        }
    }

    self->visible = false;

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(sprite_hide_obj, sprite_hide);

static const mp_rom_map_elem_t sprite_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_set_pixel), MP_ROM_PTR(&sprite_set_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_draw), MP_ROM_PTR(&sprite_draw_obj) },
    { MP_ROM_QSTR(MP_QSTR_hide), MP_ROM_PTR(&sprite_hide_obj) },
};
static MP_DEFINE_CONST_DICT(sprite_locals_dict, sprite_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    sprite_type,
    MP_QSTR_Sprite,
    MP_TYPE_FLAG_NONE,
    make_new, sprite_make_new,
    locals_dict, &sprite_locals_dict
);

// Update entire display using DMA with bounce buffer
// Transfers framebuffer from PSRAM to display in optimized chunks
static mp_obj_t ili9488_show(void) {
    if (!framebuffer || !spi_device || !dma_buffer) {
        ESP_LOGE(TAG, "Cannot show - missing resources");
        return mp_const_none;
    }

    ili9488_set_window(0, 0, display_width - 1, display_height - 1);

    size_t total_bytes = display_width * display_height * 3;
    size_t expected_chunks = (total_bytes + DMA_BUFFER_SIZE - 1) / DMA_BUFFER_SIZE;

#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    mp_printf(&mp_plat_print, "ILI9488: Updating display with DMA (%d bytes, %d chunks)...\n", 
              total_bytes, expected_chunks);
#endif

    // Set DC high once for all data transfers
    gpio_set_level(dc_pin, 1);
    
    // Transfer framebuffer using DMA bounce buffer
    spi_transaction_t t = { 0 };
    size_t offset = 0;
    int error_count = 0;
    const int MAX_ERRORS = 10;  // Allow some errors before failing
    
    while (offset < total_bytes) {
        size_t chunk_size = (total_bytes - offset > DMA_BUFFER_SIZE) ? 
                           DMA_BUFFER_SIZE : (total_bytes - offset);
        
        // Copy chunk from PSRAM framebuffer to DMA-capable internal SRAM buffer
        memcpy(dma_buffer, framebuffer + offset, chunk_size);
        
        // Transfer via DMA
        t.length = chunk_size * 8;  // Length in bits
        t.tx_buffer = dma_buffer;
        
        esp_err_t ret = spi_device_transmit(spi_device, &t);
        if (ret != ESP_OK) {
            error_count++;
            ESP_LOGE(TAG, "DMA transfer failed at offset %d/%d: %d (error %d/%d)", 
                     offset, total_bytes, ret, error_count, MAX_ERRORS);
            
            // Too many errors - give up
            if (error_count >= MAX_ERRORS) {
                mp_raise_msg(&mp_type_RuntimeError, 
                           MP_ERROR_TEXT("Display update failed: too many DMA errors"));
            }
            
            // Retry this chunk
            mp_hal_delay_ms(1);
            continue;
        }
        
        offset += chunk_size;
    }

#if LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG
    mp_printf(&mp_plat_print, "ILI9488: Display updated successfully\n");
#endif

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(ili9488_show_obj, ili9488_show);

static const mp_rom_map_elem_t ili9488_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_ili9488) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&ili9488_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&ili9488_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_fill), MP_ROM_PTR(&ili9488_fill_obj) },
    { MP_ROM_QSTR(MP_QSTR_pixel), MP_ROM_PTR(&ili9488_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_line), MP_ROM_PTR(&ili9488_line_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_line_thickness), MP_ROM_PTR(&ili9488_set_line_thickness_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_line_thickness), MP_ROM_PTR(&ili9488_get_line_thickness_obj) },
    { MP_ROM_QSTR(MP_QSTR_rect), MP_ROM_PTR(&ili9488_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_circle), MP_ROM_PTR(&ili9488_circle_obj) },
    { MP_ROM_QSTR(MP_QSTR_triangle), MP_ROM_PTR(&ili9488_triangle_obj) },
    { MP_ROM_QSTR(MP_QSTR_show), MP_ROM_PTR(&ili9488_show_obj) },
    { MP_ROM_QSTR(MP_QSTR_update_region), MP_ROM_PTR(&ili9488_update_region_obj) },
    { MP_ROM_QSTR(MP_QSTR_Sprite), MP_ROM_PTR(&sprite_type) },
    { MP_ROM_QSTR(MP_QSTR_get_width), MP_ROM_PTR(&ili9488_get_width_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_height), MP_ROM_PTR(&ili9488_get_height_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_orientation), MP_ROM_PTR(&ili9488_get_orientation_obj) },
    { MP_ROM_QSTR(MP_QSTR_WIDTH), MP_ROM_INT(ILI9488_PHYS_WIDTH) },
    { MP_ROM_QSTR(MP_QSTR_HEIGHT), MP_ROM_INT(ILI9488_PHYS_HEIGHT) },
    { MP_ROM_QSTR(MP_QSTR_ORIENTATION_PORTRAIT), MP_ROM_INT(ORIENTATION_PORTRAIT) },
    { MP_ROM_QSTR(MP_QSTR_ORIENTATION_LANDSCAPE), MP_ROM_INT(ORIENTATION_LANDSCAPE) },
    { MP_ROM_QSTR(MP_QSTR_ORIENTATION_PORTRAIT_INV), MP_ROM_INT(ORIENTATION_PORTRAIT_INV) },
    { MP_ROM_QSTR(MP_QSTR_ORIENTATION_LANDSCAPE_INV), MP_ROM_INT(ORIENTATION_LANDSCAPE_INV) },
    { MP_ROM_QSTR(MP_QSTR_mem_info), MP_ROM_PTR(&ili9488_mem_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_COLOR_NONE), MP_ROM_INT(COLOR_NONE) },
};
static MP_DEFINE_CONST_DICT(ili9488_module_globals, ili9488_module_globals_table);

const mp_obj_module_t ili9488_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&ili9488_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_ili9488, ili9488_module);

