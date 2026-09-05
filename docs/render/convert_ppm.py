"""Convert documentation framebuffer captures to PNG using only the standard library."""
from pathlib import Path
import struct,zlib
def chunk(tag,data):
    return struct.pack(">I",len(data))+tag+data+struct.pack(">I",zlib.crc32(tag+data)&0xffffffff)
for p in Path("docs/images/embedded").glob("*.ppm"):
    data=p.read_bytes().split(b"\n",3); w,h=map(int,data[1].split()); rgb=data[3]
    assert len(rgb)==w*h*3
    raw=b"".join(b"\0"+rgb[y*w*3:(y+1)*w*3] for y in range(h))
    png=b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",w,h,8,2,0,0,0))+chunk(b"IDAT",zlib.compress(raw,9))+chunk(b"IEND",b"")
    p.with_suffix(".png").write_bytes(png)
