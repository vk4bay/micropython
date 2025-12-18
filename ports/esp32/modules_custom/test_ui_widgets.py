"""
test_ui_widgets.py - Simple test script for ili9488_ui widgets

This script tests the basic functionality of the UI widget library
without requiring actual hardware.
"""

import sys
import os

# Mock the ili9488 module for testing
class MockILI9488:
    _width = 320
    _height = 480
    
    @staticmethod
    def get_width():
        return MockILI9488._width
    
    @staticmethod
    def get_height():
        return MockILI9488._height
    
    @staticmethod
    def fill(color):
        print(f"  Fill screen with color: 0x{color:06X}")
    
    @staticmethod
    def rect(x, y, w, h, border_color, fill_color=None):
        if fill_color is not None:
            print(f"  Draw rect at ({x},{y}) size {w}x{h} border=0x{border_color:06X} fill=0x{fill_color:06X}")
        else:
            print(f"  Draw rect at ({x},{y}) size {w}x{h} border=0x{border_color:06X}")
    
    @staticmethod
    def line(x0, y0, x1, y1, color):
        print(f"  Draw line from ({x0},{y0}) to ({x1},{y1}) color=0x{color:06X}")
    
    @staticmethod
    def circle(x, y, r, border_color, fill_color=None):
        if fill_color is not None:
            print(f"  Draw circle at ({x},{y}) radius={r} border=0x{border_color:06X} fill=0x{fill_color:06X}")
        else:
            print(f"  Draw circle at ({x},{y}) radius={r} border=0x{border_color:06X}")
    
    @staticmethod
    def update_region(x, y, w, h):
        print(f"  Update region at ({x},{y}) size {w}x{h}")
    
    @staticmethod
    def show():
        print("  Show (update full screen)")

# Replace ili9488 import with our mock
sys.modules['ili9488'] = MockILI9488

# Now import the UI library
sys.path.insert(0, os.path.join(os.path.dirname(__file__)))
import ili9488_ui as ui


def test_button():
    """Test Button widget."""
    print("\n=== Testing Button Widget ===")
    btn = ui.Button(10, 10, 100, 40, "Test", ui.COLOR_BTN_PRIMARY)
    print(f"Created button at ({btn.x}, {btn.y}) size {btn.width}x{btn.height}")
    btn.draw()
    
    # Test pressed state
    btn.set_pressed(True)
    btn.set_pressed(False)
    
    # Test contains
    assert btn.contains(50, 25) == True
    assert btn.contains(5, 5) == False
    print("✓ Button widget test passed")


def test_button3d():
    """Test Button3D widget."""
    print("\n=== Testing Button3D Widget ===")
    btn = ui.Button3D(20, 20, 120, 50, "3D Button", ui.COLOR_BLUE)
    print(f"Created 3D button at ({btn.x}, {btn.y}) size {btn.width}x{btn.height}")
    btn.draw()
    
    # Test pressed state
    btn.set_pressed(True)
    btn.set_pressed(False)
    print("✓ Button3D widget test passed")


def test_progress_bar():
    """Test ProgressBar widget."""
    print("\n=== Testing ProgressBar Widget ===")
    pb = ui.ProgressBar(10, 10, 200, 30)
    print(f"Created progress bar at ({pb.x}, {pb.y}) size {pb.width}x{pb.height}")
    
    # Test value setting
    pb.set_value(0)
    pb.set_value(50)
    pb.set_value(100)
    pb.set_value(150)  # Should clamp to max
    
    assert pb.value == 100
    print("✓ ProgressBar widget test passed")


def test_checkbox():
    """Test CheckBox widget."""
    print("\n=== Testing CheckBox Widget ===")
    cb = ui.CheckBox(10, 10, size=25)
    print(f"Created checkbox at ({cb.x}, {cb.y}) size {cb.width}x{cb.height}")
    cb.draw()
    
    # Test toggle
    assert cb.checked == False
    cb.toggle()
    assert cb.checked == True
    cb.toggle()
    assert cb.checked == False
    print("✓ CheckBox widget test passed")


def test_radio_button():
    """Test RadioButton widget."""
    print("\n=== Testing RadioButton Widget ===")
    rb1 = ui.RadioButton(30, 30, radius=12, selected=True)
    rb2 = ui.RadioButton(30, 60, radius=12, selected=False)
    rb3 = ui.RadioButton(30, 90, radius=12, selected=False)
    
    # Set up group
    group = [rb1, rb2, rb3]
    for rb in group:
        rb.group = group
    
    # Test selection
    assert rb1.selected == True
    assert rb2.selected == False
    
    rb2.select()
    assert rb1.selected == False
    assert rb2.selected == True
    assert rb3.selected == False
    
    print("✓ RadioButton widget test passed")


