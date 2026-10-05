#include "compiler_setup.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
const std::string kOwner = "winapi-macos compiler setup v1\n";
const std::string kBegin = "# >>> winapi-macos compiler >>>";
const std::string kEnd = "# <<< winapi-macos compiler <<<";

struct Configuration {
    fs::path home, root, bin;
    std::vector<fs::path> profiles;
};

struct Profile {
    fs::path path;
    std::string before, after;
    bool existed;
};

fs::file_status Status(const fs::path &path)
{
    std::error_code error;
    auto status = fs::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory)
        return fs::file_status(fs::file_type::not_found);
    if (error) throw std::runtime_error(path.string() + ": " + error.message());
    return status;
}

void Directory(const fs::path &path)
{
    auto status = Status(path);
    if (fs::exists(status) && !fs::is_directory(status))
        throw std::runtime_error("Ожидалась обычная папка: " + path.string());
}

bool Regular(const fs::path &path)
{
    auto status = Status(path);
    if (fs::exists(status) && !fs::is_regular_file(status))
        throw std::runtime_error("Ожидался обычный файл, без символической ссылки: " + path.string());
    return fs::exists(status);
}

std::string Read(const fs::path &path)
{
    if (!Regular(path)) return {};
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Не удалось прочитать " + path.string());
    std::string data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (input.bad()) throw std::runtime_error("Ошибка чтения " + path.string());
    return data;
}

std::string Quote(const std::string &value)
{
    std::string result = "'";
    for (char c : value) result += c == '\'' ? "'\\''" : std::string(1, c);
    return result + "'";
}

// Detect edits to an owned executable, independent of app version/architecture.
// This fingerprint is an ownership check; release integrity uses SHA-256.
std::string Fingerprint(const std::string &data)
{
    uint64_t value = 14695981039346656037ULL;
    for (unsigned char byte : data) { value ^= byte; value *= 1099511628211ULL; }
    std::ostringstream output;
    output << std::hex << value << '\n';
    return output.str();
}

Configuration Config(bool select_profiles = true)
{
    const char *override_home = std::getenv("WINAPI_MACOS_CONFIG_HOME");
    const char *home = override_home && *override_home ? override_home : std::getenv("HOME");
    if (!home || !*home) throw std::runtime_error("Не удалось определить домашнюю папку.");
    Configuration config;
    config.home = fs::weakly_canonical(fs::absolute(home));
    if (config.home.string().find_first_of(":\r\n") != std::string::npos)
        throw std::runtime_error("Путь настройки не должен содержать двоеточие или перевод строки.");
    Directory(config.home);
    config.root = config.home / ".winapi-macos";
    config.bin = config.root / "bin";
    Directory(config.root);
    Directory(config.bin);
    if (!select_profiles) return config;
    const char *shell_env = std::getenv("SHELL");
    std::string shell = shell_env && *shell_env ? fs::path(shell_env).filename().string() : "zsh";
    if (shell == "zsh") {
        const char *dotdir = (!override_home || !*override_home) ? std::getenv("ZDOTDIR") : nullptr;
        fs::path profile_dir = dotdir && *dotdir ? fs::weakly_canonical(fs::absolute(dotdir)) : config.home;
        Directory(profile_dir);
        config.profiles.push_back(profile_dir / ".zshrc");
    } else if (shell == "bash") {
        config.profiles = {config.home / ".bashrc", config.home / ".bash_profile"};
    } else {
        throw std::runtime_error("Автонастройка поддерживает zsh и bash. Выбран shell: " + shell);
    }
    for (const auto &profile : config.profiles)
        if (profile.string().find_first_of("\r\n") != std::string::npos)
            throw std::runtime_error("Путь профиля не должен содержать перевод строки.");
    return config;
}

std::string Environment(const Configuration &config)
{
    std::string bin = Quote(config.bin.string());
    return "# winapi-macos compiler environment v1\n"
           "case \"$PATH\" in\n  " + bin + "|" + bin + ":*) ;;\n"
           "  *) export PATH=" + bin + ":\"$PATH\" ;;\nesac\n";
}

std::string Wrapper(const std::string &name)
{
    return "#!/bin/sh\n# winapi-macos compiler wrapper v1\n"
           "exec \"${0%/*}/winapi-macos-driver\" --compiler " + name + " -- \"$@\"\n";
}

std::string Block(const Configuration &config)
{
    return "\n" + kBegin + "\n. " + Quote((config.root / "env.sh").string()) + "\n" + kEnd + "\n";
}

