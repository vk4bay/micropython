"""
simple_ui_example.py - Simple example for using ili9488_ui with hardware

This is a minimal example showing how to create a simple UI with buttons.
Copy this to your ESP32 and run it after initializing the SPI bus and display.

Hardware Setup:
- Connect ILI9488 display to SPI2 (adjust pins as needed)
- Optional: Connect FT6336 touch controller for interaction

Usage:
1. Upload this file to your ESP32
2. Import and run: import simple_ui_example; simple_ui_example.run()
"""

import ili9488
import ili9488_ui as ui

# Hardware configuration - adjust these for your setup
SPI_HOST = 1  # SPI2_HOST
DC_PIN = 21
RST_PIN = 22
CS_PIN = 15


def create_simple_button_ui():
    """Create a simple UI with three colored buttons."""
    
    # Clear screen with light gray background
    ili9488.fill(ui.COLOR_GRAY_LIGHT)
    
    # Create title bar
    title_height = 40
    ili9488.rect(0, 0, ili9488.get_width(), title_height,
                 ui.COLOR_BTN_PRIMARY, ui.COLOR_BTN_PRIMARY)
    
    # Add title text indicator (circle)
    ili9488.circle(20, title_height // 2, 5, 
                  ui.COLOR_WHITE, ui.COLOR_WHITE)
    
    # Create main content panel
    panel_y = title_height + 10
    panel = ui.Panel(10, panel_y, 
                     ili9488.get_width() - 20, 
                     ili9488.get_height() - panel_y - 10,
                     bg_color=ui.COLOR_WHITE)
    panel.draw()
    
    # Create three 3D buttons
    btn_width = 100
    btn_height = 50
    btn_spacing = 10
    start_y = panel_y + 20
    
    btn_red = ui.Button3D(20, start_y, btn_width, btn_height, 
                          "Red", ui.COLOR_RED)
    
    btn_green = ui.Button3D(20, start_y + btn_height + btn_spacing, 
                           btn_width, btn_height, 
                           "Green", ui.COLOR_GREEN)
    
    btn_blue = ui.Button3D(20, start_y + (btn_height + btn_spacing) * 2, 
                          btn_width, btn_height, 
                          "Blue", ui.COLOR_BLUE)
    
    # Draw all buttons
    btn_red.draw()
    btn_green.draw()
    btn_blue.draw()
    
    # Add a progress bar
    pb_y = start_y + (btn_height + btn_spacing) * 3 + 10
    progress = ui.ProgressBar(20, pb_y, 
                             ili9488.get_width() - 40, 30,
                             fg_color=ui.COLOR_BTN_PRIMARY)
    progress.set_value(60)
    
    # Add checkboxes
    cb_y = pb_y + 50
    cb1 = ui.CheckBox(20, cb_y, size=20, checked=True)
    cb2 = ui.CheckBox(20, cb_y + 35, size=20, checked=False)
    cb3 = ui.CheckBox(20, cb_y + 70, size=20, checked=True)
    
    cb1.draw()
    cb2.draw()
    cb3.draw()
    
    # Update the entire display
    ili9488.show()
    
    print("Simple UI created!")
    print("Buttons at positions:")
    print(f"  Red: ({btn_red.x}, {btn_red.y})")
    print(f"  Green: ({btn_green.x}, {btn_green.y})")
    print(f"  Blue: ({btn_blue.x}, {btn_blue.y})")
    
    return {
        'buttons': [btn_red, btn_green, btn_blue],
        'progress': progress,
        'checkboxes': [cb1, cb2, cb3]
    }


def create_dialog_example():
    """Show a dialog box example."""
    
    # Clear screen
    ili9488.fill(ui.COLOR_GRAY)
    
    # Add some background content
    ili9488.rect(50, 50, 220, 150, 
                ui.COLOR_BLUE, ui.COLOR_BLUE)
    ili9488.circle(160, 125, 40, 
                  ui.COLOR_YELLOW, ui.COLOR_YELLOW)
    
    ili9488.show()
    
    # Show a dialog on top
    dialog = ui.show_ok_cancel_dialog("Confirmation", 
                                      "Do you want to continue?")
    
    print("Dialog created!")
    print(f"  Position: ({dialog.x}, {dialog.y})")
    print(f"  Size: {dialog.width}x{dialog.height}")
    print(f"  Buttons: {len(dialog.buttons)}")
    
    return dialog


