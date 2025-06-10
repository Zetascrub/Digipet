import os
import time
import datetime
import random
import psutil
from PIL import Image, ImageDraw
from modules.sprites import load_sprites



# Modules
from modules import netinfo
from modules import constants as C
from modules.layout import draw_layout
from modules.constants import FONT
from modules.textutil import wrap_text
from modules.netinfo import discover_local_devices, get_ping_latency
from utils.timestamp import get_now


# Choose which EPD to use (mock or hardware)
if os.getenv("USE_MOCK", "1") == "1":
    from epd.mock_epd2in13_V4 import EPD
else:
    from epd.mock_epd2in13_V4 import EPD

# Load sprite frames used for animations
sprites = load_sprites("Neko.png")


# Font path (adjust if needed)
FONT_PATH = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"

start_time = time.time()
prev_net_io = psutil.net_io_counters(pernic=True)



def render_frame(lines, blink=False, center=False):
    """Render a list of text lines to an image for the eInk display."""
    image = Image.new('1', (C.DISPLAY_WIDTH, C.DISPLAY_HEIGHT), 255)
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
    """Return ``emoji`` if printable, otherwise ``fallback``."""

    try:
        emoji.encode('utf-8')
        return emoji
    except UnicodeEncodeError:
        return fallback

def idle_loop(epd):
    import time, datetime, socket, psutil
    global prev_net_io, start_time

    blink = False
    cycle = 0

    while True:
        blink = not blink
        mode = cycle % 3
        dialog = []
        stats = []

        # sprite animation frame
        sprite_index = cycle % len(sprites)
        sprite = sprites[sprite_index]

        if mode == 0:
            # ——— Network / Passive Recon Screen ———
            print("Entering network loop...")
            interfaces = netinfo.get_all_interfaces_with_ips()
            gw         = netinfo.get_gateway()
            hostname   = netinfo.get_hostname()

            # Stats row showing interface IPs and gateway
            for iface, ip in interfaces:
                if iface == "lo":
                    continue
                stats.append((iface, ip))
                if len(stats) == 2:
                    break
            stats.append(("GW", gw))

            # Header
            dialog = [f"{C.DEVICE_NAME} Idle"]

            # Interface IPs
            for iface, ip in interfaces:
                if iface != "lo":
                    dialog.append(f"{iface}: {ip}")

            # ARP‐table neighbours
            devices = netinfo.discover_local_devices()
            for dev in devices[:3]:
                try:
                    name = socket.gethostbyaddr(dev['ip'])[0]
                except Exception:
                    name = None
                entry = f"{dev['iface']} {dev['ip']}"
                if name:
                    entry += f" ({name})"
                entry += f" – {dev['mac']}"
                dialog.append(entry)
            total = len(devices)
            if total > 3:
                dialog.append(f"...and {total - 3} more")

            # Gateway ping
            ping = netinfo.get_ping_latency(gw)
            dialog.append(f"Ping GW: {ping}")

            # Uptime
            uptime = int(time.time() - start_time)
            dialog.append(f"Uptime: {str(datetime.timedelta(seconds=uptime))}")

            # Paginate long lists
            lines_per_page = 8
            pages = (len(dialog) + lines_per_page - 1) // lines_per_page
            page  = (cycle // 3) % pages
            display_dialog = dialog[page*lines_per_page : (page+1)*lines_per_page]
            if pages > 1:
                display_dialog.append(f"[{page+1}/{pages}]")

            # Footer / status bar
            display_dialog.extend([
                f"GW: {gw}",
                f"Host: {hostname}",
                "",
                "Mode: Passive Recon",
                f"Monitoring... {C.REFRESH}",
                f"{C.DOT if blink else '     '}"
            ])

            dialog = display_dialog

        elif mode == 1:
            # ——— System Stats & Throughput Screen ———
            print("Entering stats loop...")
            # CPU & RAM %
            cpu = int(psutil.cpu_percent(interval=None))
            ram = int(psutil.virtual_memory().percent)

            # Network throughput since last cycle
            cur_io = psutil.net_io_counters(pernic=True)
            up_total = down_total = 0
            for ifc, stats_cur in cur_io.items():
                if ifc == "lo":
                    continue
                prev = prev_net_io.get(ifc)
                if prev:
                    up_total   += stats_cur.bytes_sent - prev.bytes_sent
                    down_total += stats_cur.bytes_recv - prev.bytes_recv
            prev_net_io = cur_io

            up_rate   = up_total   // C.DISPLAY_CYCLE_TIME // 1024
            down_rate = down_total // C.DISPLAY_CYCLE_TIME // 1024

            stats = [
                (safe_icon("🧠", "CPU"), cpu),
                (safe_icon("💾", "RAM"), ram),
                (safe_icon("⬇️", "DN"), down_rate),
                (safe_icon("⬆️", "UP"), up_rate),
            ]
            dialog = [
                "Throughput KB/s",
                f"↓ {down_rate}",
                f"↑ {up_rate}"
            ]

        # mode == 2 will simply redisplay the last dialog/stats with sprite animation

        # Render and display
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
