// shlobj.h minimal: solo SHGetFolderPathW (firma real), para syntax-check.
#pragma once
#include "windows.h"

#ifdef __cplusplus
extern "C" {
#endif
HRESULT SHGetFolderPathW(void* hwnd, int csid, HANDLE hToken, DWORD dwFlags,
                         LPWSTR pszPath);
#ifdef __cplusplus
}
#endif
