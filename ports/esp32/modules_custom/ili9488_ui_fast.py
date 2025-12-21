"""
ili9488_ui_fast.py - High-level UI wrapper using C backend for performance

This module provides the same API as ili9488_ui.py but uses the C implementation
for 10-100x better performance. It maintains the widget class structure for
compatibility while delegating rendering to optimized C functions.

Usage:
    import ili9488
    import ili9488_ui_fast as ui
    
    # Initialize display
    ili9488.init(SPI_HOST, DC_PIN, RST_PIN, CS_PIN)
    ili9488.fill(0x000000)
    
    # Create and draw a 3D button (renders in C for speed)
    btn = ui.Button3D(10, 10, 120, 40, "Click Me", ui.COLOR_BLUE)
    btn.draw()  # Fast C rendering!
"""

import ili9488
import ili9488_ui as ui_c  # This is the C module

# Re-export all color constants from C module
COLOR_BLACK = ui_c.COLOR_BLACK
COLOR_WHITE = ui_c.COLOR_WHITE
COLOR_RED = ui_c.COLOR_RED
COLOR_GREEN = ui_c.COLOR_GREEN
COLOR_BLUE = ui_c.COLOR_BLUE
COLOR_YELLOW = ui_c.COLOR_YELLOW
COLOR_CYAN = ui_c.COLOR_CYAN
COLOR_MAGENTA = ui_c.COLOR_MAGENTA
COLOR_ORANGE = ui_c.COLOR_ORANGE
COLOR_PURPLE = ui_c.COLOR_PURPLE

COLOR_GRAY_DARK = ui_c.COLOR_GRAY_DARK
COLOR_GRAY = ui_c.COLOR_GRAY
COLOR_GRAY_LIGHT = ui_c.COLOR_GRAY_LIGHT
COLOR_GRAY_LIGHTER = ui_c.COLOR_GRAY_LIGHTER

COLOR_BTN_PRIMARY = ui_c.COLOR_BTN_PRIMARY
COLOR_BTN_SUCCESS = ui_c.COLOR_BTN_SUCCESS
COLOR_BTN_WARNING = ui_c.COLOR_BTN_WARNING
COLOR_BTN_DANGER = ui_c.COLOR_BTN_DANGER
COLOR_BTN_DEFAULT = ui_c.COLOR_BTN_DEFAULT

# Dialog types
DIALOG_OK = 1
DIALOG_OK_CANCEL = 2
DIALOG_YES_NO = 3
DIALOG_YES_NO_CANCEL = 4

# Dialog results
RESULT_OK = 1
RESULT_CANCEL = 2
RESULT_YES = 3
RESULT_NO = 4


class Widget:
    """Base class for all UI widgets."""
    
    def __init__(self, x, y, width, height):
        self.x = x
        self.y = y
        self.width = width
        self.height = height
        self.visible = True
        self.enabled = True
    
    def draw(self):
        """Draw the widget. Override in subclasses."""
        pass
    
    def contains(self, x, y):
        """Check if a point is within the widget bounds."""
        return (self.x <= x < self.x + self.width and 
                self.y <= y < self.y + self.height)
    
    def update(self):
        """Update the widget's region on display (not needed - C does it)."""
        pass  # C functions auto-update


class Button3D(Widget):
    """3D-style button with raised/pressed appearance - C optimized."""
    
    def __init__(self, x, y, width, height, text, color=COLOR_BTN_PRIMARY, 
                 text_color=COLOR_WHITE):
        super().__init__(x, y, width, height)
        self.text = text
        self.color = color
        self.text_color = text_color
        self.pressed = False
    
    def draw(self):
        """Draw the 3D button using fast C implementation."""
        if not self.visible:
            return
        
        # Single C function call instead of dozens of Python calls!
        ui_c.draw_button3d(self.x, self.y, self.width, self.height, 
                          self.color, self.pressed, self.enabled)
    
    def set_pressed(self, pressed):
        """Set the button pressed state."""
        if self.pressed != pressed:
            self.pressed = pressed
            self.draw()


