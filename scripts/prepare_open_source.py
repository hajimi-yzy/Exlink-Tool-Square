#!/usr/bin/env python3
"""Export a software-only snapshot; never copy developer or server state."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

SOURCE_ROOTS = {"src", "include", "lib", "vendor", "scripts", "android", "docs", ".github"}
ROOT_FILES = {"platformio.ini", "my.csv", "version.txt", ".gitattributes", ".gitignore"}
EXCLUDE_DOCS = {"OTA_APP_REVIEW.md", "UI_STALL_DIAGNOSTICS.md", "OTA_NO_TOUCH_TEST.md", "OTA_BLE_RECOVERY_TEST.md"}
EXCLUDE_DIRS = {".git", ".pio", ".gradle", ".kotlin", "build", "__pycache__", ".venv", "node_modules", "data", "certs", "server"}
EXCLUDE_NAMES = {"local.properties", "cloud.local.properties", "smtp.json", "apply-smtp-fix.sh", ".DS_Store"}
MEDIA_EXTENSIONS = {".jpg", ".jpeg", ".png", ".gif", ".bmp", ".mp4", ".mjpeg", ".svg", ".pdf", ".step", ".stl", ".epro", ".epro2", ".ai"}
SECRET_EXTENSIONS = {".key", ".pem", ".p12", ".pfx", ".jks", ".keystore", ".sqlite3", ".db", ".pyc", ".log"}
TEXT_EXTENSIONS = {"", ".py", ".sh", ".md", ".txt", ".ini", ".csv", ".properties", ".kts", ".kt", ".c", ".cpp", ".h", ".json", ".yml", ".yaml", ".conf", ".service", ".xml", ".rst", ".bat", ".html", ".mk", ".cmake"}


def include(rel: Path) -> bool:
    if rel.parts[0] not in SOURCE_ROOTS and rel.as_posix() not in ROOT_FILES:
        return False
    name = rel.as_posix()
    if name.startswith(("android/app/src/test/", "android/app/src/androidTest/")):
        return False
    if (name.startswith("scripts/run_") and name.endswith("tests.py")) or name in {
        "scripts/check_display_contracts.py", ".github/workflows/reliability-tests.yml",
        "docs/RELIABILITY_1.0.24_RESULTS.json",
    }:
        return False
    if rel.parts[0] == "docs" and rel.name in EXCLUDE_DOCS:
        return False
    if any(part in EXCLUDE_DIRS for part in rel.parts):
        return False
    if rel.name in EXCLUDE_NAMES or "private" in rel.name.lower() or rel.name.startswith(".env"):
        return False
    if rel.suffix.lower() in SECRET_EXTENSIONS:
        return False
    # Keep runtime Android resources, but not illustrative photos and videos.
    if rel.suffix.lower() in MEDIA_EXTENSIONS and not rel.as_posix().startswith("android/app/src/main/res/"):
        return False
    return True


def private_replacements(repo: Path) -> dict[str, str]:
    result = {}
    local = repo / "android/cloud.local.properties"
    if local.exists():
        for line in local.read_text(encoding="utf-8").splitlines():
            key, sep, value = line.partition("=")
            value = value.strip()
            if not sep or not value:
                continue
            if key.strip() == "EXLINK_API_BASE_URL":
                result[value] = "https://ota.example.com"
                host = value.split("://", 1)[-1].split(":", 1)[0].rstrip("/")
                result[host] = "ota.example.com"
            elif key.strip() == "EXLINK_TLS_FINGERPRINT_SHA256":
                result[value] = "AA:" * 31 + "AA"
                result[re.sub(r"[:\s]", "", value)] = "AA" * 32
    return result


def write_source(repo: Path, output: Path, replacements: dict[str, str]) -> None:
    selected = []
    # Enumerate source roots explicitly, not the whole checkout or git history.
    for name in sorted(SOURCE_ROOTS):
        root = repo / name
        if root.exists():
            for base, dirs, files in os.walk(root):
                dirs[:] = sorted(d for d in dirs if d not in EXCLUDE_DIRS)
                selected.extend(Path(base) / name for name in sorted(files))
    selected.extend(repo / name for name in sorted(ROOT_FILES) if (repo / name).is_file())
    for source in selected:
        rel = source.relative_to(repo)
        if not include(rel):
            continue
        destination = output / rel
        destination.parent.mkdir(parents=True, exist_ok=True)
        raw = source.read_bytes()
        if source.suffix.lower() in TEXT_EXTENSIONS:
            try:
                text = raw.decode("utf-8")
            except UnicodeDecodeError:
                pass
            else:
                for old, new in replacements.items():
                    text = text.replace(old, new)
                home = str(Path.home())
                text = text.replace(home.replace("\\", "\\\\"), "C:\\\\Users\\\\USER")
                text = text.replace(home.replace("\\", "/"), "C:/Users/USER")
                text = text.replace(home, "C:\\Users\\USER")
                # Shell deployment tools must retain Unix newlines after Windows checkout.
                if source.suffix == ".sh":
                    text = text.replace("\r\n", "\n")
                raw = text.encode("utf-8")
        destination.write_bytes(raw)
    templates = repo / "scripts/public_release"
    for name in ("README.md", "THIRD_PARTY_NOTICES.md"):
        shutil.copyfile(templates / name, output / name)


def copy_firmware(repo: Path, output: Path) -> None:
    build = repo / ".pio/build/esp32s3"
    target = output / "firmware"
    target.mkdir()
    for name in ("firmware.bin", "bootloader.bin", "partitions.bin"):
        shutil.copyfile(build / name, target / name)
    image = (target / "firmware.bin").read_bytes()
    version = image[48:80].split(b"\0", 1)[0].decode("ascii")
    expected = (repo / "version.txt").read_text(encoding="ascii").strip()
    if version != expected or image[:1] != b"\xe9":
        raise RuntimeError("Release firmware/version mismatch")
    platformio_home = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    packages = platformio_home / "packages"
    boot_app0 = packages / "framework-arduinoespressif32/tools/partitions/boot_app0.bin"
    shutil.copyfile(boot_app0, target / "boot_app0.bin")
    merged = target / "exlink-{}-usb-full.bin".format(version)
    command = [
        os.environ.get("PYTHON", os.sys.executable), str(packages / "tool-esptoolpy/esptool.py"),
        "--chip", "esp32s3", "merge_bin", "-o", str(merged),
        "--flash_mode", "dio", "--flash_freq", "80m", "--flash_size", "16MB",
        "0x0", str(target / "bootloader.bin"), "0x8000", str(target / "partitions.bin"),
        "0xe000", str(target / "boot_app0.bin"), "0x20000", str(target / "firmware.bin"),
    ]
    subprocess.run(command, check=True)
    (target / "README.md").write_text('''# 固件使用

`firmware.bin` 是应用固件，仅用于服务器发布与 BLE OTA，不是完整 USB 镜像。
`exlink-''' + version + '''-usb-full.bin` 是 USB 完整刷写镜像，从地址 0x0 写入。

安装 esptool 4.x，退出串口监视器，在本目录执行（COM5 换成实际端口）：

```sh
python -m pip install "esptool>=4.5,<5"
python -m esptool --chip esp32s3 -p COM5 -b 115200 --before default_reset --after hard_reset write_flash --flash_size 16MB 0x0 exlink-''' + version + '''-usb-full.bin
```

硬件需与本工程引脚及16MB Flash布局相符。完整镜像包含OTA初始化数据，已有双槽设备升级优先使用App OTA；完整刷写可能重置当前OTA启动槽。首次从不同分区迁移时先备份原设备内容。

版本 ''' + version + '''，触摸启用，内部 I²C 默认 100 kHz。
''', encoding="utf-8")


def checksums(root: Path) -> None:
    lines = []
    for file in sorted(root.rglob("*")):
        if file.is_file() and file.name != "SHA256SUMS.txt":
            lines.append(hashlib.sha256(file.read_bytes()).hexdigest() + "  " + file.relative_to(root).as_posix())
    (root / "SHA256SUMS.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")


def verify(root: Path, forbidden: dict[str, str]) -> None:
    for file in root.rglob("*"):
        if not file.is_file():
            continue
        rel = file.relative_to(root)
        if any(part in EXCLUDE_DIRS for part in rel.parts) or file.suffix in SECRET_EXTENSIONS:
            raise RuntimeError("Excluded file in export: " + rel.as_posix())
        # Official APK needs a connection address; source must not disclose it.
        if file.suffix.lower() == ".apk":
            continue
        raw = file.read_bytes()
        if any(value.encode("utf-8") in raw for value in forbidden):
            raise RuntimeError("Private cloud configuration in export: " + rel.as_posix())
        if re.search(rb"-----BEGIN (?:RSA )?PRIVATE KEY-----\s+[A-Za-z0-9+/]{16,}", raw):
            raise RuntimeError("Private key in export: " + rel.as_posix())


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cloud-apk", type=Path, required=True)
    parser.add_argument("--local-apk", type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    output = args.output.resolve()
    if output.exists():
        raise SystemExit("Choose a new output folder; nothing overwritten")
    if output == repo or repo in output.parents:
        raise SystemExit("Export outside the development checkout")
    output.mkdir(parents=True)
    forbidden = private_replacements(repo)
    write_source(repo, output, forbidden)
    copy_firmware(repo, output)
    (output / "app").mkdir()
    shutil.copyfile(args.cloud_apk, output / "app/exlink-android-0.4.0.apk")
    if args.local_apk:
        shutil.copyfile(args.local_apk, output / "app/exlink-android-0.4.0-local.apk")
    verify(output, forbidden)
    checksums(output)
    archive = output.with_suffix(".zip")
    with zipfile.ZipFile(archive, "x", zipfile.ZIP_DEFLATED) as zipped:
        for file in sorted(output.rglob("*")):
            if file.is_file():
                zipped.write(file, file.relative_to(output).as_posix())
    with zipfile.ZipFile(archive) as zipped:
        if zipped.testzip() is not None:
            raise RuntimeError("ZIP verification failed")
    print(json.dumps({"source_folder": str(output), "software_zip": str(archive),
                      "files": sum(1 for file in output.rglob("*") if file.is_file())}, ensure_ascii=False))


if __name__ == "__main__":
    main()
