#pragma once
#include <string>

// Just a dirty wrapper on GetOpenFileNameA
// 'filter' should be same as for the latter, e.g. "Image Files\0*.exr\0"
bool openFileDialog(const char* const title, const char *const filter, std::string *const result);
void shellExecute(const char* const cmd);