def test_panel():
    """Test Panel widget."""
    print("\n=== Testing Panel Widget ===")
    panel = ui.Panel(10, 10, 200, 150, bg_color=ui.COLOR_GRAY_LIGHT)
    print(f"Created panel at ({panel.x}, {panel.y}) size {panel.width}x{panel.height}")
    panel.draw()
    print("✓ Panel widget test passed")


def test_dialog():
    """Test Dialog widgets."""
    print("\n=== Testing Dialog Widgets ===")
    
    # Test OK dialog
    dialog = ui.show_ok_dialog("Test", "This is a test")
    print(f"Created OK dialog at ({dialog.x}, {dialog.y}) size {dialog.width}x{dialog.height}")
    assert len(dialog.buttons) == 1
    
    # Test OK/Cancel dialog
    dialog = ui.show_ok_cancel_dialog("Test", "This is a test")
    assert len(dialog.buttons) == 2
    
    # Test Yes/No/Cancel dialog
    dialog = ui.show_yes_no_cancel_dialog("Test", "This is a test")
    assert len(dialog.buttons) == 3
    
    print("✓ Dialog widget test passed")


def test_button_group():
    """Test ButtonGroup."""
    print("\n=== Testing ButtonGroup ===")
    group = ui.ButtonGroup()
    
    btn1 = ui.Button3D(10, 10, 80, 40, "Btn1", ui.COLOR_BLUE)
    btn2 = ui.Button3D(100, 10, 80, 40, "Btn2", ui.COLOR_GREEN)
    
    group.add(btn1)
    group.add(btn2)
    
    assert len(group.buttons) == 2
    
    # Test find_at
    found = group.find_at(50, 25)
    assert found == btn1
    
    found = group.find_at(140, 25)
    assert found == btn2
    
    found = group.find_at(5, 5)
    assert found is None
    
    print("✓ ButtonGroup test passed")


def test_color_functions():
    """Test color utility functions."""
    print("\n=== Testing Color Functions ===")
    
    # Test darken
    dark = ui._darken_color(0xFFFFFF, 0.5)
    print(f"Darken white by 50%: 0x{dark:06X}")
    assert dark < 0xFFFFFF
    
    # Test lighten
    light = ui._lighten_color(0x808080, 1.5)
    print(f"Lighten gray by 50%: 0x{light:06X}")
    assert light > 0x808080
    
    # Test blend
    blend = ui._blend_color(0xFF0000, 0x0000FF, 0.5)
    print(f"Blend red and blue 50/50: 0x{blend:06X}")
    
    print("✓ Color functions test passed")


def test_colors():
    """Test color constants."""
    print("\n=== Testing Color Constants ===")
    
    # Test basic colors
    assert ui.COLOR_BLACK == 0x000000
    assert ui.COLOR_WHITE == 0xFFFFFF
    assert ui.COLOR_RED == 0xFF0000
    assert ui.COLOR_GREEN == 0x00FF00
    assert ui.COLOR_BLUE == 0x0000FF
    
    # Test theme colors
    assert ui.COLOR_BTN_PRIMARY == 0x0066CC
    assert ui.COLOR_BTN_SUCCESS == 0x00AA00
    assert ui.COLOR_BTN_WARNING == 0xFF8800
    assert ui.COLOR_BTN_DANGER == 0xCC0000
    
    print("✓ Color constants test passed")


def run_all_tests():
    """Run all test functions."""
    print("\n" + "="*60)
    print("ILI9488 UI Widget Library Test Suite")
    print("="*60)
    
    try:
        test_colors()
        test_color_functions()
        test_button()
        test_button3d()
        test_progress_bar()
        test_checkbox()
        test_radio_button()
        test_panel()
        test_dialog()
        test_button_group()
        
        print("\n" + "="*60)
        print("✓ ALL TESTS PASSED")
        print("="*60 + "\n")
        return True
        
    except AssertionError as e:
        print(f"\n✗ TEST FAILED: {e}")
        import traceback
        traceback.print_exc()
        return False
    except Exception as e:
        print(f"\n✗ ERROR: {e}")
        import traceback
        traceback.print_exc()
        return False


if __name__ == "__main__":
    success = run_all_tests()
    sys.exit(0 if success else 1)
