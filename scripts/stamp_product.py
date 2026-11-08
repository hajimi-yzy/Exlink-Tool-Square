"""Stamp Arduino's prebuilt app descriptor before elf2image hashes the image."""
Import("env")
import os
import re
from pathlib import Path

# Keep developer-specific paths out of distributed assertion strings and debug
# records. The more specific project mapping wins over the home-directory one.
env.Append(CCFLAGS=[
    "-ffile-prefix-map={}=~".format(Path.home().as_posix()),
    "-ffile-prefix-map={}=.".format(Path(env.subst("$PROJECT_DIR")).as_posix()),
])

version_path = Path(env.subst("$PROJECT_DIR")) / "version.txt"
release_version = os.environ.get("EXLINK_FIRMWARE_VERSION", env.GetProjectOption(
    "custom_firmware_version", version_path.read_text(encoding="ascii").strip()))
if (not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?", release_version)
        or len(release_version.encode("ascii")) > 31):
    raise RuntimeError("EXLINK_FIRMWARE_VERSION/version.txt must be a <=31-byte semantic version")
# Version is a build input, not a hash. Ensure a release change invalidates the
# binary target even if the application source files did not change.
env.Depends("$BUILD_DIR/${PROGNAME}.bin", env.Value(release_version))

def stamp_product(source, target, env):
    from elftools.elf.elffile import ELFFile
    path = env.subst("$BUILD_DIR/${PROGNAME}.elf")
    with open(path, "r+b") as stream:
        elf = ELFFile(stream)
        symbols = elf.get_section_by_name(".symtab").get_symbol_by_name("esp_app_desc")
        if not symbols:
            raise RuntimeError("esp_app_desc missing; refusing an unidentified firmware")
        symbol = symbols[0]
        section = elf.get_section(symbol["st_shndx"])
        offset = section["sh_offset"] + symbol["st_value"] - section["sh_addr"]
        stream.seek(offset)
        if stream.read(4) != b"\x32\x54\xcd\xab":
            raise RuntimeError("app descriptor magic mismatch")
        # Fixed-size writes are deterministic and idempotent. elf2image will
        # compute the descriptor's ELF hash and image hash after this action.
        stream.seek(offset + 16)
        stream.write(release_version.encode("ascii").ljust(32, b"\0"))
        stream.write(b"exlink-co5300-31pin".ljust(32, b"\0"))

env.AddPreAction("$BUILD_DIR/${PROGNAME}.bin", stamp_product)
