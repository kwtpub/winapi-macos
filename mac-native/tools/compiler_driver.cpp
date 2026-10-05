#include "compiler_driver.h"

#include <mach-o/dyld.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;

bool StartsWith(const std::string &value, const std::string &prefix)
{
    return value.compare(0, prefix.size(), prefix) == 0;
}

bool Equivalent(const fs::path &left, const fs::path &right)
{
    std::error_code error;
    return fs::equivalent(left, right, error) && !error;
}

fs::path CurrentExecutable()
{
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (buffer.empty() || _NSGetExecutablePath(buffer.data(), &size) != 0)
        throw std::runtime_error("Не удалось определить путь compiler driver.");
    return fs::canonical(buffer.data());
}

fs::path OriginalCompiler(const std::string &name)
{
    if (name != "g++" && name != "clang++")
        throw std::runtime_error("Поддерживаются только компиляторы g++ и clang++.");
    const fs::path executable = CurrentExecutable();
    const char *environment = std::getenv("PATH");
    const std::string path = environment ? environment : "/usr/bin:/bin";
    for (std::size_t begin = 0;;) {
        const std::size_t end = path.find(':', begin);
        fs::path directory = path.substr(begin, end == std::string::npos ? end : end - begin);
        if (directory.empty()) directory = fs::current_path();
        directory = fs::absolute(directory);
        const fs::path candidate = directory / name;
        std::error_code error;
        if (!Equivalent(directory, executable.parent_path()) &&
            !Equivalent(candidate, executable) &&
            fs::is_regular_file(candidate, error) && !error && ::access(candidate.c_str(), X_OK) == 0)
            return candidate; // Preserve argv[0], including a g++/clang++ symlink name.
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    throw std::runtime_error("Не найден исходный компилятор «" + name + "» в PATH.");
}

std::vector<std::string> Tokenize(const std::string &text, const fs::path &file)
{
    std::vector<std::string> tokens;
    std::string token;
    char quote = 0;
    bool started = false;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char character = text[index];
        if (character == '\0')
            throw std::runtime_error("Нулевой байт в response file «" + file.string() + "».");
        if (character == '\\') {
            if (++index == text.size())
                throw std::runtime_error("Незавершённое экранирование в response file «" + file.string() + "».");
            token += text[index];
            started = true;
        } else if (quote) {
            if (character == quote) quote = 0;
            else token += character;
            started = true;
        } else if (character == '\'' || character == '"') {
            quote = character;
            started = true;
        } else if (std::isspace(static_cast<unsigned char>(character))) {
            if (started) { tokens.push_back(token); token.clear(); started = false; }
        } else {
            token += character;
            started = true;
        }
    }
    if (quote)
        throw std::runtime_error("Незакрытая кавычка в response file «" + file.string() + "».");
    if (started) tokens.push_back(token);
    return tokens;
}

struct Expansion {
    static constexpr std::size_t maximumBytes = 1024 * 1024;
    std::size_t bytes = 0;
    bool responses = false;
    std::vector<fs::path> stack;
};

void Expand(const std::string &argument, Expansion &state, unsigned depth,
            std::vector<std::string> &result)
{
    if (argument.size() < 2 || argument.front() != '@') {
        result.push_back(argument);
        return;
    }
    state.responses = true;
    if (depth >= 16)
        throw std::runtime_error("Response files вложены глубже 16 уровней.");
    const fs::path file = fs::canonical(argument.substr(1));
    for (const auto &active : state.stack)
        if (Equivalent(file, active))
            throw std::runtime_error("Циклический response file: «" + file.string() + "».");
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("Не удалось прочитать response file «" + file.string() + "».");
    std::string text;
    std::array<char, 8192> buffer;
    while (input) {
        input.read(buffer.data(), buffer.size());
        const std::size_t count = static_cast<std::size_t>(input.gcount());
        if (count > Expansion::maximumBytes - state.bytes)
            throw std::runtime_error("Суммарный размер response files превышает 1 МиБ.");
        state.bytes += count;
        text.append(buffer.data(), count);
    }
    if (input.bad()) throw std::runtime_error("Ошибка чтения response file «" + file.string() + "».");
    state.stack.push_back(file);
    for (const auto &token : Tokenize(text, file)) Expand(token, state, depth + 1, result);
    state.stack.pop_back();
}

