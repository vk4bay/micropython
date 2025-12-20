"""
ili9488_ui.py - High-level UI widget library for ili9488 display

This module provides common UI widgets and dialogs for the ili9488 display driver,
including 3D buttons, dialog boxes, and other UI components with font support.

Usage:
    import ili9488
    import ili9488_ui as ui
    
    # Initialize display
    ili9488.init(SPI_HOST, DC_PIN, RST_PIN, CS_PIN)
    ili9488.fill(0x000000)
    
    # Create and draw a 3D button
    btn = ui.Button3D(10, 10, 120, 40, "Click Me", ui.COLOR_BLUE)
    btn.draw()
    ili9488.update_region(btn.x, btn.y, btn.width, btn.height)
    
    # Show a dialog
    result = ui.show_dialog("Confirm", "Are you sure?", ui.DIALOG_YES_NO_CANCEL)
"""

import ili9488

# Color constants (RGB888 format)
COLOR_BLACK = 0x000000
COLOR_WHITE = 0xFFFFFF
COLOR_RED = 0xFF0000
COLOR_GREEN = 0x00FF00
COLOR_BLUE = 0x0000FF
COLOR_YELLOW = 0xFFFF00
COLOR_CYAN = 0x00FFFF
COLOR_MAGENTA = 0xFF00FF
COLOR_ORANGE = 0xFF8000
COLOR_PURPLE = 0x8000FF

# Gray scale colors
COLOR_GRAY_DARK = 0x404040
COLOR_GRAY = 0x808080
COLOR_GRAY_LIGHT = 0xC0C0C0
COLOR_GRAY_LIGHTER = 0xE0E0E0

# UI theme colors
COLOR_BTN_PRIMARY = 0x0066CC
COLOR_BTN_SUCCESS = 0x00AA00
COLOR_BTN_WARNING = 0xFF8800
COLOR_BTN_DANGER = 0xCC0000
COLOR_BTN_DEFAULT = COLOR_GRAY

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

# Font sizes
FONT_SMALL = 1
FONT_MEDIUM = 2
FONT_LARGE = 3

# Text alignment
ALIGN_LEFT = 0
ALIGN_CENTER = 1
ALIGN_RIGHT = 2
ALIGN_TOP = 0
ALIGN_MIDDLE = 1
ALIGN_BOTTOM = 2


class Font:
    """Font rendering class with multiple size support."""
    
    def __init__(self, size=FONT_MEDIUM):
        self.size = size
        self.char_width = 8 * size
        self.char_height = 8 * size
    
    def get_text_width(self, text):
        """Calculate the width of text in pixels."""
        return len(text) * self.char_width
    
    def get_text_height(self):
        """Get the height of text in pixels."""
        return self.char_height
    
    def draw_char(self, x, y, char, color):
        """Draw a single character at the given position."""
        # Use native ili9488.text for single character
        ili9488.text(x, y, char, color, None, self.size)
    
    def draw_text(self, x, y, text, color, bg_color=None):
        """Draw text at the given position."""
        # Use native text rendering
        ili9488.text(x, y, text, color, bg_color, self.size)
    
    def draw_text_aligned(self, x, y, width, height, text, color, 
                         h_align=ALIGN_CENTER, v_align=ALIGN_MIDDLE, bg_color=None):
        """Draw text with alignment within a bounding box."""
        text_width = self.get_text_width(text)
        text_height = self.get_text_height()
        
        # Calculate horizontal position
        if h_align == ALIGN_LEFT:
            text_x = x
        elif h_align == ALIGN_CENTER:
            text_x = x + (width - text_width) // 2
        else:  # ALIGN_RIGHT
            text_x = x + width - text_width
        
        # Calculate vertical position
        if v_align == ALIGN_TOP:
            text_y = y
        elif v_align == ALIGN_MIDDLE:
            text_y = y + (height - text_height) // 2
        else:  # ALIGN_BOTTOM
            text_y = y + height - text_height
        
        self.draw_text(text_x, text_y, text, color, bg_color)


