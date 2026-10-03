#!/usr/bin/env python3
"""Собрать однофайловый установщик и ZIP переносного модуля на macOS."""

import argparse
import gzip
import hashlib
import io
import platform
import re
import shutil
import subprocess
import tarfile
import zipfile
from pathlib import Path


FILES = (
    "windows.h",
    "gdi_compat.mm",
    "build.sh",
    "run.sh",
    "run.command",
    "mac-native.pri",
    "README.md",
    ".gitignore",
    "tests/smoke.mm",
)
EXECUTABLES = {"build.sh", "run.sh", "run.command"}
DEFAULT_VERSION = "0.1.0"


def add_release_file(archive, name, data, mode):
    info = tarfile.TarInfo(name)
    info.size = len(data)
    info.mode = mode
    info.mtime = 0
    info.uid = info.gid = 0
    info.uname = info.gname = ""
    archive.addfile(info, io.BytesIO(data))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    if platform.system() != "Darwin":
        parser.error("Сборка установщика требует macOS и Apple Clang.")

    module = Path(__file__).resolve().parents[1]
    version_file = module.parent / "VERSION"
    version = version_file.read_text(encoding="utf-8").strip() if version_file.exists() else DEFAULT_VERSION
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?", version):
        parser.error("VERSION должен содержать безопасную версию вида 0.1.0 или 0.1.0-beta.1.")
    output = (args.output_dir or module / "dist").resolve()
    build = module / ".build" / "package"
    files = FILES + (("LICENSE",) if (module / "LICENSE").exists() else ())
    payload = [(name, (module / name).read_bytes()) for name in files]
    build.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)

    lines = [
        "#pragma once",
        "#include <cstddef>",
        "namespace payload {",
        "struct File { const char* path; const unsigned char* data; std::size_t size; bool executable; };",
    ]
    for index, (_, data) in enumerate(payload):
        lines.append(f"inline constexpr unsigned char data_{index}[] = {{")
        for offset in range(0, len(data), 24):
            lines.append(",".join(f"0x{byte:02x}" for byte in data[offset:offset + 24]) + ",")
        lines.append("};")
    lines.append("inline constexpr File files[] = {")
    for index, (name, data) in enumerate(payload):
        executable = "true" if name in EXECUTABLES else "false"
        lines.append(f'{{"mac-native/{name}", data_{index}, {len(data)}, {executable}}},')
    lines.extend(["};", "}"])
    (build / "payload.generated.h").write_text("\n".join(lines) + "\n", encoding="utf-8")

    compiler = subprocess.check_output(["xcrun", "--find", "clang++"], text=True).strip()
    sdk = subprocess.check_output(["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True).strip()
    binary = build / "winapi-macos"
    subprocess.run([
        compiler, "-isysroot", sdk, "-std=c++17", "-O2", "-Wall", "-Wextra",
        f'-DWINAPI_MACOS_VERSION="{version}"',
        "-arch", "arm64", "-arch", "x86_64", "-mmacosx-version-min=11.0",
        "-I", str(build), str(module / "tools" / "installer.cpp"), "-o", str(binary),
    ], check=True)
    installed_binary = output / binary.name
    shutil.copy2(binary, installed_binary)
    installed_binary.chmod(0o755)

    archive = output / "mac-native.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as package:
        for name, data in payload:
            info = zipfile.ZipInfo(f"mac-native/{name}")
            info.create_system = 3
            mode = 0o100755 if name in EXECUTABLES else 0o100644
            info.external_attr = mode << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            package.writestr(info, data)

    release = output / f"winapi-macos-{version}-universal-macos.tar.gz"
    with release.open("wb") as destination:
        with gzip.GzipFile(filename="", mode="wb", fileobj=destination, mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w", format=tarfile.USTAR_FORMAT) as package:
                add_release_file(package, "winapi-macos", installed_binary.read_bytes(), 0o755)
                license_file = module.parent / "LICENSE"
                if license_file.exists():
                    add_release_file(package, "LICENSE", license_file.read_bytes(), 0o644)

    checksums = output / "SHA256SUMS"
    checksums.write_text("".join(
        f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n"
        for path in (installed_binary, archive, release)
    ), encoding="ascii")
    print(f"Установщик (Apple Silicon + Intel), версия {version}: {installed_binary}")
    print(f"Архив переносного модуля: {archive}")
    print(f"Архив релиза: {release}")
    print(f"Контрольные суммы SHA-256: {checksums}")


if __name__ == "__main__":
    main()
