from PIL import Image, ImageDraw, ImageFont, ImageTk
import tkinter as tk
import time
import logging

EPD_WIDTH = 122
EPD_HEIGHT = 250

logger = logging.getLogger(__name__)
font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 12)

class EPD:
    def __init__(self):
        self.width = EPD_WIDTH
        self.height = EPD_HEIGHT
        self.root = tk.Tk()
        self.root.title("Mock eInk Display")

        # Portrait mode: width 122, height 250
        self.canvas = tk.Canvas(self.root, width=self.width, height=self.height)
        self.canvas.pack()

        self.tk_img = None
        self.img_obj = None
        self.is_initialized = False



    def init(self):
        logger.info("MockEPD: init()")
        self.is_initialized = True
        return 0

    def Clear(self, color=0xFF):
        logger.info("MockEPD: Clear()")
        img = Image.new('1', (self.width, self.height), 255 if color == 0xFF else 0)
        self.display(img)

    def getbuffer(self, image):
        logger.info("MockEPD: getbuffer()")
        if image.size != (self.width, self.height):
            image = image.resize((self.width, self.height)).convert('1')
        else:
            image = image.convert('1')
        return image

    def display(self, image):
        logger.info("MockEPD: display()")

        width, height = image.size
        self.canvas.config(width=width, height=height)

        self.tk_img = ImageTk.PhotoImage(image)
        if self.img_obj:
            self.canvas.itemconfig(self.img_obj, image=self.tk_img)
        else:
            self.img_obj = self.canvas.create_image(0, 0, anchor="nw", image=self.tk_img)

        self.root.update()






    def sleep(self):
        logger.info("MockEPD: sleep()")
        self.root.destroy()

    def display_fast(self, image):
        self.display(image)

    def displayPartial(self, image):
        self.display(image)

    def displayPartBaseImage(self, image):
        self.display(image)
