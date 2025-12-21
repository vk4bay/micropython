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
#define ILI9488_SLPOUT 0x11
#define ILI9488_DISPON 0x29
#define ILI9488_CASET 0x2A
#define ILI9488_PASET 0x2B
#define ILI9488_RAMWR 0x2C
#define ILI9488_MADCTL 0x36
#define ILI9488_PIXFMT 0x3A

// MADCTL bits
#define MADCTL_MY 0x80  // Row Address Order
#define MADCTL_MX 0x40  // Column Address Order
#define MADCTL_MV 0x20  // Row/Column Exchange
#define MADCTL_ML 0x10  // Vertical Refresh Order
#define MADCTL_BGR 0x08 // RGB-BGR Order
#define MADCTL_MH 0x04  // Horizontal Refresh Order

// Orientation definitions
#define ORIENTATION_PORTRAIT 0      // 0 degrees (320x480)
#define ORIENTATION_LANDSCAPE 1     // 90 degrees (480x320)
#define ORIENTATION_PORTRAIT_INV 2  // 180 degrees (320x480)
#define ORIENTATION_LANDSCAPE_INV 3 // 270 degrees (480x320)

// Display dimensions (physical)
#define ILI9488_PHYS_WIDTH 320
#define ILI9488_PHYS_HEIGHT 480

// DMA configuration
#define DMA_BUFFER_SIZE 4080 // Just under 4KB to stay within SPI transaction limits

// Special color value for "no fill"
#define COLOR_NONE 0xFFFFFFFF

// Global state
static spi_device_handle_t spi_device = NULL;
static int dc_pin = -1;
static int rst_pin = -1;
static uint8_t *framebuffer = NULL;
static uint8_t *dma_buffer = NULL; // DMA-capable bounce buffer
static int line_thickness = 1;
static uint8_t current_orientation = ORIENTATION_PORTRAIT;
static int display_width = ILI9488_PHYS_WIDTH;
static int display_height = ILI9488_PHYS_HEIGHT;
// NOTE: custom_font is not thread-safe. All accesses (e.g., via ili9488_set_font/ili9488_text)
// must be made from a single task/thread and not from ISRs to avoid race conditions.
static mp_obj_t custom_font = MP_OBJ_NULL; // Custom Python font module

// Helper: Send command (polling mode for control commands)
static void ili9488_write_cmd(uint8_t cmd)
{
    gpio_set_level(dc_pin, 0);
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
        .flags = 0,
    };
    spi_device_polling_transmit(spi_device, &t);
}

// Helper: Send data (polling mode for small control data)
static void ili9488_write_data(uint8_t *data, size_t len)
{
    if (len == 0)
        return;
    gpio_set_level(dc_pin, 1);
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
        .flags = 0,
    };
    spi_device_polling_transmit(spi_device, &t);
}

