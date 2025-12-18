"""
ili9488_ui_demo.py - Demo script for ili9488_ui widgets

This script demonstrates the various UI widgets available in the ili9488_ui module.

Note: This is a demonstration script showing how to use the widgets.
For actual touch interaction, you would need to integrate with a touch controller
(e.g., ft6336.c) and handle touch events.
"""

import ili9488
import ili9488_ui as ui

# Configuration - adjust these for your hardware
SPI_HOST = 1  # SPI2_HOST
DC_PIN = 21
RST_PIN = 22
CS_PIN = 15

def demo_buttons():
    """Demonstrate button widgets."""
    print("=== Button Demo ===")
    
    # Clear screen
    ili9488.fill(ui.COLOR_BLACK)
    
    # Create various button styles
    y_offset = 20
    spacing = 50
    
    # Flat buttons with different colors
    btn1 = ui.Button(20, y_offset, 100, 40, "Primary", ui.COLOR_BTN_PRIMARY)
    btn2 = ui.Button(140, y_offset, 100, 40, "Success", ui.COLOR_BTN_SUCCESS)
    btn3 = ui.Button(260, y_offset, 100, 40, "Warning", ui.COLOR_BTN_WARNING)
    
    y_offset += spacing
    
    # 3D buttons
    btn4 = ui.Button3D(20, y_offset, 100, 40, "3D Primary", ui.COLOR_BTN_PRIMARY)
    btn5 = ui.Button3D(140, y_offset, 100, 40, "3D Success", ui.COLOR_BTN_SUCCESS)
    btn6 = ui.Button3D(260, y_offset, 100, 40, "3D Danger", ui.COLOR_BTN_DANGER)
    
    y_offset += spacing
    
    # Pressed state demonstration
    btn7 = ui.Button3D(20, y_offset, 100, 40, "Raised", ui.COLOR_BLUE)
    btn8 = ui.Button3D(140, y_offset, 100, 40, "Pressed", ui.COLOR_BLUE)
    btn8.set_pressed(True)
    
    # Draw all buttons
    for btn in [btn1, btn2, btn3, btn4, btn5, btn6, btn7, btn8]:
        btn.draw()
    
    # Update display
    ili9488.show()
    print("Button demo complete")


def demo_dialogs():
    """Demonstrate dialog boxes."""
    print("=== Dialog Demo ===")
    
    # Clear screen with a gradient background
    ili9488.fill(ui.COLOR_GRAY)
    
    # Draw some background elements to show dialog overlay
    for i in range(10):
        ili9488.circle(50 + i * 30, 100, 20, ui.COLOR_BLUE, ui.COLOR_BLUE)
    
    ili9488.show()
    
    # Show OK dialog
    print("Showing OK dialog...")
    dialog = ui.show_ok_dialog("Information", "This is an OK dialog")
    
    # In a real application, you would wait for user input here
    # For demo, we just show it
    print("OK dialog displayed")


def demo_dialogs_all_types():
    """Demonstrate all dialog types."""
    print("=== All Dialog Types Demo ===")
    
    # Clear screen
    ili9488.fill(ui.COLOR_GRAY_LIGHT)
    ili9488.show()
    
    # OK/Cancel dialog
    print("Showing OK/Cancel dialog...")
    dialog = ui.show_ok_cancel_dialog("Confirm", "Do you want to continue?")
    
    # Note: In a real app, you'd handle touch events here to determine which button was pressed
    

def demo_progress_bars():
    """Demonstrate progress bar widget."""
    print("=== Progress Bar Demo ===")
    
    # Clear screen
    ili9488.fill(ui.COLOR_BLACK)
    
    # Create progress bars with different colors
    pb1 = ui.ProgressBar(20, 50, 280, 30, fg_color=ui.COLOR_BTN_PRIMARY)
    pb2 = ui.ProgressBar(20, 100, 280, 30, fg_color=ui.COLOR_BTN_SUCCESS)
    pb3 = ui.ProgressBar(20, 150, 280, 30, fg_color=ui.COLOR_BTN_WARNING)
    pb4 = ui.ProgressBar(20, 200, 280, 30, fg_color=ui.COLOR_BTN_DANGER)
    
    # Draw progress bars at different levels
    pb1.set_value(25)
    pb2.set_value(50)
    pb3.set_value(75)
    pb4.set_value(100)
    
    ili9488.show()
    print("Progress bar demo complete")


def demo_checkboxes():
    """Demonstrate checkbox widgets."""
    print("=== Checkbox Demo ===")
    
    # Clear screen
    ili9488.fill(ui.COLOR_WHITE)
    
    # Create checkboxes
    cb1 = ui.CheckBox(20, 20, size=25, checked=False)
    cb2 = ui.CheckBox(20, 60, size=25, checked=True)
    cb3 = ui.CheckBox(20, 100, size=25, checked=True, color=ui.COLOR_GREEN)
    cb4 = ui.CheckBox(20, 140, size=25, checked=False, color=ui.COLOR_RED)
    
    # Disabled checkbox
    cb5 = ui.CheckBox(20, 180, size=25, checked=True)
    cb5.enabled = False
    
    # Draw all checkboxes
    for cb in [cb1, cb2, cb3, cb4, cb5]:
        cb.draw()
    
    ili9488.show()
    print("Checkbox demo complete")


