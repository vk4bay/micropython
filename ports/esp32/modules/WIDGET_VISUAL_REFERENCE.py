"""
UI Widget Visual Reference
===========================

This file provides ASCII art representations of the various UI widgets
to help visualize what they look like on the display.

Note: These are approximations. Actual widgets use colors and smooth graphics.
"""

# Button (Flat)
# ┌─────────────┐
# │   PRIMARY   │
# └─────────────┘

# Button3D (Raised)
# ╔═════════════╗
# ║   PRIMARY   ║
# ╚═════════════╝

# Button3D (Pressed)
# ╔═════════════╗
# ║   PRIMARY   ║  (appears sunken)
# ╚═════════════╝

# ProgressBar at 60%
# ┌────────────────────────────────┐
# │████████████████████            │
# └────────────────────────────────┘

# CheckBox (Unchecked)
# ┌───┐
# │   │
# └───┘

# CheckBox (Checked)
# ┌───┐
# │ X │
# └───┘

# RadioButton (Unselected)
#   ○

# RadioButton (Selected)
#   ●

# Panel
# ┌────────────────────────────┐
# │                            │
# │     Panel Contents         │
# │                            │
# └────────────────────────────┘

# Dialog (OK)
# ┌────────────────────────────┐
# │██████████ Title ███████████│ (blue title bar)
# ├────────────────────────────┤
# │                            │
# │    This is a message       │
# │                            │
# │         ╔═══════╗          │
# │         ║  OK   ║          │
# │         ╚═══════╝          │
# └────────────────────────────┘

# Dialog (OK/Cancel)
# ┌────────────────────────────┐
# │██████████ Title ███████████│
# ├────────────────────────────┤
# │                            │
# │    Are you sure?           │
# │                            │
# │  ╔═══════╗  ╔═══════╗      │
# │  ║  OK   ║  ║Cancel ║      │
# │  ╚═══════╝  ╚═══════╝      │
# └────────────────────────────┘

# Dialog (Yes/No/Cancel)
# ┌────────────────────────────┐
# │██████████ Title ███████████│
# ├────────────────────────────┤
# │                            │
# │    Choose an option        │
# │                            │
# │ ╔════╗ ╔════╗ ╔════════╗   │
# │ ║Yes ║ ║ No ║ ║ Cancel ║   │
# │ ╚════╝ ╚════╝ ╚════════╝   │
# └────────────────────────────┘

# Complete UI Example
# ┌──────────────────────────────────────┐
# │███████████ Application █████████████│ (title bar)
# ├──────────────────────────────────────┤
# │ ┌──────────────────────────────────┐ │
# │ │                                  │ │
# │ │  ┌───┐ Option 1                 │ │
# │ │  │ X │                           │ │
# │ │  └───┘                           │ │
# │ │                                  │ │
# │ │  ┌───┐ Option 2                 │ │
# │ │  │   │                           │ │
# │ │  └───┘                           │ │
# │ │                                  │ │
# │ │  Progress:                       │ │
# │ │  ┌──────────────────────────┐    │ │
# │ │  │██████████████████        │    │ │
# │ │  └──────────────────────────┘    │ │
# │ │                                  │ │
# │ │  ╔════════╗ ╔════════╗          │ │
# │ │  ║   OK   ║ ║ Cancel ║          │ │
# │ │  ╚════════╝ ╚════════╝          │ │
# │ └──────────────────────────────────┘ │
# └──────────────────────────────────────┘

"""
Actual Color Examples
=====================

Button Colors:
- COLOR_BTN_PRIMARY:  Blue (0x0066CC)
- COLOR_BTN_SUCCESS:  Green (0x00AA00)
- COLOR_BTN_WARNING:  Orange (0xFF8800)
- COLOR_BTN_DANGER:   Red (0xCC0000)
- COLOR_BTN_DEFAULT:  Gray (0x808080)

3D Button Effects:
- Raised: Light edge on top/left, dark edge on bottom/right
- Pressed: Dark edge on top/left, light edge on bottom/right
- Face color changes slightly when pressed

Progress Bar:
- Background: Light gray
- Fill: Customizable (default: primary blue)
- Border: Dark gray

CheckBox:
- Box: White background with dark border
- Check mark: X shape in configured color

RadioButton:
- Outer circle: Dark border
- Inner circle (when selected): Filled with configured color

Dialog:
- Shadow: Dark gray offset to bottom-right
- Title bar: Primary blue
- Background: Light gray
- Buttons: 3D style with appropriate colors

Panel:
- Background: Customizable (default: light gray)
- Border: Customizable (default: dark gray)
- Can be nested
"""