# Default font instance
_default_font = Font(FONT_MEDIUM)


def set_default_font(size=FONT_MEDIUM):
    """Set the default font size for all widgets."""
    global _default_font
    _default_font = Font(size)


def get_default_font():
    """Get the current default font."""
    return _default_font


def _darken_color(color, factor=0.6):
    """Darken a color by the given factor (0.0-1.0)."""
    r = int(((color >> 16) & 0xFF) * factor)
    g = int(((color >> 8) & 0xFF) * factor)
    b = int((color & 0xFF) * factor)
    return (r << 16) | (g << 8) | b


def _lighten_color(color, factor=1.3):
    """Lighten a color by the given factor (>1.0)."""
    r = min(255, int(((color >> 16) & 0xFF) * factor))
    g = min(255, int(((color >> 8) & 0xFF) * factor))
    b = min(255, int((color & 0xFF) * factor))
    return (r << 16) | (g << 8) | b


def _blend_color(color1, color2, alpha=0.5):
    """Blend two colors with given alpha (0.0-1.0)."""
    r1 = (color1 >> 16) & 0xFF
    g1 = (color1 >> 8) & 0xFF
    b1 = color1 & 0xFF
    
    r2 = (color2 >> 16) & 0xFF
    g2 = (color2 >> 8) & 0xFF
    b2 = color2 & 0xFF
    
    r = int(r1 * (1 - alpha) + r2 * alpha)
    g = int(g1 * (1 - alpha) + g2 * alpha)
    b = int(b1 * (1 - alpha) + b2 * alpha)
    
    return (r << 16) | (g << 8) | b


class Widget:
    """Base class for all UI widgets."""
    
    def __init__(self, x, y, width, height):
        self.x = x
        self.y = y
        self.width = width
        self.height = height
        self.visible = True
        self.enabled = True
        self.font = _default_font
    
    def set_font(self, font):
        """Set the font for this widget."""
        self.font = font
    
    def draw(self):
        """Draw the widget. Override in subclasses."""
        pass
    
    def contains(self, x, y):
        """Check if a point is within the widget bounds."""
        return (self.x <= x < self.x + self.width and 
                self.y <= y < self.y + self.height)
    
    def update(self):
        """Update the widget's region on display."""
        if self.visible:
            ili9488.update_region(self.x, self.y, self.width, self.height)


class Label(Widget):
    """Text label widget."""
    
    def __init__(self, x, y, text, color=COLOR_BLACK, bg_color=None,
                 h_align=ALIGN_LEFT, v_align=ALIGN_TOP):
        font = _default_font
        width = font.get_text_width(text)
        height = font.get_text_height()
        super().__init__(x, y, width, height)
        self.text = text
        self.color = color
        self.bg_color = bg_color
        self.h_align = h_align
        self.v_align = v_align
    
    def set_text(self, text):
        """Update the label text."""
        self.text = text
        self.width = self.font.get_text_width(text)
        self.draw()
        self.update()
    
    def draw(self):
        """Draw the label."""
        if not self.visible:
            return
        
        self.font.draw_text_aligned(self.x, self.y, self.width, self.height,
                                   self.text, self.color, 
                                   self.h_align, self.v_align, self.bg_color)


