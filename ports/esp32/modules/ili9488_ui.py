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
import time

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
    # Class variable to track if a custom font is set
    _custom_font = None
    
    def __init__(self, size=FONT_MEDIUM):
        self.size = size
    
    @classmethod
    def set_custom_font(cls, font_module):
        ili9488.set_font(font_module)
        cls._custom_font = font_module


    @classmethod
    def clear_custom_font(cls):
        ili9488.clear_font()
        cls._custom_font = None
    
    def get_text_width(self, text):
        """Calculate the width of text in pixels."""
        if self._custom_font is not None:
            total_width = 0
            for ch in text:
                try:
                    char_data = self._custom_font.get_ch(ch)
                    char_width = char_data[2]
                    total_width += char_width
                except:
                    total_width += 8
            return total_width
        else:
            return len(text) * 8 * self.size
    
    def get_text_height(self):
        """Get the height of text in pixels."""
        if self._custom_font is not None:
            try:
                char_data = self._custom_font.get_ch('X')
                return char_data[1]
            except:
                return 16
        else:
            return 8 * self.size
    
    def draw_text(self, x, y, text, color, bg_color=None):
        """Draw text at position."""
        ili9488.text(x, y, text, color, bg_color, self.size)
    
    def draw_text_aligned(self, x, y, width, height, text, color, 
                         h_align=ALIGN_CENTER, v_align=ALIGN_MIDDLE, bg_color=None):
        text_width = self.get_text_width(text)
        text_height = self.get_text_height()
        
        if h_align == ALIGN_LEFT:
            text_x = x
        elif h_align == ALIGN_RIGHT:
            text_x = x + width - text_width
        else:
            text_x = x + (width - text_width) // 2
        
        if v_align == ALIGN_TOP:
            text_y = y
        elif v_align == ALIGN_BOTTOM:
            text_y = y + height - text_height
        else:
            text_y = y + (height - text_height) // 2
        
        if bg_color is not None:
            ili9488.rect(x, y, width, height, bg_color, bg_color)
        
        ili9488.text(text_x, text_y, text, color, None, self.size)

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
        # Save old width for clearing
        old_width = self.width
        old_height = self.height
        
        # Update text and calculate new dimensions
        self.text = text
        self.width = self.font.get_text_width(text)
        self.height = self.font.get_text_height()
        
        # Clear the larger of old/new area
        clear_width = max(old_width, self.width)
        clear_height = max(old_height, self.height)
        
        if self.bg_color is not None:
            ili9488.rect(self.x, self.y, clear_width, clear_height+1, 
                        self.bg_color, self.bg_color)
        
        # Draw new text
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
                 text_color=COLOR_WHITE, border_color=None, sheen=True, on_click=None):
        super().__init__(x, y, width, height)
        self.text = text
        self.color = color
        self.text_color = text_color
        self.border_color = border_color if border_color is not None else _darken_color(color)
        self.pressed = False
        self.sheen = sheen  # Add sheen parameter
        self.on_click = on_click 
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
        
        # Add sheen effect (gradient highlight on top portion)
        if self.sheen and not self.pressed and self.enabled:
            sheen_height = self.height / 3
            highlight_color = COLOR_WHITE
            
            # Draw gradient from white to base color
            for i in range(sheen_height):
                # Calculate alpha blend factor (fades from 0.7 to 0.0)
                alpha = 0.7 * (1 - i / sheen_height)
                line_color = _blend_color(display_color, highlight_color, alpha)
                print(f"Step {i}: alpha={alpha:.2f}, color=0x{line_color:06X}")
                
                ili9488.line(self.x + 2, self.y + 2 + i,
                           self.x + self.width - 3, self.y + 2 + i,
                           line_color)
        
            # Draw centered text
        text_width = self.font.get_text_width(self.text)
        text_height = self.font.get_text_height()
        text_x = self.x + (self.width - text_width) // 2
        text_y = self.y + (self.height - text_height) // 2
        
        # Draw text with transparent background
        ili9488.text(text_x, text_y, self.text, self.text_color, None, self.font.size)
    
    def set_pressed(self, pressed):
        """Set the button pressed state."""
        if self.pressed != pressed:
            was_pressed = self.pressed
            self.pressed = pressed
            self.draw()
            self.update()
            
            if was_pressed and not pressed and self.on_click:
                self.on_click(self)
    
    def click(self):
        if self.enabled and self.on_click:
            self.on_click(self)

