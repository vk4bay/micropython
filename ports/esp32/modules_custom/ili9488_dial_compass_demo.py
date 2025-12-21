"""
ili9488_dial_compass_demo.py - Demo for Dial and Compass widgets

Demonstrates the use of the new Dial (gauge) and Compass widgets
for the ili9488 display.

Usage:
    import ili9488
    import ili9488_dial_compass_demo as demo
    
    # Initialize display first
    ili9488.init(1, 21, 22, 15)  # Adjust pins for your hardware
    
    # Run demos
    demo.demo_dial()
    demo.demo_compass()
    demo.demo_both()
"""

import ili9488
import ili9488_ui as ui
import time


def demo_dial():
    """Demonstrate the Dial widget (speedometer style)."""
    print("Dial Demo - Drawing speedometer...")
    
    # Clear screen
    ili9488.fill(ui.COLOR_BLACK)
    
    # Create a speedometer dial (0-200 km/h)
    speed_dial = ui.Dial(
        x=160,                    # Center x
        y=240,                    # Center y
        radius=100,               # Dial radius
        min_val=0,                # Minimum value
        max_val=200,              # Maximum value (km/h)
        start_angle=-135,         # Starting angle (7 o'clock)
        end_angle=135,            # Ending angle (5 o'clock)
        color=ui.COLOR_WHITE,     # Dial color
        needle_color=ui.COLOR_RED,  # Needle color
        bg_color=ui.COLOR_BLACK   # Background color
    )
    
    # Draw the dial
    speed_dial.draw()
    ili9488.show()
    
    print("Animating speed changes...")
    
    # Animate through some speed values
    speeds = [0, 60, 120, 180, 200, 150, 80, 30, 0]
    
    for speed in speeds:
        print(f"  Speed: {speed} km/h")
        speed_dial.animate_to(speed, steps=15, delay_ms=20)
        time.sleep_ms(500)
    
    print("Dial demo complete!")


def demo_compass():
    """Demonstrate the Compass widget."""
    print("Compass Demo - Drawing compass...")
    
    # Clear screen
    ili9488.fill(ui.COLOR_BLACK)
    
    # Create a compass
    compass = ui.Compass(
        x=160,                      # Center x
        y=240,                      # Center y
        radius=100,                 # Compass radius
        color=ui.COLOR_WHITE,       # Compass color
        needle_color=ui.COLOR_RED,  # Needle color
        bg_color=ui.COLOR_BLACK     # Background color
    )
    
    # Draw the compass
    compass.draw()
    ili9488.show()
    
    print("Animating heading changes...")
    
    # Rotate through various headings
    headings = [
        (0, "North"),
        (45, "NE"),
        (90, "East"),
        (135, "SE"),
        (180, "South"),
        (225, "SW"),
        (270, "West"),
        (315, "NW"),
        (360, "North")
    ]
    
    for heading, direction in headings:
        print(f"  Heading: {heading}° ({direction})")
        compass.rotate_to(heading, steps=20, delay_ms=20)
        time.sleep_ms(500)
    
    print("Compass demo complete!")


def demo_both():
    """Demonstrate both Dial and Compass side by side."""
    print("Combined Demo - Drawing both widgets...")
    
    # Clear screen
    ili9488.fill(ui.COLOR_BLACK)
    
    # Create a smaller dial on the left
    temp_dial = ui.Dial(
        x=100,                     # Left side
        y=160,                     # Center vertically
        radius=70,                 # Smaller radius
        min_val=0,                 # Temperature range 0-100°C
        max_val=100,
        start_angle=-135,
        end_angle=135,
        color=ui.COLOR_CYAN,
        needle_color=ui.COLOR_ORANGE,
        bg_color=ui.COLOR_BLACK
    )
    temp_dial.num_major_ticks = 6  # Fewer ticks for smaller dial
    
    # Create a smaller compass on the right
    heading_compass = ui.Compass(
        x=220,                     # Right side
        y=160,                     # Center vertically
        radius=70,                 # Smaller radius
        color=ui.COLOR_GREEN,
        needle_color=ui.COLOR_YELLOW,
        bg_color=ui.COLOR_BLACK
    )
    
    # Draw both widgets
    temp_dial.draw()
    heading_compass.draw()
    ili9488.show()
    
    print("Animating both widgets simultaneously...")
    
    # Animate both in parallel (simplified - not truly parallel)
    for i in range(36):
        temp = 20 + (i * 2)  # Temperature rises from 20 to 90
        heading = i * 10      # Heading rotates 0-350
        
        temp_dial.set_value(temp)
        heading_compass.set_heading(heading)
        
        time.sleep_ms(100)
    
    print("Combined demo complete!")


def demo_arc():
    """Demonstrate the arc drawing primitive."""
    print("Arc Demo - Drawing various arcs...")
    
    # Clear screen
    ili9488.fill(ui.COLOR_BLACK)
    
    # Draw several arcs to show the capability
    arcs = [
        # (x, y, radius, start_angle, end_angle, color)
        (80, 120, 60, 0, 90, ui.COLOR_RED),        # Quarter circle (top-right)
        (240, 120, 60, 90, 180, ui.COLOR_GREEN),   # Quarter circle (bottom-right)
        (80, 360, 60, 180, 270, ui.COLOR_BLUE),    # Quarter circle (bottom-left)
        (240, 360, 60, 270, 360, ui.COLOR_YELLOW), # Quarter circle (top-left)
        (160, 240, 80, -45, 45, ui.COLOR_MAGENTA), # Small arc
        (160, 240, 100, 135, 225, ui.COLOR_CYAN),  # Large arc
    ]
    
    print("Drawing arcs...")
    for x, y, r, start, end, color in arcs:
        print(f"  Arc at ({x},{y}), radius={r}, {start}° to {end}°")
        ili9488.arc(x, y, r, start, end, color)
    
    ili9488.show()
    print("Arc demo complete!")


def run_all_demos():
    """Run all dial and compass demos in sequence."""
    print("="*50)
    print("ILI9488 Dial & Compass Demo Suite")
    print("="*50)
    
    # Check if display is initialized
    try:
        width = ili9488.get_width()
        height = ili9488.get_height()
        print(f"Display: {width}x{height}")
    except:
        print("ERROR: Display not initialized!")
        print("Please run: ili9488.init(spi_host, dc_pin, rst_pin, cs_pin)")
        return
    
    demos = [
        ("Arc Primitives", demo_arc),
        ("Dial Widget", demo_dial),
        ("Compass Widget", demo_compass),
        ("Combined Demo", demo_both)
    ]
    
    for name, demo_func in demos:
        print(f"\n--- {name} ---")
        demo_func()
        time.sleep(1)
    
    print("\n" + "="*50)
    print("All demos complete!")
    print("="*50)


if __name__ == "__main__":
    run_all_demos()