bool Query(const std::string &argument)
{
    return argument == "--version" || argument == "--help" || argument == "-help" ||
           argument == "--help-hidden" || argument == "-dumpmachine" || argument == "-dumpversion" ||
           StartsWith(argument, "-print-") || StartsWith(argument, "--print-") ||
           StartsWith(argument, "--help=");
}

bool TakesValue(const std::string &option)
{
    static const char *options[] = {
        "-o", "-x", "-I", "-isystem", "-iquote", "-idirafter", "-iframework", "-F", "-L", "-l",
        "-framework", "-weak_framework", "-isysroot", "--sysroot", "-target", "--target", "-arch",
        "-include", "-imacros", "-iprefix", "-iwithprefix", "-iwithprefixbefore", "-iwithsysroot",
        "-iframeworkwithsysroot", "-MF", "-MT", "-MQ", "-MJ", "-D", "-U", "-B", "-Xclang",
        "-Xlinker", "-Xpreprocessor", "-Xassembler", "-mllvm", "-resource-dir", "--gcc-toolchain",
        "-gcc-toolchain", "-gcc-install-dir", "-ivfsoverlay", "-serialize-diagnostics",
        "-dependency-file", "--config", "-working-directory"
    };
    return std::any_of(std::begin(options), std::end(options), [&](const char *value) { return option == value; }) ||
           StartsWith(option, "-Xarch_");
}

bool RuntimeArgument(const std::string &argument)
{
    if (argument == "-lwinapi_macos" || argument == "-l:libwinapi_macos.a") return true;
    if (StartsWith(argument, "-Wl,")) {
        for (std::size_t begin = 4;;) {
            const std::size_t end = argument.find(',', begin);
            if (fs::path(argument.substr(begin, end == std::string::npos ? end : end - begin)).filename() == "libwinapi_macos.a")
                return true;
            if (end == std::string::npos) break;
            begin = end + 1;
        }
    }
    return fs::path(argument).filename() == "libwinapi_macos.a";
}

struct Invocation {
    bool query = false, inputs = false, compileOnly = false;
    bool separator = false, trailingInputs = false, runtime = false, cocoa = false, libcxx = false;
    std::string separatorLanguage = "none";
    std::vector<fs::path> directories;
};

std::vector<std::string>::const_iterator Separator(const std::vector<std::string> &arguments)
{
    for (auto argument = arguments.begin(); argument != arguments.end(); ++argument) {
        if (*argument == "--") return argument;
        if (TakesValue(*argument) && argument + 1 != arguments.end()) ++argument;
    }
    return arguments.end();
}

Invocation Classify(const std::vector<std::string> &arguments)
{
    Invocation call;
    std::string language = "none";
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string &argument = arguments[index];
        if (!call.separator && argument == "--") {
            call.separator = true;
            call.separatorLanguage = language;
            continue;
        }
        if (!call.separator) {
            if (Query(argument)) call.query = true;
            if (argument == "-c" || argument == "-E" || argument == "-S" || argument == "-M" ||
                argument == "-MM" || argument == "-fsyntax-only") call.compileOnly = true;
            if (RuntimeArgument(argument)) call.runtime = true;
            if (argument == "-lc++") call.libcxx = true;
            if (TakesValue(argument)) {
                if (index + 1 == arguments.size()) continue; // The real compiler diagnoses a missing value.
                const std::string &value = arguments[++index];
                if (argument == "-x") language = value;
                if (argument == "-l" && value == "winapi_macos") call.runtime = true;
                if (argument == "-l" && value == "c++") call.libcxx = true;
                if (argument == "-framework" && value == "Cocoa") call.cocoa = true;
                if (argument == "-Xlinker" && RuntimeArgument(value)) call.runtime = true;
                continue;
            }
            if (StartsWith(argument, "-x") && argument.size() > 2) language = argument.substr(2);
            if (!argument.empty() && argument.front() == '-' && argument != "-") continue;
        }
        if (argument.empty()) continue;
        call.inputs = true;
        if (call.separator) call.trailingInputs = true;
        if (RuntimeArgument(argument)) call.runtime = true;
        if (argument == "-" || (argument.size() > 1 && argument.front() == '@')) continue;
        const fs::path path(argument);
        const std::string extension = path.extension().string();
        if (extension == ".cpp" || extension == ".cc" || extension == ".cxx" || extension == ".C" ||
            extension == ".c++" || extension == ".CPP" || extension == ".o" || extension == ".obj" ||
            extension == ".mm" || (language != "none" && extension != ".a" && extension != ".dylib"))
            call.directories.push_back(fs::absolute(path).parent_path());
    }
    return call;
}