class Button3D(Widget):
    """3D raised button with border effects and optional rounded corners."""
    
    def __init__(self, x, y, width, height, text, color=COLOR_BTN_PRIMARY, 
                 text_color=COLOR_WHITE, sheen=True, corner_radius=90, on_click=None):
        super().__init__(x, y, width, height)
        self.text = text
        self.color = color
        self.text_color = text_color
        self.pressed = False
        self.sheen = sheen
        self.corner_radius = min(corner_radius, min(width, height) // 2)  # Cap radius
        self.on_click = on_click  
            
    def _draw_rounded_rect_filled(self, x, y, w, h, r, fill_color):
        if r <= 0:
            # No rounding - use fast rect fill
            ili9488.rect(x, y, w, h, fill_color, fill_color)
            return
        
        # Center rectangle (full height, inset by radius)
        ili9488.rect(x + r, y, w - 2*r, h, fill_color, fill_color)
        # Left and right rectangles (height minus corners)
        ili9488.rect(x, y + r, r, h - 2*r, fill_color, fill_color)
        ili9488.rect(x + w - r, y + r, r, h - 2*r, fill_color, fill_color)
        # Draw four corner circles (filled)
        ili9488.circle(x + r, y + r, r, fill_color, fill_color)
        ili9488.circle(x + w - 1 - r, y + r, r, fill_color, fill_color)
        ili9488.circle(x + r, y + h - 1 - r, r, fill_color, fill_color)
        ili9488.circle(x + w - 1 - r, y + h - 1 - r, r, fill_color, fill_color)
    
    def draw(self):
        """Draw the 3D button with rounded corners."""
        if not self.visible:
            return
        
        # Calculate colors for 3D effect
        if self.pressed:
            face_color = _darken_color(self.color, 0.7)
            highlight_color = _darken_color(self.color, 0.5)
            shadow_color = _lighten_color(self.color, 1.3)
        else:
            face_color = self.color
            highlight_color = _lighten_color(self.color, 1.5)
            shadow_color = _darken_color(self.color, 0.5)
        
        if not self.enabled:
            face_color = COLOR_GRAY
            highlight_color = COLOR_GRAY_LIGHT
            shadow_color = COLOR_GRAY_DARK
        
        r = self.corner_radius
        
        # Draw filled button face with rounded corners
        self._draw_rounded_rect_filled(self.x, self.y, self.width, self.height, 
                                       r, face_color)
        # Add sheen effect
        if self.sheen and not self.pressed and self.enabled:
             sheen_height = self.height // 3
             sheen_white = COLOR_WHITE
             
             for i in range(sheen_height):
                 alpha = 0.5 * (1 - i / sheen_height)
                 line_color = _blend_color(face_color, sheen_white, alpha)
                 
                 if r > 0 and i < r:
                     # Shorten line for rounded top corners
                     dy = r - i
                     # For a circle: x = sqrt(r^2 - y^2)
                     dx = int((r * r - dy * dy) ** 0.5)
                     inset = r - dx
                     
                     ili9488.line(self.x + r - dx + 2, self.y + 2 + i,
                                self.x + self.width - (r - dx) - 3, self.y + 2 + i,
                                line_color)
                 else:
                     ili9488.line(self.x + 2, self.y + 2 + i,
                                self.x + self.width - 3, self.y + 2 + i,
                                line_color)
         
        text_width = self.font.get_text_width(self.text)
        text_height = self.font.get_text_height()
        text_x = self.x + (self.width - text_width) // 2
        text_y = self.y + (self.height - text_height) // 2
        
        if self.pressed:
            text_x += 1
            text_y += 1
        
        ili9488.text(text_x, text_y, self.text, self.text_color, None, self.font.size)
    
    def set_pressed(self, pressed):
        """Set the button pressed state."""
        if self.pressed != pressed:
            was_pressed = self.pressed
            self.pressed = pressed
            self.draw()
            self.update()
            
            if was_pressed and not pressed and self.on_click:
                self.on_click(self)
    
    def click(self):
        if self.enabled and self.on_click:
            self.on_click(self)


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
                 border_color=COLOR_GRAY_DARK, show_percent=False, on_change=None):
        super().__init__(x, y, width, height)
        self.min_val = min_val
        self.max_val = max_val
        self.value = min_val
        self.fg_color = fg_color
        self.bg_color = bg_color
        self.border_color = border_color
        self.show_percent = show_percent
        self.on_change = on_change  # Callback for value changes
    
    def set_value(self, value):
        """Set the progress value."""
        old_value = self.value
        self.value = max(self.min_val, min(self.max_val, value))
        self.draw()
        self.update()
        
        if old_value != self.value and self.on_change:
            self.on_change(self, self.value)
    
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
                 color=COLOR_BTN_PRIMARY, bg_color=COLOR_WHITE, label="", on_change=None):
        super().__init__(x, y, size, size)
        self.checked = checked
        self.color = color
        self.bg_color = bg_color
        self.label = label
        self.on_change = on_change  
        # Adjust width if there's a label
        if label:
            self.width = size + 8 + self.font.get_text_width(label)
    
    def toggle(self):
        """Toggle the checkbox state."""
        if not self.enabled:
            return
        
        self.checked = not self.checked
        self.draw()
        self.update()
        
        if self.on_change:
            self.on_change(self, self.checked)
    
    def set_checked(self, checked):
        if self.checked != checked:
            self.checked = checked
            self.draw()
            self.update()
            
            if self.on_change:
                self.on_change(self, self.checked)
    
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
                 color=COLOR_BTN_PRIMARY, bg_color=COLOR_WHITE, label="", on_select=None):
        super().__init__(x - radius, y - radius, radius * 2, radius * 2)
        self.center_x = x
        self.center_y = y
        self.radius = radius
        self.selected = selected
        self.color = color
        self.bg_color = bg_color
        self.label = label
        self.group = None
        self.on_select = on_select  
        # Adjust width if there's a label
        if label:
            self.width = radius * 2 + 8 + self.font.get_text_width(label)
    
    def select(self):
        """Select this radio button and deselect others in group."""
        if not self.enabled:
            return
        
        was_selected = self.selected
        
        if self.group:
            for rb in self.group:
                if rb != self:
                    rb.selected = False
                    rb.draw()
                    rb.update()
        self.selected = True
        self.draw()
        self.update()
        
        if not was_selected and self.on_select:
            self.on_select(self)
    
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
        
        ili9488.text(self.x + 10, self.y + 7, self.title, COLOR_WHITE, COLOR_BTN_PRIMARY, FONT_MEDIUM)
        
        # Draw message text
        message_y = self.y + title_height + 15
        ili9488.text(self.x + 10, message_y, self.message, COLOR_BLACK, COLOR_GRAY_LIGHT, FONT_MEDIUM)

        # TODO: Paul needs to add word wrapping for long messages.

    
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
    btn_width = 95
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
    btn_width = 95
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


