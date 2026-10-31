# Переводит кадр sint_editor_preview в PNG, чтобы его можно было посмотреть.
# Запуск: python3 -I tools/editorPreview/ppmToPng.py кадр.ppm кадр.png

import struct
import sys
import zlib


def make_chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))


source = open(sys.argv[1], 'rb').read()
header_end = source.index(b'\n') + 1
width, height = (int(value) for value in source[:header_end].split()[1:3])
pixels = source[header_end:]
rows = b''.join(b'\0' + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))

with open(sys.argv[2], 'wb') as target:
    target.write(b'\x89PNG\r\n\x1a\n')
    target.write(make_chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)))
    target.write(make_chunk(b'IDAT', zlib.compress(rows, 6)))
    target.write(make_chunk(b'IEND', b''))