class Panel(Widget):
    """A container panel with optional border and background - C optimized."""
    
    def __init__(self, x, y, width, height, bg_color=COLOR_GRAY_LIGHT, 
                 border_color=COLOR_GRAY_DARK, has_border=True):
        super().__init__(x, y, width, height)
        self.bg_color = bg_color
        self.border_color = border_color
        self.has_border = has_border
    
    def draw(self):
        """Draw the panel using fast C implementation."""
        if not self.visible:
            return
        
        ui_c.draw_panel(self.x, self.y, self.width, self.height,
                       self.bg_color, self.border_color, self.has_border)


class ProgressBar(Widget):
    """A progress bar widget - C optimized."""
    
    def __init__(self, x, y, width, height, min_val=0, max_val=100, 
                 fg_color=COLOR_BTN_PRIMARY, bg_color=COLOR_GRAY_LIGHT,
                 border_color=COLOR_GRAY_DARK):
        super().__init__(x, y, width, height)
        self.min_val = min_val
        self.max_val = max_val
        self.value = min_val
        self.fg_color = fg_color
        self.bg_color = bg_color
        self.border_color = border_color
    
    def set_value(self, value):
        """Set the progress value."""
        self.value = max(self.min_val, min(self.max_val, value))
        self.draw()
    
    def draw(self):
        """Draw the progress bar using fast C implementation."""
        if not self.visible:
            return
        
        # Normalize value to 0-max_val range
        normalized_value = self.value - self.min_val
        normalized_max = self.max_val - self.min_val
        
        ui_c.draw_progressbar(self.x, self.y, self.width, self.height,
                             normalized_value, normalized_max,
                             self.fg_color, self.bg_color, self.border_color)


class CheckBox(Widget):
    """A checkbox widget - C optimized."""
    
    def __init__(self, x, y, size=20, checked=False, 
                 color=COLOR_BTN_PRIMARY, bg_color=COLOR_WHITE):
        super().__init__(x, y, size, size)
        self.checked = checked
        self.color = color
        self.bg_color = bg_color
    
    def toggle(self):
        """Toggle the checkbox state."""
        self.checked = not self.checked
        self.draw()
    
    def draw(self):
        """Draw the checkbox using fast C implementation."""
        if not self.visible:
            return
        
        ui_c.draw_checkbox(self.x, self.y, self.width, self.checked, 
                          self.enabled, self.color)


class RadioButton(Widget):
    """A radio button widget - C optimized."""
    
    def __init__(self, x, y, radius=10, selected=False, 
                 color=COLOR_BTN_PRIMARY, bg_color=COLOR_WHITE):
        super().__init__(x - radius, y - radius, radius * 2, radius * 2)
        self.center_x = x
        self.center_y = y
        self.radius = radius
        self.selected = selected
        self.color = color
        self.bg_color = bg_color
        self.group = None
    
    def select(self):
        """Select this radio button and deselect others in group."""
        if self.group:
            for rb in self.group:
                if rb != self:
                    rb.selected = False
                    rb.draw()
        self.selected = True
        self.draw()
    
    def draw(self):
        """Draw the radio button using fast C implementation."""
        if not self.visible:
            return
        
        ui_c.draw_radiobutton(self.center_x, self.center_y, self.radius,
                             self.selected, self.enabled, self.color)


class Dialog:
    """Base class for dialog boxes - uses C for frame rendering."""
    
    def __init__(self, title, message, width=280, height=160):
        # Center the dialog on screen
        screen_width = ili9488.get_width()
        screen_height = ili9488.get_height()
        self.x = (screen_width - width) // 2
        self.y = (screen_height - height) // 2
        self.width = width
        self.height = height
        self.title = title
        self.message = message
        self.result = None
        self.buttons = []
    
    def draw_frame(self):
        """Draw the dialog frame using fast C implementation."""
        ui_c.draw_dialog_frame(self.x, self.y, self.width, self.height, 30)
    
    def draw(self):
        """Draw the dialog."""
        self.draw_frame()
        
        # Draw buttons
        for btn in self.buttons:
            btn.draw()


# Dialog helper functions remain the same as Python version
def show_ok_dialog(title, message):
    """Show a simple OK dialog."""
    dialog = Dialog(title, message)
    
    btn_width = 80
    btn_height = 35
    btn_x = dialog.x + (dialog.width - btn_width) // 2
    btn_y = dialog.y + dialog.height - btn_height - 15
    
    ok_btn = Button3D(btn_x, btn_y, btn_width, btn_height, "OK", COLOR_BTN_PRIMARY)
    dialog.buttons.append(ok_btn)
    
    dialog.draw()
    
    return dialog