std::string EditBlock(const std::string &text, const std::string &replacement)
{
    auto begin = text.find(kBegin), end = text.find(kEnd);
    if (begin == std::string::npos && end == std::string::npos) return text + replacement;
    if (begin == std::string::npos || end == std::string::npos || end < begin ||
        text.find(kBegin, begin + kBegin.size()) != std::string::npos ||
        text.find(kEnd, end + kEnd.size()) != std::string::npos ||
        (begin > 0 && text[begin - 1] != '\n') ||
        begin + kBegin.size() >= text.size() || text[begin + kBegin.size()] != '\n' ||
        (end > 0 && text[end - 1] != '\n'))
        throw std::runtime_error("Повреждён или повторяется блок настройки winapi-macos; файл не изменён.");
    std::size_t after = end + kEnd.size();
    if (after < text.size() && text[after] != '\n')
        throw std::runtime_error("Некорректная строка окончания блока winapi-macos.");
    if (after < text.size()) ++after;
    std::size_t before = begin > 0 ? begin - 1 : begin;
    std::string prefix = text.substr(0, before), suffix = text.substr(after);
    if (replacement.empty() && !prefix.empty() && !suffix.empty() &&
        prefix.back() != '\n' && suffix.front() != '\n') prefix += '\n';
    return prefix + replacement + suffix;
}

void Write(const fs::path &path, const std::string &data, mode_t mode, bool executable = false)
{
    bool present = Regular(path);
    if (present) {
        mode = static_cast<mode_t>(Status(path).permissions() & fs::perms::mask);
        if (executable) mode |= S_IXUSR;
        if (Read(path) == data) {
            if (executable) fs::permissions(path, fs::perms::owner_exec, fs::perm_options::add);
            return;
        }
    }
    fs::create_directories(path.parent_path());
    std::string temporary = path.string() + ".winapi-XXXXXX";
    int descriptor = ::mkstemp(temporary.data());
    if (descriptor < 0) throw std::runtime_error("Не удалось создать " + path.string());
    try {
        for (std::size_t offset = 0; offset < data.size();) {
            ssize_t count = ::write(descriptor, data.data() + offset, data.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) throw std::runtime_error("Ошибка записи " + path.string());
            offset += static_cast<std::size_t>(count);
        }
        if (::fchmod(descriptor, mode) != 0) throw std::runtime_error("Не удалось сохранить права " + path.string());
        int result = ::close(descriptor);
        descriptor = -1;
        if (result != 0) throw std::runtime_error("Ошибка закрытия " + path.string());
        fs::rename(temporary, path);
    } catch (...) {
        if (descriptor >= 0) ::close(descriptor);
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}

std::map<fs::path, bool> Manifest(const Configuration &config)
{
    std::map<fs::path, bool> result;
    std::string data = Read(config.root / "profiles");
    std::size_t start = 0;
    while (start < data.size()) {
        auto end = data.find('\n', start);
        if (end == std::string::npos || end < start + 3 ||
            (data[start] != '0' && data[start] != '1') || data[start + 1] != ' ')
            throw std::runtime_error("Повреждён список профилей настройки winapi-macos.");
        fs::path path = data.substr(start + 2, end - start - 2);
        std::string name = path.filename().string();
        if (!path.is_absolute() || (name != ".zshrc" && name != ".bashrc" && name != ".bash_profile"))
            throw std::runtime_error("Некорректный профиль настройки winapi-macos.");
        result[path] = data[start] == '1';
        start = end + 1;
    }
    return result;
}

void CheckOwned(const Configuration &config)
{
    auto marker = config.root / "owner";
    if (Regular(marker) && Read(marker) != kOwner)
        throw std::runtime_error("Папка настройки занята другими файлами: " + config.root.string());
}
} // namespace

fs::path CurrentExecutable()
{
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
        throw std::runtime_error("Не удалось найти установщик.");
    return fs::canonical(buffer.data());
}

