import os
import time
import datetime
import random
from PIL import Image, ImageDraw
from modules.sprites import load_sprites



# Modules
from modules import netinfo
from modules import constants as C
from modules.layout import draw_layout
from modules.constants import FONT
from utils.timestamp import get_now

# Choose which EPD to use (mock or hardware)
if os.getenv("USE_MOCK", "1") == "1":
    from epd.mock_epd2in13_V4 import EPD
else:
    from epd.mock_epd2in13_V4 import EPD

# Display dimensions (from the driver)
EPD_WIDTH = 122
EPD_HEIGHT = 250

sprites = load_sprites("Neko.png")


# Font path (adjust if needed)
FONT_PATH = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"

def render_frame(lines, blink=False, center=False):
    image = Image.new('1', (122, 250), 255)
    draw = ImageDraw.Draw(image)
    y_offset = 5
    line_height = 14

    for line in lines:
        wrapped = wrap_text(draw, line, 112, FONT)  # margin = 5px
        for subline in wrapped:
            draw.text((5, y_offset), subline, font=FONT, fill=0)
            y_offset += line_height


    if blink:
        draw.rectangle((110, 235, 115, 240), fill=0)

    return image

def wrap_text(draw, text, max_width, font):
    lines = []
    words = text.split()
    line = ""

    for word in words:
        test_line = line + word + " "
        width = font.getlength(test_line)
        if width <= max_width:
            line = test_line
        else:
            lines.append(line.strip())
            line = word + " "
    if line:
        lines.append(line.strip())

    return lines



def boot_sequence(epd):
    boot_lines = [
        "Booting Tama-Pi...",
        "Loading kernel modules...",
        "Bringing up wlan0/eth0...",
        "Starting background tasks...",
        "Preparing attack surface...",
        "System ready."
    ]

    for i in range(1, len(boot_lines) + 1):
        frame = render_frame(boot_lines[:i])
        epd.display(epd.getbuffer(frame))
        time.sleep(1)

def safe_icon(emoji, fallback):
    try:
        emoji.encode('utf-8')
        return emoji
    except UnicodeEncodeError:
        return fallback

def idle_loop(epd):
    blink = False
    cycle = 0

    while True:
        blink = not blink
        mode = cycle % 3
        dialog = []
        stats = []

        sprite_index = cycle % len(sprites)
        sprite = sprites[sprite_index]


        if mode == 0:
            print("Entering network loop...")
            interfaces = netinfo.get_all_interfaces_with_ips()
            gw = netinfo.get_gateway()
            hostname = netinfo.get_hostname()

            dialog = [f"{C.DEVICE_NAME} Idle"]

            for iface, ip in interfaces:
                if iface != "lo":
                    dialog.append(f"{iface}: {ip}")


            dialog.extend([
                f"GW: {gw}",
                f"Host: {hostname}",
                "",
                "Mode: Passive Recon",
                f"Monitoring... {C.REFRESH}",
                f"{C.DOT if blink else '     '}"
            ])


        elif mode == 1:
            print("Entering stats loop...")
            stats = [(safe_icon("📶", "SIG"), 5), (safe_icon("🧠", "CPU"), 3), (safe_icon("🔒", "SEC"), 2)]
            dialog = [
                "New device detected",
                "IP: 192.168.8.140",
                "Host: iPhone"
            ]

        # 🧠 Moved here so sprite always gets used
        frame = draw_layout(dialog, stats=stats, blink=blink, sprite_frame=sprite)
        epd.display(epd.getbuffer(frame))
        cycle += 1
        time.sleep(C.DISPLAY_CYCLE_TIME)




def main():
    epd = EPD()
    epd.init()
    epd.Clear()

    print("Starting boot sequence...")
    boot_sequence(epd)

    print("Entering idle loop...")
    idle_loop(epd)

if __name__ == "__main__":
    main()
