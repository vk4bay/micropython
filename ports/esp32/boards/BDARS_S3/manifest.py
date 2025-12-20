# Include base modules
include("$(PORT_DIR)/boards/manifest.py")

# Freeze our custom modules
# freeze("$(PORT_DIR)/modules")

# Freeze UI library and demos
freeze("$(PORT_DIR)/modules", (
    "ili9488_ui.py",
    "display_writer.py",
    "dejavu16.py",
    "dejavu24.py",
    "dejavu24bold.py",
    "dejavu32.py",
    "dejavu48.py",
    "mono32.py",
))



