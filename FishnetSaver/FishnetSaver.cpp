
#include <Windows.h>
#include <ScrnSave.h>
#include <objbase.h>
#include <shobjidl.h>
#include <shobjidl_core.h>
#include <Shlwapi.h>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include "messages.h"
#include "resource.h"
#include "registry.h"

// TODO
// - use wrapper registry key to find the wrapper program

#define PROVIDER_NAME L"FishnetProvider"
#define REGISTRY_LOCATION L"Software\\FishnetSaver"

void LogEvent(UINT message, WPARAM wParam, LPARAM lParam);
void LogError(LPCWSTR fun);
void LogError(LPCWSTR fun, LRESULT lr);

LPWSTR makeCommandLine(HKEY subkey);
std::wstring makeCommand(HKEY subkey);
void SetControlFromReg(HWND hCtrl, HKEY subkey, LPCWSTR regKey, HWND xx);
void SetRegFromControl(HWND hCtrl, HKEY subkey, LPCWSTR regKey);

#if 0
HRESULT BasicFileOpen(void);
#endif

struct child_handles {
    HANDLE hProcess;
    HANDLE waitHandle;
    HWND hWnd;
    HANDLE hStdin, hStdout;
};

struct child_handles* StartIt(HKEY subkey);
void StopIt(struct child_handles* ch);

VOID CALLBACK ProcessReaper(_In_ PVOID lpParameter, _In_ BOOLEAN timerOrWaitFired);

#define WM_PROCESS_EXITED (WM_USER + 1)

HANDLE hEventLog = NULL;

LRESULT WINAPI ScreenSaverProc(HWND hWnd,
	UINT message, WPARAM wParam, LPARAM lParam)
{
    static struct child_handles* runningProc = NULL;
    static UINT_PTR uTimer;
    static bool quitOnExit = false;
    static HKEY hSubkey = (HKEY)INVALID_HANDLE_VALUE;

    if (hEventLog == NULL)
        RegisterEventSource(NULL, PROVIDER_NAME);
    //LogEvent(message, wParam, lParam);

    switch (message)
    {

    case WM_CREATE:
    {
        if (hEventLog == NULL)
            RegisterEventSource(NULL, PROVIDER_NAME);

        LRESULT lp = RegCreateKeyEx(HKEY_LOCAL_MACHINE, REGISTRY_LOCATION, 0, NULL, REG_OPTION_NON_VOLATILE,
            KEY_READ | KEY_QUERY_VALUE,
            NULL, &hSubkey, NULL);
        if (lp != ERROR_SUCCESS) {
            hSubkey = (HKEY)INVALID_HANDLE_VALUE;
            LogError(TEXT("RegCreateKeyExe"));
        }

        if (hEventLog == NULL)
            hEventLog = RegisterEventSource(NULL, PROVIDER_NAME);

        uTimer = SetTimer(hWnd, 1, 10000, NULL);
        break;
    }
    case WM_DESTROY:
        RegCloseKey(hSubkey);

        if (uTimer)
            KillTimer(hWnd, 1);

        if (runningProc == NULL)
            PostQuitMessage(0);
        else {
            quitOnExit = true;
            StopIt(runningProc);
        }
        break;

    case WM_TIMER:
        if (quitOnExit)
            break;

        if (runningProc == NULL)
            runningProc = StartIt(hSubkey);

        {
            RECT wrec = { 0 };
            GetWindowRect(hWnd, &wrec);
            InvalidateRect(hWnd, &wrec, TRUE);
        }
        break;

    case WM_ERASEBKGND:
    {
        // I see a colorful screen and I want to paint it black
        HDC hDC = GetDC(hWnd);
        RECT rc = { 0 };
        GetClientRect(hWnd, &rc);
        FillRect(hDC, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        ReleaseDC(hWnd, hDC);
        break;
    }

    case WM_PAINT:
    {
        HDC hDC;
        RECT rc = { 0 };
        PAINTSTRUCT ps = { 0 };

        OutputDebugString(runningProc ? TEXT("paint: proc not null\n") : TEXT("print: proc is null\n"));
        hDC = BeginPaint(hWnd, &ps);

        GetClientRect(hWnd, &rc);
        FillRect(hDC, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));

        SetBkColor(hDC, RGB(0, 0, 0));

        if (runningProc == NULL)
            SetTextColor(hDC, RGB(255, 0, 0));
        else
            SetTextColor(hDC, RGB(120, 120, 120));

        if (fChildPreview)
            TextOut(hDC, 2, 45, L"FishnetSaver", (int)wcslen(L"FishnetSaver"));
        else
            TextOut(hDC, GetSystemMetrics(SM_CXSCREEN) / 2,
                GetSystemMetrics(SM_CYSCREEN) / 2, TEXT("FishnetSaver"), (int)wcslen(L"FishnetSaver"));

        EndPaint(hWnd, &ps);
        break;
    }
    case WM_PROCESS_EXITED:
    {
        struct child_handles* ch = reinterpret_cast<struct child_handles*>(lParam);
        CloseHandle(ch->hProcess);
        (void) UnregisterWait(ch->waitHandle);
        if (ch->hStdin != INVALID_HANDLE_VALUE) {
            CloseHandle(ch->hStdin);
            ch->hStdin = INVALID_HANDLE_VALUE;
        }
        if (ch->hStdout != INVALID_HANDLE_VALUE) {
            CloseHandle(ch->hStdout);
            ch->hStdout = INVALID_HANDLE_VALUE;
        }
        if (runningProc == ch)
            runningProc = NULL;
        LocalFree(ch);
        if (quitOnExit)
            PostQuitMessage(0);
        {
            RECT wrec = { 0 };
            GetWindowRect(hWnd, &wrec);
            InvalidateRect(hWnd, &wrec, TRUE);
        }

        break;
    }

    default:
        return DefScreenSaverProc(hWnd, message, wParam, lParam);
    }

    return 0;
}

