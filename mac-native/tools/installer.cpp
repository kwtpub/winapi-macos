#include <cstddef>
#include "payload.generated.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

#ifndef WINAPI_MACOS_VERSION
#define WINAPI_MACOS_VERSION "0.1.0"
#endif

namespace fs = std::filesystem;

struct PlannedFile {
    const payload::File *file;
    fs::path destination;
    bool present;
    bool identical;
    fs::perms permissions;
};

static void Usage()
{
    std::cout << "winapi-macos — установка переносной WinAPI/GDI-библиотеки для macOS\n\n"
                 "Использование: winapi-macos [--dir ПУТЬ] [--force] [--run]\n"
                 "  Без опций   Добавить mac-native/ в текущую папку проекта.\n"
                 "  --dir ПУТЬ  Установить в указанную папку.\n"
                 "  --force     Заменить отличающиеся файлы комплекта.\n"
                 "  --run       После установки собрать и запустить проект.\n"
                 "  --version   Показать версию установщика.\n"
                 "  --help      Показать эту справку.\n";
}

static fs::file_status Status(const fs::path &path)
{
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory)
        return fs::file_status(fs::file_type::not_found);
    if (error)
        throw std::runtime_error("Не удалось проверить «" + path.string() + "»: " + error.message());
    return status;
}

static void RequireDirectory(const fs::path &path)
{
    const fs::file_status status = Status(path);
    if (fs::is_symlink(status))
        throw std::runtime_error("Символические ссылки внутри комплекта запрещены: «" + path.string() + "».");
    if (fs::exists(status) && !fs::is_directory(status))
        throw std::runtime_error("Вместо папки найден другой объект: «" + path.string() + "».");
}

// Выбранная папка уже приведена к canonical пути. Проверяем только пути
// устанавливаемого комплекта: mac-native и каждый его дочерний компонент.
static fs::file_status CheckDestination(const fs::path &target, const fs::path &relative)
{
    if (relative.empty() || relative.is_absolute())
        throw std::runtime_error("Некорректный путь во встроенном комплекте.");
    for (const auto &part : relative)
        if (part == "." || part == "..")
            throw std::runtime_error("Некорректный путь во встроенном комплекте: «" + relative.string() + "».");
    if (*relative.begin() != "mac-native")
        throw std::runtime_error("Встроенный комплект содержит файл вне mac-native/.");

    fs::path directory = target;
    for (const auto &part : relative.parent_path()) {
        directory /= part;
        RequireDirectory(directory);
    }
    const fs::path destination = target / relative;
    const fs::file_status status = Status(destination);
    if (fs::is_symlink(status))
        throw std::runtime_error("Символические ссылки внутри комплекта запрещены: «" + destination.string() + "».");
    if (fs::exists(status) && !fs::is_regular_file(status))
        throw std::runtime_error("Вместо файла найден другой объект: «" + destination.string() + "».");
    return status;
}

static bool Identical(const fs::path &path, const payload::File &file)
{
    if (fs::file_size(path) != file.size) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Не удалось прочитать «" + path.string() + "».");
    char buffer[8192];
    for (std::size_t offset = 0; offset < file.size;) {
        const std::size_t count = std::min(sizeof(buffer), file.size - offset);
        input.read(buffer, static_cast<std::streamsize>(count));
        if (input.gcount() != static_cast<std::streamsize>(count))
            throw std::runtime_error("Не удалось полностью прочитать «" + path.string() + "».");
        if (std::memcmp(buffer, file.data + offset, count) != 0) return false;
        offset += count;
    }
    return input.peek() == std::char_traits<char>::eof();
}

