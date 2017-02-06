#pragma once

#include "Common.h"

#include <filesystem>
namespace fs = ::std::experimental::filesystem;

// return the filenames of all files that have the specified extension
// in the specified directory and all subdirectories
void getFilesMatchingExtension(const fs::path& root, const std::string& ext, vector<fs::path>& ret);

vector<char> loadTextFileZ(const char* path);