#!/usr/bin/env python3
"""Проверить автономный установщик и запуск в чужой папке с кириллицей."""

import argparse
import os
import shutil
import subprocess
import tempfile
from pathlib import Path


def command(arguments, cwd, expected=0, env=None):
    result = subprocess.run(arguments, cwd=cwd, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    if result.returncode != expected:
        raise AssertionError(f"{arguments}: exit {result.returncode}\n{result.stdout}")
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-gui", action="store_true",
                        help="Запускать тестовый пример без окна для CI.")
    args = parser.parse_args()
    module = Path(__file__).resolve().parents[1]
    binary = module / "dist" / "winapi-macos"
    with tempfile.TemporaryDirectory(prefix="WinAPI перенос с пробелами ", dir="/private/tmp") as location:
        root = Path(location)
        project = root / "Другая практика"
        project.mkdir()
        standalone = project / "winapi-macos"
        shutil.copy2(binary, standalone)
        source = project / "main.cpp"
        source.write_text(
            ('#define WINAPI_TEST_NO_GUI\n' if args.no_gui else '') +
            '#include <windows.h>\n#include <iostream>\n'
            'int extra();\nint main() {\n'
            '  if (extra() != 42 || RGB(255,0,0) != 255) return 10;\n'
            '#ifndef WINAPI_TEST_NO_GUI\n'
            '  HWND window = GetConsoleWindow(); HDC dc = GetWindowDC(window);\n'
            '  if (!dc) return 10;\n'
            '  SetPixel(dc, 12, 34, RGB(255,0,0));\n'
            '  HPEN pen = CreatePen(PS_SOLID, 2, RGB(255,0,0));\n'
            '  HGDIOBJ old = SelectObject(dc, pen);\n'
            '  if (!Ellipse(dc, 50, 60, 100, 120)) return 11;\n'
            '  SelectObject(dc, old); DeleteObject(pen);\n'
            "  while (!(GetAsyncKeyState('1') & 0x8000)) {}\n"
            '  ReleaseDC(window, dc);\n#endif\n'
            '  std::cout << "PORTABLE_OK\\n";\n}\n', encoding="utf-8")
        (project / "helper.cc").write_text("int extra() { return 42; }\n", encoding="utf-8")
        original = source.read_bytes()
        command([str(standalone)], project)
        installed = project / "mac-native"
        assert (installed / "windows.h").is_file()
        assert os.access(installed / "run.sh", os.X_OK)
        assert source.read_bytes() == original
        assert not list(project.glob("*.pro"))
        stamp = (installed / "windows.h").stat().st_mtime_ns
        command([str(standalone)], project)
        assert (installed / "windows.h").stat().st_mtime_ns == stamp
        print("PASS: standalone install; sources preserved; repeat install is idempotent")

        (installed / "windows.h").write_text("// my local changes\n", encoding="utf-8")
        (installed / "README.md").unlink()
        command([str(standalone)], project, expected=1)
        assert not (installed / "README.md").exists()
        assert (installed / "windows.h").read_text() == "// my local changes\n"
        command([str(standalone), "--force"], project)
        assert (installed / "windows.h").read_bytes() == (module / "windows.h").read_bytes()
        print("PASS: conflicting edits rejected before changes; explicit force restores the module")

        outside = root / "outside.txt"
        outside.write_text("KEEP\n", encoding="utf-8")
        (installed / "windows.h").unlink()
        (installed / "windows.h").symlink_to(outside)
        command([str(standalone), "--force"], project, expected=1)
        assert outside.read_text() == "KEEP\n"
        (installed / "windows.h").unlink()
        command([str(standalone)], project)
        command([str(standalone), "--unknown"], project, expected=2)
        print("PASS: symbolic link cannot overwrite files outside the module; bad options rejected")

        environment = os.environ.copy()
        environment["GDI_AUTOPLAY"] = "1"
        for name in ("GDI_BACKGROUND", "GDI_TITLE"):
            environment.pop(name, None)
        output = command([str(standalone), "--run"], project, env=environment)
        assert "PORTABLE_OK" in output
        assert (installed / "Другая практика_native").is_file()
        print("PASS: install + auto-discover .cpp/.cc + build + native launch in a new folder")

        (project / "other.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
        command(["/bin/bash", str(installed / "build.sh")], project, expected=1)
        output = command(["/bin/bash", str(installed / "run.sh"), "main.cpp", "helper.cc"],
                         root, env=environment)
        assert "PORTABLE_OK" in output
        print("PASS: multiple main functions reported; explicit sources work from any working directory")

        destination = root / "Ещё один проект"
        destination.mkdir()
        command([str(standalone), "--dir", str(destination)], root)
        assert (destination / "mac-native" / "windows.h").is_file()
        assert not (root / "mac-native").exists()
        command(["/bin/bash", str(destination / "mac-native" / "build.sh")], root, expected=1)
        print("PASS: --dir selects the target; empty projects get a clear build failure")
    print("Все проверки переносимости прошли.")


if __name__ == "__main__":
    main()
