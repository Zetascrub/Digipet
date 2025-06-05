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
