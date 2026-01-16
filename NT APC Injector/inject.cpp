#include "header.h"
#include <cstring>

BOOL ApcInjectDll(HANDLE ProcessHandle, BOOL bSuspendWake, const char* DllPath)
{
    if (!ProcessHandle || !DllPath) return FALSE;

    DWORD dwProcessId = GetProcessId(ProcessHandle);
    if (!dwProcessId) return FALSE;

    SIZE_T sDllLength = strlen(DllPath) + 1;
    PVOID pvDllMemory = NULL;
    SIZE_T NumberOfBytesWritten = 0;

    FARPROC fpLoadLibraryA = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    FARPROC fpNtAllocateVirtualMemory = GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtAllocateVirtualMemory");
    FARPROC fpNtWriteVirtualMemory = GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtWriteVirtualMemory");
    FARPROC fpNtSuspendThread = GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtSuspendThread");
    FARPROC fpNtAlertResumeThread = GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtAlertResumeThread");

    if (!fpLoadLibraryA || !fpNtAllocateVirtualMemory || !fpNtWriteVirtualMemory || !fpNtSuspendThread || !fpNtAlertResumeThread)
        return FALSE;

    MyNtAllocateVirtualMemory fNtAllocateVirtualMemory = (MyNtAllocateVirtualMemory)fpNtAllocateVirtualMemory;
    MyNtWriteVirtualMemory fNtWriteVirtualMemory = (MyNtWriteVirtualMemory)fpNtWriteVirtualMemory;
    MyNtSuspendThread fNtSuspendThread = (MyNtSuspendThread)fpNtSuspendThread;
    MyNtAlertResumeThread fNtAlertResumeThread = (MyNtAlertResumeThread)fpNtAlertResumeThread;

    SIZE_T regionSize = sDllLength;
    MY_NTSTATUS status = fNtAllocateVirtualMemory(ProcessHandle, &pvDllMemory, 0, &regionSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!MY_NT_SUCCESS(status) || pvDllMemory == NULL) {
        return FALSE;
    }

    status = fNtWriteVirtualMemory(ProcessHandle, pvDllMemory, (PVOID)DllPath, sDllLength, &NumberOfBytesWritten);
    if (!MY_NT_SUCCESS(status) || NumberOfBytesWritten != sDllLength) {
        VirtualFreeEx(ProcessHandle, pvDllMemory, 0, MEM_RELEASE);
        return FALSE;
    }

    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        VirtualFreeEx(ProcessHandle, pvDllMemory, 0, MEM_RELEASE);
        return FALSE;
    }

    BOOL ok = Thread32First(hSnap, &te);
    while (ok)
    {
        if (te.th32OwnerProcessID == dwProcessId)
        {
            HANDLE hThread = NtOpenThread(dwProcessId, te.th32ThreadID);
            if (hThread)
            {
                if (bSuspendWake)
                {
                    MY_NTSTATUS s1 = fNtSuspendThread(hThread, NULL);
                    if (!MY_NT_SUCCESS(s1)) {
                        CloseHandle(hThread);
                        ok = Thread32Next(hSnap, &te);
                        continue;
                    }
                }

                QueueUserAPC((PAPCFUNC)fpLoadLibraryA, hThread, (ULONG_PTR)pvDllMemory);

                if (bSuspendWake)
                {
                    MY_NTSTATUS s2 = fNtAlertResumeThread(hThread, NULL);
                    (void)s2;
                }

                CloseHandle(hThread);
            }
        }
        ok = Thread32Next(hSnap, &te);
    }

    CloseHandle(hSnap);

    return TRUE;
}
