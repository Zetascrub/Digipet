from PIL import Image
import os

SPRITE_SIZE = 16


def load_sprites(path="Neko.png"):
    """Load a sprite sheet and return individual frames.

    Parameters
    ----------
    path : str
        Path to the sprite sheet. If a relative path is given it will be
        resolved relative to the repository's ``assets`` directory.

    Returns
    -------
    list[Image.Image]
        A list of PIL ``Image`` objects for each sprite frame.
    """

    if not os.path.isabs(path):
        path = os.path.join(os.path.dirname(__file__), "..", "assets", path)

    sheet = Image.open(path).convert("L")  # grayscale
    sprites = []

    for row in range(5):
        for col in range(6):
            x = col * SPRITE_SIZE
            y = row * SPRITE_SIZE
            frame = sheet.crop((x, y, x + SPRITE_SIZE, y + SPRITE_SIZE))
            sprites.append(frame)

    return sprites
