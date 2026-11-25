"""Render a textured OBJ with the public command and verify colour regions."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib


def chunk(kind, data):
    payload = kind + data
    return struct.pack(">I", len(data)) + payload + struct.pack(">I", zlib.crc32(payload))


def decoded_rows(path):
    data = path.read_bytes()
    offset = 8
    compressed = bytearray()
    while offset < len(data):
        size = int.from_bytes(data[offset:offset + 4], "big")
        if data[offset + 4:offset + 8] == b"IDAT":
            compressed.extend(data[offset + 8:offset + 8 + size])
        offset += size + 12
    raw = zlib.decompress(compressed)
    assert len(raw) == 512 * 2049
    for y in range(512):
        start = y * 2049
        assert raw[start] == 0  # CameraPng stores rows without filters
        yield raw[start + 1:start + 2049]


def check(command):
    with tempfile.TemporaryDirectory(dir=Path.cwd()) as work:
        root = Path(work)
        model = root / "panel.obj"
        model.write_text("""mtllib panel.mtl
usemtl paint
v -1 -1 0
v 1 -1 0
v 1 1 0
v -1 1 0
vt 0 0
vt 1 0
vt 1 1
vt 0 1
f 1/1 2/2 3/3 4/4
""")
        (root / "panel.mtl").write_text("newmtl paint\nKd 1 1 1\nmap_Kd paint.png\n")
        image = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 1, 8, 6, 0, 0, 0))
                 + chunk(b"IDAT", zlib.compress(b"\0" + b"\xff\0\0\xff"*2 + b"\0\xff\0\xff"*2))
                 + chunk(b"IEND", b""))
        (root / "paint.png").write_bytes(image)
        output = root / "capture.png"
        subprocess.run([*command, model, output], check=True)
        red = green = 0
        for row in decoded_rows(output):
            for x in range(0, len(row), 4):
                r, g, b = row[x:x+3]
                red += r > 60 and r > 3*g and r > 3*b
                green += g > 60 and g > 3*r and g > 3*b
        if min(red, green) < 1000:
            raise AssertionError(f"missing texture regions: {red=} {green=}")
        print(f"Textured OBJ headless capture: {red} red and {green} green pixels")


if __name__ == "__main__":
    check(sys.argv[1:])
