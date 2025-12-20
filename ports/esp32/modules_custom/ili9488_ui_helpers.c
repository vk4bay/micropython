/*
 * ili9488_ui_helpers.c - High-performance UI widget library for ILI9488 display
 * This is a C port of the Python ili9488_ui module for significantly better performance.
 * Widgets are rendered with minimal Python overhead and optimized batching.
 *
 * IMPORTANT: For maximum performance, draw functions do NOT automatically update the display.
 * Call ili9488.update_region() manually after drawing multiple widgets to batch updates:
 *
 * Example:
 *   import ili9488_ui as ui
 *   import ili9488
 *
 *   # Draw multiple widgets without updating
 *   ui.draw_button3d(10, 10, 120, 40, ui.COLOR_BTN_PRIMARY, False, True)
 *   ui.draw_button3d(10, 60, 120, 40, ui.COLOR_BTN_SUCCESS, False, True)
 *
 *   # Update display once after all drawing is done
 *   ili9488.update_region(10, 10, 120, 100)
 */

#include "py/obj.h"
#include "py/runtime.h"
#include "py/mphal.h"
#include <string.h>
#include <math.h>

// Forward declarations from ili9488.c - these are the low-level drawing primitives
// We declare them extern here so we can call them directly
extern mp_obj_t ili9488_rect(size_t n_args, const mp_obj_t *args);
extern mp_obj_t ili9488_line(size_t n_args, const mp_obj_t *args);
extern mp_obj_t ili9488_circle(size_t n_args, const mp_obj_t *args);
extern mp_obj_t ili9488_update_region(size_t n_args, const mp_obj_t *args);
extern mp_obj_t ili9488_fill(mp_obj_t color_obj);
extern mp_obj_t ili9488_get_width(void);
extern mp_obj_t ili9488_get_height(void);

// Color constants
#define COLOR_BLACK 0x000000
#define COLOR_WHITE 0xFFFFFF
#define COLOR_RED 0xFF0000
#define COLOR_GREEN 0x00FF00
#define COLOR_BLUE 0x0000FF
#define COLOR_YELLOW 0xFFFF00
#define COLOR_CYAN 0x00FFFF
#define COLOR_MAGENTA 0xFF00FF
#define COLOR_ORANGE 0xFF8000
#define COLOR_PURPLE 0x8000FF

#define COLOR_GRAY_DARK 0x404040
#define COLOR_GRAY 0x808080
#define COLOR_GRAY_LIGHT 0xC0C0C0
#define COLOR_GRAY_LIGHTER 0xE0E0E0

#define COLOR_BTN_PRIMARY 0x0066CC
#define COLOR_BTN_SUCCESS 0x00AA00
#define COLOR_BTN_WARNING 0xFF8800
#define COLOR_BTN_DANGER 0xCC0000
#define COLOR_BTN_DEFAULT COLOR_GRAY

// Fast color manipulation functions using integer arithmetic
static inline uint32_t darken_color(uint32_t color, int percent)
{
    // percent: 0-100, where 100 = no darkening, 0 = black
    uint8_t r = (((color >> 16) & 0xFF) * percent) / 100;
    uint8_t g = (((color >> 8) & 0xFF) * percent) / 100;
    uint8_t b = ((color & 0xFF) * percent) / 100;
    return (r << 16) | (g << 8) | b;
}

static inline uint32_t lighten_color(uint32_t color, int percent)
{
    // percent: 100+ where 100 = no change, 130 = 30% lighter
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    r = (r * percent) / 100;
    g = (g * percent) / 100;
    b = (b * percent) / 100;
    return (r << 16) | (g << 8) | b;
}

static inline uint32_t blend_color(uint32_t color1, uint32_t color2, int alpha)
{
    // alpha: 0-100, where 0 = full color1, 100 = full color2
    uint8_t r1 = (color1 >> 16) & 0xFF;
    uint8_t g1 = (color1 >> 8) & 0xFF;
    uint8_t b1 = color1 & 0xFF;

    uint8_t r2 = (color2 >> 16) & 0xFF;
    uint8_t g2 = (color2 >> 8) & 0xFF;
    uint8_t b2 = color2 & 0xFF;

    uint8_t r = (r1 * (100 - alpha) + r2 * alpha) / 100;
    uint8_t g = (g1 * (100 - alpha) + g2 * alpha) / 100;
    uint8_t b = (b1 * (100 - alpha) + b2 * alpha) / 100;

    return (r << 16) | (g << 8) | b;
}