def demo_radio_buttons():
    """Demonstrate radio button widgets."""
    print("=== Radio Button Demo ===")
    
    # Clear screen
    ili9488.fill(ui.COLOR_WHITE)
    
    # Create radio button group
    rb1 = ui.RadioButton(30, 40, radius=12, selected=True)
    rb2 = ui.RadioButton(30, 80, radius=12, selected=False)
    rb3 = ui.RadioButton(30, 120, radius=12, selected=False)
    
    # Set up group (so selecting one deselects others)
    group = [rb1, rb2, rb3]
    for rb in group:
        rb.group = group
    
    # Draw all radio buttons
    for rb in group:
        rb.draw()
    
    ili9488.show()
    print("Radio button demo complete")


def demo_panel():
    """Demonstrate panel widget."""
    print("=== Panel Demo ===")
    
    # Clear screen
    ili9488.fill(ui.COLOR_GRAY_DARK)
    
    # Create nested panels
    panel1 = ui.Panel(10, 10, 300, 200, bg_color=ui.COLOR_GRAY_LIGHT)
    panel2 = ui.Panel(30, 50, 120, 80, bg_color=ui.COLOR_WHITE)
    panel3 = ui.Panel(170, 50, 120, 80, bg_color=ui.COLOR_BLUE, has_border=False)
    
    # Draw panels
    panel1.draw()
    panel2.draw()
    panel3.draw()
    
    # Add some buttons on top of panels
    btn1 = ui.Button3D(50, 150, 80, 35, "OK", ui.COLOR_GREEN)
    btn2 = ui.Button3D(180, 150, 80, 35, "Cancel", ui.COLOR_RED)
    btn1.draw()
    btn2.draw()
    
    ili9488.show()
    print("Panel demo complete")


def demo_complete_ui():
    """Demonstrate a complete UI with multiple widget types."""
    print("=== Complete UI Demo ===")
    
    # Clear screen
    ili9488.fill(ui.COLOR_GRAY_LIGHT)
    
    # Title bar
    ili9488.rect(0, 0, ili9488.get_width(), 35, 
                 ui.COLOR_BTN_PRIMARY, ui.COLOR_BTN_PRIMARY)
    
    # Main panel
    panel = ui.Panel(10, 45, ili9488.get_width() - 20, 
                     ili9488.get_height() - 55, 
                     bg_color=ui.COLOR_WHITE)
    panel.draw()
    
    # Add checkboxes
    cb1 = ui.CheckBox(20, 60, size=20, checked=True)
    cb2 = ui.CheckBox(20, 95, size=20, checked=False)
    cb3 = ui.CheckBox(20, 130, size=20, checked=True)
    
    for cb in [cb1, cb2, cb3]:
        cb.draw()
    
    # Add progress bar
    pb = ui.ProgressBar(20, 170, ili9488.get_width() - 40, 25, 
                       fg_color=ui.COLOR_BTN_SUCCESS)
    pb.set_value(65)
    
    # Add action buttons at bottom
    btn_ok = ui.Button3D(20, ili9488.get_height() - 90, 90, 40, 
                         "OK", ui.COLOR_BTN_SUCCESS)
    btn_cancel = ui.Button3D(120, ili9488.get_height() - 90, 90, 40, 
                            "Cancel", ui.COLOR_BTN_DANGER)
    btn_apply = ui.Button3D(220, ili9488.get_height() - 90, 90, 40, 
                           "Apply", ui.COLOR_BTN_PRIMARY)
    
    for btn in [btn_ok, btn_cancel, btn_apply]:
        btn.draw()
    
    ili9488.show()
    print("Complete UI demo finished")


def run_all_demos():
    """Run all widget demonstrations. Each demo overwrites the previous one."""
    print("\n" + "="*50)
    print("ILI9488 UI Widget Library Demo")
    print("="*50 + "\n")
    
    print("Running all demos sequentially...")
    print("Note: Each demo will overwrite the screen.")
    
    demo_buttons()
    demo_dialogs()
    demo_progress_bars()
    demo_checkboxes()
    demo_radio_buttons()
    demo_panel()
    demo_complete_ui()
    
    print("\nAll demos completed!")


# Example usage guide
def usage_example():
    """
    Example usage of the UI library:
    
    # 1. Initialize display
    import ili9488
    import ili9488_ui as ui
    
    ili9488.init(1, 21, 22, 15)  # SPI_HOST, DC, RST, CS
    ili9488.fill(0x000000)
    
    # 2. Create widgets
    btn = ui.Button3D(10, 10, 120, 40, "Click Me", ui.COLOR_BTN_PRIMARY)
    progress = ui.ProgressBar(10, 60, 200, 30)
    checkbox = ui.CheckBox(10, 100, size=25, checked=True)
    
    # 3. Draw widgets
    btn.draw()
    progress.set_value(50)
    checkbox.draw()
    
    # 4. Update display
    ili9488.show()
    
    # 5. Show a dialog
    dialog = ui.show_ok_cancel_dialog("Confirm", "Are you sure?")
    
    # 6. Handle touch events (with touch controller)
    # if touch_detected:
    #     x, y = get_touch_coordinates()
    #     if btn.contains(x, y):
    #         btn.set_pressed(True)
    #         # Handle button press
    #         btn.set_pressed(False)
    """
    pass


if __name__ == "__main__":
    # Initialize display first
    # ili9488.init(SPI_HOST, DC_PIN, RST_PIN, CS_PIN)
    # run_all_demos()
    print("UI Demo module loaded. Call run_all_demos() to see demonstrations.")