class Dial(Widget):
    """A dial/gauge widget for displaying analog values.
    
    Displays a circular gauge with a rotating needle to indicate a value
    within a specified range. Useful for speedometers, temperature gauges, etc.
    
    Args:
        x, y: Center position of the dial
        radius: Radius of the dial
        min_val: Minimum value
        max_val: Maximum value
        start_angle: Starting angle in degrees (default -135, 7-8 o'clock position)
        end_angle: Ending angle in degrees (default 135, 4-5 o'clock position)
        color: Main dial color
        needle_color: Color of the needle/pointer
        bg_color: Background color for erasing old needle
    """
    
    def __init__(self, x, y, radius, min_val=0, max_val=100, 
                 start_angle=-135, end_angle=135,
                 color=COLOR_WHITE, needle_color=COLOR_RED, bg_color=COLOR_BLACK, on_change=None):
        # Widget bounds are a square around the circle
        super().__init__(x - radius - 5, y - radius - 5, 
                        (radius + 5) * 2, (radius + 5) * 2)
        self.center_x = x
        self.center_y = y
        self.radius = radius
        self.min_val = min_val
        self.max_val = max_val
        self.start_angle = start_angle
        self.end_angle = end_angle
        self.color = color
        self.needle_color = needle_color
        self.bg_color = bg_color
        self.current_value = min_val
        self.last_needle_angle = None
        self.show_ticks = True
        self.num_major_ticks = 9
        self.num_minor_ticks = 4  # Between each major tick
        self.on_change = on_change  # Callback for value changes
    
    def _value_to_angle(self, value):
        """Convert a value to an angle in degrees."""
        # Clamp value to range
        value = max(self.min_val, min(self.max_val, value))
        
        # Calculate proportion through range
        proportion = (value - self.min_val) / (self.max_val - self.min_val)
        
        # Map to angle range
        angle_range = self.end_angle - self.start_angle
        angle = self.start_angle + (proportion * angle_range)
        
        return angle
    
    def _angle_to_point(self, angle, distance):
        """Convert angle and distance to x, y coordinates.
        
        Angle in degrees, 0=right, 90=down, 180=left, 270=up
        """
        import math
        rad = math.radians(angle)
        x = self.center_x + int(distance * math.cos(rad))
        y = self.center_y + int(distance * math.sin(rad))
        return x, y
    
    def draw_bezel(self):
        """Draw the outer circle bezel."""
        # Draw outer circle
        ili9488.circle(self.center_x, self.center_y, self.radius, self.color)
        # Draw inner circle for double-ring effect
        if self.radius > 10:
            ili9488.circle(self.center_x, self.center_y, self.radius - 2, 
                          _darken_color(self.color, 0.7))
    
    def draw_ticks(self):
        """Draw tick marks around the dial."""
        if not self.show_ticks:
            return
        
        import math
        angle_range = self.end_angle - self.start_angle
        
        # Draw major ticks
        for i in range(self.num_major_ticks):
            proportion = i / (self.num_major_ticks - 1)
            angle = self.start_angle + (proportion * angle_range)
            
            # Major tick
            outer_x, outer_y = self._angle_to_point(angle, self.radius - 3)
            inner_x, inner_y = self._angle_to_point(angle, self.radius - 12)
            ili9488.line(inner_x, inner_y, outer_x, outer_y, self.color)
        
        # Draw minor ticks
        if self.num_minor_ticks > 0:
            total_segments = (self.num_major_ticks - 1) * (self.num_minor_ticks + 1)
            for i in range(total_segments + 1):
                # Skip positions where major ticks are
                if i % (self.num_minor_ticks + 1) == 0:
                    continue
                
                proportion = i / total_segments
                angle = self.start_angle + (proportion * angle_range)
                
                # Minor tick (shorter)
                outer_x, outer_y = self._angle_to_point(angle, self.radius - 3)
                inner_x, inner_y = self._angle_to_point(angle, self.radius - 8)
                tick_color = _darken_color(self.color, 0.6)
                ili9488.line(inner_x, inner_y, outer_x, outer_y, tick_color)
    
    def draw_needle(self, value, color=None):
        """Draw the needle pointing to the specified value."""
        if color is None:
            color = self.needle_color
        
        angle = self._value_to_angle(value)
        
        # Calculate needle endpoint (80% of radius)
        needle_length = int(self.radius * 0.75)
        end_x, end_y = self._angle_to_point(angle, needle_length)
        
        # Draw needle line
        ili9488.set_line_thickness(3)
        ili9488.line(self.center_x, self.center_y, end_x, end_y, color)
        ili9488.set_line_thickness(1)
        
        # Draw center cap
        cap_radius = 5
        ili9488.circle(self.center_x, self.center_y, cap_radius, 
                      _darken_color(color, 0.6), color)
        
        return angle
    
    def erase_needle(self, angle):
        """Erase the needle at the specified angle."""
        # Calculate needle endpoint
        needle_length = int(self.radius * 0.75)
        end_x, end_y = self._angle_to_point(angle, needle_length)
        
        # Erase with background color (slightly thicker to fully cover)
        ili9488.set_line_thickness(4)
        ili9488.line(self.center_x, self.center_y, end_x, end_y, self.bg_color)
        ili9488.set_line_thickness(1)
    
    def draw(self):
        """Draw the complete dial."""
        if not self.visible:
            return
        
        self.draw_bezel()
        self.draw_ticks()
        self.last_needle_angle = self.draw_needle(self.current_value)
    
    def set_value(self, value):
        """Update the dial to show a new value."""
        if value == self.current_value:
            return
        
        old_value = self.current_value
        
        # Erase old needle if it was drawn
        if self.last_needle_angle is not None:
            self.erase_needle(self.last_needle_angle)
            # Redraw any ticks that might have been erased
            self.draw_ticks()
        
        # Draw new needle
        self.current_value = value
        self.last_needle_angle = self.draw_needle(value)
        
        # Update display
        self.update()
        
        if self.on_change:
            self.on_change(self, value)
    
    def animate_to(self, target_value, steps=10, delay_ms=30):
        """Smoothly animate the needle to a target value.
        
        Args:
            target_value: The value to animate to
            steps: Number of intermediate steps
            delay_ms: Delay between steps in milliseconds
        """
        import time
        
        start_value = self.current_value
        step_size = (target_value - start_value) / steps
        
        for i in range(steps + 1):
            value = start_value + (step_size * i)
            self.set_value(value)
            time.sleep_ms(delay_ms)


