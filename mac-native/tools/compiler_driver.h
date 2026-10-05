#pragma once

#include <string>
#include <vector>

// Вызывает исходный компилятор из PATH; в проектах с mac-native
// добавляет заголовок и, при линковке, готовую библиотеку Cocoa.
int RunCompiler(const std::string &name, const std::vector<std::string> &args);
