#include "header.h"

HANDLE NtOpenProcess(DWORD dwProcessId)
{
    if (dwProcessId == 0) return NULL;

    FARPROC fp = GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtOpenProcess");
    if (!fp) return NULL;

    MyNtOpenProcess f = (MyNtOpenProcess)fp;

    HANDLE hProcess = NULL;
    OBJECT_ATTRIBUTES objAttr;
    InitializeObjectAttributes(&objAttr, NULL, 0, NULL, NULL);

    CLIENT_ID cid;
    cid.UniqueProcess = UlongToHandle(dwProcessId);
    cid.UniqueThread = NULL;

    MY_NTSTATUS status = f(&hProcess, PROCESS_ALL_ACCESS, &objAttr, &cid);
    return MY_NT_SUCCESS(status) ? hProcess : NULL;
}

HANDLE NtOpenThread(DWORD dwProcessId, DWORD dwThreadId)
{
    FARPROC fp = GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtOpenThread");
    if (!fp) return NULL;

    MyNtOpenThread f = (MyNtOpenThread)fp;

    HANDLE hThread = NULL;
    OBJECT_ATTRIBUTES objAttr;
    InitializeObjectAttributes(&objAttr, NULL, 0, NULL, NULL);

    CLIENT_ID cid;
    cid.UniqueProcess = UlongToHandle(dwProcessId);
    cid.UniqueThread = UlongToHandle(dwThreadId);

    MY_NTSTATUS status = f(&hThread, THREAD_ALL_ACCESS, &objAttr, &cid);
    return MY_NT_SUCCESS(status) ? hThread : NULL;
}