class Compass(Widget):
    """A compass widget for displaying directional headings.
    
    Displays a circular compass rose with cardinal directions (N, S, E, W)
    and a rotating needle indicating the current heading.
    
    Args:
        x, y: Center position of the compass
        radius: Radius of the compass
        color: Main compass color
        needle_color: Color of the heading needle
        bg_color: Background color
    """
    
    def __init__(self, x, y, radius, color=COLOR_WHITE, 
                 needle_color=COLOR_RED, bg_color=COLOR_BLACK, on_change=None):
        # Widget bounds are a square around the circle
        super().__init__(x - radius - 5, y - radius - 5, 
                        (radius + 5) * 2, (radius + 5) * 2)
        self.center_x = x
        self.center_y = y
        self.radius = radius
        self.color = color
        self.needle_color = needle_color
        self.bg_color = bg_color
        self.heading = 0  # Current heading in degrees (0-360, 0=North)
        self.last_needle_angle = None
        self.show_rose = True
        self.show_degrees = True
        self.on_change = on_change 
    
    def _heading_to_angle(self, heading):
        """Convert compass heading to screen angle.
        
        Compass: 0=North, 90=East, 180=South, 270=West
        Screen: 0=Right, 90=Down, 180=Left, 270=Up
        Conversion: screen_angle = heading - 90
        """
        angle = heading - 90
        while angle < 0:
            angle += 360
        while angle >= 360:
            angle -= 360
        return angle
    
    def _angle_to_point(self, angle, distance):
        """Convert angle and distance to x, y coordinates."""
        import math
        rad = math.radians(angle)
        x = self.center_x + int(distance * math.cos(rad))
        y = self.center_y + int(distance * math.sin(rad))
        return x, y
    
    def draw_bezel(self):
        """Draw the outer circle bezel."""
        # Draw outer circle
        ili9488.circle(self.center_x, self.center_y, self.radius, self.color)
        # Draw inner circle for double-ring effect
        if self.radius > 10:
            ili9488.circle(self.center_x, self.center_y, self.radius - 2, 
                          _darken_color(self.color, 0.7))
    
    def draw_cardinal_points(self):
        """Draw N, S, E, W markers."""
        if not self.show_rose:
            return
        
        # Cardinal directions: N=0°, E=90°, S=180°, W=270° (compass heading)
        cardinals = [
            (0, 'N', COLOR_RED),      # North - Red
            (90, 'E', COLOR_WHITE),   # East - White
            (180, 'S', COLOR_WHITE),  # South - White
            (270, 'W', COLOR_WHITE)   # West - White
        ]
        
        for heading, label, label_color in cardinals:
            screen_angle = self._heading_to_angle(heading)
            
            # Draw tick mark
            outer_x, outer_y = self._angle_to_point(screen_angle, self.radius - 3)
            inner_x, inner_y = self._angle_to_point(screen_angle, self.radius - 15)
            ili9488.line(inner_x, inner_y, outer_x, outer_y, label_color)
            
            # Draw label indicator (small circle for now, would be text with font support)
            label_x, label_y = self._angle_to_point(screen_angle, self.radius - 22)
            font = FONT_MEDIUM
            if(self.radius < 30):
                font = FONT_SMALL
            ili9488.text(label_x - 4, label_y - 4, label, label_color, self.bg_color, font)
    def draw_degree_marks(self):
        """Draw degree tick marks around the compass."""
        if not self.show_degrees:
            return
        
        # Draw marks every 30 degrees
        for heading in range(0, 360, 30):
            # Skip cardinal directions (already drawn with longer ticks)
            if heading % 90 == 0:
                continue
            
            screen_angle = self._heading_to_angle(heading)
            
            # Draw tick
            outer_x, outer_y = self._angle_to_point(screen_angle, self.radius - 3)
            inner_x, inner_y = self._angle_to_point(screen_angle, self.radius - 10)
            tick_color = _darken_color(self.color, 0.6)
            ili9488.line(inner_x, inner_y, outer_x, outer_y, tick_color)
    
    def draw_needle(self, heading, color=None):
        """Draw the compass needle pointing to the specified heading."""
        if color is None:
            color = self.needle_color
        
        screen_angle = self._heading_to_angle(heading)
        
        # Draw main needle (pointing to heading)
        needle_length = int(self.radius * 0.7)
        end_x, end_y = self._angle_to_point(screen_angle, needle_length)
        
        ili9488.set_line_thickness(3)
        ili9488.line(self.center_x, self.center_y, end_x, end_y, color)
        ili9488.set_line_thickness(1)
        
        # Draw tail (opposite direction, shorter, darker)
        tail_angle = screen_angle + 180
        if tail_angle >= 360:
            tail_angle -= 360
        tail_length = int(self.radius * 0.25)
        tail_x, tail_y = self._angle_to_point(tail_angle, tail_length)
        tail_color = _darken_color(color, 0.5)
        
        ili9488.set_line_thickness(2)
        ili9488.line(self.center_x, self.center_y, tail_x, tail_y, tail_color)
        ili9488.set_line_thickness(1)
        
        # Draw center cap
        cap_radius = 5
        ili9488.circle(self.center_x, self.center_y, cap_radius, 
                      _darken_color(color, 0.6), color)
        
        return screen_angle
    
    def erase_needle(self, screen_angle):
        """Erase the needle at the specified screen angle."""
        # Erase main needle
        needle_length = int(self.radius * 0.7)
        end_x, end_y = self._angle_to_point(screen_angle, needle_length)
        
        ili9488.set_line_thickness(4)
        ili9488.line(self.center_x, self.center_y, end_x, end_y, self.bg_color)
        
        # Erase tail
        tail_angle = screen_angle + 180
        if tail_angle >= 360:
            tail_angle -= 360
        tail_length = int(self.radius * 0.25)
        tail_x, tail_y = self._angle_to_point(tail_angle, tail_length)
        
        ili9488.set_line_thickness(3)
        ili9488.line(self.center_x, self.center_y, tail_x, tail_y, self.bg_color)
        ili9488.set_line_thickness(1)
    
    def draw(self):
        """Draw the complete compass."""
        if not self.visible:
            return
        
        self.draw_bezel()
        self.draw_degree_marks()
        self.draw_cardinal_points()
        self.last_needle_angle = self.draw_needle(self.heading)
    
    def set_heading(self, heading):
        """Update the compass to show a new heading (0-360 degrees).
        
        Args:
            heading: Compass heading in degrees (0=North, 90=East, 180=South, 270=West)
        """
        # Normalize heading to 0-360
        while heading < 0:
            heading += 360
        while heading >= 360:
            heading -= 360
        
        if heading == self.heading:
            return
        
        old_heading = self.heading
        
        # Erase old needle if it was drawn
        if self.last_needle_angle is not None:
            self.erase_needle(self.last_needle_angle)
            # Redraw any marks that might have been erased
            self.draw_degree_marks()
            self.draw_cardinal_points()
        
        # Draw new needle
        self.heading = heading
        self.last_needle_angle = self.draw_needle(heading)
        
        # Update display
        self.update()
        
        if self.on_change:
            self.on_change(self, heading)
    
    def rotate_to(self, target_heading, steps=20, delay_ms=30):
        """Smoothly animate the compass needle to a target heading.
        Takes the shortest path around the circle.
        Args:
            target_heading: The heading to rotate to (0-360)
            steps: Number of intermediate steps
            delay_ms: Delay between steps in milliseconds
        """
        import time
    
        # Normalize target
        while target_heading < 0:
            target_heading += 360
        while target_heading >= 360:
            target_heading -= 360
    
        # Calculate shortest rotation direction
        start_heading = self.heading  # Save the starting position
        diff = target_heading - start_heading
    
        # Normalize diff to -180 to 180
        if diff > 180:
            diff -= 360
        elif diff < -180:
            diff += 360
    
        step_size = diff / steps
    
        for i in range(steps + 1):
            heading = start_heading + (step_size * i)  
            self.set_heading(heading)
            time.sleep_ms(delay_ms)