// Helper: Call ili9488 rect function
static inline void draw_rect(int x, int y, int w, int h, uint32_t border, uint32_t fill)
{
    mp_obj_t args[6];
    args[0] = mp_obj_new_int(x);
    args[1] = mp_obj_new_int(y);
    args[2] = mp_obj_new_int(w);
    args[3] = mp_obj_new_int(h);
    args[4] = mp_obj_new_int(border);
    args[5] = mp_obj_new_int(fill);
    ili9488_rect(6, args);
}

// Helper: Call ili9488 line function
static inline void draw_line(int x0, int y0, int x1, int y1, uint32_t color)
{
    mp_obj_t args[5];
    args[0] = mp_obj_new_int(x0);
    args[1] = mp_obj_new_int(y0);
    args[2] = mp_obj_new_int(x1);
    args[3] = mp_obj_new_int(y1);
    args[4] = mp_obj_new_int(color);
    ili9488_line(5, args);
}

// Helper: Call ili9488 circle function
static inline void draw_circle(int x, int y, int r, uint32_t border, uint32_t fill)
{
    mp_obj_t args[5];
    args[0] = mp_obj_new_int(x);
    args[1] = mp_obj_new_int(y);
    args[2] = mp_obj_new_int(r);
    args[3] = mp_obj_new_int(border);
    args[4] = mp_obj_new_int(fill);
    ili9488_circle(5, args);
}

// Helper: Update display region
static inline void update_region(int x, int y, int w, int h)
{
    mp_obj_t args[4];
    args[0] = mp_obj_new_int(x);
    args[1] = mp_obj_new_int(y);
    args[2] = mp_obj_new_int(w);
    args[3] = mp_obj_new_int(h);
    ili9488_update_region(4, args);
}

//=============================================================================
// Button3D - Optimized 3D button rendering
//=============================================================================

// Draw a 3D button with raised/sunken appearance
// args: x, y, width, height, color, pressed (bool), enabled (bool)
static mp_obj_t ui_draw_button3d(size_t n_args, const mp_obj_t *args)
{
    int x = mp_obj_get_int(args[0]);
    int y = mp_obj_get_int(args[1]);
    int width = mp_obj_get_int(args[2]);
    int height = mp_obj_get_int(args[3]);
    uint32_t color = mp_obj_get_int(args[4]);
    bool pressed = (n_args > 5) ? mp_obj_is_true(args[5]) : false;
    bool enabled = (n_args > 6) ? mp_obj_is_true(args[6]) : true;

    const int border_width = 2;
    uint32_t base_color = enabled ? color : COLOR_GRAY;
    uint32_t top_color, bottom_color, face_color;

    if (pressed)
    {
        // Pressed state - sunken appearance
        top_color = darken_color(base_color, 50);      // Dark top/left
        bottom_color = lighten_color(base_color, 120); // Light bottom/right
        face_color = darken_color(base_color, 80);     // Darker face
    }
    else
    {
        // Raised state - elevated appearance
        top_color = lighten_color(base_color, 130);  // Light top/left
        bottom_color = darken_color(base_color, 60); // Dark bottom/right
        face_color = base_color;                     // Normal face
    }

    // Draw button face (inner rectangle)
    draw_rect(x + border_width, y + border_width,
              width - border_width * 2, height - border_width * 2,
              face_color, face_color);

    // Draw 3D edges - top and left (highlight)
    for (int i = 0; i < border_width; i++)
    {
        // Top edge
        draw_line(x + i, y + i, x + width - i - 1, y + i, top_color);
        // Left edge
        draw_line(x + i, y + i, x + i, y + height - i - 1, top_color);
    }

    // Draw 3D edges - bottom and right (shadow)
    for (int i = 0; i < border_width; i++)
    {
        // Bottom edge
        draw_line(x + i, y + height - i - 1,
                  x + width - i - 1, y + height - i - 1, bottom_color);
        // Right edge
        draw_line(x + width - i - 1, y + i,
                  x + width - i - 1, y + height - i - 1, bottom_color);
    }

    // Draw text indicator (simple dot in center for now - could add real text later)
    int center_x = x + width / 2;
    int center_y = y + height / 2;
    if (pressed)
    {
        center_x += 1;
        center_y += 1;
    }
    draw_circle(center_x, center_y, 3, COLOR_WHITE, COLOR_WHITE);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ui_draw_button3d_obj, 5, 7, ui_draw_button3d);

