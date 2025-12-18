# Getting Started with ILI9488 UI Widgets

## Quick Start Checklist

### Hardware Setup
- [ ] Connect ILI9488 display to ESP32 SPI bus
- [ ] Note your pin assignments:
  - [ ] SPI Host (e.g., 1 for SPI2_HOST)
  - [ ] DC Pin (e.g., 21)
  - [ ] RST Pin (e.g., 22)
  - [ ] CS Pin (e.g., 15)
- [ ] Optional: Connect FT6336 touch controller
  - [ ] I2C SDA Pin
  - [ ] I2C SCL Pin
  - [ ] INT Pin
  - [ ] RST Pin

### Software Setup
- [ ] Build MicroPython for your board (BDARS_S3 or ESP32_GENERIC_S3)
- [ ] Flash firmware to ESP32
- [ ] Verify ili9488 module is available

### First Test
```python
# 1. Import modules
import ili9488
import ili9488_ui as ui

# 2. Initialize display
ili9488.init(1, 21, 22, 15)  # Adjust pins for your hardware

# 3. Clear screen
ili9488.fill(ui.COLOR_BLACK)

# 4. Create a button
btn = ui.Button3D(10, 10, 120, 40, "Hello", ui.COLOR_BTN_PRIMARY)
btn.draw()

# 5. Update display
ili9488.show()

# Success! You should see a blue 3D button on your display
```

### Running Examples

#### Simple Example
```python
import simple_ui_example
simple_ui_example.run()
```

#### Full Demo
```python
import ili9488_ui_demo
ili9488_ui_demo.run_all_demos()
```

### Adding Touch Support

```python
import ili9488
import ili9488_ui as ui
import ft6336  # Your touch controller

# Initialize display and touch
ili9488.init(1, 21, 22, 15)
ft6336.init(scl_pin, sda_pin, int_pin, rst_pin)

# Create buttons
btn_ok = ui.Button3D(10, 10, 100, 40, "OK", ui.COLOR_BTN_SUCCESS)
btn_ok.draw()
ili9488.show()

# Main loop
while True:
    if ft6336.touched():
        x, y = ft6336.get_coordinates()
        
        if btn_ok.contains(x, y):
            btn_ok.set_pressed(True)
            # Handle OK button press
            print("OK button pressed!")
            btn_ok.set_pressed(False)
```

## Common Tasks

### Creating Different Widget Types

#### Button (Flat)
```python
btn = ui.Button(x, y, width, height, "Label", ui.COLOR_BTN_PRIMARY)
btn.draw()
btn.update()
```

#### Button (3D)
```python
btn = ui.Button3D(x, y, width, height, "Label", ui.COLOR_BTN_SUCCESS)
btn.draw()
btn.update()
```

#### Progress Bar
```python
pb = ui.ProgressBar(x, y, width, height)
pb.set_value(50)  # 50%
```

#### CheckBox
```python
cb = ui.CheckBox(x, y, size=20, checked=False)
cb.draw()
# Later: cb.toggle()
```

#### Radio Buttons
```python
rb1 = ui.RadioButton(x, y, radius=12, selected=True)
rb2 = ui.RadioButton(x, y+40, radius=12)
group = [rb1, rb2]
for rb in group:
    rb.group = group
# Later: rb2.select()
```

#### Panel
```python
panel = ui.Panel(x, y, width, height, 
                 bg_color=ui.COLOR_GRAY_LIGHT)
panel.draw()
```

#### Dialog
```python
dialog = ui.show_ok_cancel_dialog("Title", "Message")
# Handle button presses on dialog.buttons[0] and dialog.buttons[1]
```

### Using Colors

