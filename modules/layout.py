from PIL import Image, ImageDraw
from modules import constants as C
from modules.textutil import wrap_text
from datetime import datetime

def draw_layout(dialog_lines=None, stats=None, blink=False, sprite_frame=None):
    image = Image.new('1', (C.DISPLAY_WIDTH, C.DISPLAY_HEIGHT), 255)
    draw = ImageDraw.Draw(image)

    if dialog_lines is None:
        dialog_lines = ["Found 10 hosts on subnet 192.168.8.0/24"]

    y = 5  # starting Y

    # ☺   Tama-Pi   08:35
    status_icon = "☺"
    name = C.DEVICE_NAME
    time_str = datetime.now().strftime("%H:%M")

    draw.text((5, y), status_icon, font=C.FONT_NORMAL, fill=0)

    name_width = draw.textbbox((0, 0), name, font=C.FONT_NORMAL)[2]
    name_x = (C.DISPLAY_WIDTH - name_width) // 2
    draw.text((name_x, y), name, font=C.FONT_NORMAL, fill=0)

    time_width = draw.textbbox((0, 0), time_str, font=C.FONT_NORMAL)[2]
    time_x = C.DISPLAY_WIDTH - time_width - 5
    draw.text((time_x, y), time_str, font=C.FONT_NORMAL, fill=0)

    y += 15
    draw.line((0, y, C.DISPLAY_WIDTH - 1, y), fill=0)
    y += 3

    # © 4   Ω 2   ∞ 1
    stat_icons = [("©", 4), ("Ω", 2), ("∞", 1)]
    col_width = C.DISPLAY_WIDTH // 3
    for i, (icon, val) in enumerate(stat_icons):
        text = f"{icon} {val}"
        text_width = draw.textbbox((0, 0), text, font=C.FONT_NORMAL)[2]
        x = i * col_width + (col_width - text_width) // 2
        draw.text((x, y), text, font=C.FONT_NORMAL, fill=0)

    y += 15
    draw.line((0, y, C.DISPLAY_WIDTH - 1, y), fill=0)
    y += 3

    # 🛈: Scanning Network (short form)
    short_status = "Scanning Network"
    short_lines = wrap_text(draw, f"🛈: {short_status}", C.DISPLAY_WIDTH - 10, C.FONT_NORMAL)
    for line in short_lines:
        draw.text((5, y), line, font=C.FONT_NORMAL, fill=0)
        y += 12

    draw.line((0, y, C.DISPLAY_WIDTH - 1, y), fill=0)
    y += 3

    # 📡: Long form message
    long_message = dialog_lines[0] if dialog_lines else "No status available"
    long_status = f"📡: {long_message}"

    long_lines = wrap_text(draw, long_status, C.DISPLAY_WIDTH - 10, C.FONT_NORMAL)
    for line in long_lines:
        draw.text((5, y), line, font=C.FONT_NORMAL, fill=0)
        y += 12

    draw.line((0, y, C.DISPLAY_WIDTH - 1, y), fill=0)

    # Sprite box reserved (220–249)
    draw.rectangle((0, 220, C.DISPLAY_WIDTH - 1, C.DISPLAY_HEIGHT - 1), outline=0)
    if sprite_frame:
        scaled = sprite_frame.resize((32, 32), Image.NEAREST)
        image.paste(scaled.convert("1"), ((C.DISPLAY_WIDTH - 32) // 2, 215))

    if blink:
        draw.rectangle((110, 235, 115, 240), fill=0)

    return image
