# utils/timestamp.py
import datetime

def get_now():
    return datetime.datetime.now().strftime("%H:%M:%S")
