import re
import struct
import sys
from pathlib import Path


def declared_length(header, name):
    match = re.search(rf"^#define {name}Len (\d+)$", header, re.MULTILINE)
    if match is None:
        raise ValueError(f"{name}Len is missing from icon.h")
    return int(match.group(1))


def make_bmp(header_path, image_path, palette_path, output_path):
    header = Path(header_path).read_text(encoding="ascii")
    image = Path(image_path).read_bytes()
    palette = Path(palette_path).read_bytes()
    if len(image) != declared_length(header, "iconTiles"):
        raise ValueError("icon image length does not match icon.h")
    if len(palette) != declared_length(header, "iconPal"):
        raise ValueError("icon palette length does not match icon.h")

    width = height = 32
    if len(image) != 16 * 32 or len(palette) != 16 * 2:
        raise ValueError("icon assets must describe a 32x32 4bpp image")

    color_table = bytearray()
    for (color,) in struct.iter_unpack("<H", palette):
        red = (color & 0x1F) * 255 // 31
        green = ((color >> 5) & 0x1F) * 255 // 31
        blue = ((color >> 10) & 0x1F) * 255 // 31
        color_table.extend((blue, green, red, 0))

    pixels = bytearray()
    for row in range(height - 1, -1, -1):
        for column in range(0, width, 2):
            packed = bytearray()
            for pixel_x in (column, column + 1):
                tile = (row // 8) * 4 + pixel_x // 8
                pixel = (row % 8) * 8 + pixel_x % 8
                value = image[tile * 32 + pixel // 2]
                packed.append((value >> (4 * (pixel % 2))) & 0x0F)
            pixels.append(packed[0] | (packed[1] << 4))

    pixel_offset = 14 + 40 + len(color_table)
    file_size = pixel_offset + len(pixels)
    file_header = struct.pack("<2sIHHI", b"BM", file_size, 0, 0, pixel_offset)
    dib_header = struct.pack(
        "<IiiHHIIiiII", 40, width, height, 1, 4, 0, len(pixels), 2835, 2835, 16, 16
    )
    Path(output_path).write_bytes(file_header + dib_header + color_table + pixels)


if __name__ == "__main__":
    try:
        make_bmp(*sys.argv[1:])
    except (OSError, ValueError) as error:
        sys.exit(str(error))