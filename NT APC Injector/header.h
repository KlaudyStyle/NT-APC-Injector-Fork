#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <string>

//
// Минимальные определения UNICODE_STRING, OBJECT_ATTRIBUTES и InitializeObjectAttributes
// Используем стандартные имена, чтобы не ломать существующий код (.cpp).
// Защищаемся от повторного определения через include-guards.
// These mirror the usual definitions from winternl.h but are minimal.
//


// UNICODE_STRING (компактная версия)
#ifndef _CUSTOM_UNICODE_STRING_DEFINED
#define _CUSTOM_UNICODE_STRING_DEFINED
typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} UNICODE_STRING, * PUNICODE_STRING;
#endif

// OBJECT_ATTRIBUTES (минимальный)
#ifndef _CUSTOM_OBJECT_ATTRIBUTES_DEFINED
#define _CUSTOM_OBJECT_ATTRIBUTES_DEFINED
typedef struct _OBJECT_ATTRIBUTES {
    ULONG           Length;
    HANDLE          RootDirectory;
    PUNICODE_STRING ObjectName;
    ULONG           Attributes;
    PVOID           SecurityDescriptor;
    PVOID           SecurityQualityOfService;
} OBJECT_ATTRIBUTES, * POBJECT_ATTRIBUTES;
#endif

// InitializeObjectAttributes макрос, если не определён
#ifndef InitializeObjectAttributes
#define InitializeObjectAttributes(p, n, a, r, s) \
    do { \
        (p)->Length = sizeof(OBJECT_ATTRIBUTES); \
        (p)->RootDirectory = (r); \
        (p)->Attributes = (a); \
        (p)->ObjectName = (PUNICODE_STRING)(n); \
        (p)->SecurityDescriptor = (s); \
        (p)->SecurityQualityOfService = NULL; \
    } while(0)
#endif

// CLIENT_ID (стандартное имя) — защищаем от переопределения
#ifndef _CUSTOM_CLIENT_ID_DEFINED
#define _CUSTOM_CLIENT_ID_DEFINED
typedef struct _CLIENT_ID {
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
} CLIENT_ID, * PCLIENT_ID;
#endif

// Наши NT-типы (отдельные имена для избежания конфликтов)
typedef LONG MY_NTSTATUS;
#define MY_NT_SUCCESS(Status) (((MY_NTSTATUS)(Status)) >= 0)

// Уникальные typedef'ы для NTAPI-функций (обозначены MyNt*)
typedef MY_NTSTATUS(NTAPI* MyNtAllocateVirtualMemory)(
    HANDLE ProcessHandle,
    PVOID* BaseAddress,
    ULONG_PTR ZeroBits,
    PSIZE_T RegionSize,
    ULONG AllocationType,
    ULONG Protect
    );

typedef MY_NTSTATUS(NTAPI* MyNtWriteVirtualMemory)(
    HANDLE ProcessHandle,
    PVOID BaseAddress,
    PVOID Buffer,
    SIZE_T NumberOfBytesToWrite,
    PSIZE_T NumberOfBytesWritten
    );

typedef MY_NTSTATUS(NTAPI* MyNtSuspendThread)(
    HANDLE ThreadHandle,
    PULONG PreviousSuspendCount
    );

typedef MY_NTSTATUS(NTAPI* MyNtAlertResumeThread)(
    HANDLE ThreadHandle,
    PULONG PreviousSuspendCount
    );

typedef MY_NTSTATUS(NTAPI* MyNtOpenProcess)(
    PHANDLE ProcessHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    PCLIENT_ID ClientId
    );

typedef MY_NTSTATUS(NTAPI* MyNtOpenThread)(
    PHANDLE ThreadHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    PCLIENT_ID ClientId
    );

// Декларации наших функций (реализации в .cpp)
#ifdef __cplusplus
extern "C" {
#endif

    HANDLE NtOpenProcess(DWORD dwProcessId);
    HANDLE NtOpenThread(DWORD dwProcessId, DWORD dwThreadId);
    DWORD dwRetProcessId(const std::string& processname);
    BOOL ApcInjectDll(HANDLE ProcessHandle, BOOL bSuspendWake, const char* DllPath);

#ifdef __cplusplus
}
#endif
