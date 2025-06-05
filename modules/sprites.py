from PIL import Image
import os

SPRITE_SIZE = 16

def load_sprites(path="Neko.png"):
    full_path = os.path.join(os.path.dirname(__file__), "..", "assets", "Neko.png")
    sheet = Image.open(full_path).convert("L")  # grayscale
    sprites = []

    for row in range(5):
        for col in range(6):
            x = col * SPRITE_SIZE
            y = row * SPRITE_SIZE
            frame = sheet.crop((x, y, x + SPRITE_SIZE, y + SPRITE_SIZE))
            sprites.append(frame)

    return sprites
