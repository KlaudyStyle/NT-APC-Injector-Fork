#include "header.h"
#include <cstring>

DWORD dwRetProcessId(const std::string& processname)
{
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;

    BOOL ok = Process32First(hSnap, &pe);
    while (ok)
    {
        if (_stricmp(pe.szExeFile, processname.c_str()) == 0)
        {
            DWORD pid = pe.th32ProcessID;
            CloseHandle(hSnap);
            return pid;
        }
        ok = Process32Next(hSnap, &pe);
    }
    CloseHandle(hSnap);
    return 0;
}
