# modules/constants.py

# Display + Device Identity
DEVICE_NAME = "Tama-Pi"
DISPLAY_WIDTH = 122
DISPLAY_HEIGHT = 250

# Interfaces
WIFI_INTERFACE = "wlan0"
ETH_INTERFACE = "eth0"

# System Info
LOG_PATH = "/tmp/tamapi.log"
CAPTURE_DIR = "/home/pi/handshakes"

# Timing (in seconds)
DISPLAY_CYCLE_TIME = 4
BLINK_INTERVAL = 1.5

# Icons / Symbols
DOT = "⦿"
ARROW = "→"
REFRESH = "⟳"
LOCK = "🔒"
UNLOCK = "🔓"

# Status Presets
SIMULATED_BATTERY = "92%"
SIMULATED_UPTIME = "00:14:33"


from PIL import ImageFont
FONT_PATH = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
FONT = ImageFont.truetype(FONT_PATH, 10)

# Font sizes (customizable per section)
FONT_SMALL = ImageFont.truetype(FONT_PATH, 8)
FONT_NORMAL = ImageFont.truetype(FONT_PATH, 10)
FONT_LARGE = ImageFont.truetype(FONT_PATH, 12)