def show_ok_cancel_dialog(title, message):
    """Show an OK/Cancel dialog."""
    dialog = Dialog(title, message)
    
    btn_width = 80
    btn_height = 35
    btn_spacing = 10
    total_btn_width = btn_width * 2 + btn_spacing
    start_x = dialog.x + (dialog.width - total_btn_width) // 2
    btn_y = dialog.y + dialog.height - btn_height - 15
    
    ok_btn = Button3D(start_x, btn_y, btn_width, btn_height, "OK", COLOR_BTN_SUCCESS)
    cancel_btn = Button3D(start_x + btn_width + btn_spacing, btn_y, 
                         btn_width, btn_height, "Cancel", COLOR_BTN_DANGER)
    
    dialog.buttons.append(ok_btn)
    dialog.buttons.append(cancel_btn)
    
    dialog.draw()
    
    return dialog


def show_yes_no_dialog(title, message):
    """Show a Yes/No dialog."""
    dialog = Dialog(title, message, width=260)
    
    btn_width = 80
    btn_height = 35
    btn_spacing = 10
    total_btn_width = btn_width * 2 + btn_spacing
    start_x = dialog.x + (dialog.width - total_btn_width) // 2
    btn_y = dialog.y + dialog.height - btn_height - 15
    
    yes_btn = Button3D(start_x, btn_y, btn_width, btn_height, "Yes", COLOR_BTN_SUCCESS)
    no_btn = Button3D(start_x + btn_width + btn_spacing, btn_y, 
                     btn_width, btn_height, "No", COLOR_BTN_WARNING)
    
    dialog.buttons.append(yes_btn)
    dialog.buttons.append(no_btn)
    
    dialog.draw()
    
    return dialog


def show_yes_no_cancel_dialog(title, message):
    """Show a Yes/No/Cancel dialog."""
    dialog = Dialog(title, message, width=300)
    
    btn_width = 70
    btn_height = 35
    btn_spacing = 10
    total_btn_width = btn_width * 3 + btn_spacing * 2
    start_x = dialog.x + (dialog.width - total_btn_width) // 2
    btn_y = dialog.y + dialog.height - btn_height - 15
    
    yes_btn = Button3D(start_x, btn_y, btn_width, btn_height, "Yes", COLOR_BTN_SUCCESS)
    no_btn = Button3D(start_x + btn_width + btn_spacing, btn_y, 
                     btn_width, btn_height, "No", COLOR_BTN_WARNING)
    cancel_btn = Button3D(start_x + (btn_width + btn_spacing) * 2, btn_y, 
                         btn_width, btn_height, "Cancel", COLOR_BTN_DANGER)
    
    dialog.buttons.append(yes_btn)
    dialog.buttons.append(no_btn)
    dialog.buttons.append(cancel_btn)
    
    dialog.draw()
    
    return dialog


def show_dialog(title, message, dialog_type=DIALOG_OK):
    """Show a dialog of the specified type."""
    if dialog_type == DIALOG_OK:
        return show_ok_dialog(title, message)
    elif dialog_type == DIALOG_OK_CANCEL:
        return show_ok_cancel_dialog(title, message)
    elif dialog_type == DIALOG_YES_NO:
        return show_yes_no_dialog(title, message)
    elif dialog_type == DIALOG_YES_NO_CANCEL:
        return show_yes_no_cancel_dialog(title, message)
    else:
        return show_ok_dialog(title, message)


class ButtonGroup:
    """Manager for a group of buttons."""
    
    def __init__(self):
        self.buttons = []
    
    def add(self, button):
        """Add a button to the group."""
        self.buttons.append(button)
    
    def draw_all(self):
        """Draw all buttons in the group."""
        for btn in self.buttons:
            btn.draw()
    
    def find_at(self, x, y):
        """Find which button (if any) contains the given point."""
        for btn in self.buttons:
            if btn.contains(x, y) and btn.visible and btn.enabled:
                return btn
        return None
