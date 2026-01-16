#define _CRT_SECURE_NO_WARNINGS

#include "header.h"

#include <windows.h>
#include <shlobj.h>
#include <commctrl.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <commdlg.h>

#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <ctime>
#include <mutex>
#include <thread>
#include <chrono>
#include <algorithm>
#include <shellapi.h>
#include <cctype>
#include <random>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comdlg32.lib")

#define IDC_LIST_WINDOWS 1001
#define IDC_EDIT_DLLPATH 1002
#define IDC_BTN_REFRESH  1003
#define IDC_BTN_START    1004
#define IDC_STATIC_INFO  1005
#define IDC_BTN_BROWSE   1006

static HWND g_hList = NULL;
static HWND g_hEditDll = NULL;
static HWND g_hBtnRefresh = NULL;
static HWND g_hBtnStart = NULL;
static HWND g_hStatus = NULL;
static HWND g_hBtnBrowse = NULL;

static std::wstring GetWindowTextWString(HWND hwnd)
{
    int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return L"";
    std::wstring s; s.resize(len + 1);
    GetWindowTextW(hwnd, &s[0], len + 1);
    s.resize(wcslen(s.c_str()));
    return s;
}

static BOOL CALLBACK EnumWindowsProc_GUI(HWND hwnd, LPARAM lParam)
{
    if (!IsWindowVisible(hwnd)) return TRUE;

    std::wstring title = GetWindowTextWString(hwnd);
    if (title.empty()) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    std::wstringstream ss;
    ss << title << L"    [PID: " << pid << L"]";

    HWND hList = (HWND)lParam;
    int index = (int)SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)ss.str().c_str());
    if (index >= 0) {
        SendMessageW(hList, LB_SETITEMDATA, (WPARAM)index, (LPARAM)pid);
    }
    return TRUE;
}

static void PopulateWindowList(HWND hList)
{
    SendMessageW(hList, LB_RESETCONTENT, 0, 0);
    EnumWindows(EnumWindowsProc_GUI, (LPARAM)hList);
}

static void MsgBoxInfo(HWND owner, const wchar_t* title, const wchar_t* msg)
{
    MessageBoxW(owner, msg, title, MB_OK | MB_ICONINFORMATION);
}
static void MsgBoxError(HWND owner, const wchar_t* title, const wchar_t* msg)
{
    MessageBoxW(owner, msg, title, MB_OK | MB_ICONERROR);
}

static bool BrowseForDllPath(HWND hwndOwner, std::wstring& outPath)
{
    wchar_t fileBuffer[MAX_PATH] = { 0 };

    if (g_hEditDll) {
        GetWindowTextW(g_hEditDll, fileBuffer, MAX_PATH);
    }

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwndOwner;
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"DLL files\0*.dll\0All files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER | OFN_HIDEREADONLY;

    std::wstring initDir;
    std::wstring cur = GetWindowTextWString(g_hEditDll);
    if (!cur.empty()) {
        size_t pos = cur.find_last_of(L"\\/");
        if (pos != std::wstring::npos) {
            initDir = cur.substr(0, pos);
            ofn.lpstrInitialDir = initDir.c_str();
        }
    }

    if (GetOpenFileNameW(&ofn)) {
        outPath = fileBuffer;
        return true;
    }
    return false;
}
static void OnStart(HWND hwnd)
{
    int sel = (int)SendMessageW(g_hList, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) {
        MsgBoxError(hwnd, L"Ошибка", L"Не выбрано окно. Выберите процесс из списка.");
        return;
    }
    LPARAM data = SendMessageW(g_hList, LB_GETITEMDATA, (WPARAM)sel, 0);
    DWORD pid = (DWORD)data;
    if (pid == 0) {
        MsgBoxError(hwnd, L"Ошибка", L"Не удалось получить PID выбранного окна.");
        return;
    }

    wchar_t dllPathW[MAX_PATH];
    GetWindowTextW(g_hEditDll, dllPathW, MAX_PATH);
    if (wcslen(dllPathW) == 0) {
        MsgBoxError(hwnd, L"Ошибка", L"Укажите путь до DLL в поле ввода.");
        return;
    }
    int needed = WideCharToMultiByte(CP_UTF8, 0, dllPathW, -1, NULL, 0, NULL, NULL);
    if (needed <= 0 || needed > 8192) {
        MsgBoxError(hwnd, L"Ошибка", L"Неверный путь к DLL.");
        return;
    }
    std::vector<char> dllPathUtf8(needed);
    WideCharToMultiByte(CP_UTF8, 0, dllPathW, -1, dllPathUtf8.data(), needed, NULL, NULL);

    HANDLE hProcess = NtOpenProcess(pid);
    if (!hProcess) {
        MsgBoxError(hwnd, L"Ошибка", L"Не удалось открыть процесс. Попробуйте запустить от администратора.");
        return;
    }

    BOOL ok = ApcInjectDll(hProcess, TRUE, dllPathUtf8.data());
    CloseHandle(hProcess);

    if (ok) {
        MsgBoxInfo(hwnd, L"Успех", L"Инъекция успешно поставлена в очередь.");
    }
    else {
        MsgBoxError(hwnd, L"Неудача", L"Инъекция не удалась. Проверьте права и путь к DLL.");
    }
}