class Button(Widget):
    """Basic flat button widget."""
    
    def __init__(self, x, y, width, height, text, color=COLOR_BTN_PRIMARY, 
                 text_color=COLOR_WHITE, border_color=None):
        super().__init__(x, y, width, height)
        self.text = text
        self.color = color
        self.text_color = text_color
        self.border_color = border_color if border_color is not None else _darken_color(color)
        self.pressed = False
    
    def draw(self):
        """Draw the button."""
        if not self.visible:
            return
        
        # Draw button background
        display_color = _darken_color(self.color, 0.7) if self.pressed else self.color
        if not self.enabled:
            display_color = COLOR_GRAY
        
        ili9488.rect(self.x, self.y, self.width, self.height, 
                     self.border_color, display_color)
        
        # Draw centered text
        self.font.draw_text_aligned(self.x, self.y, self.width, self.height,
                                   self.text, self.text_color,
                                   ALIGN_CENTER, ALIGN_MIDDLE)
    
    def set_pressed(self, pressed):
        """Set the button pressed state."""
        if self.pressed != pressed:
            self.pressed = pressed
            self.draw()
            self.update()


class Button3D(Widget):
    """3D-style button with raised/pressed appearance."""
    
    def __init__(self, x, y, width, height, text, color=COLOR_BTN_PRIMARY, 
                 text_color=COLOR_WHITE):
        super().__init__(x, y, width, height)
        self.text = text
        self.color = color
        self.text_color = text_color
        self.pressed = False
        self.border_width = 2
    
    def draw(self):
        """Draw the 3D button."""
        if not self.visible:
            return
        
        base_color = COLOR_GRAY if not self.enabled else self.color
        
        if self.pressed:
            # Pressed state - appears sunken
            # Dark top/left, light bottom/right
            top_color = _darken_color(base_color, 0.5)
            bottom_color = _lighten_color(base_color, 1.2)
            face_color = _darken_color(base_color, 0.8)
            text_offset = 1
        else:
            # Raised state - appears elevated
            # Light top/left, dark bottom/right
            top_color = _lighten_color(base_color, 1.3)
            bottom_color = _darken_color(base_color, 0.6)
            face_color = base_color
            text_offset = 0
        
        # Draw face
        ili9488.rect(self.x + self.border_width, 
                     self.y + self.border_width,
                     self.width - self.border_width * 2, 
                     self.height - self.border_width * 2,
                     face_color, face_color)
        
        # Draw top edge
        for i in range(self.border_width):
            ili9488.line(self.x + i, self.y + i, 
                        self.x + self.width - i - 1, self.y + i, 
                        top_color)
        
        # Draw left edge
        for i in range(self.border_width):
            ili9488.line(self.x + i, self.y + i, 
                        self.x + i, self.y + self.height - i - 1, 
                        top_color)
        
        # Draw bottom edge
        for i in range(self.border_width):
            ili9488.line(self.x + i, self.y + self.height - i - 1,
                        self.x + self.width - i - 1, self.y + self.height - i - 1,
                        bottom_color)
        
        # Draw right edge
        for i in range(self.border_width):
            ili9488.line(self.x + self.width - i - 1, self.y + i,
                        self.x + self.width - i - 1, self.y + self.height - i - 1,
                        bottom_color)
        
        # Draw centered text with offset when pressed
        self.font.draw_text_aligned(self.x + text_offset, self.y + text_offset, 
                                   self.width, self.height,
                                   self.text, self.text_color,
                                   ALIGN_CENTER, ALIGN_MIDDLE)
    
    def set_pressed(self, pressed):
        """Set the button pressed state."""
        if self.pressed != pressed:
            self.pressed = pressed
            self.draw()
            self.update()


class Panel(Widget):
    """A container panel with optional border and background."""
    
    def __init__(self, x, y, width, height, bg_color=COLOR_GRAY_LIGHT, 
                 border_color=COLOR_GRAY_DARK, has_border=True):
        super().__init__(x, y, width, height)
        self.bg_color = bg_color
        self.border_color = border_color
        self.has_border = has_border
    
    def draw(self):
        """Draw the panel."""
        if not self.visible:
            return
        
        if self.has_border:
            # Draw panel with border
            ili9488.rect(self.x, self.y, self.width, self.height,
                        self.border_color, self.bg_color)
        else:
            # Draw filled rectangle without border
            for i in range(self.height):
                ili9488.line(self.x, self.y + i, 
                           self.x + self.width - 1, self.y + i, 
                           self.bg_color)


