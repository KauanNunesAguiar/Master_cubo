import struct, sys, time, zlib
import serial

PORT = "COM6"
BAUD = 921600  # igual ao do flasher
TABLES = [
    ("twist_slice.bin", 0x000000),
    ("flip_slice.bin", 0x085000),
    ("cperm_sp.bin", 0x101000),
    ("udperm_sp.bin", 0x178000),
]

s = serial.Serial(PORT, BAUD, timeout=5)
time.sleep(2)  # abrir a porta pode resetar a placa
s.reset_input_buffer()

s.write(b"I")
ident = s.read(3)
print("ID da flash:", ident.hex(), "(esperado ef4015)")
if ident != bytes([0xEF, 0x40, 0x15]):
    sys.exit("W25Q16 não respondeu")

for name, addr in TABLES:
    data = open(name, "rb").read()
    print(f"{name}: {len(data)} bytes em 0x{addr:06X}")
    s.write(b"W" + struct.pack("<II", addr, len(data)))
    if s.read(1) != b"K":
        sys.exit("erro no comando W")
    for off in range(0, len(data), 256):
        s.write(data[off:off + 256])
        if s.read(1) != b"K":
            sys.exit(f"erro no offset {off}")
        if off % (64 * 1024) == 0:
            print(f"  {100 * off // len(data)}%")
    crc = struct.unpack("<I", s.read(4))[0]
    if crc != (zlib.crc32(data) & 0xFFFFFFFF):
        sys.exit(f"  CRC diferente: {crc:08X}")
    print("  CRC OK")
print("Tudo gravado.")