"""SD card image tools for the FNIRSI 1014D (Allwinner F1C100s).

  python sunxi_pack.py egon FILE
      Pad FILE to a multiple of 512 bytes and write the eGON boot header length and checksum
      (the boot ROM and the loader check both).

  python sunxi_pack.py pack LOADER FIRMWARE OUTPUT
      Build the SD card image: the loader at the start, the firmware at offset 0x8000 (SD sector
      80 when the image is written at 8 KiB, sector 16). The firmware must end before the settings
      sectors (2046 and 2047).
"""
import struct
import sys

EGON_CHECKSUM_SEED = 0x5F0A6C39
FIRMWARE_OFFSET = 0x8000
IMAGE_START_SECTOR = 16
FIRST_SETTINGS_SECTOR = 2046
#72 bytes of padding after the firmware keep the image the same size as with the original tool
TAIL_PADDING = 72


def egon(data):
    data = bytearray(data)
    if data[4:12] != b"eGON.EXE" and data[4:12] != b"eGON.BT0":
        raise SystemExit("no eGON header")
    data += bytes((-len(data)) % 512)
    struct.pack_into("<I", data, 16, len(data))
    struct.pack_into("<I", data, 12, EGON_CHECKSUM_SEED)
    checksum = sum(struct.unpack("<%dI" % (len(data) // 4), bytes(data))) & 0xFFFFFFFF
    struct.pack_into("<I", data, 12, checksum)
    return bytes(data)


def pack(loader, firmware):
    if len(loader) > FIRMWARE_OFFSET:
        raise SystemExit("loader larger than 0x%X bytes" % FIRMWARE_OFFSET)
    limit = (FIRST_SETTINGS_SECTOR - IMAGE_START_SECTOR) * 512 - FIRMWARE_OFFSET
    if len(firmware) > limit:
        raise SystemExit("firmware of %d bytes would overlap the settings sectors (limit %d)" % (len(firmware), limit))
    return loader + bytes(FIRMWARE_OFFSET - len(loader)) + firmware + bytes(TAIL_PADDING)


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "egon":
        data = open(sys.argv[2], "rb").read()
        open(sys.argv[2], "wb").write(egon(data))
    elif len(sys.argv) == 5 and sys.argv[1] == "pack":
        loader = open(sys.argv[2], "rb").read()
        firmware = open(sys.argv[3], "rb").read()
        open(sys.argv[4], "wb").write(pack(loader, firmware))
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
