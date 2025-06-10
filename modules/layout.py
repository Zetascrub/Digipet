from PIL import Image, ImageDraw
from modules import constants as C
from modules.textutil import wrap_text
from datetime import datetime

def draw_layout(dialog_lines=None, stats=None, blink=False, sprite_frame=None):
    """Render dialog and stats to an image for the eInk display."""
    image = Image.new("1", (C.DISPLAY_WIDTH, C.DISPLAY_HEIGHT), 255)
    draw = ImageDraw.Draw(image)

    if dialog_lines is None:
        dialog_lines = []

    y = 5

    # Header with device name and time
    status_icon = "☺"
    name = C.DEVICE_NAME
    time_str = datetime.now().strftime("%H:%M")

    draw.text((5, y), status_icon, font=C.FONT_NORMAL, fill=0)

    name_w = draw.textbbox((0, 0), name, font=C.FONT_NORMAL)[2]
    name_x = (C.DISPLAY_WIDTH - name_w) // 2
    draw.text((name_x, y), name, font=C.FONT_NORMAL, fill=0)

    time_w = draw.textbbox((0, 0), time_str, font=C.FONT_NORMAL)[2]
    time_x = C.DISPLAY_WIDTH - time_w - 5
    draw.text((time_x, y), time_str, font=C.FONT_NORMAL, fill=0)

    y += 15
    draw.line((0, y, C.DISPLAY_WIDTH - 1, y), fill=0)
    y += 3

    # Stats row
    if stats:
        col_width = C.DISPLAY_WIDTH // len(stats)
        for i, (icon, val) in enumerate(stats):
            text = f"{icon} {val}"
            text_w = draw.textbbox((0, 0), text, font=C.FONT_NORMAL)[2]
            x = i * col_width + (col_width - text_w) // 2
            draw.text((x, y), text, font=C.FONT_NORMAL, fill=0)
        y += 15
        draw.line((0, y, C.DISPLAY_WIDTH - 1, y), fill=0)
        y += 3

    # Dialog lines
    for line in dialog_lines:
        wrapped = wrap_text(draw, line, C.DISPLAY_WIDTH - 10, C.FONT_NORMAL)
        for sub in wrapped:
            draw.text((5, y), sub, font=C.FONT_NORMAL, fill=0)
            y += 12

    draw.line((0, y, C.DISPLAY_WIDTH - 1, y), fill=0)

    # Sprite area
    draw.rectangle((0, 220, C.DISPLAY_WIDTH - 1, C.DISPLAY_HEIGHT - 1), outline=0)
    if sprite_frame:
        scaled = sprite_frame.resize((32, 32), Image.NEAREST)
        image.paste(scaled.convert("1"), ((C.DISPLAY_WIDTH - 32) // 2, 215))

    if blink:
        draw.rectangle((110, 235, 115, 240), fill=0)

    return image
