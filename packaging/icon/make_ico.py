"""Packs src/gui/resources/icons/app_<size>.png (from make_icon.ps1) into
src/gui/resources/app.ico. Each size is stored as a PNG entry, which Windows
Vista and later read at every size."""
import os
import struct

here = os.path.dirname(os.path.abspath(__file__))
icons = os.path.join(here, '..', '..', 'src', 'gui', 'resources', 'icons')
out = os.path.join(here, '..', '..', 'src', 'gui', 'resources', 'app.ico')
sizes = [16, 24, 32, 48, 64, 128, 256]
images = [open(os.path.join(icons, 'app_%d.png' % s), 'rb').read() for s in sizes]

header = struct.pack('<HHH', 0, 1, len(images))  # reserved, type 1 = icon, count
offset = len(header) + 16 * len(images)
entries, data = b'', b''
for s, png in zip(sizes, images):
    dim = 0 if s >= 256 else s  # 0 means 256
    entries += struct.pack('<BBBBHHII', dim, dim, 0, 0, 1, 32, len(png), offset)
    offset += len(png)
    data += png
with open(out, 'wb') as f:
    f.write(header + entries + data)
print('wrote', os.path.normpath(out), os.path.getsize(out), 'bytes')