BOOL WINAPI ScreenSaverConfigureDialog(HWND hDlg,
    UINT message, WPARAM wParam, LPARAM lParam)
{
    static HWND hFishnetKey, hFishnetExe, hOk;
    static HINSTANCE hMainInstance;
    static HKEY hSubkey;

    switch (message)
    {
    case WM_INITDIALOG:
    {
        hMainInstance = GetModuleHandle(NULL);

        // Retrieve the application name from the .rc file.  
        LoadString(hMainInstance, idsAppName, szAppName,
            sizeof(szAppName) / sizeof(TCHAR));

        LRESULT lp = RegCreateKeyEx(HKEY_LOCAL_MACHINE, REGISTRY_LOCATION, 0, NULL, REG_OPTION_NON_VOLATILE,
            KEY_READ | KEY_QUERY_VALUE,
            NULL, &hSubkey, NULL);
        if (lp != ERROR_SUCCESS) {
            hSubkey = (HKEY)INVALID_HANDLE_VALUE;
            LogError(TEXT("RegCreateKeyExe"));
        }

        hFishnetKey = GetDlgItem(hDlg, IDC_FISHNET_KEY);
        SetControlFromReg(hFishnetKey, hSubkey, L"Key", GetDlgItem(hDlg, IDC_EDIT1));

        hFishnetExe = GetDlgItem(hDlg, IDC_FISHNET_EXE);
        SetControlFromReg(hFishnetExe, hSubkey, L"Program", GetDlgItem(hDlg, IDC_EDIT1));

        hOk = GetDlgItem(hDlg, ID_OK);

        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_OK:
        case ID_CANCEL:
            if (LOWORD(wParam) == ID_OK) {
                SetRegFromControl(hFishnetKey, hSubkey, L"Key");
                SetRegFromControl(hFishnetExe, hSubkey, L"Program");
            }
            RegCloseKey(hSubkey);
            EndDialog(hDlg, LOWORD(wParam) == ID_OK);
            return TRUE;

        case ID_BROWSE:
            return TRUE;

        }
    }
    return FALSE;
}

BOOL WINAPI RegisterDialogClasses(HANDLE hInst)
{
    //Since we do not register any special window class 
    //for the configuration dialog box, we must return TRUE

    return TRUE;
}