class ProgressBar(Widget):
    """A progress bar widget."""
    
    def __init__(self, x, y, width, height, min_val=0, max_val=100, 
                 fg_color=COLOR_BTN_PRIMARY, bg_color=COLOR_GRAY_LIGHT,
                 border_color=COLOR_GRAY_DARK, show_percent=False):
        super().__init__(x, y, width, height)
        self.min_val = min_val
        self.max_val = max_val
        self.value = min_val
        self.fg_color = fg_color
        self.bg_color = bg_color
        self.border_color = border_color
        self.show_percent = show_percent
    
    def set_value(self, value):
        """Set the progress value."""
        self.value = max(self.min_val, min(self.max_val, value))
        self.draw()
        self.update()
    
    def draw(self):
        """Draw the progress bar."""
        if not self.visible:
            return
        
        # Draw border
        ili9488.rect(self.x, self.y, self.width, self.height,
                    self.border_color, self.bg_color)
        
        # Calculate fill width
        progress = (self.value - self.min_val) / (self.max_val - self.min_val)
        fill_width = int((self.width - 4) * progress)
        
        # Draw progress fill
        if fill_width > 0:
            for i in range(self.height - 4):
                ili9488.line(self.x + 2, self.y + 2 + i,
                           self.x + 2 + fill_width - 1, self.y + 2 + i,
                           self.fg_color)
        
        # Draw percentage text if enabled
        if self.show_percent:
            percent = int(progress * 100)
            text = f"{percent}%"
            text_color = COLOR_WHITE if fill_width > self.width // 2 else COLOR_BLACK
            self.font.draw_text_aligned(self.x, self.y, self.width, self.height,
                                       text, text_color,
                                       ALIGN_CENTER, ALIGN_MIDDLE)


class CheckBox(Widget):
    """A checkbox widget with optional label."""
    
    def __init__(self, x, y, size=20, checked=False, 
                 color=COLOR_BTN_PRIMARY, bg_color=COLOR_WHITE, label=""):
        super().__init__(x, y, size, size)
        self.checked = checked
        self.color = color
        self.bg_color = bg_color
        self.label = label
        # Adjust width if there's a label
        if label:
            self.width = size + 8 + self.font.get_text_width(label)
    
    def toggle(self):
        """Toggle the checkbox state."""
        self.checked = not self.checked
        self.draw()
        self.update()
    
    def draw(self):
        """Draw the checkbox."""
        if not self.visible:
            return
        
        size = self.height  # Original box size
        
        # Draw box
        box_color = COLOR_GRAY if not self.enabled else COLOR_GRAY_DARK
        ili9488.rect(self.x, self.y, size, size,
                    box_color, self.bg_color)
        
        # Draw check mark if checked
        if self.checked:
            # Draw a simple X or checkmark
            margin = 4
            ili9488.line(self.x + margin, self.y + margin,
                        self.x + size - margin, self.y + size - margin,
                        self.color)
            ili9488.line(self.x + size - margin, self.y + margin,
                        self.x + margin, self.y + size - margin,
                        self.color)
        
        # Draw label if present
        if self.label:
            label_x = self.x + size + 8
            label_y = self.y + (size - self.font.get_text_height()) // 2
            self.font.draw_text(label_x, label_y, self.label, COLOR_BLACK)


