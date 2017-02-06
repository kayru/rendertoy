#include "FileUtil.h"


void getFilesMatchingExtension(const fs::path& root, const std::string& ext, vector<fs::path>& ret)
{
	if (!fs::exists(root) || !fs::is_directory(root)) return;

	fs::recursive_directory_iterator it(root);
	fs::recursive_directory_iterator endit;

	while (it != endit)
	{
		if (fs::is_regular_file(*it) && it->path().extension() == ext) ret.push_back(it->path().filename());
		++it;
	}
}

vector<char> loadTextFileZ(const char* path)
{
	vector<char> programSource;
	const char* programSourceFile = path;
	FILE *f = fopen(programSourceFile, "rb");
	fseek(f, 0, SEEK_END);
	const int fsize = ftell(f);
	fseek(f, 0, SEEK_SET);
	programSource.resize(fsize + 1);
	fread(programSource.data(), 1, fsize, f);
	fclose(f);
	programSource.back() = '\0';
	return programSource;
}