fs::path NearestModule(fs::path directory)
{
    directory = fs::weakly_canonical(fs::absolute(directory));
    for (;;) {
        const fs::path module = directory / "mac-native";
        std::error_code error;
        if (fs::is_regular_file(module / "windows.h", error) && !error &&
            fs::is_regular_file(module / "libwinapi_macos.a", error) && !error)
            return fs::canonical(module);
        const fs::path parent = directory.parent_path();
        if (parent == directory || parent.empty()) return {};
        directory = parent;
    }
}

fs::path SelectModule(const Invocation &call)
{
    fs::path selected;
    std::vector<fs::path> directories = call.directories;
    directories.insert(directories.begin(), fs::current_path());
    for (const auto &directory : directories) {
        const fs::path module = NearestModule(directory);
        if (module.empty()) continue;
        if (!selected.empty() && selected != module)
            throw std::runtime_error("Исходники относятся к разным комплектам mac-native: «" +
                                     selected.string() + "» и «" + module.string() + "».");
        selected = module;
    }
    return selected;
}

bool GNUCompiler(const fs::path &compiler)
{
    const fs::path real = fs::canonical(compiler);
    const std::string filename = real.filename().string(), path = real.string();
    return (StartsWith(filename, "g++-") && filename.size() > 4 &&
            std::isdigit(static_cast<unsigned char>(filename[4]))) ||
           path.find("/Cellar/gcc/") != std::string::npos || path.find("/Cellar/gcc@") != std::string::npos;
}

std::vector<std::string> Adapt(const std::vector<std::string> &original, const Invocation &call,
                               const Expansion &expansion, const fs::path &module, const fs::path &compiler)
{
    if (call.separator && expansion.responses)
        throw std::runtime_error("Автоподключение не поддерживает сочетание response files и «--». Уберите разделитель или используйте явные параметры сборки.");
    const bool library = !call.compileOnly && !call.runtime;
    if (library && call.separator && call.trailingInputs && call.separatorLanguage != "none")
        throw std::runtime_error("Автоподключение не может безопасно сочетать активный «-x» с исходниками после «--». Уберите разделитель или используйте явные параметры сборки.");

    std::vector<std::string> result = {"-I", module.string()};
    auto separator = Separator(original);
    result.insert(result.end(), original.begin(), separator);
    if (library) result.insert(result.end(), {"-x", "none"});
    if (separator == original.end() && library)
        result.push_back((module / "libwinapi_macos.a").string());
    if (!call.compileOnly) {
        if (!call.cocoa) result.insert(result.end(), {"-framework", "Cocoa"});
        if (!call.libcxx && GNUCompiler(compiler)) result.push_back("-lc++");
    }
    result.insert(result.end(), separator, original.end());
    if (separator != original.end() && library)
        result.push_back((module / "libwinapi_macos.a").string());
    return result;
}

int Execute(const fs::path &compiler, const std::vector<std::string> &arguments)
{
    std::vector<std::string> storage = {compiler.string()};
    storage.insert(storage.end(), arguments.begin(), arguments.end());
    std::vector<char *> argv;
    for (auto &argument : storage) argv.push_back(argument.data());
    argv.push_back(nullptr);
    ::execv(compiler.c_str(), argv.data());
    throw std::runtime_error("Не удалось запустить «" + compiler.string() + "»: " + std::strerror(errno));
}
} // namespace

int RunCompiler(const std::string &name, const std::vector<std::string> &args)
{
    try {
        const fs::path compiler = OriginalCompiler(name);
        const char *disabled = std::getenv("WINAPI_MACOS_DISABLE");
        if (disabled && std::strcmp(disabled, "1") == 0) return Execute(compiler, args);
        if (Classify(args).query) return Execute(compiler, args);
        Expansion expansion;
        std::vector<std::string> expanded;
        try {
            for (const auto &argument : args) Expand(argument, expansion, 0, expanded);
        } catch (const std::exception &) {
            if (SelectModule(Classify(args)).empty()) return Execute(compiler, args);
            throw;
        }
        const Invocation call = Classify(expanded);
        if (call.query || !call.inputs) return Execute(compiler, args);
        const fs::path module = SelectModule(call);
        return Execute(compiler, module.empty() ? args : Adapt(args, call, expansion, module, compiler));
    } catch (const std::exception &error) {
        std::cerr << "winapi-macos: " << error.what() << '\n';
        return 1;
    }
}