class HBoxLayout(Widget):
    """Horizontal box layout."""
    def __init__(self, x, y, width, height, spacing=5):
        super().__init__(x, y, width, height)
        self.children = []
        self.spacing = spacing
    
    def add_child(self, widget, stretch=1):
        self.children.append((widget, stretch))
        self._layout()
    
    def _layout(self):
        total_stretch = sum(s for _, s in self.children)
        available = self.width - (len(self.children) - 1) * self.spacing
        x = self.x
        
        for widget, stretch in self.children:
            w = int(available * stretch / total_stretch)
            widget.x = x
            widget.y = self.y
            widget.width = w
            widget.height = self.height
            x += w + self.spacing

class Screen:
    def __init__(self, name):
        self.name = name
        self.widgets = []
        
    def on_enter(self):
        pass
        
    def on_exit(self):
        pass
        
    def draw(self):
        for widget in self.widgets:
            if widget.visible:
                widget.draw()

class ScreenManager:
    def __init__(self, display):
        self.display = display
        self.screens = {}
        self.current_screen = None
        self.screen_history = []  # Track navigation history
        self.max_history = 10    
    def add_screen(self, screen):
        self.screens[screen.name] = screen
    
    def goto_screen(self, name, transition=None,  add_to_history=True):
        if name not in self.screens:
            raise ValueError(f"Screen '{name}' not found")
        
        old_screen = self.current_screen
        new_screen = self.screens[name]
        
        # Add current screen to history before switching
        if add_to_history and old_screen:
            self.screen_history.append(old_screen.name)
            # Limit history size
            if len(self.screen_history) > self.max_history:
                self.screen_history.pop(0)
        
        if old_screen:
            old_screen.on_exit()
        
        if transition:
            transition.animate(old_screen, new_screen)
        else:
            self.display.fill(0x000000)
        
        self.current_screen = new_screen
        new_screen.draw()
        ili9488.show()
        new_screen.on_enter()      
    
    def go_back(self, transition=None):
        if not self.screen_history:
            return False  # No history
        
        previous_name = self.screen_history.pop()
        self.goto_screen(previous_name, transition=transition, 
                        add_to_history=False)  # Don't re-add to history
        return True      
    
    def clear_history(self):
        self.screen_history = []

    #last minute helpers, TODO: Paul to implement tests
    def get_current_screen(self):
        return self.current_screen

    def get_screen(self, name):
        return self.screens.get(name)

    def has_screen(self, name):
        return name in self.screens    

