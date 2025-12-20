# ILI9488 UI Widget Library

A high-level UI widget library for the ILI9488 display driver on ESP32. This library provides common UI components like buttons, dialogs, progress bars, checkboxes, and more.

## Features

- **3D Buttons**: Raised/pressed button effects with customizable colors
- **Flat Buttons**: Simple flat-style buttons
- **Dialog Boxes**: OK, OK/Cancel, Yes/No/Cancel dialogs
- **Progress Bars**: Visual progress indicators
- **Checkboxes**: Toggle checkboxes with custom colors
- **Radio Buttons**: Grouped radio button selections
- **Panels**: Container widgets for organizing UI elements
- **Color Themes**: Pre-defined color schemes for consistent UI

## Installation

The library is included in the `modules_custom` directory and will be available when you build MicroPython for the BDARS_S3 or ESP32_GENERIC_S3 boards.

## Quick Start

```python
import ili9488
import ili9488_ui as ui

# Initialize display
SPI_HOST = 1  # SPI2_HOST
DC_PIN = 21
RST_PIN = 22
CS_PIN = 15

ili9488.init(SPI_HOST, DC_PIN, RST_PIN, CS_PIN)
ili9488.fill(ui.COLOR_BLACK)

# Create a 3D button
button = ui.Button3D(10, 10, 120, 40, "Click Me", ui.COLOR_BTN_PRIMARY)
button.draw()

# Update display
ili9488.show()
```

## Available Widgets

### Button

Flat button widget with customizable colors.

```python
btn = ui.Button(x, y, width, height, text, 
                color=ui.COLOR_BTN_PRIMARY,
                text_color=ui.COLOR_WHITE,
                border_color=None)
btn.draw()
btn.update()

# Toggle pressed state
btn.set_pressed(True)  # Pressed
btn.set_pressed(False) # Released
```

### Button3D

3D button with raised/pressed visual effects.

```python
btn = ui.Button3D(x, y, width, height, text,
                  color=ui.COLOR_BTN_PRIMARY,
                  text_color=ui.COLOR_WHITE)
btn.draw()
btn.update()

# Toggle pressed state
btn.set_pressed(True)  # Appears sunken
btn.set_pressed(False) # Appears raised
```

### Progress Bar

Visual progress indicator with customizable colors.

```python
pb = ui.ProgressBar(x, y, width, height,
                    min_val=0, max_val=100,
                    fg_color=ui.COLOR_BTN_PRIMARY,
                    bg_color=ui.COLOR_GRAY_LIGHT)
pb.set_value(50)  # Set to 50%
```

### CheckBox

Toggle checkbox with check mark.

```python
cb = ui.CheckBox(x, y, size=20, checked=False,
                 color=ui.COLOR_BTN_PRIMARY,
                 bg_color=ui.COLOR_WHITE)
cb.draw()
cb.toggle()  # Toggle checked state
```

### Radio Button

Radio button for exclusive selections.

```python
# Create radio buttons
rb1 = ui.RadioButton(x, y, radius=10, selected=True)
rb2 = ui.RadioButton(x, y+40, radius=10, selected=False)
rb3 = ui.RadioButton(x, y+80, radius=10, selected=False)

# Group them together
group = [rb1, rb2, rb3]
for rb in group:
    rb.group = group

# Select one (deselects others)
rb2.select()
```

### Panel

Container panel with optional border.

```python
panel = ui.Panel(x, y, width, height,
                 bg_color=ui.COLOR_GRAY_LIGHT,
                 border_color=ui.COLOR_GRAY_DARK,
                 has_border=True)
panel.draw()
```

### Dialogs

Pre-built dialog boxes for common interactions.

```python
# OK dialog
dialog = ui.show_ok_dialog("Title", "Message")

# OK/Cancel dialog
dialog = ui.show_ok_cancel_dialog("Confirm", "Are you sure?")

# Yes/No/Cancel dialog
dialog = ui.show_yes_no_cancel_dialog("Question", "Choose an option")

# Generic dialog
dialog = ui.show_dialog("Title", "Message", ui.DIALOG_OK_CANCEL)
```

## Color Constants

The library provides pre-defined colors:

### Basic Colors
- `COLOR_BLACK` - Black (0x000000)
- `COLOR_WHITE` - White (0xFFFFFF)
- `COLOR_RED` - Red (0xFF0000)
- `COLOR_GREEN` - Green (0x00FF00)
- `COLOR_BLUE` - Blue (0x0000FF)
- `COLOR_YELLOW` - Yellow (0xFFFF00)
- `COLOR_CYAN` - Cyan (0x00FFFF)
- `COLOR_MAGENTA` - Magenta (0xFF00FF)
- `COLOR_ORANGE` - Orange (0xFF8000)
- `COLOR_PURPLE` - Purple (0x8000FF)

### Gray Scale
- `COLOR_GRAY_DARK` - Dark gray (0x404040)
- `COLOR_GRAY` - Gray (0x808080)
- `COLOR_GRAY_LIGHT` - Light gray (0xC0C0C0)
- `COLOR_GRAY_LIGHTER` - Lighter gray (0xE0E0E0)