//=============================================================================
// Panel - Optimized panel/container rendering
//=============================================================================

// Draw a panel with optional border
// args: x, y, width, height, bg_color, border_color, has_border (bool)
static mp_obj_t ui_draw_panel(size_t n_args, const mp_obj_t *args)
{
    int x = mp_obj_get_int(args[0]);
    int y = mp_obj_get_int(args[1]);
    int width = mp_obj_get_int(args[2]);
    int height = mp_obj_get_int(args[3]);
    uint32_t bg_color = mp_obj_get_int(args[4]);
    uint32_t border_color = (n_args > 5) ? mp_obj_get_int(args[5]) : COLOR_GRAY_DARK;
    bool has_border = (n_args > 6) ? mp_obj_is_true(args[6]) : true;

    if (has_border)
    {
        // Draw panel with border
        draw_rect(x, y, width, height, border_color, bg_color);
    }
    else
    {
        // Draw filled rectangle without border (use fill color for both)
        draw_rect(x, y, width, height, bg_color, bg_color);
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ui_draw_panel_obj, 5, 7, ui_draw_panel);

//=============================================================================
// ProgressBar - Optimized progress bar rendering
//=============================================================================

// Draw a progress bar
// args: x, y, width, height, value, max_value, fg_color, bg_color, border_color
static mp_obj_t ui_draw_progressbar(size_t n_args, const mp_obj_t *args)
{
    int x = mp_obj_get_int(args[0]);
    int y = mp_obj_get_int(args[1]);
    int width = mp_obj_get_int(args[2]);
    int height = mp_obj_get_int(args[3]);
    int value = mp_obj_get_int(args[4]);
    int max_value = mp_obj_get_int(args[5]);
    uint32_t fg_color = (n_args > 6) ? mp_obj_get_int(args[6]) : COLOR_BTN_PRIMARY;
    uint32_t bg_color = (n_args > 7) ? mp_obj_get_int(args[7]) : COLOR_GRAY_LIGHT;
    uint32_t border_color = (n_args > 8) ? mp_obj_get_int(args[8]) : COLOR_GRAY_DARK;

    // Clamp value
    if (value < 0)
        value = 0;
    if (value > max_value)
        value = max_value;

    // Draw border and background
    draw_rect(x, y, width, height, border_color, bg_color);

    // Calculate fill width (with 2-pixel margin)
    int fill_width = 0;
    if (max_value > 0)
    {
        fill_width = ((width - 4) * value) / max_value;
    }

    // Draw progress fill
    if (fill_width > 2)
    {
        draw_rect(x + 2, y + 2, fill_width, height - 4, fg_color, fg_color);
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ui_draw_progressbar_obj, 6, 9, ui_draw_progressbar);

//=============================================================================
// CheckBox - Optimized checkbox rendering
//=============================================================================

// Draw a checkbox
// args: x, y, size, checked (bool), enabled (bool), color
static mp_obj_t ui_draw_checkbox(size_t n_args, const mp_obj_t *args)
{
    int x = mp_obj_get_int(args[0]);
    int y = mp_obj_get_int(args[1]);
    int size = mp_obj_get_int(args[2]);
    bool checked = mp_obj_is_true(args[3]);
    bool enabled = (n_args > 4) ? mp_obj_is_true(args[4]) : true;
    uint32_t color = (n_args > 5) ? mp_obj_get_int(args[5]) : COLOR_BTN_PRIMARY;

    uint32_t box_color = enabled ? COLOR_GRAY_DARK : COLOR_GRAY;

    // Draw box
    draw_rect(x, y, size, size, box_color, COLOR_WHITE);

    // Draw checkmark if checked
    if (checked)
    {
        int margin = size / 5;
        // Draw X pattern for checkmark
        draw_line(x + margin, y + margin,
                  x + size - margin, y + size - margin, color);
        draw_line(x + size - margin, y + margin,
                  x + margin, y + size - margin, color);
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ui_draw_checkbox_obj, 4, 6, ui_draw_checkbox);

//=============================================================================
// RadioButton - Optimized radio button rendering
//=============================================================================

// Draw a radio button
// args: center_x, center_y, radius, selected (bool), enabled (bool), color
static mp_obj_t ui_draw_radiobutton(size_t n_args, const mp_obj_t *args)
{
    int center_x = mp_obj_get_int(args[0]);
    int center_y = mp_obj_get_int(args[1]);
    int radius = mp_obj_get_int(args[2]);
    bool selected = mp_obj_is_true(args[3]);
    bool enabled = (n_args > 4) ? mp_obj_is_true(args[4]) : true;
    uint32_t color = (n_args > 5) ? mp_obj_get_int(args[5]) : COLOR_BTN_PRIMARY;

    uint32_t border_color = enabled ? COLOR_GRAY_DARK : COLOR_GRAY;

    // Draw outer circle
    draw_circle(center_x, center_y, radius, border_color, COLOR_WHITE);

    // Draw inner circle if selected
    if (selected)
    {
        int inner_radius = (radius > 4) ? (radius - 4) : 1;
        draw_circle(center_x, center_y, inner_radius, color, color);
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ui_draw_radiobutton_obj, 4, 6, ui_draw_radiobutton);

//=============================================================================
// Dialog - Optimized dialog box rendering
//=============================================================================

// Draw a dialog frame with title bar
// args: x, y, width, height, title_height
static mp_obj_t ui_draw_dialog_frame(size_t n_args, const mp_obj_t *args)
{
    int x = mp_obj_get_int(args[0]);
    int y = mp_obj_get_int(args[1]);
    int width = mp_obj_get_int(args[2]);
    int height = mp_obj_get_int(args[3]);
    int title_height = (n_args > 4) ? mp_obj_get_int(args[4]) : 30;

    const int shadow_offset = 4;

    // Draw shadow
    draw_rect(x + shadow_offset, y + shadow_offset, width, height,
              COLOR_GRAY_DARK, COLOR_GRAY_DARK);

    // Draw dialog background
    draw_rect(x, y, width, height, COLOR_GRAY_DARK, COLOR_GRAY_LIGHT);

    // Draw title bar
    draw_rect(x, y, width, title_height, COLOR_GRAY_DARK, COLOR_BTN_PRIMARY);

    // Draw title indicator (simple dot)
    draw_circle(x + 15, y + title_height / 2, 3, COLOR_WHITE, COLOR_WHITE);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ui_draw_dialog_frame_obj, 4, 5, ui_draw_dialog_frame);

//=============================================================================
// Color manipulation functions exposed to Python
//=============================================================================

static mp_obj_t ui_darken_color(mp_obj_t color_obj, mp_obj_t percent_obj)
{
    uint32_t color = mp_obj_get_int(color_obj);
    int percent = mp_obj_get_int(percent_obj);
    return mp_obj_new_int(darken_color(color, percent));
}
static MP_DEFINE_CONST_FUN_OBJ_2(ui_darken_color_obj, ui_darken_color);

static mp_obj_t ui_lighten_color(mp_obj_t color_obj, mp_obj_t percent_obj)
{
    uint32_t color = mp_obj_get_int(color_obj);
    int percent = mp_obj_get_int(percent_obj);
    return mp_obj_new_int(lighten_color(color, percent));
}
static MP_DEFINE_CONST_FUN_OBJ_2(ui_lighten_color_obj, ui_lighten_color);

static mp_obj_t ui_blend_color(size_t n_args, const mp_obj_t *args)
{
    uint32_t color1 = mp_obj_get_int(args[0]);
    uint32_t color2 = mp_obj_get_int(args[1]);
    int alpha = mp_obj_get_int(args[2]);
    return mp_obj_new_int(blend_color(color1, color2, alpha));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ui_blend_color_obj, 3, 3, ui_blend_color);

//=============================================================================
// Display update helper
//=============================================================================

// Wrapper for ili9488.update_region() for convenience
static mp_obj_t ui_update_region(size_t n_args, const mp_obj_t *args)
{
    return ili9488_update_region(n_args, args);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(ui_update_region_obj, 4, 4, ui_update_region);

//=============================================================================
// Module definition
//=============================================================================

static const mp_rom_map_elem_t ui_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_ili9488_ui)},

    // Drawing functions
    {MP_ROM_QSTR(MP_QSTR_draw_button3d), MP_ROM_PTR(&ui_draw_button3d_obj)},
    {MP_ROM_QSTR(MP_QSTR_draw_panel), MP_ROM_PTR(&ui_draw_panel_obj)},
    {MP_ROM_QSTR(MP_QSTR_draw_progressbar), MP_ROM_PTR(&ui_draw_progressbar_obj)},
    {MP_ROM_QSTR(MP_QSTR_draw_checkbox), MP_ROM_PTR(&ui_draw_checkbox_obj)},
    {MP_ROM_QSTR(MP_QSTR_draw_radiobutton), MP_ROM_PTR(&ui_draw_radiobutton_obj)},
    {MP_ROM_QSTR(MP_QSTR_draw_dialog_frame), MP_ROM_PTR(&ui_draw_dialog_frame_obj)},

    // Display update
    {MP_ROM_QSTR(MP_QSTR_update_region), MP_ROM_PTR(&ui_update_region_obj)},

    // Color utilities
    {MP_ROM_QSTR(MP_QSTR_darken_color), MP_ROM_PTR(&ui_darken_color_obj)},
    {MP_ROM_QSTR(MP_QSTR_lighten_color), MP_ROM_PTR(&ui_lighten_color_obj)},
    {MP_ROM_QSTR(MP_QSTR_blend_color), MP_ROM_PTR(&ui_blend_color_obj)},

    // Color constants
    {MP_ROM_QSTR(MP_QSTR_COLOR_BLACK), MP_ROM_INT(COLOR_BLACK)},
    {MP_ROM_QSTR(MP_QSTR_COLOR_WHITE), MP_ROM_INT(COLOR_WHITE)},
    {MP_ROM_QSTR(MP_QSTR_COLOR_RED), MP_ROM_INT(COLOR_RED)},
    {MP_ROM_QSTR(MP_QSTR_COLOR_GREEN), MP_ROM_INT(COLOR_GREEN)},
    {MP_ROM_QSTR(MP_QSTR_COLOR_BLUE), MP_ROM_INT(COLOR_BLUE)},
    {MP_ROM_QSTR(MP_QSTR_COLOR_YELLOW), MP_ROM_INT(COLOR_YELLOW)},
    {MP_ROM_QSTR(MP_QSTR_COLOR_CYAN), MP_ROM_INT(COLOR_CYAN)},
    {MP_ROM_QSTR(MP_QSTR_COLOR_MAGENTA), MP_ROM_INT(COLOR_MAGENTA)},
    {MP_ROM_QSTR(MP_QSTR_COLOR_ORANGE), MP_ROM_INT(COLOR_ORANGE)},
    {MP_ROM_QSTR(MP_QSTR_COLOR_PURPLE), MP_ROM_INT(COLOR_PURPLE)},

    {MP_ROM_QSTR(MP_QSTR_COLOR_GRAY_DARK), MP_ROM_INT(COLOR_GRAY_DARK)},
    {MP_ROM_QSTR(MP_QSTR_COLOR_GRAY), MP_ROM_INT(COLOR_GRAY)},
    {MP_ROM_QSTR(MP_QSTR_COLOR_GRAY_LIGHT), MP_ROM_INT(COLOR_GRAY_LIGHT)},
    {MP_ROM_QSTR(MP_QSTR_COLOR_GRAY_LIGHTER), MP_ROM_INT(COLOR_GRAY_LIGHTER)},

    {MP_ROM_QSTR(MP_QSTR_COLOR_BTN_PRIMARY), MP_ROM_INT(COLOR_BTN_PRIMARY)},
    {MP_ROM_QSTR(MP_QSTR_COLOR_BTN_SUCCESS), MP_ROM_INT(COLOR_BTN_SUCCESS)},
    {MP_ROM_QSTR(MP_QSTR_COLOR_BTN_WARNING), MP_ROM_INT(COLOR_BTN_WARNING)},
    {MP_ROM_QSTR(MP_QSTR_COLOR_BTN_DANGER), MP_ROM_INT(COLOR_BTN_DANGER)},
    {MP_ROM_QSTR(MP_QSTR_COLOR_BTN_DEFAULT), MP_ROM_INT(COLOR_BTN_DEFAULT)},
};
static MP_DEFINE_CONST_DICT(ui_module_globals, ui_module_globals_table);

const mp_obj_module_t ili9488_ui_helpers_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&ui_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_ili9488_ui_helpers, ili9488_ui_helpers_module);