void SetupCompiler()
{
    Configuration config = Config();
    CheckOwned(config);
    bool owned = Regular(config.root / "owner");
    std::map<fs::path, bool> manifest = owned ? Manifest(config) : std::map<fs::path, bool>{};
    std::vector<Profile> profiles;
    for (const auto &path : config.profiles) {
        bool existed = Regular(path);
        std::string before = Read(path);
        auto old = manifest.find(path);
        profiles.push_back({path, before, EditBlock(before, Block(config)),
                            old != manifest.end() ? old->second : existed});
        manifest[path] = profiles.back().existed;
    }
    for (const auto &name : {"g++", "clang++"}) {
        auto path = config.bin / name;
        if (Regular(path) && (!owned || Read(path) != Wrapper(name)))
            throw std::runtime_error("Команда уже содержит другие изменения: " + path.string());
    }
    auto driver = config.bin / "winapi-macos-driver";
    auto fingerprint = config.root / "driver-fingerprint";
    auto environment = config.root / "env.sh";
    if (Regular(driver) && !owned)
        throw std::runtime_error("Уже существует другой compiler driver: " + driver.string());
    if (Regular(environment) && (!owned || Read(environment) != Environment(config)))
        throw std::runtime_error("Файл среды уже содержит другие изменения: " + environment.string());
    if (!owned && Regular(config.root / "profiles"))
        throw std::runtime_error("Список профилей уже занят другими файлами.");
    std::string executable = Read(CurrentExecutable());
    if (Regular(fingerprint) && !owned)
        throw std::runtime_error("Файл проверки драйвера уже занят другими файлами.");
    if (Regular(driver) && owned) {
        std::string previous = Read(driver);
        bool matches = Regular(fingerprint) ? Fingerprint(previous) == Read(fingerprint)
                                            : previous == executable;
        if (!matches) throw std::runtime_error("Драйвер изменён пользователем; файл не заменён: " + driver.string());
    }
    std::string manifest_data;
    for (const auto &item : manifest)
        manifest_data += (item.second ? "1 " : "0 ") + item.first.string() + "\n";
    Write(config.root / "owner", kOwner, 0600);
    Write(driver, executable, 0755, true);
    Write(fingerprint, Fingerprint(executable), 0600);
    for (const auto &name : {"g++", "clang++"}) Write(config.bin / name, Wrapper(name), 0755, true);
    Write(environment, Environment(config), 0644);
    Write(config.root / "profiles", manifest_data, 0600);
    for (const auto &profile : profiles) Write(profile.path, profile.after, 0600);
    std::cout << "Автонастройка g++ и clang++ завершена.\n"
                 "Откройте новую вкладку Terminal. В папке практики:\n"
                 "  winapi-macos\n  g++ *.cpp -o app\n  ./app\n"
                 "Отмена настройки: winapi-macos --unsetup\n";
}

void RemoveCompilerSetup()
{
    Configuration config = Config(false);
    CheckOwned(config);
    if (!Regular(config.root / "owner")) {
        std::cout << "Автонастройка winapi-macos отсутствует.\n";
        return;
    }
    std::vector<Profile> profiles;
    for (const auto &item : Manifest(config)) {
        std::string before = Read(item.first);
        profiles.push_back({item.first, before, EditBlock(before, ""), item.second});
    }
    // Preflight all owned files before changing any profile.
    for (const auto &path : {config.bin / "g++", config.bin / "clang++",
                            config.bin / "winapi-macos-driver", config.root / "env.sh",
                            config.root / "driver-fingerprint"}) Regular(path);
    auto driver = config.bin / "winapi-macos-driver";
    bool owned_driver = Regular(driver) &&
        (Regular(config.root / "driver-fingerprint")
         ? Fingerprint(Read(driver)) == Read(config.root / "driver-fingerprint")
         : Read(driver) == Read(CurrentExecutable()));
    for (const auto &profile : profiles) {
        if (profile.after == profile.before) continue;
        if (!profile.existed && profile.after.empty()) fs::remove(profile.path);
        else Write(profile.path, profile.after, 0600);
    }
    bool custom_wrapper = false;
    for (const auto &name : {"g++", "clang++"}) {
        fs::path path = config.bin / name;
        if (Regular(path) && Read(path) == Wrapper(name)) fs::remove(path);
        else if (Regular(path)) custom_wrapper = true;
    }
    if (!custom_wrapper) {
        if (owned_driver) fs::remove(driver);
        if (Read(config.root / "env.sh") == Environment(config)) fs::remove(config.root / "env.sh");
    }
    fs::remove(config.root / "profiles");
    fs::remove(config.root / "driver-fingerprint");
    fs::remove(config.root / "owner");
    // Leave directories containing any user's extra files.
    if (fs::exists(config.bin) && fs::is_empty(config.bin)) fs::remove(config.bin);
    if (fs::exists(config.root) && fs::is_empty(config.root)) fs::remove(config.root);
    std::cout << "Автонастройка отменена. Откройте новую вкладку Terminal.\n";
}
