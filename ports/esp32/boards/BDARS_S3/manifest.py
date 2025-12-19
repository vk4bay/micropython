# Include base modules
include("$(PORT_DIR)/boards/manifest.py")

# Freeze our custom modules
freeze("$(PORT_DIR)/modules")

# Optionally freeze specific font files if you want them in flash
# This saves RAM but makes them read-only
freeze("$(PORT_DIR)/modules", (
    "display_writer.py",
    "dejavu16.py",
    "dejavu24.py",
    "dejavu24bold.py",
    "dejavu32.py",
    "dejavu48.py",
    "mono32.py",
    "ili9488_ui_demo.py",
    "test_ui_widgets.py",
))



