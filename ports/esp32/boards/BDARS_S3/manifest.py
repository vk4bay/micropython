# Include base modules
include("$(PORT_DIR)/boards/manifest.py")

# Freeze a curated subset of modules from $(PORT_DIR)/modules.
# NOTE: We intentionally do NOT call `freeze("$(PORT_DIR)/modules")` to avoid
# freezing every module in that directory (e.g. to keep firmware size under
# control and only include board-specific functionality).
# If new modules are added under $(PORT_DIR)/modules that should be frozen
# for this board, they MUST be added explicitly to the list below.
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