std::wstring GetDesktopDllPath(const std::wstring& fileName = L"lol.dll")
{
    PWSTR pPath = nullptr;
    std::wstring result;

    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, NULL, &pPath)) && pPath)
    {
        result = pPath;
        CoTaskMemFree(pPath);
        if (!result.empty() && result.back() != L'\\') result += L'\\';
        result += fileName;
        return result;
    }

    wchar_t buf[MAX_PATH] = { 0 };
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, buf)))
    {
        result = buf;
        if (!result.empty() && result.back() != L'\\') result += L'\\';
        result += fileName;
        return result;
    }

    if (GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH) > 0)
    {
        result = buf;
        if (!result.empty() && result.back() != L'\\') result += L'\\';
        result += L"Desktop\\";
        result += fileName;
        return result;
    }

    return std::wstring();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        CreateWindowW(L"STATIC", L"Список окон (выберите окно):",
            WS_VISIBLE | WS_CHILD,
            10, 10, 480, 20,
            hwnd, (HMENU)0, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);

        g_hList = CreateWindowW(L"LISTBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | WS_BORDER,
            10, 35, 560, 240,
            hwnd, (HMENU)IDC_LIST_WINDOWS, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);
        SendMessageW(g_hList, WM_SETFONT, (WPARAM)hFont, TRUE);

        CreateWindowW(L"STATIC", L"Путь до DLL (UTF-8 будет использован):",
            WS_VISIBLE | WS_CHILD,
            10, 285, 300, 20,
            hwnd, (HMENU)0, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);

        g_hEditDll = CreateWindowW(L"EDIT", GetDesktopDllPath().c_str(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            10, 305, 320, 24,
            hwnd, (HMENU)IDC_EDIT_DLLPATH, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);
        SendMessageW(g_hEditDll, WM_SETFONT, (WPARAM)hFont, TRUE);

        g_hBtnBrowse = CreateWindowW(L"BUTTON", L"Browse...",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            340, 305, 80, 24,
            hwnd, (HMENU)IDC_BTN_BROWSE, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);

        g_hBtnRefresh = CreateWindowW(L"BUTTON", L"Refresh",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            430, 305, 60, 24,
            hwnd, (HMENU)IDC_BTN_REFRESH, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);

        g_hBtnStart = CreateWindowW(L"BUTTON", L"Start",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            500, 305, 70, 24,
            hwnd, (HMENU)IDC_BTN_START, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);

        g_hStatus = CreateWindowW(L"STATIC", L"Выберите окно и укажите путь к DLL, затем нажмите Start",
            WS_CHILD | WS_VISIBLE,
            10, 340, 560, 20,
            hwnd, (HMENU)IDC_STATIC_INFO, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);
        SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)hFont, TRUE);

        PopulateWindowList(g_hList);
    }
    return 0;

    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);
        WORD code = HIWORD(wParam);
        if (id == IDC_BTN_REFRESH && code == BN_CLICKED) {
            PopulateWindowList(g_hList);
        }
        else if (id == IDC_BTN_START && code == BN_CLICKED) {
            OnStart(hwnd);
        }
        else if (id == IDC_LIST_WINDOWS && code == LBN_DBLCLK) {
            OnStart(hwnd);
        }
        else if (id == IDC_BTN_BROWSE && code == BN_CLICKED) {
            std::wstring chosen;
            if (BrowseForDllPath(hwnd, chosen)) {
                SetWindowTextW(g_hEditDll, chosen.c_str());
            }
        }
    }
    return 0;

    case WM_SIZE:
    {
    }
    return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static std::wstring RandomWindowTitle(size_t len = 16) {
    static const wchar_t alphabet[] =
        L"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        L"abcdefghijklmnopqrstuvwxyz"
        L"0123456789";

    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<size_t> dist(0, (sizeof(alphabet) / sizeof(wchar_t)) - 2);

    std::wstring s;
    s.reserve(len + 8);
    s += L"";
    for (size_t i = 0; i < len; ++i) {
        s += alphabet[dist(gen)];
    }
    return s;
}

static bool IsRunAsAdmin()
{
    BOOL fIsElevated = FALSE;
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
    {
        TOKEN_ELEVATION elevation;
        DWORD dwSize = 0;
        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &dwSize))
        {
            fIsElevated = elevation.TokenIsElevated;
        }
        CloseHandle(hToken);
    }
    return fIsElevated != FALSE;
}

static void RelaunchAsAdminAndExit()
{
    wchar_t exePath[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exePath, MAX_PATH)) {
        MessageBoxW(NULL, L"Не удалось получить путь к исполняемому файлу.", L"Ошибка", MB_OK | MB_ICONERROR);
        ExitProcess(1);
    }

    LPWSTR cmdLine = GetCommandLineW();
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    sei.hwnd = NULL;
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.lpParameters = NULL;
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        DWORD err = GetLastError();
        if (err == ERROR_CANCELLED) {
            MessageBoxW(NULL, L"Приложение требует прав администратора. Запуск отменён пользователем.", L"Требуются права", MB_OK | MB_ICONWARNING);
        }
        else {
            MessageBoxW(NULL, L"Не удалось перезапустить приложение с правами администратора.", L"Ошибка", MB_OK | MB_ICONERROR);
        }
        ExitProcess(1);
    }
    ExitProcess(0);
}

void showNotification(const std::string& title, const std::string& message) {
    MessageBoxA(nullptr, message.c_str(), title.c_str(), MB_OK | MB_ICONWARNING);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    if (!IsRunAsAdmin()) {
        RelaunchAsAdminAndExit();
    }
    const wchar_t CLASS_NAME[] = L"InjectionWindowClass";

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassW(&wc)) {
        MessageBoxW(NULL, L"RegisterClass failed", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }
    std::wstring wndTitle = RandomWindowTitle(10);

    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        wndTitle.c_str(),
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 420,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if (!hwnd) {
        MessageBoxW(NULL, L"CreateWindowEx failed", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}