struct child_handles *
    StartIt(HKEY subkey) {
    std::wstring command;
    LPWSTR commandline = NULL;
    struct child_handles* ch = NULL;
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    HANDLE inputWriteSide = INVALID_HANDLE_VALUE;
    HANDLE inputReadSide = INVALID_HANDLE_VALUE;
    HANDLE outputWriteSide = INVALID_HANDLE_VALUE;
    HANDLE outputReadSide = INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES saAttr;

    OutputDebugStringW(L"Starting Process\n");

    saAttr.nLength = sizeof(saAttr);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));

    ch = (struct child_handles*)LocalAlloc(LPTR, sizeof(*ch));
    if (ch == NULL)
        return (NULL);
    ch->hStdin = INVALID_HANDLE_VALUE;
    ch->hStdout = INVALID_HANDLE_VALUE;

    si.cb = sizeof(si);

    if (!CreatePipe(&inputReadSide, &inputWriteSide, &saAttr, 0)) {
        LogError(TEXT("CreatePipe(in)"));
        goto errout;
    }

    if (!SetHandleInformation(inputWriteSide, HANDLE_FLAG_INHERIT, 0)) {
        // handle should not be inherited
        LogError(TEXT("SetHandleInformation(in)"));
        goto errout;
    }

    if (!CreatePipe(&outputReadSide, &outputWriteSide, &saAttr, 0)) {
        LogError(TEXT("CreatePipe(out)"));
        goto errout;
    }

    if (!SetHandleInformation(outputReadSide, HANDLE_FLAG_INHERIT, 0)) {
        // handle should not be inherited
        LogError(TEXT("SetHandleInformation(out)"));
        goto errout;
    }

    try {
        command = makeCommand(subkey);
        commandline = makeCommandLine(subkey);
    }
    catch (std::runtime_error&) {
        goto errout;
    }

    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput = inputReadSide;
    si.hStdOutput = outputWriteSide;
    si.hStdError = outputWriteSide;

    if (!CreateProcess(command.c_str(),
        commandline,
        NULL,       // process handler not inheritable
        NULL,       // thread handle not inheritable
        TRUE,      // Set handle inheritance to FALSE
        DETACHED_PROCESS,
        NULL,       // use parent environment block
        NULL,       // use parent starting directory
        &si,        // STARTUPINFO
        &pi)) {     // PROCESS_INFORMATION
        LogError(L"CreateProcess");
        goto errout;
    }

    free(commandline);
    commandline = NULL;

    // handle of child thread, unneeded
    CloseHandle(pi.hThread);

    // handle passed to child, unreference
    CloseHandle(inputReadSide);
    inputReadSide = INVALID_HANDLE_VALUE;

    // handle passed to child, unreference
    CloseHandle(outputWriteSide);
    outputWriteSide = INVALID_HANDLE_VALUE;

    ch->hStdin = inputWriteSide;
    ch->hStdout = outputReadSide;

    OutputDebugStringW(L"Process created\n");

    ch->hProcess = pi.hProcess;

    if (!RegisterWaitForSingleObject(&ch->waitHandle, ch->hProcess, ProcessReaper, ch, INFINITE, WT_EXECUTEONLYONCE))
        LogError(L"RegistryWaitForSingleObject");

    SetLastError(0);
    LogError(L"Process started?");

    return ch;

errout:
    if (ch != NULL) {
        if (ch->hStdin != INVALID_HANDLE_VALUE)
            CloseHandle(ch->hStdin);
        if (ch->hStdout != INVALID_HANDLE_VALUE)
            CloseHandle(ch->hStdin);
    }
    LocalFree(ch);

    if (inputWriteSide != INVALID_HANDLE_VALUE)
        CloseHandle(inputWriteSide);
    if (inputReadSide != INVALID_HANDLE_VALUE)
        CloseHandle(inputReadSide);
    if (outputWriteSide != INVALID_HANDLE_VALUE)
        CloseHandle(outputWriteSide);
    if (outputReadSide != INVALID_HANDLE_VALUE)
        CloseHandle(outputReadSide);

    if (commandline != NULL)
        free(commandline);

    return (NULL);
}

VOID CALLBACK
ProcessReaper(
    _In_ PVOID lpParameter,
    _In_ BOOLEAN timerOrWaitFired
)
{
    struct child_handles* ch = reinterpret_cast<struct child_handles*>(lpParameter);

    // Let the main thread do the work.
    PostMessage(ch->hWnd, WM_PROCESS_EXITED, 0, reinterpret_cast<LPARAM>(ch));
}

void
StopIt(struct child_handles* ch) {
    // All we really need to do is close the client handles
    CloseHandle(ch->hStdin);
    ch->hStdin = INVALID_HANDLE_VALUE;
    CloseHandle(ch->hStdout);
    ch->hStdout = INVALID_HANDLE_VALUE;
}

void
LogEvent(UINT message, WPARAM wParam, LPARAM lParam)
{
    LPWSTR s;

    if (hEventLog == NULL)
        return;

    LPCWSTR pInsertStrings[2] = { NULL, NULL };

    std::wstringstream ss;

    ss << "message: " << message << " wParam: " << wParam << " lParam: " << lParam;


    pInsertStrings[0] = L"winevent";
    s = (LPWSTR)LocalAlloc(LMEM_FIXED, (ss.str().length()+1) * sizeof(wchar_t));
    if (s == NULL)
        return;

    lstrcpyW(s, ss.str().c_str());
    pInsertStrings[1] = s;
    ReportEvent(hEventLog, EVENTLOG_INFORMATION_TYPE, GENERAL_CATEGORY, MSG_FUNCTION_ERROR, NULL, 2, 0, pInsertStrings, NULL);
    LocalFree(s);
}

