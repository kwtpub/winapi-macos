#pragma once

#include <filesystem>

std::filesystem::path CurrentExecutable();
void SetupCompiler();
void RemoveCompilerSetup();