// Замена через соседний временный файл не обрывает запись существующего
// файла при ошибке и не изменяет другие имена, связанные с ним hardlink.
static void Install(const PlannedFile &plan)
{
    std::string temporary = plan.destination.string() + ".install-XXXXXX";
    int descriptor = ::mkstemp(temporary.data());
    if (descriptor < 0)
        throw std::runtime_error("Не удалось создать файл «" + plan.destination.string() + "»: " + std::strerror(errno));
    try {
        for (std::size_t offset = 0; offset < plan.file->size;) {
            const ssize_t written = ::write(descriptor, plan.file->data + offset, plan.file->size - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0)
                throw std::runtime_error("Не удалось записать «" + plan.destination.string() + "»: " + std::strerror(errno));
            offset += static_cast<std::size_t>(written);
        }
        const mode_t mode = plan.present
            ? static_cast<mode_t>(plan.permissions & fs::perms::mask)
            : (plan.file->executable ? 0755 : 0644);
        if (::fchmod(descriptor, mode) != 0)
            throw std::runtime_error("Не удалось назначить права файлу «" + plan.destination.string() + "»: " + std::strerror(errno));
        const int result = ::close(descriptor);
        descriptor = -1;
        if (result != 0)
            throw std::runtime_error("Не удалось завершить запись «" + plan.destination.string() + "»: " + std::strerror(errno));
        fs::rename(temporary, plan.destination);
    } catch (...) {
        if (descriptor >= 0) ::close(descriptor);
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}

int main(int argc, char **argv)
{
    fs::path requested;
    bool force = false, run = false, help = false, version = false;
    for (int i = 1; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--help") help = true;
        else if (option == "--version") version = true;
        else if (option == "--force") force = true;
        else if (option == "--run") run = true;
        else if (option == "--dir") {
            if (i + 1 == argc || std::string(argv[i + 1]).compare(0, 2, "--") == 0 || argv[i + 1][0] == '\0') {
                std::cerr << "Для --dir нужен путь к папке. Используйте --help для справки.\n";
                return 2;
            }
            requested = argv[++i];
        } else {
            std::cerr << "Неизвестная опция: «" << option << "». Используйте --help для справки.\n";
            return 2;
        }
    }
    if (help) { Usage(); return 0; }
    if (version) { std::cout << "winapi-macos " << WINAPI_MACOS_VERSION << '\n'; return 0; }

    try {
        const fs::path target = fs::weakly_canonical(fs::absolute(requested.empty() ? fs::current_path() : requested));
        RequireDirectory(target);
        std::vector<PlannedFile> plans;
        std::vector<fs::path> conflicts;
        for (const auto &file : payload::files) {
            const fs::path relative = file.path;
            const fs::file_status status = CheckDestination(target, relative);
            const bool present = fs::exists(status);
            const fs::path destination = target / relative;
            const bool identical = present && Identical(destination, file);
            plans.push_back({&file, destination, present, identical, status.permissions()});
            if (present && !identical && !force) conflicts.push_back(destination);
        }
        if (!conflicts.empty()) {
            std::cerr << "Установка отменена: следующие файлы отличаются от встроенного комплекта:\n";
            for (const auto &path : conflicts) std::cerr << "  " << path.string() << '\n';
            std::cerr << "Для их замены запустите с --force. До этого момента файлы не изменялись.\n";
            return 1;
        }

        std::size_t installed = 0, unchanged = 0;
        for (const auto &plan : plans) {
            if (plan.identical) { ++unchanged; continue; }
            CheckDestination(target, plan.file->path);
            fs::create_directories(plan.destination.parent_path());
            Install(plan);
            ++installed;
        }
        std::cout << "Комплект установлен: " << (target / "mac-native").string() << '\n'
                  << "Добавлено или обновлено: " << installed << "; уже совпадают: " << unchanged << ".\n";
        if (run) {
            std::cout.flush();
            if (::chdir(target.c_str()) != 0)
                throw std::runtime_error("Не удалось перейти в папку проекта: " + std::string(std::strerror(errno)));
            ::execl("/bin/bash", "bash", "mac-native/run.sh", static_cast<char *>(nullptr));
            throw std::runtime_error("Не удалось запустить /bin/bash: " + std::string(std::strerror(errno)));
        }
        std::cout << "Для сборки и запуска из папки проекта: ./mac-native/run.sh\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Ошибка установки: " << error.what() << '\n';
        return 1;
    }
}
