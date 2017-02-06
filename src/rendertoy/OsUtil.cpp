#include "OsUtil.h"
#define VC_EXTRALEAN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>


bool openFileDialog(const char* const title, const char *const filter, std::string *const result)
{
	OPENFILENAME ofn = {};
	char filename[1024] = { '\0' };
	ofn.lStructSize = sizeof(ofn);
	ofn.lpstrFilter = filter;
	ofn.lpstrFile = filename;
	ofn.nMaxFile = sizeof(filename);
	ofn.lpstrTitle = title;
	ofn.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
	if (GetOpenFileNameA(&ofn))
	{
		*result = filename;
		return true;
	} else {
		return false;
	}
}

void shellExecute(const char* const cmd)
{
	char fullPath[1024];
	GetFullPathName(cmd, sizeof(fullPath), fullPath, nullptr);
	ShellExecuteA(0, nullptr, fullPath, 0, 0, SW_SHOW);
}