// Helper: Set address window
static void ili9488_set_window(int x0, int y0, int x1, int y1)
{
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
static inline void set_pixel_fb(int x, int y, uint32_t color)
{
    if (x >= 0 && x < display_width && y >= 0 && y < display_height && framebuffer)
    {
        int offset = (y * display_width + x) * 3;
        framebuffer[offset + 0] = (color >> 16) & 0xFF; // R
        framebuffer[offset + 1] = (color >> 8) & 0xFF;  // G
        framebuffer[offset + 2] = color & 0xFF;         // B
    }
}

// Helper: Get MADCTL value for orientation
static uint8_t get_madctl_for_orientation(uint8_t orientation)
{
    uint8_t madctl = MADCTL_BGR;

    switch (orientation)
    {
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
static mp_obj_t ili9488_init(size_t n_args, const mp_obj_t *args)
{
    mp_printf(&mp_plat_print, "ILI9488: Starting init\n");

    // args: spi_bus, dc_pin, rst_pin, cs_pin, [orientation]
    int spi_host = mp_obj_get_int(args[0]);
    dc_pin = mp_obj_get_int(args[1]);
    rst_pin = mp_obj_get_int(args[2]);
    int cs_pin = mp_obj_get_int(args[3]);

    if (n_args >= 5)
    {
        current_orientation = mp_obj_get_int(args[4]);
        if (current_orientation > ORIENTATION_LANDSCAPE_INV)
        {
            current_orientation = ORIENTATION_PORTRAIT;
        }
    }
    else
    {
        current_orientation = ORIENTATION_PORTRAIT;
    }

    // Set logical display dimensions based on orientation
    if (current_orientation == ORIENTATION_LANDSCAPE ||
        current_orientation == ORIENTATION_LANDSCAPE_INV)
    {
        display_width = ILI9488_PHYS_HEIGHT;
        display_height = ILI9488_PHYS_WIDTH;
    }
    else
    {
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
        .post_cb = NULL};

    esp_err_t ret = spi_bus_add_device(spi_host, &devcfg, &spi_device);
    if (ret != ESP_OK)
    {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Failed to add SPI device"));
    }

    // Initialize display
    ili9488_write_cmd(ILI9488_SWRESET);
    mp_hal_delay_ms(120);

    ili9488_write_cmd(ILI9488_SLPOUT);
    mp_hal_delay_ms(120);

    // Set color format to 18-bit (6-6-6 RGB)
    ili9488_write_cmd(ILI9488_PIXFMT);
    uint8_t pixfmt = 0x66;
    ili9488_write_data(&pixfmt, 1);

    // Configure MADCTL with selected orientation
    uint8_t madctl = get_madctl_for_orientation(current_orientation);
    ili9488_write_cmd(ILI9488_MADCTL);
    ili9488_write_data(&madctl, 1);
    mp_printf(&mp_plat_print, "ILI9488: MADCTL set to 0x%02X\n", madctl);

    ili9488_write_cmd(ILI9488_DISPON);
    mp_hal_delay_ms(100);

    // Allocate framebuffer in PSRAM
    if (framebuffer == NULL)
    {
        framebuffer = heap_caps_malloc(display_width * display_height * 3,
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (framebuffer == NULL)
        {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Failed to allocate framebuffer"));
        }
        // Initialize to black
        memset(framebuffer, 0, display_width * display_height * 3);
        mp_printf(&mp_plat_print, "ILI9488: Framebuffer allocated in PSRAM (%d bytes)\n",
                  display_width * display_height * 3);
    }

    // Allocate DMA bounce buffer in internal SRAM
    if (dma_buffer == NULL)
    {
        dma_buffer = heap_caps_malloc(DMA_BUFFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (dma_buffer == NULL)
        {
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
static mp_obj_t ili9488_deinit(void)
{
    mp_printf(&mp_plat_print, "ILI9488: Deinitializing...\n");

    if (spi_device != NULL)
    {
        esp_err_t ret = spi_bus_remove_device(spi_device);
        if (ret == ESP_OK)
        {
            mp_printf(&mp_plat_print, "ILI9488: SPI device removed\n");
        }
        else
        {
            mp_printf(&mp_plat_print, "ILI9488: Failed to remove SPI device\n");
        }
        spi_device = NULL;
    }

    if (framebuffer != NULL)
    {
        heap_caps_free(framebuffer);
        framebuffer = NULL;
        mp_printf(&mp_plat_print, "ILI9488: Framebuffer freed\n");
    }

    if (dma_buffer != NULL)
    {
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
mp_obj_t ili9488_get_width(void)
{
    return mp_obj_new_int(display_width);
}
static MP_DEFINE_CONST_FUN_OBJ_0(ili9488_get_width_obj, ili9488_get_width);

mp_obj_t ili9488_get_height(void)
{
    return mp_obj_new_int(display_height);
}
static MP_DEFINE_CONST_FUN_OBJ_0(ili9488_get_height_obj, ili9488_get_height);

static mp_obj_t ili9488_get_orientation(void)
{
    return mp_obj_new_int(current_orientation);
}
static MP_DEFINE_CONST_FUN_OBJ_0(ili9488_get_orientation_obj, ili9488_get_orientation);

static mp_obj_t ili9488_set_line_thickness(mp_obj_t thickness_obj)
{
    int thickness = mp_obj_get_int(thickness_obj);
    if (thickness < 1)
        thickness = 1;
    if (thickness > 20)
        thickness = 20;
    line_thickness = thickness;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(ili9488_set_line_thickness_obj, ili9488_set_line_thickness);

static mp_obj_t ili9488_get_line_thickness(void)
{
    return mp_obj_new_int(line_thickness);
}
static MP_DEFINE_CONST_FUN_OBJ_0(ili9488_get_line_thickness_obj, ili9488_get_line_thickness);

static mp_obj_t ili9488_mem_info(void)
{
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
mp_obj_t ili9488_update_region(size_t n_args, const mp_obj_t *args)
{
    if (!framebuffer || !spi_device || !dma_buffer)
        return mp_const_none;

    int x = mp_obj_get_int(args[0]);
    int y = mp_obj_get_int(args[1]);
    int w = mp_obj_get_int(args[2]);
    int h = mp_obj_get_int(args[3]);

    // Clamp to screen bounds
    if (x < 0)
    {
        w += x;
        x = 0;
    }
    if (y < 0)
    {
        h += y;
        y = 0;
    }
    if (x + w > display_width)
        w = display_width - x;
    if (y + h > display_height)
        h = display_height - y;

    if (w <= 0 || h <= 0)
        return mp_const_none;

    ili9488_set_window(x, y, x + w - 1, y + h - 1);

    size_t row_bytes = w * 3;

    // For small updates, use direct polling transfer
    if (row_bytes <= 512 || h <= 4)
    {
        for (int row = 0; row < h; row++)
        {
            int fb_offset = ((y + row) * display_width + x) * 3;
            ili9488_write_data((uint8_t *)framebuffer + fb_offset, row_bytes);
        }
        return mp_const_none;
    }

    // For larger updates, use DMA with bounce buffer
    gpio_set_level(dc_pin, 1); // Set DC high for data once

    size_t accumulated = 0; // Track bytes in DMA buffer
    spi_transaction_t t = {0};

    for (int row = 0; row < h; row++)
    {
        int fb_offset = ((y + row) * display_width + x) * 3;

        if (accumulated + row_bytes <= DMA_BUFFER_SIZE)
        {
            memcpy(dma_buffer + accumulated, framebuffer + fb_offset, row_bytes);
            accumulated += row_bytes;
        }
        else
        {
            if (accumulated > 0)
            {
                t.length = accumulated * 8;
                t.tx_buffer = dma_buffer;
                spi_device_transmit(spi_device, &t);
                accumulated = 0;
            }

            if (row_bytes <= DMA_BUFFER_SIZE)
            {
                memcpy(dma_buffer, framebuffer + fb_offset, row_bytes);
                accumulated = row_bytes;
            }
            else
            {
                size_t offset = 0;
                while (offset < row_bytes)
                {
                    size_t chunk = (row_bytes - offset > DMA_BUFFER_SIZE) ? DMA_BUFFER_SIZE : (row_bytes - offset);
                    memcpy(dma_buffer, framebuffer + fb_offset + offset, chunk);

                    t.length = chunk * 8;
                    t.tx_buffer = dma_buffer;
                    spi_device_transmit(spi_device, &t);

                    offset += chunk;
                }
            }
        }
    }

    if (accumulated > 0)
    {
        t.length = accumulated * 8;
        t.tx_buffer = dma_buffer;
        spi_device_transmit(spi_device, &t);
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ili9488_update_region_obj, 4, 4, ili9488_update_region);

mp_obj_t ili9488_fill(mp_obj_t color_obj)
{
    uint32_t color = mp_obj_get_int(color_obj);

    if (framebuffer)
    {
        uint8_t r = (color >> 16) & 0xFF;
        uint8_t g = (color >> 8) & 0xFF;
        uint8_t b = color & 0xFF;

        for (int i = 0; i < display_width * display_height; i++)
        {
            framebuffer[i * 3 + 0] = r;
            framebuffer[i * 3 + 1] = g;
            framebuffer[i * 3 + 2] = b;
        }
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(ili9488_fill_obj, ili9488_fill);

static mp_obj_t ili9488_pixel(size_t n_args, const mp_obj_t *args)
{
    int x = mp_obj_get_int(args[0]);
    int y = mp_obj_get_int(args[1]);
    uint32_t color = mp_obj_get_int(args[2]);

    set_pixel_fb(x, y, color);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ili9488_pixel_obj, 3, 3, ili9488_pixel);

// Draw line (Bresenham's algorithm with thickness support)
mp_obj_t ili9488_line(size_t n_args, const mp_obj_t *args)
{
    int x0 = mp_obj_get_int(args[0]);
    int y0 = mp_obj_get_int(args[1]);
    int x1 = mp_obj_get_int(args[2]);
    int y1 = mp_obj_get_int(args[3]);
    uint32_t color = mp_obj_get_int(args[4]);

    if (line_thickness == 1)
    {
        int dx = abs(x1 - x0);
        int dy = abs(y1 - y0);
        int sx = x0 < x1 ? 1 : -1;
        int sy = y0 < y1 ? 1 : -1;
        int err = dx - dy;

        while (1)
        {
            set_pixel_fb(x0, y0, color);
            if (x0 == x1 && y0 == y1)
                break;
            int e2 = 2 * err;
            if (e2 > -dy)
            {
                err -= dy;
                x0 += sx;
            }
            if (e2 < dx)
            {
                err += dx;
                y0 += sy;
            }
        }
        return mp_const_none;
    }

    // Thick line implementation
    int dx = x1 - x0;
    int dy = y1 - y0;
    float length = sqrt(dx * dx + dy * dy);

    if (length < 0.1f)
    {
        int radius = line_thickness / 2;
        for (int j = -radius; j <= radius; j++)
        {
            for (int i = -radius; i <= radius; i++)
            {
                if (i * i + j * j <= radius * radius)
                {
                    set_pixel_fb(x0 + i, y0 + j, color);
                }
            }
        }
        return mp_const_none;
    }

    if (dy == 0)
    {
        int min_x = x0 < x1 ? x0 : x1;
        int max_x = x0 < x1 ? x1 : x0;
        int half_thick = line_thickness / 2;
        for (int y = y0 - half_thick; y <= y0 + half_thick; y++)
        {
            for (int x = min_x; x <= max_x; x++)
            {
                set_pixel_fb(x, y, color);
            }
        }
        return mp_const_none;
    }

    if (dx == 0)
    {
        int min_y = y0 < y1 ? y0 : y1;
        int max_y = y0 < y1 ? y1 : y0;
        int half_thick = line_thickness / 2;
        for (int x = x0 - half_thick; x <= x0 + half_thick; x++)
        {
            for (int y = min_y; y <= max_y; y++)
            {
                set_pixel_fb(x, y, color);
            }
        }
        return mp_const_none;
    }

    float perp_x = -dy / length;
    float perp_y = dx / length;
    float half_thick = line_thickness / 2.0f;

    float c1x = x0 + perp_x * half_thick;
    float c1y = y0 + perp_y * half_thick;
    float c2x = x0 - perp_x * half_thick;
    float c2y = y0 - perp_y * half_thick;
    float c3x = x1 - perp_x * half_thick;
    float c3y = y1 - perp_y * half_thick;
    float c4x = x1 + perp_x * half_thick;
    float c4y = y1 + perp_y * half_thick;

    int min_y = (int)c1y;
    int max_y = (int)c1y;
    if ((int)c2y < min_y)
        min_y = (int)c2y;
    if ((int)c3y < min_y)
        min_y = (int)c3y;
    if ((int)c4y < min_y)
        min_y = (int)c4y;
    if ((int)c2y > max_y)
        max_y = (int)c2y;
    if ((int)c3y > max_y)
        max_y = (int)c3y;
    if ((int)c4y > max_y)
        max_y = (int)c4y;
    min_y -= 1;
    max_y += 1;

    for (int scan_y = min_y; scan_y <= max_y; scan_y++)
    {
        int intersections[4];
        int num_intersections = 0;

        if ((c1y <= scan_y && scan_y <= c2y) || (c2y <= scan_y && scan_y <= c1y))
        {
            if (fabs(c2y - c1y) > 0.01f)
            {
                float t = (scan_y - c1y) / (c2y - c1y);
                intersections[num_intersections++] = (int)(c1x + t * (c2x - c1x));
            }
        }
        if ((c2y <= scan_y && scan_y <= c3y) || (c3y <= scan_y && scan_y <= c2y))
        {
            if (fabs(c3y - c2y) > 0.01f)
            {
                float t = (scan_y - c2y) / (c3y - c2y);
                intersections[num_intersections++] = (int)(c2x + t * (c3x - c2x));
            }
        }
        if ((c3y <= scan_y && scan_y <= c4y) || (c4y <= scan_y && scan_y <= c3y))
        {
            if (fabs(c4y - c3y) > 0.01f)
            {
                float t = (scan_y - c3y) / (c4y - c3y);
                intersections[num_intersections++] = (int)(c3x + t * (c4x - c3x));
            }
        }
        if ((c4y <= scan_y && scan_y <= c1y) || (c1y <= scan_y && scan_y <= c4y))
        {
            if (fabs(c1y - c4y) > 0.01f)
            {
                float t = (scan_y - c4y) / (c1y - c4y);
                intersections[num_intersections++] = (int)(c4x + t * (c1x - c4x));
            }
        }

        for (int i = 0; i < num_intersections - 1; i++)
        {
            for (int j = 0; j < num_intersections - i - 1; j++)
            {
                if (intersections[j] > intersections[j + 1])
                {
                    int temp = intersections[j];
                    intersections[j] = intersections[j + 1];
                    intersections[j + 1] = temp;
                }
            }
        }

        if (num_intersections >= 2)
        {
            int min_x = intersections[0];
            int max_x = intersections[num_intersections - 1];
            for (int x = min_x; x <= max_x; x++)
            {
                set_pixel_fb(x, scan_y, color);
            }
        }
    }

    int radius = line_thickness / 2;
    for (int j = -radius; j <= radius; j++)
    {
        for (int i = -radius; i <= radius; i++)
        {
            if (i * i + j * j <= radius * radius)
            {
                set_pixel_fb(x0 + i, y0 + j, color);
                set_pixel_fb(x1 + i, y1 + j, color);
            }
        }
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ili9488_line_obj, 5, 5, ili9488_line);

mp_obj_t ili9488_rect(size_t n_args, const mp_obj_t *args)
{
    int x = mp_obj_get_int(args[0]);
    int y = mp_obj_get_int(args[1]);
    int w = mp_obj_get_int(args[2]);
    int h = mp_obj_get_int(args[3]);
    uint32_t color = mp_obj_get_int(args[4]);
    uint32_t fill_color = (n_args > 5) ? mp_obj_get_int(args[5]) : COLOR_NONE;

    if (!framebuffer)
        return mp_const_none;

    if (fill_color != COLOR_NONE)
    {
        for (int j = 0; j < h; j++)
        {
            for (int i = 0; i < w; i++)
            {
                set_pixel_fb(x + i, y + j, fill_color);
            }
        }
    }

    for (int i = 0; i < w; i++)
    {
        set_pixel_fb(x + i, y, color);
        set_pixel_fb(x + i, y + h - 1, color);
    }
    for (int i = 0; i < h; i++)
    {
        set_pixel_fb(x, y + i, color);
        set_pixel_fb(x + w - 1, y + i, color);
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ili9488_rect_obj, 5, 6, ili9488_rect);

mp_obj_t ili9488_circle(size_t n_args, const mp_obj_t *args)
{
    int x0 = mp_obj_get_int(args[0]);
    int y0 = mp_obj_get_int(args[1]);
    int r = mp_obj_get_int(args[2]);
    uint32_t color = mp_obj_get_int(args[3]);
    uint32_t fill_color = (n_args > 4) ? mp_obj_get_int(args[4]) : COLOR_NONE;

    if (!framebuffer)
        return mp_const_none;

    if (fill_color != COLOR_NONE)
    {
        for (int y = -r; y <= r; y++)
        {
            for (int x = -r; x <= r; x++)
            {
                if (x * x + y * y <= r * r)
                {
                    set_pixel_fb(x0 + x, y0 + y, fill_color);
                }
            }
        }
    }

    int x = r;
    int y = 0;
    int err = 0;

    while (x >= y)
    {
        set_pixel_fb(x0 + x, y0 + y, color);
        set_pixel_fb(x0 + y, y0 + x, color);
        set_pixel_fb(x0 - y, y0 + x, color);
        set_pixel_fb(x0 - x, y0 + y, color);
        set_pixel_fb(x0 - x, y0 - y, color);
        set_pixel_fb(x0 - y, y0 - x, color);
        set_pixel_fb(x0 + y, y0 - x, color);
        set_pixel_fb(x0 + x, y0 - y, color);

        if (err <= 0)
        {
            y += 1;
            err += 2 * y + 1;
        }
        if (err > 0)
        {
            x -= 1;
            err -= 2 * x + 1;
        }
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ili9488_circle_obj, 4, 5, ili9488_circle);

static mp_obj_t ili9488_triangle(size_t n_args, const mp_obj_t *args)
{
    int x0 = mp_obj_get_int(args[0]);
    int y0 = mp_obj_get_int(args[1]);
    int x1 = mp_obj_get_int(args[2]);
    int y1 = mp_obj_get_int(args[3]);
    int x2 = mp_obj_get_int(args[4]);
    int y2 = mp_obj_get_int(args[5]);
    uint32_t fill_color = (n_args > 7) ? mp_obj_get_int(args[7]) : COLOR_NONE;

    if (!framebuffer)
        return mp_const_none;

    if (fill_color != COLOR_NONE)
    {
        if (y0 > y1)
        {
            int t;
            t = y0;
            y0 = y1;
            y1 = t;
            t = x0;
            x0 = x1;
            x1 = t;
        }
        if (y1 > y2)
        {
            int t;
            t = y1;
            y1 = y2;
            y2 = t;
            t = x1;
            x1 = x2;
            x2 = t;
        }
        if (y0 > y1)
        {
            int t;
            t = y0;
            y0 = y1;
            y1 = t;
            t = x0;
            x0 = x1;
            x1 = t;
        }

        for (int y = y0; y <= y2; y++)
        {
            int xa, xb;
            if (y < y1)
            {
                xa = x0 + (y - y0) * (x1 - x0) / (y1 - y0 + 1);
                xb = x0 + (y - y0) * (x2 - x0) / (y2 - y0 + 1);
            }
            else
            {
                xa = x1 + (y - y1) * (x2 - x1) / (y2 - y1 + 1);
                xb = x0 + (y - y0) * (x2 - x0) / (y2 - y0 + 1);
            }
            if (xa > xb)
            {
                int t = xa;
                xa = xb;
                xb = t;
            }
            for (int x = xa; x <= xb; x++)
            {
                set_pixel_fb(x, y, fill_color);
            }
        }
    }

    mp_obj_t line_args[5];
    line_args[4] = args[6];
    line_args[0] = args[0];
    line_args[1] = args[1];
    line_args[2] = args[2];
    line_args[3] = args[3];
    ili9488_line(5, line_args);
    line_args[0] = args[2];
    line_args[1] = args[3];
    line_args[2] = args[4];
    line_args[3] = args[5];
    ili9488_line(5, line_args);
    line_args[0] = args[4];
    line_args[1] = args[5];
    line_args[2] = args[0];
    line_args[3] = args[1];
    ili9488_line(5, line_args);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ili9488_triangle_obj, 7, 8, ili9488_triangle);

// Basic 8x8 ASCII font (characters 32-127)
// Each character is 8 bytes, each byte is one row of 8 pixels
static const uint8_t font_8x8[96][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // Space (32)
    {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00}, // !
    {0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // "
    {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00}, // #
    {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00}, // $
    {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00}, // %
    {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00}, // &
    {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}, // '
    {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00}, // (
    {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00}, // )
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00}, // *
    {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00}, // +
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06}, // ,
    {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00}, // -
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // .
    {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00}, // /
    {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00}, // 0
    {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00}, // 1
    {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00}, // 2
    {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00}, // 3
    {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00}, // 4
    {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00}, // 5
    {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00}, // 6
    {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00}, // 7
    {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00}, // 8
    {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00}, // 9
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // :
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06}, // ;
    {0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00}, // <
    {0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00}, // =
    {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00}, // >
    {0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00}, // ?
    {0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, // @
    {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00}, // A
    {0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00}, // B
    {0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00}, // C
    {0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00}, // D
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00}, // E
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00}, // F
    {0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00}, // G
    {0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00}, // H
    {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // I
    {0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00}, // J
    {0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00}, // K
    {0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00}, // L
    {0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00}, // M
    {0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00}, // N
    {0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00}, // O
    {0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00}, // P
    {0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00}, // Q
    {0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00}, // R
    {0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00}, // S
    {0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // T
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00}, // U
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, // V
    {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00}, // W
    {0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00}, // X
    {0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00}, // Y
    {0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00}, // Z
    {0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00}, // [
    {0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00}, // backslash
    {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00}, // ]
    {0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00}, // ^
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, // _
    {0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00}, // `
    {0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00}, // a
    {0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00}, // b
    {0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00}, // c
    {0x38, 0x30, 0x30, 0x3e, 0x33, 0x33, 0x6E, 0x00}, // d
    {0x00, 0x00, 0x1E, 0x33, 0x3f, 0x03, 0x1E, 0x00}, // e
    {0x1C, 0x36, 0x06, 0x0f, 0x06, 0x06, 0x0F, 0x00}, // f
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // g
    {0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00}, // h
    {0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // i
    {0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E}, // j
    {0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00}, // k
    {0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // l
    {0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00}, // m
    {0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00}, // n
    {0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00}, // o
    {0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F}, // p
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78}, // q
    {0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00}, // r
    {0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00}, // s
    {0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00}, // t
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00}, // u
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, // v
    {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00}, // w
    {0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00}, // x
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // y
    {0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00}, // z
    {0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00}, // {
    {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00}, // |
    {0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00}, // }
    {0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // ~
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}  // DEL
};

// Draw arc (partial circle)
// Args: center_x, center_y, radius, start_angle_deg, end_angle_deg, color
// Angles in degrees: 0=right, 90=down, 180=left, 270=up (standard screen coordinates)
static mp_obj_t ili9488_arc(size_t n_args, const mp_obj_t *args)
{
    int x0 = mp_obj_get_int(args[0]);
    int y0 = mp_obj_get_int(args[1]);
    int r = mp_obj_get_int(args[2]);
    float start_angle = mp_obj_get_float(args[3]);
    float end_angle = mp_obj_get_float(args[4]);
    uint32_t color = mp_obj_get_int(args[5]);

    if (!framebuffer)
        return mp_const_none;

    // Normalize angles to 0-360 range
    while (start_angle < 0)
        start_angle += 360.0f;
    while (start_angle >= 360.0f)
        start_angle -= 360.0f;
    while (end_angle < 0)
        end_angle += 360.0f;
    while (end_angle >= 360.0f)
        end_angle -= 360.0f;

    // Use Bresenham's circle algorithm but only draw points in angle range
    int x = r;
    int y = 0;
    int err = 0;

    while (x >= y)
    {
        // Calculate angles for all 8 octants
        struct
        {
            int px, py;
        } points[8] = {
            {x0 + x, y0 + y}, // Octant 0
            {x0 + y, y0 + x}, // Octant 1
            {x0 - y, y0 + x}, // Octant 2
            {x0 - x, y0 + y}, // Octant 3
            {x0 - x, y0 - y}, // Octant 4
            {x0 - y, y0 - x}, // Octant 5
            {x0 + y, y0 - x}, // Octant 6
            {x0 + x, y0 - y}  // Octant 7
        };

        // Check and draw each point if it's in the angle range
        for (int i = 0; i < 8; i++)
        {
            int dx = points[i].px - x0;
            int dy = points[i].py - y0;

            // Calculate angle in degrees (atan2 returns radians)
            // atan2(dy, dx) gives angle from -π to π
            float angle = atan2f((float)dy, (float)dx) * 180.0f / M_PI;
            if (angle < 0)
                angle += 360.0f;

            // Check if angle is within the arc range (handles wraparound)
            bool in_range;
            if (start_angle <= end_angle)
            {
                in_range = (angle >= start_angle && angle <= end_angle);
            }
            else
            {
                // Wraparound case (e.g., 350 to 10 degrees)
                in_range = (angle >= start_angle || angle <= end_angle);
            }

            if (in_range)
            {
                set_pixel_fb(points[i].px, points[i].py, color);
            }
        }

        if (err <= 0)
        {
            y += 1;
            err += 2 * y + 1;
        }
        if (err > 0)
        {
            x -= 1;
            err -= 2 * x + 1;
        }
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ili9488_arc_obj, 6, 6, ili9488_arc);

// Helper function to render text using custom Python font
static void render_text_custom_font(int x, int y, const char *text, uint32_t color, uint32_t bg_color)
{
    mp_printf(&mp_plat_print, "ILI9488: Rendering custom font text\n");
    mp_printf(&mp_plat_print, "ILI9488: Custom font object: %p\n", custom_font);

    // Get font functions using string literals
    qstr get_ch_qstr = qstr_from_str("get_ch");

    mp_obj_t get_ch_func = mp_load_attr(custom_font, get_ch_qstr);
    mp_printf(&mp_plat_print, "ILI9488: get_ch function object: %p\n", get_ch_func);

    int cursor_x = x;

    // Render each character
    for (const char *c = text; *c != '\0'; c++)
    {
        uint8_t ch = (uint8_t)*c;

        // Skip non-printable characters
        if (ch < 32 || ch > 126)
        {
            continue;
        }

        char ch_str[2] = {(char)ch, '\0'};
        mp_obj_t ch_obj = mp_obj_new_str(ch_str, 1);

        // Get character data: get_ch(ch) returns (buffer, height, width)
        // Note: font_to_py format returns HEIGHT first, then WIDTH
        mp_obj_t char_data = mp_call_function_1(get_ch_func, ch_obj);

        // Unpack tuple: (buffer, height, width)
        mp_obj_t *items;
        size_t len;
        mp_obj_get_array(char_data, &len, &items);

        if (len != 3)
        {
            continue; // Invalid data
        }

        // Get buffer, height (items[1]), and width (items[2])
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(items[0], &bufinfo, MP_BUFFER_READ);
        int char_height = mp_obj_get_int(items[1]); // HEIGHT is second
        int char_width = mp_obj_get_int(items[2]);  // WIDTH is third

        const uint8_t *glyph_data = (const uint8_t *)bufinfo.buf;

        // Draw character
        int bytes_per_row = (char_width + 7) / 8;

        for (int py = 0; py < char_height; py++)
        {
            for (int px = 0; px < char_width; px++)
            {
                // Calculate byte and bit position (row-by-row layout)
                int byte_pos = py * bytes_per_row + px / 8;
                int bit_pos = px % 8;

                // Check if pixel is set (MSB-first bit order)
                bool pixel_set = (glyph_data[byte_pos] & (0x80 >> bit_pos)) != 0;

                int screen_x = cursor_x + px;
                int screen_y = y + py;

                if (pixel_set)
                {
                    set_pixel_fb(screen_x, screen_y, color);
                }
                else if (bg_color != COLOR_NONE)
                {
                    set_pixel_fb(screen_x, screen_y, bg_color);
                }
            }
        }

        cursor_x += char_width;
    }
}

// Text rendering function
// Args: x, y, text, color, [bg_color], [size]
static mp_obj_t ili9488_text(size_t n_args, const mp_obj_t *args)
{
    if (!framebuffer)
    {
        return mp_const_none;
    }

    int x = mp_obj_get_int(args[0]);
    int y = mp_obj_get_int(args[1]);
    const char *text = mp_obj_str_get_str(args[2]);
    uint32_t color = mp_obj_get_int(args[3]);

    // Optional background color (default: transparent)
    uint32_t bg_color = COLOR_NONE;
    if (n_args >= 5 && args[4] != mp_const_none)
    {
        bg_color = mp_obj_get_int(args[4]);
    }

    // If custom font is set, use it
    if (custom_font != MP_OBJ_NULL)
    {
        render_text_custom_font(x, y, text, color, bg_color);
        return mp_const_none;
    }

    // Otherwise, use built-in 8x8 font with size multiplier
    // Optional size multiplier (default: 1)
    int size = 1;
    if (n_args >= 6)
    {
        size = mp_obj_get_int(args[5]);
        if (size < 1)
            size = 1;
        if (size > 8)
            size = 8; // Limit to reasonable size
    }

    int char_width = 8 * size;
    int cursor_x = x;

    // Render each character
    for (const char *c = text; *c != '\0'; c++)
    {
        uint8_t ch = (uint8_t)*c;

        // Only render printable ASCII characters
        if (ch < 32 || ch > 127)
        {
            ch = 32; // Replace with space
        }

        const uint8_t *glyph = font_8x8[ch - 32];

        // Draw character
        for (int row = 0; row < 8; row++)
        {
            uint8_t byte = glyph[row];
            for (int col = 0; col < 8; col++)
            {
                // Check if pixel is set in font bitmap (LSB = leftmost pixel)
                if (byte & (1 << col))
                {
                    // Draw foreground pixel(s)
                    for (int sy = 0; sy < size; sy++)
                    {
                        for (int sx = 0; sx < size; sx++)
                        {
                            int px = cursor_x + col * size + sx;
                            int py = y + row * size + sy;
                            set_pixel_fb(px, py, color);
                        }
                    }
                }
                else if (bg_color != COLOR_NONE)
                {
                    // Draw background pixel(s)
                    for (int sy = 0; sy < size; sy++)
                    {
                        for (int sx = 0; sx < size; sx++)
                        {
                            int px = cursor_x + col * size + sx;
                            int py = y + row * size + sy;
                            set_pixel_fb(px, py, bg_color);
                        }
                    }
                }
            }
        }

        cursor_x += char_width;
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ili9488_text_obj, 4, 6, ili9488_text);

// Set custom font from Python module
// Args: font_module
static mp_obj_t ili9488_set_font(mp_obj_t font_module)
{
    custom_font = font_module;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(ili9488_set_font_obj, ili9488_set_font);

// Clear custom font (revert to built-in)
static mp_obj_t ili9488_clear_font(void)
{
    custom_font = MP_OBJ_NULL;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(ili9488_clear_font_obj, ili9488_clear_font);

typedef struct
{
    mp_obj_base_t base;
    uint8_t *pixels;
    uint8_t *background;
    int width;
    int height;
    int x;
    int y;
    int old_x;
    int old_y;
    bool visible;
    bool moved;
} sprite_obj_t;

const mp_obj_type_t sprite_type;

static mp_obj_t sprite_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args)
{
    (void)type;
    mp_arg_check_num(n_args, n_kw, 2, 2, false);

    sprite_obj_t *self = m_new_obj(sprite_obj_t);
    self->base.type = &sprite_type;
    self->width = mp_obj_get_int(args[0]);
    self->height = mp_obj_get_int(args[1]);
    self->pixels = m_new(uint8_t, self->width * self->height * 3);
    self->background = m_new(uint8_t, self->width * self->height * 3);
    self->x = 0;
    self->y = 0;
    self->old_x = 0;
    self->old_y = 0;
    self->visible = false;
    self->moved = false;

    memset(self->pixels, 0, self->width * self->height * 3);
    memset(self->background, 0, self->width * self->height * 3);

    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t sprite_set_pixel(size_t n_args, const mp_obj_t *args)
{
    sprite_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    int x = mp_obj_get_int(args[1]);
    int y = mp_obj_get_int(args[2]);
    uint32_t color = mp_obj_get_int(args[3]);

    if (x >= 0 && x < self->width && y >= 0 && y < self->height)
    {
        int offset = (y * self->width + x) * 3;
        self->pixels[offset + 0] = (color >> 16) & 0xFF;
        self->pixels[offset + 1] = (color >> 8) & 0xFF;
        self->pixels[offset + 2] = color & 0xFF;
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sprite_set_pixel_obj, 4, 4, sprite_set_pixel);

static mp_obj_t sprite_draw(size_t n_args, const mp_obj_t *args)
{
    sprite_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    int new_x = mp_obj_get_int(args[1]);
    int new_y = mp_obj_get_int(args[2]);
    bool auto_update = (n_args > 3) ? mp_obj_is_true(args[3]) : false;

    if (!framebuffer)
        return mp_const_none;

    bool sprite_moved = self->visible && (self->x != new_x || self->y != new_y);

    if (self->visible)
    {
        for (int j = 0; j < self->height; j++)
        {
            for (int i = 0; i < self->width; i++)
            {
                int px = self->x + i;
                int py = self->y + j;
                if (px >= 0 && px < display_width && py >= 0 && py < display_height)
                {
                    int fb_offset = (py * display_width + px) * 3;
                    int bg_offset = (j * self->width + i) * 3;
                    framebuffer[fb_offset + 0] = self->background[bg_offset + 0];
                    framebuffer[fb_offset + 1] = self->background[bg_offset + 1];
                    framebuffer[fb_offset + 2] = self->background[bg_offset + 2];
                }
            }
        }
    }

    for (int j = 0; j < self->height; j++)
    {
        for (int i = 0; i < self->width; i++)
        {
            int px = new_x + i;
            int py = new_y + j;
            if (px >= 0 && px < display_width && py >= 0 && py < display_height)
            {
                int fb_offset = (py * display_width + px) * 3;
                int bg_offset = (j * self->width + i) * 3;
                self->background[bg_offset + 0] = framebuffer[fb_offset + 0];
                self->background[bg_offset + 1] = framebuffer[fb_offset + 1];
                self->background[bg_offset + 2] = framebuffer[fb_offset + 2];
            }
        }
    }

    for (int j = 0; j < self->height; j++)
    {
        for (int i = 0; i < self->width; i++)
        {
            int px = new_x + i;
            int py = new_y + j;
            int sp_offset = (j * self->width + i) * 3;

            uint8_t r = self->pixels[sp_offset + 0];
            uint8_t g = self->pixels[sp_offset + 1];
            uint8_t b = self->pixels[sp_offset + 2];

            if ((r != 0 || g != 0 || b != 0) &&
                px >= 0 && px < display_width && py >= 0 && py < display_height)
            {
                int fb_offset = (py * display_width + px) * 3;
                framebuffer[fb_offset + 0] = r;
                framebuffer[fb_offset + 1] = g;
                framebuffer[fb_offset + 2] = b;
            }
        }
    }

    if (auto_update && spi_device && dma_buffer)
    {
        if (sprite_moved)
        {
            int min_x = (self->x < new_x) ? self->x : new_x;
            int min_y = (self->y < new_y) ? self->y : new_y;
            int max_x = ((self->x + self->width) > (new_x + self->width)) ? (self->x + self->width) : (new_x + self->width);
            int max_y = ((self->y + self->height) > (new_y + self->height)) ? (self->y + self->height) : (new_y + self->height);

            int update_w = max_x - min_x;
            int update_h = max_y - min_y;

            if (min_x < 0)
            {
                update_w += min_x;
                min_x = 0;
            }
            if (min_y < 0)
            {
                update_h += min_y;
                min_y = 0;
            }
            if (min_x + update_w > display_width)
                update_w = display_width - min_x;
            if (min_y + update_h > display_height)
                update_h = display_height - min_y;

            if (update_w > 0 && update_h > 0)
            {
                ili9488_set_window(min_x, min_y, min_x + update_w - 1, min_y + update_h - 1);

                gpio_set_level(dc_pin, 1);
                size_t row_bytes = update_w * 3;

                for (int row = 0; row < update_h; row++)
                {
                    int fb_offset = ((min_y + row) * display_width + min_x) * 3;

                    if (row_bytes <= DMA_BUFFER_SIZE)
                    {
                        memcpy(dma_buffer, framebuffer + fb_offset, row_bytes);

                        spi_transaction_t t;
                        memset(&t, 0, sizeof(t));
                        t.length = row_bytes * 8;
                        t.tx_buffer = dma_buffer;
                        spi_device_transmit(spi_device, &t);
                    }
                    else
                    {
                        ili9488_write_data((uint8_t *)framebuffer + fb_offset, row_bytes);
                    }
                }
            }
        }
        else
        {
            if (new_x >= 0 && new_y >= 0 &&
                new_x + self->width <= display_width &&
                new_y + self->height <= display_height)
            {
                ili9488_set_window(new_x, new_y,
                                   new_x + self->width - 1,
                                   new_y + self->height - 1);

                gpio_set_level(dc_pin, 1);
                size_t row_bytes = self->width * 3;

                for (int row = 0; row < self->height; row++)
                {
                    int fb_offset = ((new_y + row) * display_width + new_x) * 3;

                    if (row_bytes <= DMA_BUFFER_SIZE)
                    {
                        memcpy(dma_buffer, framebuffer + fb_offset, row_bytes);

                        spi_transaction_t t;
                        memset(&t, 0, sizeof(t));
                        t.length = row_bytes * 8;
                        t.tx_buffer = dma_buffer;
                        spi_device_transmit(spi_device, &t);
                    }
                    else
                    {
                        ili9488_write_data((uint8_t *)framebuffer + fb_offset, row_bytes);
                    }
                }
            }
        }
    }

    self->old_x = self->x;
    self->old_y = self->y;
    self->x = new_x;
    self->y = new_y;
    self->visible = true;
    self->moved = sprite_moved;

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sprite_draw_obj, 3, 4, sprite_draw);

static mp_obj_t sprite_hide(mp_obj_t self_in)
{
    sprite_obj_t *self = MP_OBJ_TO_PTR(self_in);

    if (!framebuffer || !self->visible)
        return mp_const_none;

    for (int j = 0; j < self->height; j++)
    {
        for (int i = 0; i < self->width; i++)
        {
            int px = self->x + i;
            int py = self->y + j;
            if (px >= 0 && px < display_width && py >= 0 && py < display_height)
            {
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
    {MP_ROM_QSTR(MP_QSTR_set_pixel), MP_ROM_PTR(&sprite_set_pixel_obj)},
    {MP_ROM_QSTR(MP_QSTR_draw), MP_ROM_PTR(&sprite_draw_obj)},
    {MP_ROM_QSTR(MP_QSTR_hide), MP_ROM_PTR(&sprite_hide_obj)},
};
static MP_DEFINE_CONST_DICT(sprite_locals_dict, sprite_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    sprite_type,
    MP_QSTR_Sprite,
    MP_TYPE_FLAG_NONE,
    make_new, sprite_make_new,
    locals_dict, &sprite_locals_dict);

// Update display using DMA with bounce buffer
static mp_obj_t ili9488_show(void)
{
    if (!framebuffer || !spi_device || !dma_buffer)
    {
        mp_printf(&mp_plat_print, "ILI9488: Cannot show - missing resources\n");
        return mp_const_none;
    }

    ili9488_set_window(0, 0, display_width - 1, display_height - 1);

    size_t total_bytes = display_width * display_height * 3;
    size_t chunks_sent = 0;

    mp_printf(&mp_plat_print, "ILI9488: Updating display with DMA (%d bytes, %d chunks)...\n",
              total_bytes, (total_bytes + DMA_BUFFER_SIZE - 1) / DMA_BUFFER_SIZE);

    // Set DC high once for all data transfers
    gpio_set_level(dc_pin, 1);

    // Transfer framebuffer using DMA bounce buffer
    size_t offset = 0;
    while (offset < total_bytes)
    {
        size_t chunk_size = (total_bytes - offset > DMA_BUFFER_SIZE) ? DMA_BUFFER_SIZE : (total_bytes - offset);

        // Copy chunk from PSRAM framebuffer to DMA buffer
        memcpy(dma_buffer, framebuffer + offset, chunk_size);

        // Transfer via DMA
        spi_transaction_t t;
        memset(&t, 0, sizeof(t));
        t.length = chunk_size * 8; // Length in bits
        t.tx_buffer = dma_buffer;

        esp_err_t ret = spi_device_transmit(spi_device, &t);
        if (ret != ESP_OK)
        {
            mp_printf(&mp_plat_print, "ILI9488: DMA transfer failed at offset %d/%d: %d\n",
                      offset, total_bytes, ret);
            return mp_const_none;
        }

        offset += chunk_size;
        chunks_sent++;
    }

    mp_printf(&mp_plat_print, "ILI9488: Display updated successfully (%d chunks)\n", chunks_sent);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(ili9488_show_obj, ili9488_show);

static const mp_rom_map_elem_t ili9488_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_ili9488)},
    {MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&ili9488_init_obj)},
    {MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&ili9488_deinit_obj)},
    {MP_ROM_QSTR(MP_QSTR_fill), MP_ROM_PTR(&ili9488_fill_obj)},
    {MP_ROM_QSTR(MP_QSTR_pixel), MP_ROM_PTR(&ili9488_pixel_obj)},
    {MP_ROM_QSTR(MP_QSTR_line), MP_ROM_PTR(&ili9488_line_obj)},
    {MP_ROM_QSTR(MP_QSTR_set_line_thickness), MP_ROM_PTR(&ili9488_set_line_thickness_obj)},
    {MP_ROM_QSTR(MP_QSTR_get_line_thickness), MP_ROM_PTR(&ili9488_get_line_thickness_obj)},
    {MP_ROM_QSTR(MP_QSTR_rect), MP_ROM_PTR(&ili9488_rect_obj)},
    {MP_ROM_QSTR(MP_QSTR_circle), MP_ROM_PTR(&ili9488_circle_obj)},
    {MP_ROM_QSTR(MP_QSTR_arc), MP_ROM_PTR(&ili9488_arc_obj)},
    {MP_ROM_QSTR(MP_QSTR_triangle), MP_ROM_PTR(&ili9488_triangle_obj)},
    {MP_ROM_QSTR(MP_QSTR_text), MP_ROM_PTR(&ili9488_text_obj)},
    {MP_ROM_QSTR(MP_QSTR_set_font), MP_ROM_PTR(&ili9488_set_font_obj)},
    {MP_ROM_QSTR(MP_QSTR_clear_font), MP_ROM_PTR(&ili9488_clear_font_obj)},
    {MP_ROM_QSTR(MP_QSTR_show), MP_ROM_PTR(&ili9488_show_obj)},
    {MP_ROM_QSTR(MP_QSTR_update_region), MP_ROM_PTR(&ili9488_update_region_obj)},
    {MP_ROM_QSTR(MP_QSTR_Sprite), MP_ROM_PTR(&sprite_type)},
    {MP_ROM_QSTR(MP_QSTR_get_width), MP_ROM_PTR(&ili9488_get_width_obj)},
    {MP_ROM_QSTR(MP_QSTR_get_height), MP_ROM_PTR(&ili9488_get_height_obj)},
    {MP_ROM_QSTR(MP_QSTR_get_orientation), MP_ROM_PTR(&ili9488_get_orientation_obj)},
    {MP_ROM_QSTR(MP_QSTR_WIDTH), MP_ROM_INT(ILI9488_PHYS_WIDTH)},
    {MP_ROM_QSTR(MP_QSTR_HEIGHT), MP_ROM_INT(ILI9488_PHYS_HEIGHT)},
    {MP_ROM_QSTR(MP_QSTR_ORIENTATION_PORTRAIT), MP_ROM_INT(ORIENTATION_PORTRAIT)},
    {MP_ROM_QSTR(MP_QSTR_ORIENTATION_LANDSCAPE), MP_ROM_INT(ORIENTATION_LANDSCAPE)},
    {MP_ROM_QSTR(MP_QSTR_ORIENTATION_PORTRAIT_INV), MP_ROM_INT(ORIENTATION_PORTRAIT_INV)},
    {MP_ROM_QSTR(MP_QSTR_ORIENTATION_LANDSCAPE_INV), MP_ROM_INT(ORIENTATION_LANDSCAPE_INV)},
    {MP_ROM_QSTR(MP_QSTR_mem_info), MP_ROM_PTR(&ili9488_mem_info_obj)},
    {MP_ROM_QSTR(MP_QSTR_COLOR_NONE), MP_ROM_INT(COLOR_NONE)},
};
static MP_DEFINE_CONST_DICT(ili9488_module_globals, ili9488_module_globals_table);

const mp_obj_module_t ili9488_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&ili9488_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_ili9488, ili9488_module);