class Transition:
    def animate(self, old_screen, new_screen):
        """Override in subclasses to implement transition effect.
        
        Args:
            old_screen: Screen being left (may be None)
            new_screen: Screen being entered
        """
        pass

class Slider(Widget):
    """Horizontal or vertical slider."""
    def __init__(self, x, y, width, height, min_val=0, max_val=100,
                 orientation='horizontal'):
        self.value = min_val
        self.dragging = False
    
    def on_touch(self, touch):
        if touch.pressed:
            # Calculate value from touch position
            if self.orientation == 'horizontal':
                proportion = (touch.x - self.x) / self.width
            else:
                proportion = (touch.y - self.y) / self.height
            self.value = self.min_val + proportion * (self.max_val - self.min_val)

class ListView(Widget):
    """Scrollable list of items."""
    def __init__(self, x, y, width, height, items=[]):
        self.items = items
        self.scroll_offset = 0
        self.selected_index = -1
    
    def draw(self):
        # Draw visible items with clipping
        item_height = 30
        visible_start = self.scroll_offset // item_height
        visible_end = (self.scroll_offset + self.height) // item_height
        
        for i in range(visible_start, min(visible_end + 1, len(self.items))):
            y = self.y + i * item_height - self.scroll_offset
            self._draw_item(i, y)
   


