"""
Integrated e-ink display for Digipet - shows evolution status and attack progress.

This is an enhanced version of main.py that integrates with the evolution
and orchestrator systems.
"""

import os
import time
import datetime
import random
import psutil
import socket
from pathlib import Path
from PIL import Image, ImageDraw

from modules.sprites import load_sprites
from modules import netinfo
from modules import constants as C
from modules.layout import draw_layout
from modules.constants import FONT
from modules.textutil import wrap_text
from utils.timestamp import get_now

# Import Digipet systems (these will be passed in)
from modules.database import DigipetDatabase
from modules.evolution import EvolutionEngine
from modules.config import DigipetConfig
from modules.orchestrator import AttackOrchestrator


# Choose which EPD to use (mock or hardware)
if os.getenv("USE_MOCK", "1") == "1":
    from epd.mock_epd2in13_V4 import EPD
else:
    # When ready for hardware, import real driver
    from epd.mock_epd2in13_V4 import EPD


# Load sprite frames
sprites = load_sprites("Neko.png")

start_time = time.time()
prev_net_io = psutil.net_io_counters(pernic=True)


def render_frame(lines, blink=False):
    """Render a list of text lines to an image for the eInk display."""
    image = Image.new('1', (C.DISPLAY_WIDTH, C.DISPLAY_HEIGHT), 255)
    draw = ImageDraw.Draw(image)
    y_offset = 5
    line_height = 14

    for line in lines:
        wrapped = wrap_text(draw, line, 112, FONT)
        for subline in wrapped:
            draw.text((5, y_offset), subline, font=FONT, fill=0)
            y_offset += line_height

    if blink:
        draw.rectangle((110, 235, 115, 240), fill=0)

    return image


def boot_sequence(epd):
    """Boot sequence with Digipet-specific messaging."""
    boot_lines = [
        "Booting Digipet...",
        "Initializing evolution system...",
        "Loading attack modules...",
        "Connecting to network...",
        "Preparing autonomous mode...",
        "System ready. Let's evolve!"
    ]

    for i in range(1, len(boot_lines) + 1):
        frame = render_frame(boot_lines[:i])
        epd.display(epd.getbuffer(frame))
        time.sleep(1)


def safe_icon(emoji, fallback):
    """Return emoji if printable, otherwise fallback."""
    try:
        emoji.encode('utf-8')
        return emoji
    except UnicodeEncodeError:
        return fallback