### UI Theme Colors
- `COLOR_BTN_PRIMARY` - Primary button color (0x0066CC)
- `COLOR_BTN_SUCCESS` - Success button color (0x00AA00)
- `COLOR_BTN_WARNING` - Warning button color (0xFF8800)
- `COLOR_BTN_DANGER` - Danger button color (0xCC0000)
- `COLOR_BTN_DEFAULT` - Default button color (gray)

## Touch Integration

To make the UI interactive, integrate with a touch controller. Here's an example with the ft6336 touch controller:

```python
import ili9488
import ili9488_ui as ui
import ft6336

# Initialize display and touch
ili9488.init(SPI_HOST, DC_PIN, RST_PIN, CS_PIN)
ft6336.init(I2C_SCL, I2C_SDA, INT_PIN, RST_PIN)

# Create buttons
btn_ok = ui.Button3D(10, 10, 100, 40, "OK", ui.COLOR_BTN_SUCCESS)
btn_cancel = ui.Button3D(120, 10, 100, 40, "Cancel", ui.COLOR_BTN_DANGER)

btn_ok.draw()
btn_cancel.draw()
ili9488.show()

# Handle touch events
while True:
    if ft6336.touched():
        x, y = ft6336.get_coordinates()
        
        if btn_ok.contains(x, y):
            btn_ok.set_pressed(True)
            # Handle OK action
            print("OK pressed")
            btn_ok.set_pressed(False)
        
        elif btn_cancel.contains(x, y):
            btn_cancel.set_pressed(True)
            # Handle Cancel action
            print("Cancel pressed")
            btn_cancel.set_pressed(False)
```

## Button Group Management

Use `ButtonGroup` to manage multiple buttons:

```python
# Create button group
group = ui.ButtonGroup()

btn1 = ui.Button3D(10, 10, 80, 35, "Btn1", ui.COLOR_BLUE)
btn2 = ui.Button3D(100, 10, 80, 35, "Btn2", ui.COLOR_GREEN)
btn3 = ui.Button3D(190, 10, 80, 35, "Btn3", ui.COLOR_RED)

group.add(btn1)
group.add(btn2)
group.add(btn3)

# Draw all buttons
group.draw_all()

# Update all buttons
group.update_all()

# Find button at touch coordinates
if touch_detected:
    x, y = get_touch_coordinates()
    button = group.find_at(x, y)
    if button:
        button.set_pressed(True)
        # Handle button action
        button.set_pressed(False)
```

## Complete Example

```python
import ili9488
import ili9488_ui as ui

# Initialize display
ili9488.init(1, 21, 22, 15)
ili9488.fill(ui.COLOR_WHITE)

# Create title bar
ili9488.rect(0, 0, ili9488.get_width(), 40, 
             ui.COLOR_BTN_PRIMARY, ui.COLOR_BTN_PRIMARY)

# Create main panel
panel = ui.Panel(10, 50, 300, 200, bg_color=ui.COLOR_GRAY_LIGHTER)
panel.draw()

# Add checkboxes
cb1 = ui.CheckBox(20, 70, size=20, checked=True)
cb2 = ui.CheckBox(20, 105, size=20, checked=False)
cb3 = ui.CheckBox(20, 140, size=20, checked=True)

cb1.draw()
cb2.draw()
cb3.draw()

# Add progress bar
progress = ui.ProgressBar(20, 180, 280, 25, fg_color=ui.COLOR_BTN_SUCCESS)
progress.set_value(75)

# Add action buttons
btn_ok = ui.Button3D(20, 220, 90, 40, "OK", ui.COLOR_BTN_SUCCESS)
btn_cancel = ui.Button3D(120, 220, 90, 40, "Cancel", ui.COLOR_BTN_DANGER)

btn_ok.draw()
btn_cancel.draw()

# Update display
ili9488.show()
```

## Demo Script

A comprehensive demo script is available at `ili9488_ui_demo.py`. It demonstrates all available widgets:

```python
import ili9488_ui_demo

# Run all demonstrations
ili9488_ui_demo.run_all_demos()

# Or run individual demos
ili9488_ui_demo.demo_buttons()
ili9488_ui_demo.demo_dialogs()
ili9488_ui_demo.demo_progress_bars()
ili9488_ui_demo.demo_checkboxes()
ili9488_ui_demo.demo_radio_buttons()
ili9488_ui_demo.demo_panel()
ili9488_ui_demo.demo_complete_ui()
```

## Performance Tips

1. **Use `update_region()`** instead of `show()` when updating small areas:
   ```python
   button.draw()
   button.update()  # Only updates button area
   ```

2. **Batch updates**: Draw multiple widgets before calling `show()`:
   ```python
   btn1.draw()
   btn2.draw()
   btn3.draw()
   ili9488.show()  # Update all at once
   ```

3. **Use panels** to group related widgets for efficient updates.

## Limitations

- Text rendering currently uses simple circle indicators. For full text support, integrate with the `writer.py` module or implement a font rendering system.
- Touch event handling requires integration with a touch controller (not included in this library).
- Colors are in RGB888 format (24-bit).

## Future Enhancements

Potential additions for future versions:
- Text label widget with font support
- Slider widget
- Dropdown/combo box widget
- List view widget
- Image widget
- Custom drawing callback support
- Animation support
- Touch gesture recognition

## License

This library is part of the MicroPython project and follows the same MIT license.

## Credits

Built on top of the ili9488.c display driver for ESP32.
