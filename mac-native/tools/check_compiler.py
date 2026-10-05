#!/usr/bin/env python3
"""Проверить --setup и обычную сборку g++/clang++ в изолированном окружении."""

import json
import os
import stat
import subprocess
import sys
import tempfile
from pathlib import Path


def command(arguments, cwd, env, expected=0):
    result = subprocess.run(arguments, cwd=cwd, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    if result.returncode != expected:
        raise AssertionError(f"{arguments}: exit {result.returncode}\n{result.stdout}")
    return result.stdout


def rejects(arguments, cwd, env):
    result = subprocess.run(arguments, cwd=cwd, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    if result.returncode == 0:
        raise AssertionError(f"Expected a rejected command: {arguments}\n{result.stdout}")


def write_profile(path, text, mode):
    path.write_text(text, encoding="utf-8")
    path.chmod(mode)
    return path.read_bytes(), mode


def check_profile(path, original, shell, cwd, env):
    assert original[0] in path.read_bytes(), f"Custom profile content lost: {path}"
    assert stat.S_IMODE(path.stat().st_mode) == original[1]
    command([shell, "-n", str(path)], cwd, env)


def sourced_environment(profile, shell, cwd, environment):
    flags = ["-f"] if shell.endswith("zsh") else ["--noprofile", "--norc"]
    output = command([shell, *flags, "-c",
                      'source "$1"; source "$1"; printf "%s\\n" "$PATH"',
                      "winapi-test", str(profile)], cwd, environment)
    result = environment.copy()
    result["PATH"] = output.strip().splitlines()[-1]
    return result


def rsp_quote(path):
    return '"' + str(path).replace("\\", "\\\\").replace('"', '\\"') + '"'


def check_setup_lifecycle(binary, root, base_environment):
    config = root / "Настройки zsh с пробелами"
    config.mkdir()
    profile = config / ".zshrc"
    original = write_profile(profile, "# my custom zsh profile\nexport TEST_PROFILE_KEEP=kept\n", 0o640)
    environment = base_environment.copy()
    environment.update(WINAPI_MACOS_CONFIG_HOME=str(config), SHELL="/bin/zsh")
    dotdir = root / "Чужой ZDOTDIR"
    dotdir.mkdir()
    dot_profile = dotdir / ".zshrc"
    dot_profile.write_text("# untouched ZDOTDIR profile\n", encoding="utf-8")
    environment["ZDOTDIR"] = str(dotdir)

    command([str(binary), "--setup"], root, environment)
    managed = config / ".winapi-macos"
    managed_bin = managed / "bin"
    assert (managed / "env.sh").is_file()
    for name in ("g++", "clang++", "winapi-macos-driver"):
        assert os.access(managed_bin / name, os.X_OK)
    assert not (root / "mac-native").exists(), "--setup must not install a project module"
    assert dot_profile.read_text() == "# untouched ZDOTDIR profile\n"
    check_profile(profile, original, "/bin/zsh", root, environment)
    installed_profile = profile.read_bytes()
    command([str(binary), "--setup"], root, environment)
    assert profile.read_bytes() == installed_profile
    check_profile(profile, original, "/bin/zsh", root, environment)
    for name in ("g++", "clang++", "winapi-macos-driver"):
        executable = managed_bin / name
        executable.chmod(stat.S_IMODE(executable.stat().st_mode) & ~0o111)
        assert not os.access(executable, os.X_OK)
    command([str(binary), "--setup"], root, environment)
    for name in ("g++", "clang++", "winapi-macos-driver"):
        executable = managed_bin / name
        assert executable.stat().st_mode & stat.S_IXUSR
        assert os.access(executable, os.X_OK)
    assert profile.read_bytes() == installed_profile
    check_profile(profile, original, "/bin/zsh", root, environment)
    wrapped_environment = sourced_environment(profile, "/bin/zsh", root, environment)
    paths = wrapped_environment["PATH"].split(os.pathsep)
    assert paths[0] == str(managed_bin) and paths.count(str(managed_bin)) == 1
    print("PASS: isolated zsh setup preserves profile/mode, is idempotent, and repairs removed executable bits")
    return environment, wrapped_environment, config, profile, original, managed


def check_compilation(binary, root, environment, original_environment):
    project = root / "Практика с пробелами"
    project.mkdir()
    command([str(binary), "--dir", str(project)], root, environment)
    assert (project / "mac-native" / "libwinapi_macos.a").is_file()
    main_source = project / "main.cpp"
    helper = project / "helper.cc"
    main_source.write_text(
        '#include <windows.h>\n#include <cstdio>\nint extra();\n'
        'int main() {\n'
        '  if (extra() != 42) return 10;\n'
        '  if (SetPixel(0, 12, 34, RGB(255,0,0)) != CLR_INVALID) return 11;\n'
        '  std::puts("WRAPPER_OK");\n}\n', encoding="utf-8")
    helper.write_text(
        '#include <windows.h>\n'
        'int extra() { return SetPixel(0, 0, 0, RGB(0,0,0)) == CLR_INVALID ? 42 : -1; }\n',
        encoding="utf-8")
    original_sources = main_source.read_bytes(), helper.read_bytes()

    def run(output):
        assert "WRAPPER_OK" in command([str(output)], root, environment)

    output = project / "app"
    command(["g++", "main.cpp", "helper.cc", "-o", str(output)], project, environment)
    run(output)
    for source, obj in ((main_source, project / "main.o"), (helper, project / "helper.o")):
        command(["g++", "-c", str(source), "-Werror=unused-command-line-argument", "-o", str(obj)],
                root, environment)
    objects_output = project / "app из объектов"
    command(["clang++", "main.o", "helper.o", "-o", str(objects_output)], project, environment)
    run(objects_output)
    language_output = project / "app с языком"
    command(["g++", "-x", "c++", "main.cpp", "helper.cc", "-o", str(language_output)],
            project, environment)
    run(language_output)
    delimiter_output = project / "app с разделителем"
    command(["g++", "-o", str(delimiter_output), "--", "main.cpp", "helper.cc"], project, environment)
    run(delimiter_output)
    print("PASS: plain g++ links two C++ files; compile-only, clang++ object linking, -x c++, and -- work")

    preprocessed = command(["g++", "-E", "-Werror=unused-command-line-argument", "main.cpp"],
                           project, environment)
    assert "SetPixel" in preprocessed
    dependencies = command(["g++", "-M", "-Werror=unused-command-line-argument", "helper.cc"],
                           project, environment)
    assert "helper.cc" in dependencies and "windows.h" in dependencies
    for flags in (("-fsyntax-only",), ("-S", "-o", str(project / "helper.s"))):
        command(["g++", *flags, "-Werror=unused-command-line-argument", "helper.cc"], project, environment)
    for compiler in ("g++", "clang++"):
        for option in ("--version", "-dumpmachine"):
            expected = command(["/usr/bin/" + compiler, option], project, original_environment)
            assert command([compiler, option], project, environment) == expected
    print("PASS: preprocess/dependencies/syntax/assembly get no linker-only flags; compiler queries stay unchanged")

    response = root / "аргументы для линковки.rsp"
    response_output = root / "app из response файла"
    response.write_text(" ".join((rsp_quote(main_source), rsp_quote(helper), "-o", rsp_quote(response_output))),
                        encoding="utf-8")
    command(["g++", "@" + str(response)], root, environment)
    run(response_output)
    response_object = root / "объект из response файла.o"
    response.write_text(" ".join(("-c", rsp_quote(helper), "-Werror=unused-command-line-argument",
                                  "-o", rsp_quote(response_object))), encoding="utf-8")
    command(["g++", "@" + str(response)], root, environment)
    assert response_object.is_file()
    assert (main_source.read_bytes(), helper.read_bytes()) == original_sources
    print("PASS: @response handles quoted source/output paths and compile-only from outside the project")


def check_passthrough(root, environment, managed):
    outside = root / "Обычный проект без модуля"
    outside.mkdir()
    fake_bin = root / "Оригинальный компилятор"
    fake_bin.mkdir()
    fake = fake_bin / "g++"
    fake.write_text(
        "#!" + sys.executable + "\n"
        "import json, os, sys\n"
        "with open(os.environ['WINAPI_TEST_ARG_LOG'], 'w') as stream:\n"
        "    json.dump(sys.argv[1:], stream)\n"
        "print('ORIGINAL_COMPILER')\n"
        "sys.exit(23)\n", encoding="utf-8")
    fake.chmod(0o755)
    log = outside / "compiler argv.json"
    fake_environment = environment.copy()
    fake_environment["PATH"] = os.pathsep.join((str(managed / "bin"), str(fake_bin), "/usr/bin", "/bin"))
    fake_environment["WINAPI_TEST_ARG_LOG"] = str(log)
    source = outside / "main.cpp"
    source.write_text("int main() { return 0; }\n", encoding="utf-8")
    (outside / "arguments with spaces.rsp").write_text('main.cpp -o "app with spaces"\n', encoding="utf-8")
    for arguments in (("--version",), ("-std=c++17", str(source), "-o", "app with spaces"),
                      ("@arguments with spaces.rsp", "--", "main.cpp"), ("@missing response file.rsp",)):
        assert "ORIGINAL_COMPILER" in command(["g++", *arguments], outside, fake_environment, expected=23)
        assert json.loads(log.read_text()) == list(arguments)
    print("PASS: outside module projects the original PATH compiler receives unchanged argv and exit status")


def check_unsetup_and_conflicts(binary, root, environment, profile, original, managed):
    valid = profile.read_bytes()
    added_lines = valid[len(original[0]):].decode("utf-8").splitlines()
    closing_marker = [line for line in added_lines if line.strip()][-1]
    broken = valid.decode("utf-8").replace(closing_marker, "# deliberately missing managed closing marker", 1)
    profile.write_text(broken, encoding="utf-8")
    rejects([str(binary), "--setup"], root, environment)
    assert profile.read_text() == broken
    assert stat.S_IMODE(profile.stat().st_mode) == original[1]
    profile.write_bytes(valid)
    foreign = managed / "bin" / "foreign.txt"
    foreign.write_text("KEEP\n", encoding="utf-8")
    command([str(binary), "--unsetup"], root, environment)
    assert profile.read_bytes() == original[0]
    assert stat.S_IMODE(profile.stat().st_mode) == original[1]
    assert foreign.read_text() == "KEEP\n"
    for name in ("g++", "clang++", "winapi-macos-driver"):
        assert not (managed / "bin" / name).exists()
    assert not (managed / "env.sh").exists()
    print("PASS: malformed markers reject without overwriting; unsetup restores the profile and keeps foreign files")

    symlink_config = root / "Настройки с symlink"
    symlink_config.mkdir()
    target = root / "Чужой профиль"
    target.write_text("KEEP PROFILE\n", encoding="utf-8")
    linked = symlink_config / ".zshrc"
    linked.symlink_to(target)
    symlink_environment = environment.copy()
    symlink_environment["WINAPI_MACOS_CONFIG_HOME"] = str(symlink_config)
    rejects([str(binary), "--setup"], root, symlink_environment)
    assert linked.is_symlink() and target.read_text() == "KEEP PROFILE\n"
    assert not (symlink_config / ".winapi-macos" / "bin").exists()
    print("PASS: symlink shell profiles are rejected before setup changes")


def check_bash(binary, root, base_environment):
    config = root / "Настройки bash с пробелами"
    config.mkdir()
    originals = {
        config / ".bashrc": write_profile(config / ".bashrc", "# custom bashrc\nexport TEST_BASH_RC=kept\n", 0o640),
        config / ".bash_profile": write_profile(config / ".bash_profile", "# custom bash profile", 0o600),
    }
    environment = base_environment.copy()
    environment.update(WINAPI_MACOS_CONFIG_HOME=str(config), SHELL="/bin/bash")
    command([str(binary), "--setup"], root, environment)
    installed = {path: path.read_bytes() for path in originals}
    command([str(binary), "--setup"], root, environment)
    for path, original in originals.items():
        assert path.read_bytes() == installed[path]
        check_profile(path, original, "/bin/bash", root, environment)
        sourced = sourced_environment(path, "/bin/bash", root, environment)
        assert sourced["PATH"].split(os.pathsep)[0] == str(config / ".winapi-macos" / "bin")
    command([str(binary), "--unsetup"], root, environment)
    for path, original in originals.items():
        assert path.read_bytes() == original[0]
        assert stat.S_IMODE(path.stat().st_mode) == original[1]
    print("PASS: bashrc and bash_profile setup/source/unsetup preserve existing bytes and permissions")


def check_profile_suffix(binary, root, base_environment):
    config = root / "Профиль без завершающей строки"
    config.mkdir()
    profile = config / ".zshrc"
    original = write_profile(profile, "export ORIGINAL_VALUE=before", 0o640)
    environment = base_environment.copy()
    environment.update(WINAPI_MACOS_CONFIG_HOME=str(config), SHELL="/bin/zsh")
    command([str(binary), "--setup"], root, environment)
    with profile.open("a", encoding="utf-8") as stream:
        stream.write("export EXTRA=1\n")
    command([str(binary), "--unsetup"], root, environment)
    assert profile.read_bytes() == original[0] + b"\nexport EXTRA=1\n"
    assert stat.S_IMODE(profile.stat().st_mode) == original[1]
    command(["/bin/zsh", "-n", str(profile)], root, environment)
    output = command(["/bin/zsh", "-f", "-c",
                      'source "$1"; printf "%s:%s\\n" "$ORIGINAL_VALUE" "$EXTRA"',
                      "winapi-test", str(profile)], root, environment)
    assert output.strip() == "before:1"
    print("PASS: unsetup preserves valid newlines and user additions after a profile without a trailing newline")


def check_changed_driver(binary, root, base_environment):
    config = root / "Настройки с изменённым драйвером"
    config.mkdir()
    profile = config / ".zshrc"
    original = write_profile(profile, "# custom profile to preserve\n", 0o640)
    environment = base_environment.copy()
    environment.update(WINAPI_MACOS_CONFIG_HOME=str(config), SHELL="/bin/zsh")
    command([str(binary), "--setup"], root, environment)
    managed_bin = config / ".winapi-macos" / "bin"
    driver = managed_bin / "winapi-macos-driver"
    changed = b"# local driver changes must survive\n"
    driver.write_bytes(changed)
    installed_profile = profile.read_bytes()
    rejects([str(binary), "--setup"], root, environment)
    assert profile.read_bytes() == installed_profile
    assert driver.read_bytes() == changed
    assert stat.S_IMODE(profile.stat().st_mode) == original[1]
    environment["SHELL"] = "/bin/fish"
    command([str(binary), "--unsetup"], root, environment)
    assert profile.read_bytes() == original[0]
    assert stat.S_IMODE(profile.stat().st_mode) == original[1]
    assert driver.read_bytes() == changed
    assert not (managed_bin / "g++").exists() and not (managed_bin / "clang++").exists()
    print("PASS: a changed driver rejects setup without profile edits and survives unsetup after SHELL changes to fish")


def main():
    module = Path(__file__).resolve().parents[1]
    binary = module / "dist" / "winapi-macos"
    base_environment = os.environ.copy()
    original_home = base_environment.get("HOME")
    base_environment["PATH"] = "/usr/bin:/bin:/usr/sbin:/sbin"
    for name in ("WINAPI_MACOS_CONFIG_HOME", "ZDOTDIR", "BASH_ENV", "ENV"):
        base_environment.pop(name, None)
    with tempfile.TemporaryDirectory(prefix="WinAPI compiler с пробелами ", dir="/private/tmp") as location:
        root = Path(location)
        environment, wrapped, config, profile, original, managed = check_setup_lifecycle(binary, root, base_environment)
        assert environment.get("HOME") == original_home and wrapped.get("HOME") == original_home
        check_compilation(binary, root, wrapped, base_environment)
        check_passthrough(root, wrapped, managed)
        check_unsetup_and_conflicts(binary, root, environment, profile, original, managed)
        check_bash(binary, root, base_environment)
        check_profile_suffix(binary, root, base_environment)
        check_changed_driver(binary, root, base_environment)
    print("Все проверки обычного компилятора прошли; глобальные shell profiles не изменялись.")


if __name__ == "__main__":
    main()