def idle_loop(epd, db: DigipetDatabase, evolution: EvolutionEngine,
              config: DigipetConfig, orchestrator: AttackOrchestrator):
    """
    Enhanced idle loop that shows evolution status and attack progress.

    Display modes:
    0: Evolution Status
    1: Network Reconnaissance
    2: Attack Progress / Statistics
    3: System Stats
    4: Sprite Animation
    """
    global prev_net_io, start_time

    blink = False
    cycle = 0

    while True:
        blink = not blink
        mode = cycle % 5  # 5 display modes
        dialog = []
        stats = []

        # Sprite animation frame
        sprite_index = cycle % len(sprites)
        sprite = sprites[sprite_index]

        if mode == 0:
            # ——— Evolution Status Screen ———

            evo_status = evolution.get_full_status()
            current_stage = evo_status['current_stage']
            progress = evo_status['progress']
            stats_data = evo_status['stats']

            dialog = [
                f"=== {current_stage['name']} ===",
                f"Level {current_stage['level']}",
                "",
                f"XP: {stats_data['total_xp']}",
            ]

            if not progress['is_max_level']:
                dialog.append(f"Next: {progress['next_stage_name']}")
                dialog.append(f"Progress: {progress['progress_percent']}%")
            else:
                dialog.append("MAX LEVEL!")

            dialog.extend([
                "",
                f"Hosts: {stats_data['hosts_discovered']}",
                f"Services: {stats_data['services_enumerated']}",
                f"Creds: {stats_data['credentials_captured']}",
                f"Vulns: {stats_data['vulnerabilities_found']}"
            ])

            # Stats row
            stats = [
                ("LVL", current_stage['level']),
                ("XP", stats_data['total_xp'])
            ]

        elif mode == 1:
            # ——— Network Reconnaissance Screen ———

            interfaces = netinfo.get_all_interfaces_with_ips()
            gw = netinfo.get_gateway()
            hostname = netinfo.get_hostname()

            # Get discovered hosts from database
            all_hosts = db.get_hosts(status='active')
            total_hosts = len(all_hosts)

            dialog = [
                "=== Network Recon ===",
                f"Gateway: {gw}",
                f"Hostname: {hostname}",
                "",
                f"Discovered: {total_hosts} hosts"
            ]

            # Show some recent hosts
            for host in all_hosts[:5]:
                ip = host['ip_address']
                hostname_db = host.get('hostname', '')
                if hostname_db:
                    dialog.append(f"{ip} ({hostname_db})")
                else:
                    dialog.append(f"{ip}")

            if total_hosts > 5:
                dialog.append(f"...and {total_hosts - 5} more")

            # Stats row
            stats = [
                ("Hosts", total_hosts),
                ("GW", gw[:15] if len(gw) > 15 else gw)
            ]

        elif mode == 2:
            # ——— Attack Progress / Statistics Screen ———

            orch_status = orchestrator.get_status()
            db_stats = db.get_stats()

            status_text = "RUNNING" if orch_status['running'] else "STOPPED"
            if orch_status.get('paused'):
                status_text = "PAUSED"

            dialog = [
                "=== Attack Status ===",
                f"Status: {status_text}",
                f"State: {orch_status.get('current_state', 'idle')}",
                f"Profile: {orch_status.get('current_profile', 'N/A')}",
                "",
                f"Actions: {orch_status.get('actions_performed', 0)}",
                "",
                "=== Discoveries ===",
                f"Hosts: {db_stats.get('total_hosts', 0)}",
                f"Services: {db_stats.get('total_services', 0)}",
                f"Credentials: {db_stats.get('total_credentials', 0)}",
                f"Vulnerabilities: {db_stats.get('total_vulnerabilities', 0)}"
            ]

            # Stats row
            stats = [
                ("Status", status_text[:4]),
                ("Actions", orch_status.get('actions_performed', 0))
            ]

        elif mode == 3:
            # ——— System Stats & Throughput Screen ———

            # CPU & RAM %
            cpu = int(psutil.cpu_percent(interval=None))
            ram = int(psutil.virtual_memory().percent)

            # Network throughput
            cur_io = psutil.net_io_counters(pernic=True)
            up_total = down_total = 0
            for ifc, stats_cur in cur_io.items():
                if ifc == "lo":
                    continue
                prev = prev_net_io.get(ifc)
                if prev:
                    up_total += stats_cur.bytes_sent - prev.bytes_sent
                    down_total += stats_cur.bytes_recv - prev.bytes_recv
            prev_net_io = cur_io

            up_rate = up_total // C.DISPLAY_CYCLE_TIME // 1024
            down_rate = down_total // C.DISPLAY_CYCLE_TIME // 1024

            # Uptime
            uptime = int(time.time() - start_time)
            uptime_str = str(datetime.timedelta(seconds=uptime))

            dialog = [
                "=== System Stats ===",
                f"CPU: {cpu}%",
                f"RAM: {ram}%",
                "",
                f"Network (KB/s):",
                f"  ↓ Down: {down_rate}",
                f"  ↑ Up:   {up_rate}",
                "",
                f"Uptime: {uptime_str}"
            ]

            stats = [
                (safe_icon("🧠", "CPU"), cpu),
                (safe_icon("💾", "RAM"), ram),
                (safe_icon("⬇️", "DN"), down_rate),
                (safe_icon("⬆️", "UP"), up_rate),
            ]

        # mode == 4 will simply show sprite animation with previous dialog

        # Render and display
        frame = draw_layout(dialog, stats=stats, blink=blink, sprite_frame=sprite)
        epd.display(epd.getbuffer(frame))

        cycle += 1
        time.sleep(C.DISPLAY_CYCLE_TIME)


def run_display(db: DigipetDatabase, evolution: EvolutionEngine,
                config: DigipetConfig, orchestrator: AttackOrchestrator):
    """
    Main entry point for running the e-ink display.

    Args:
        db: Database instance
        evolution: Evolution engine instance
        config: Configuration instance
        orchestrator: Attack orchestrator instance
    """
    epd = EPD()
    epd.init()
    epd.Clear()

    print("[*] Starting Digipet display...")
    boot_sequence(epd)

    print("[*] Entering display loop...")
    idle_loop(epd, db, evolution, config, orchestrator)


if __name__ == "__main__":
    print("Error: main_integrated.py should be imported and run via digipet_daemon.py")
    print("Use: python3 digipet_daemon.py")