def create_radio_button_example():
    """Show radio button example."""
    
    # Clear screen
    ili9488.fill(ui.COLOR_WHITE)
    
    # Create title
    ili9488.rect(0, 0, ili9488.get_width(), 35,
                ui.COLOR_BTN_PRIMARY, ui.COLOR_BTN_PRIMARY)
    
    # Create radio button group
    rb_x = 40
    rb_start_y = 60
    rb_spacing = 45
    
    rb1 = ui.RadioButton(rb_x, rb_start_y, radius=15, selected=True)
    rb2 = ui.RadioButton(rb_x, rb_start_y + rb_spacing, radius=15)
    rb3 = ui.RadioButton(rb_x, rb_start_y + rb_spacing * 2, radius=15)
    
    # Set up group
    group = [rb1, rb2, rb3]
    for rb in group:
        rb.group = group
    
    # Draw radio buttons
    for rb in group:
        rb.draw()
    
    # Add labels (simple circles as indicators)
    label_x = rb_x + 25
    ili9488.circle(label_x, rb_start_y, 3, 
                  ui.COLOR_BLACK, ui.COLOR_BLACK)
    ili9488.circle(label_x, rb_start_y + rb_spacing, 3, 
                  ui.COLOR_BLACK, ui.COLOR_BLACK)
    ili9488.circle(label_x, rb_start_y + rb_spacing * 2, 3, 
                  ui.COLOR_BLACK, ui.COLOR_BLACK)
    
    ili9488.show()
    
    print("Radio button example created!")
    
    return group


def run():
    """Run the simple UI example."""
    
    print("\n" + "="*50)
    print("ILI9488 Simple UI Example")
    print("="*50 + "\n")
    
    try:
        # Initialize display
        print("Initializing display...")
        ili9488.init(SPI_HOST, DC_PIN, RST_PIN, CS_PIN)
        print(f"Display size: {ili9488.get_width()}x{ili9488.get_height()}")
        
        # Create simple button UI
        print("\nCreating button UI...")
        widgets = create_simple_button_ui()
        
        print("\nUI created successfully!")
        print("\nTo interact with buttons, integrate with a touch controller.")
        print("Example touch handling:")
        print("  if touch_detected:")
        print("    x, y = get_touch_coordinates()")
        print("    for btn in widgets['buttons']:")
        print("      if btn.contains(x, y):")
        print("        btn.set_pressed(True)")
        print("        # Handle button action")
        print("        btn.set_pressed(False)")
        
        return widgets
        
    except Exception as e:
        print(f"Error: {e}")
        import sys
        sys.print_exception(e)
        return None


def run_dialog():
    """Run dialog example."""
    
    print("\nShowing dialog example...")
    
    try:
        # Initialize display if not already done
        ili9488.init(SPI_HOST, DC_PIN, RST_PIN, CS_PIN)
        
        # Create dialog
        dialog = create_dialog_example()
        
        print("\nDialog created successfully!")
        return dialog
        
    except Exception as e:
        print(f"Error: {e}")
        import sys
        sys.print_exception(e)
        return None


def run_radio():
    """Run radio button example."""
    
    print("\nShowing radio button example...")
    
    try:
        # Initialize display if not already done
        ili9488.init(SPI_HOST, DC_PIN, RST_PIN, CS_PIN)
        
        # Create radio buttons
        group = create_radio_button_example()
        
        print("\nRadio buttons created successfully!")
        return group
        
    except Exception as e:
        print(f"Error: {e}")
        import sys
        sys.print_exception(e)
        return None


# Auto-run when imported (comment out if you want manual control)
# run()

print("Simple UI Example loaded.")
print("Call run() to create the button UI")
print("Call run_dialog() to show dialog example")
print("Call run_radio() to show radio button example")