```python
# Basic colors
ui.COLOR_BLACK, ui.COLOR_WHITE, ui.COLOR_RED, ui.COLOR_GREEN, ui.COLOR_BLUE

# Theme colors  
ui.COLOR_BTN_PRIMARY    # Blue
ui.COLOR_BTN_SUCCESS    # Green
ui.COLOR_BTN_WARNING    # Orange
ui.COLOR_BTN_DANGER     # Red

# Gray scale
ui.COLOR_GRAY_DARK, ui.COLOR_GRAY, ui.COLOR_GRAY_LIGHT

# Custom colors (RGB888 format)
my_color = 0xFF5733  # Orange-red
```

### Managing Multiple Buttons

```python
group = ui.ButtonGroup()
btn1 = ui.Button3D(10, 10, 80, 35, "1", ui.COLOR_BLUE)
btn2 = ui.Button3D(100, 10, 80, 35, "2", ui.COLOR_GREEN)

group.add(btn1)
group.add(btn2)
group.draw_all()

# In touch handler:
if touch_detected:
    x, y = get_touch_coordinates()
    button = group.find_at(x, y)
    if button:
        # Handle the button that was pressed
        pass
```

## Troubleshooting

### Display Not Working
- [ ] Check SPI pin connections
- [ ] Verify voltage levels (3.3V for ESP32)
- [ ] Confirm SPI host number
- [ ] Try calling ili9488.mem_info() to check memory

### Import Errors
- [ ] Verify firmware was built with UI library
- [ ] Check board type (must be BDARS_S3 or ESP32_GENERIC_S3)
- [ ] Confirm modules are frozen in manifest.py

### Widgets Not Visible
- [ ] Call ili9488.show() or widget.update() after drawing
- [ ] Check widget coordinates (must be within screen bounds)
- [ ] Verify widget.visible is True
- [ ] Try filling screen with a color first to test display

### Touch Not Working
- [ ] Verify touch controller is initialized
- [ ] Check I2C connections
- [ ] Confirm touch coordinates match display orientation
- [ ] Test touch controller independently first

## Performance Tips

1. **Minimize Full-Screen Updates:**
   - Use `widget.update()` for individual widgets
   - Use `ili9488.update_region()` for specific areas
   - Reserve `ili9488.show()` for complete redraws

2. **Batch Operations:**
   ```python
   # Good: Draw all widgets then update once
   btn1.draw()
   btn2.draw()
   btn3.draw()
   ili9488.show()
   
   # Avoid: Update after each widget
   # btn1.draw(); ili9488.show()
   # btn2.draw(); ili9488.show()
   # btn3.draw(); ili9488.show()
   ```

3. **Use Panels for Groups:**
   - Panel can serve as a dirty region tracker
   - Easier to update related widgets together

## Next Steps

- [ ] Read README_UI.md for detailed documentation
- [ ] Review ili9488_ui_demo.py for more examples
- [ ] Check IMPLEMENTATION_SUMMARY.md for technical details
- [ ] Browse WIDGET_VISUAL_REFERENCE.py for widget appearance
- [ ] Integrate touch controller for interactive UIs
- [ ] Build your custom application!

## Resources

- **Documentation:** README_UI.md
- **Examples:** simple_ui_example.py, ili9488_ui_demo.py
- **Tests:** test_ui_widgets.py
- **Technical Details:** IMPLEMENTATION_SUMMARY.md
- **Visual Reference:** WIDGET_VISUAL_REFERENCE.py

## Need Help?

Common questions:
1. **How do I add text to buttons?**
   - Current implementation uses circle indicators
   - For full text, integrate with writer.py module
   - Or extend Button class to use ili9488.text() if available

2. **Can I create custom widgets?**
   - Yes! Inherit from ui.Widget base class
   - Implement draw() method
   - See existing widgets as examples

3. **How do I change button colors?**
   - Pass color parameter when creating button
   - Use ui.COLOR_* constants or custom RGB888 values
   - Example: ui.Button3D(x, y, w, h, "Label", 0xFF0000)  # Red

4. **Can widgets be animated?**
   - Not built-in, but you can:
   - Redraw widget with different parameters
   - Use timers to trigger updates
   - Call widget.draw() and widget.update() periodically

Enjoy building your UI! 🎨