void
LogError(LPCWSTR fun)
{
    if (hEventLog == NULL)
        return;

    LPCWSTR pInsertStrings[2] = { fun, NULL };

    DWORD buflen = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&pInsertStrings[1], 0, NULL);
    if (pInsertStrings[1] != NULL) {
        ReportEvent(hEventLog, EVENTLOG_ERROR_TYPE, GENERAL_CATEGORY, MSG_FUNCTION_ERROR, NULL, 2, 0, pInsertStrings, NULL);
        LocalFree((HLOCAL)pInsertStrings[1]);
    }
}

void
LogError(LPCWSTR fun, LRESULT lr)
{
    if (hEventLog == NULL)
        return;

    LPCWSTR pInsertStrings[2] = { fun, NULL };

    DWORD buflen = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, (DWORD)lr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&pInsertStrings[1], 0, NULL);
    if (pInsertStrings[1] != NULL) {
        ReportEvent(hEventLog, EVENTLOG_ERROR_TYPE, GENERAL_CATEGORY, MSG_FUNCTION_ERROR, NULL, 2, 0, pInsertStrings, NULL);
        LocalFree((HLOCAL)pInsertStrings[1]);
    }
}

std::wstring
makeCommand(HKEY subkey)
{
    try {
        return RegGetString(HKEY_LOCAL_MACHINE, REGISTRY_LOCATION, TEXT("Wrapper"));
    }
    catch (RegistryError& re) {
        LogError(L"GetRegistryWrapperString", re.ErrorCode());
        std::rethrow_exception(std::current_exception());
    }
}

LPWSTR
makeCommandLine(HKEY subkey)
{
    std::wstring r;
    LPWSTR s = NULL;

    try {
        r += TEXT("\"");
        r += RegGetString(HKEY_LOCAL_MACHINE, REGISTRY_LOCATION, TEXT("Wrapper"));
        r += TEXT("\"");

        r += TEXT(" \"");
        r += RegGetString(HKEY_LOCAL_MACHINE, REGISTRY_LOCATION, TEXT("Program"));
        r += TEXT("\"");

        r += TEXT(" --key \"");
        r += RegGetString(HKEY_LOCAL_MACHINE, REGISTRY_LOCATION, TEXT("Key"));
        r += TEXT("\"");

        r += TEXT(" --no-conf run");

        s = _wcsdup(r.c_str());
        if (s == NULL)
            throw RegistryError{ "Cannot read string from registry", E_OUTOFMEMORY };
    }
    catch (RegistryError& re) {
        LogError(L"makeCommandLine", re.ErrorCode());
        std::rethrow_exception(std::current_exception());
    }

    return s;
}

void
SetControlFromReg(HWND hCtrl, HKEY subkey, LPCWSTR regKey, HWND xx)
{
    DWORD keylen;
    LRESULT lr;

    SendMessage(xx, WM_SETTEXT, 0, (LPARAM)regKey);

    keylen = 0;
    lr = RegGetValueW(subkey, NULL, regKey, RRF_RT_REG_SZ, NULL, NULL, &keylen);
    if (lr != ERROR_SUCCESS) {
        LogError(TEXT("RegGetValueW"), lr);
        SendMessage(xx, WM_SETTEXT, 0, (LPARAM)TEXT("RegGetValue"));
        return;
    }

    LPWSTR val = (LPWSTR)LocalAlloc(LMEM_FIXED, keylen);
    if (val == NULL) {
        LogError(TEXT("LocalAlloc(regkey)"));
        SendMessage(xx, WM_SETTEXT, 0, (LPARAM)TEXT("LocalAlloc"));
        return;
    }

    if (RegGetValue(subkey, NULL, regKey, RRF_RT_REG_SZ, NULL, val, &keylen) != ERROR_SUCCESS) {
        LocalFree(val);
        LogError(TEXT("RegGetValue2"));
        SendMessage(xx, WM_SETTEXT, 0, (LPARAM)TEXT("RegGetValue2"));
        return;
    }

    SendMessage(hCtrl, WM_SETTEXT, 0, (LPARAM)val);
    SendMessage(xx, WM_SETTEXT, 0, (LPARAM)TEXT("done"));
    LocalFree(val);
}

void
SetRegFromControl(HWND hCtrl, HKEY subkey, LPCWSTR regKey)
{

    LRESULT keylen = SendMessage(hCtrl, WM_GETTEXT, 0, 0);

    keylen += 1;                // null termination
    keylen *= sizeof(wchar_t);  // unicode

    LPWSTR val = (LPWSTR)LocalAlloc(LMEM_FIXED, keylen);
    if (val == NULL) {
        // XXX error handling
        return;
    }

    SendMessage(hCtrl, WM_GETTEXT, keylen / sizeof(wchar_t), (LPARAM)val);

    if (RegSetValue(subkey, regKey, REG_SZ, val, 0) != ERROR_SUCCESS) {
        LocalFree(val);
        // XXX error handling
    }
}