class RadioButton(Widget):
    """A radio button widget with optional label."""
    
    def __init__(self, x, y, radius=10, selected=False, 
                 color=COLOR_BTN_PRIMARY, bg_color=COLOR_WHITE, label=""):
        super().__init__(x - radius, y - radius, radius * 2, radius * 2)
        self.center_x = x
        self.center_y = y
        self.radius = radius
        self.selected = selected
        self.color = color
        self.bg_color = bg_color
        self.label = label
        self.group = None
        # Adjust width if there's a label
        if label:
            self.width = radius * 2 + 8 + self.font.get_text_width(label)
    
    def select(self):
        """Select this radio button and deselect others in group."""
        if self.group:
            for rb in self.group:
                if rb != self:
                    rb.selected = False
                    rb.draw()
                    rb.update()
        self.selected = True
        self.draw()
        self.update()
    
    def draw(self):
        """Draw the radio button."""
        if not self.visible:
            return
        
        # Draw outer circle
        border_color = COLOR_GRAY if not self.enabled else COLOR_GRAY_DARK
        ili9488.circle(self.center_x, self.center_y, self.radius,
                      border_color, self.bg_color)
        
        # Draw inner circle if selected
        if self.selected:
            inner_radius = max(1, self.radius - 4)
            ili9488.circle(self.center_x, self.center_y, inner_radius,
                          self.color, self.color)
        
        # Draw label if present
        if self.label:
            label_x = self.x + self.radius * 2 + 8
            label_y = self.center_y - self.font.get_text_height() // 2
            self.font.draw_text(label_x, label_y, self.label, COLOR_BLACK)


class Dialog:
    """Base class for dialog boxes."""
    
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
        
        # Save background for restore
        self.background_saved = False
    
    def draw_frame(self):
        """Draw the dialog frame."""
        # Draw shadow
        shadow_offset = 4
        shadow_color = COLOR_GRAY_DARK
        ili9488.rect(self.x + shadow_offset, self.y + shadow_offset,
                    self.width, self.height,
                    shadow_color, shadow_color)
        
        # Draw dialog background
        ili9488.rect(self.x, self.y, self.width, self.height,
                    COLOR_GRAY_DARK, COLOR_GRAY_LIGHT)
        
        # Draw title bar
        title_height = 30
        ili9488.rect(self.x, self.y, self.width, title_height,
                    COLOR_GRAY_DARK, COLOR_BTN_PRIMARY)
        
        # Draw title text indicator (circle)
        ili9488.circle(self.x + 15, self.y + title_height // 2, 3,
                      COLOR_WHITE, COLOR_WHITE)
    
    def draw(self):
        """Draw the dialog."""
        self.draw_frame()
        
        # Draw buttons
        for btn in self.buttons:
            btn.draw()
    
    def update(self):
        """Update the dialog on display."""
        ili9488.update_region(self.x, self.y, 
                             self.width + 4, self.height + 4)


def show_ok_dialog(title, message):
    """Show a simple OK dialog."""
    dialog = Dialog(title, message)
    
    # Create OK button
    btn_width = 80
    btn_height = 35
    btn_x = dialog.x + (dialog.width - btn_width) // 2
    btn_y = dialog.y + dialog.height - btn_height - 15
    
    ok_btn = Button3D(btn_x, btn_y, btn_width, btn_height, "OK", COLOR_BTN_PRIMARY)
    dialog.buttons.append(ok_btn)
    
    dialog.draw()
    dialog.update()
    
    return dialog


def show_ok_cancel_dialog(title, message):
    """Show an OK/Cancel dialog."""
    dialog = Dialog(title, message)
    
    # Create buttons
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
    dialog.update()
    
    return dialog


def show_yes_no_dialog(title, message):
    """Show a Yes/No dialog (without Cancel)."""
    dialog = Dialog(title, message, width=260)
    
    # Create buttons
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
    dialog.update()
    
    return dialog


def show_yes_no_cancel_dialog(title, message):
    """Show a Yes/No/Cancel dialog."""
    dialog = Dialog(title, message, width=300)
    
    # Create buttons
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
    dialog.update()
    
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
    
    def update_all(self):
        """Update all buttons on display."""
        for btn in self.buttons:
            btn.update()
    
    def find_at(self, x, y):
        """Find which button (if any) contains the given point."""
        for btn in self.buttons:
            if btn.contains(x, y) and btn.visible and btn.enabled:
                return btn
        return None