#Next level,  might be pushing it

class LineChart(Widget):
    """Real-time line chart widget for plotting data over time.
    
    Displays a scrolling line chart with optional grid lines, axis labels,
    and automatic scaling. Useful for sensor data visualization, performance
    monitoring, etc.
    
    Args:
        x, y: Top-left corner position
        width, height: Chart dimensions
        max_points: Maximum number of data points to display
        min_val: Minimum Y-axis value (None for auto-scale)
        max_val: Maximum Y-axis value (None for auto-scale)
        line_color: Color of the data line
        bg_color: Background color
        grid_color: Grid line color (None to disable grid)
        axis_color: Axis color
        show_labels: Whether to show axis labels
    """
    
    def __init__(self, x, y, width, height, max_points=100,
                 min_val=None, max_val=None,
                 line_color=COLOR_BTN_PRIMARY, bg_color=COLOR_BLACK,
                 grid_color=COLOR_GRAY_DARK, axis_color=COLOR_WHITE,
                 show_labels=True, show_grid=True, on_point_added=None):
        super().__init__(x, y, width, height)
        self.data_points = []
        self.max_points = max_points
        self.min_val = min_val
        self.max_val = max_val
        self.line_color = line_color
        self.bg_color = bg_color
        self.grid_color = grid_color
        self.axis_color = axis_color
        self.show_labels = show_labels
        self.show_grid = show_grid
        self.on_point_added = on_point_added        
        # Auto-scaling ranges
        self.auto_min = 0
        self.auto_max = 100
        
        # Chart area (leave space for labels if enabled)
        self.chart_padding = 25 if show_labels else 5
        self.chart_x = x + self.chart_padding
        self.chart_y = y + 5
        self.chart_width = width - self.chart_padding - 5
        self.chart_height = height - 10
    
    def _calculate_auto_range(self):
        """Calculate automatic min/max based on current data."""
        if not self.data_points:
            self.auto_min = 0
            self.auto_max = 100
            return
        
        data_min = min(self.data_points)
        data_max = max(self.data_points)
        
        # Add 10% padding for better visualization
        range_size = data_max - data_min
        if range_size == 0:
            range_size = abs(data_max) * 0.1 or 1
        
        padding = range_size * 0.1
        self.auto_min = data_min - padding
        self.auto_max = data_max + padding
    
    def _value_to_y(self, value):
        y_min = self.min_val if self.min_val is not None else self.auto_min
        y_max = self.max_val if self.max_val is not None else self.auto_max
        
        if y_max == y_min:
            return self.chart_y + self.chart_height // 2
        
        proportion = (value - y_min) / (y_max - y_min)
        y = self.chart_y + self.chart_height - int(proportion * self.chart_height)
        
        return y
    
    def _index_to_x(self, index):
        if len(self.data_points) <= 1:
            return self.chart_x
        
        x_step = self.chart_width / (self.max_points - 1)
        x = self.chart_x + int(index * x_step)
        
        return x
    
    def draw_grid(self):
        if not self.show_grid or self.grid_color is None:
            return
        
        num_h_lines = 5
        for i in range(num_h_lines):
            y = self.chart_y + int(i * self.chart_height / (num_h_lines - 1))
            ili9488.line(self.chart_x, y, 
                        self.chart_x + self.chart_width - 1, y,
                        self.grid_color)
        
        if len(self.data_points) > 1:
            v_step = max(1, self.max_points // 10)
            for i in range(0, self.max_points, v_step):
                if i >= len(self.data_points):
                    break
                x = self._index_to_x(i)
                ili9488.line(x, self.chart_y,
                           x, self.chart_y + self.chart_height - 1,
                           self.grid_color)
    
    def draw_axes(self):
        # Y axis (left)
        ili9488.line(self.chart_x, self.chart_y,
                    self.chart_x, self.chart_y + self.chart_height - 1,
                    self.axis_color)
        
        # X axis (bottom)
        ili9488.line(self.chart_x, self.chart_y + self.chart_height - 1,
                    self.chart_x + self.chart_width - 1, 
                    self.chart_y + self.chart_height - 1,
                    self.axis_color)
    
    def draw_labels(self):
        if not self.show_labels:
            return
        
        y_min = self.min_val if self.min_val is not None else self.auto_min
        y_max = self.max_val if self.max_val is not None else self.auto_max
        
        label_font = FONT_SMALL
        
        max_text = f"{int(y_max)}"
        ili9488.text(self.x + 2, self.chart_y, max_text, 
                    self.axis_color, self.bg_color, label_font)
        
        min_text = f"{int(y_min)}"
        ili9488.text(self.x + 2, 
                    self.chart_y + self.chart_height - 8, 
                    min_text, self.axis_color, self.bg_color, label_font)
    
    def draw_line(self):
        if len(self.data_points) < 2:
            if len(self.data_points) == 1:
                x = self._index_to_x(0)
                y = self._value_to_y(self.data_points[0])
                ili9488.circle(x, y, 2, self.line_color, self.line_color)
            return

        #HACK: Need to hold line thickness state or we'll break other calls
                
        old_thickness = ili9488.get_line_thickness()
        ili9488.set_line_thickness(2)
        
        for i in range(len(self.data_points) - 1):
            x1 = self._index_to_x(i)
            y1 = self._value_to_y(self.data_points[i])
            x2 = self._index_to_x(i + 1)
            y2 = self._value_to_y(self.data_points[i + 1])
            
            if (self.chart_x <= x1 <= self.chart_x + self.chart_width and
                self.chart_x <= x2 <= self.chart_x + self.chart_width):
                ili9488.line(x1, y1, x2, y2, self.line_color)
        
        ili9488.set_line_thickness(old_thickness)
    
    def draw(self):
        if not self.visible:
            return
        
        if self.min_val is None or self.max_val is None:
            self._calculate_auto_range()
        
        ili9488.rect(self.x, self.y, self.width, self.height,
                    self.bg_color, self.bg_color)
        
        self.draw_grid()
        self.draw_axes()
        self.draw_labels()
        self.draw_line()
    
    def add_point(self, value):
        self.data_points.append(value)
        
        if len(self.data_points) > self.max_points:
            self.data_points.pop(0)
        
        self.draw()
        self.update()
        # This might be overkill? but it is optional
        if self.on_point_added:
            self.on_point_added(self, value, len(self.data_points))

    def add_point_fast(self, value):
        """Add point with incremental drawing (faster)."""
        old_len = len(self.data_points)
        
        self.data_points.append(value)
        if len(self.data_points) > self.max_points:
            self.data_points.pop(0)
            # Full redraw needed when scrolling
            self.draw()
        else:
            # Just draw new segment
            if old_len >= 1:
                x1 = self._index_to_x(old_len - 1)
                y1 = self._value_to_y(self.data_points[old_len - 1])
                x2 = self._index_to_x(old_len)
                y2 = self._value_to_y(value)
                
                old_thickness = ili9488.get_line_thickness()
                ili9488.set_line_thickness(2)
                ili9488.line(x1, y1, x2, y2, self.line_color)
                ili9488.set_line_thickness(old_thickness)
    
        self.update()
    
    def clear(self):
        self.data_points = []
        self.draw()
        self.update()
    
    def set_data(self, data):
        
        self.data_points = list(data[-self.max_points:])
        self.draw()
        self.update()
    
    def set_range(self, min_val, max_val):
        self.min_val = min_val
        self.max_val = max_val
        self.draw()
        self.update()


#for testing

# chart = ui.LineChart(10, 10, 300, 220, max_points=50,
#                      line_color=ui.COLOR_GREEN,
#                      bg_color=ui.COLOR_BLACK,
#                      grid_color=ui.COLOR_GRAY_DARK,
#                      show_labels=True,
#                      show_grid=True)

# # Draw initial empty chart
# chart.draw()
# ili9488.show()

# # Simulate real-time data (sine wave)
# angle = 0
# while True:
#     value = 50 + 30 * math.sin(math.radians(angle))
#     chart.add_point(value)
    
#     angle = (angle + 10) % 360
#     time.sleep_ms(100)
