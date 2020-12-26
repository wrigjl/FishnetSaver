
#include <Windows.h>
#include <ScrnSave.h>
#include <objbase.h>
#include <shobjidl.h>
#include <shobjidl_core.h>
#include <Shlwapi.h>
#include "messages.h"
#include "resource.h"
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>

#define PROVIDER_NAME L"FishnetProvider"
#define REGISTRY_LOCATION L"Software\\FishnetSaver"

void LogEvent(UINT message, WPARAM wParam, LPARAM lParam);
void LogError(LPCWSTR fun);
void LogError(LPCWSTR fun, LRESULT lr);

LPWSTR makeCommandLine(HKEY subkey);
LPWSTR makeCommand(HKEY subkey);
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
    LPWSTR command = NULL;
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

    ch = (struct child_handles *)LocalAlloc(LPTR, sizeof(*ch));
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

    command = makeCommand(subkey);
    if (command == NULL)
        goto errout;

    commandline = makeCommandLine(subkey);
    if (command == NULL)
        goto errout;

    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput = inputReadSide;
    si.hStdOutput = outputWriteSide;
    si.hStdError = outputWriteSide;

    if (!CreateProcess(command,
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

    LocalFree(command);
    LocalFree(commandline);
    commandline = NULL;
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

    LocalFree(commandline);
    LocalFree(command);

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

LPWSTR
makeCommand(HKEY subkey)
{
#if 1
    LPWSTR val = NULL;
    std::wstringstream ss;

    ss << TEXT("C:\\Users\\jason\\source\\repos\\FishnetSaver\\x64\\Release\\FishWrapper.exe");
    val = _wcsdup(ss.str().c_str());
    return val;
#else
    DWORD keylen;
    LPWSTR val = NULL;
    std::wstringstream ss;

    keylen = 0;
    if (RegGetValueW(subkey, NULL, L"Program", RRF_RT_REG_SZ, NULL, NULL, &keylen) != ERROR_SUCCESS) {
        // XXX error handling
        return NULL;
    }

    val = (LPWSTR)LocalAlloc(LMEM_FIXED, keylen);
    if (val == NULL) {
        // XXX error handling
        return NULL;
    }

    if (RegGetValue(subkey, NULL, L"Program", RRF_RT_REG_SZ, NULL, val, &keylen) != ERROR_SUCCESS) {
        LocalFree(val);
        // XXX error handling
        return NULL;
    }

    return val;
#endif
}

LPWSTR
makeCommandLine(HKEY subkey)
{
#if 1
    LPWSTR val = NULL;
    std::wstringstream ss;

    ss << "\"";
    ss << TEXT("C:\\Users\\jason\\source\\repos\\FishnetSaver\\x64\\Release\\FishWrapper.exe");
    ss << "\"";

    ss << " \"";
    ss << TEXT("C:\\Users\\jason\\source\\repos\\FishnetSaver\\x64\\Release\\DummyFish.exe");
    ss << "\"";

    ss << " --key \"" << TEXT("mykey") << '"';
    ss << " --no-conf run";

    val = _wcsdup(ss.str().c_str());
    return val;
#else
    DWORD keylen = 0;
    LPWSTR val = NULL;
    std::wstringstream ss;

    if (RegGetValueW(subkey, NULL, L"Program", RRF_RT_REG_SZ, NULL, NULL, &keylen) != ERROR_SUCCESS) {
        // XXX error handling
        return NULL;
    }


    val = (LPWSTR)LocalAlloc(LMEM_FIXED, keylen);
    if (val == NULL) {
        // XXX error handling
        return NULL;
    }

    if (RegGetValue(subkey, NULL, L"Program", RRF_RT_REG_SZ, NULL, val, &keylen) != ERROR_SUCCESS) {
        LocalFree(val);
        // XXX error handling
        return NULL;
    }

    ss << "\"" << val << "\" --key \"";
    LocalFree(val);

    keylen = 0;
    if (RegGetValueW(subkey, NULL, L"Key", RRF_RT_REG_SZ, NULL, NULL, &keylen) != ERROR_SUCCESS) {
        // XXX error handling
        return NULL;
    }

    val = (LPWSTR)LocalAlloc(LMEM_FIXED, keylen);
    if (val == NULL) {
        // XXX error handling
        return NULL;
    }

    if (RegGetValue(subkey, NULL, L"Key", RRF_RT_REG_SZ, NULL, val, &keylen) != ERROR_SUCCESS) {
        LocalFree(val);
        // XXX error handling
        return NULL;
    }

    ss << val << "\" --no-conf run";
    LocalFree(val);

    LPWSTR s = (LPWSTR)LocalAlloc(LMEM_FIXED, (ss.str().length() + 1) * sizeof(wchar_t));
    if (s != NULL)
        lstrcpyW(s, ss.str().c_str());
    return s;
#endif
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

#if 0
// XXX code for file dialog chooser from:
// https://docs.microsoft.com/en-us/previous-versions/windows/desktop/legacy/bb776913(v=vs.85)

class CDialogEventHandler : public IFileDialogEvents,
    public IFileDialogControlEvents
{
public:
    // IUnknown methods
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        static const QITAB qit[] = {
            QITABENT(CDialogEventHandler, IFileDialogEvents),
            QITABENT(CDialogEventHandler, IFileDialogControlEvents),
            { 0 },
        };
        return QISearch(this, qit, riid, ppv);
    }

    IFACEMETHODIMP_(ULONG) AddRef()
    {
        return InterlockedIncrement(&_cRef);
    }

    IFACEMETHODIMP_(ULONG) Release()
    {
        long cRef = InterlockedDecrement(&_cRef);
        if (!cRef)
            delete this;
        return cRef;
    }

    // IFileDialogEvents methods
    IFACEMETHODIMP OnFileOk(IFileDialog*) { return S_OK; };
    IFACEMETHODIMP OnFolderChange(IFileDialog*) { return S_OK; };
    IFACEMETHODIMP OnFolderChanging(IFileDialog*, IShellItem*) { return S_OK; };
    IFACEMETHODIMP OnHelp(IFileDialog*) { return S_OK; };
    IFACEMETHODIMP OnSelectionChange(IFileDialog*) { return S_OK; };
    IFACEMETHODIMP OnShareViolation(IFileDialog*, IShellItem*, FDE_SHAREVIOLATION_RESPONSE*) { return S_OK; };
    IFACEMETHODIMP OnTypeChange(IFileDialog* pfd);
    IFACEMETHODIMP OnOverwrite(IFileDialog*, IShellItem*, FDE_OVERWRITE_RESPONSE*) { return S_OK; };

    // IFileDialogControlEvents methods
    IFACEMETHODIMP OnItemSelected(IFileDialogCustomize* pfdc, DWORD dwIDCtl, DWORD dwIDItem);
    IFACEMETHODIMP OnButtonClicked(IFileDialogCustomize*, DWORD) { return S_OK; };
    IFACEMETHODIMP OnCheckButtonToggled(IFileDialogCustomize*, DWORD, BOOL) { return S_OK; };
    IFACEMETHODIMP OnControlActivating(IFileDialogCustomize*, DWORD) { return S_OK; };

    CDialogEventHandler() : _cRef(1) { };
private:
    ~CDialogEventHandler() { };
    long _cRef;
};

HRESULT CDialogEventHandler_CreateInstance(REFIID riid, void** ppv)
{
    *ppv = NULL;
    CDialogEventHandler* pDialogEventHandler = new (std::nothrow) CDialogEventHandler();
    HRESULT hr = pDialogEventHandler ? S_OK : E_OUTOFMEMORY;
    if (SUCCEEDED(hr))
    {
        hr = pDialogEventHandler->QueryInterface(riid, ppv);
        pDialogEventHandler->Release();
    }
    return hr;
}

HRESULT CDialogEventHandler::OnItemSelected(IFileDialogCustomize* pfdc, DWORD dwIDCtl, DWORD dwIDItem)
{
    IFileDialog* pfd = NULL;
    HRESULT hr = pfdc->QueryInterface(&pfd);
    if (SUCCEEDED(hr))
        pfd->Release();
    return hr;
}

HRESULT CDialogEventHandler::OnTypeChange(IFileDialog* pfd)
{
    IFileSaveDialog* pfsd;
    HRESULT hr = pfd->QueryInterface(&pfsd);
    if (SUCCEEDED(hr))
        pfsd->Release();
    return hr;
}

HRESULT BasicFileOpen()
{
    const COMDLG_FILTERSPEC c_rgSaveTypes[] = { {L"Executable Programs (*.exe)", L"*.exe"} };
#define INDEX_EXES 1
    // CoCreate the File Open Dialog object.
    IFileDialog* pfd = NULL;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog,
        NULL,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pfd));
    if (SUCCEEDED(hr))
    {
        // Create an event handling object, and hook it up to the dialog.
        IFileDialogEvents* pfde = NULL;
        hr = CDialogEventHandler_CreateInstance(IID_PPV_ARGS(&pfde));
        if (SUCCEEDED(hr))
        {
            // Hook up the event handler.
            DWORD dwCookie;
            hr = pfd->Advise(pfde, &dwCookie);
            if (SUCCEEDED(hr))
            {
                // Set the options on the dialog.
                DWORD dwFlags;

                // Before setting, always get the options first in order 
                // not to override existing options.
                hr = pfd->GetOptions(&dwFlags);
                if (SUCCEEDED(hr))
                {
                    // In this case, get shell items only for file system items.
                    hr = pfd->SetOptions(dwFlags | FOS_FORCEFILESYSTEM);
                    if (SUCCEEDED(hr))
                    {
                        // Set the file types to display only. 
                        // Notice that this is a 1-based array.
                        hr = pfd->SetFileTypes(ARRAYSIZE(c_rgSaveTypes), c_rgSaveTypes);
                        if (SUCCEEDED(hr))
                        {
                            // Set the selected file type index to Word Docs for this example.
                            hr = pfd->SetFileTypeIndex(INDEX_EXES);
                            if (SUCCEEDED(hr))
                            {
                                // Set the default extension to be ".doc" file.
                                hr = pfd->SetDefaultExtension(L"exe");
                                if (SUCCEEDED(hr))
                                {
                                    // Show the dialog
                                    hr = pfd->Show(NULL);
                                    if (SUCCEEDED(hr))
                                    {
                                        // Obtain the result once the user clicks 
                                        // the 'Open' button.
                                        // The result is an IShellItem object.
                                        IShellItem* psiResult;
                                        hr = pfd->GetResult(&psiResult);
                                        if (SUCCEEDED(hr))
                                        {
                                            // We are just going to print out the 
                                            // name of the file for sample sake.
                                            PWSTR pszFilePath = NULL;
                                            hr = psiResult->GetDisplayName(SIGDN_FILESYSPATH,
                                                &pszFilePath);
                                            if (SUCCEEDED(hr))
                                            {
                                                TaskDialog(NULL,
                                                    NULL,
                                                    L"CommonFileDialogApp",
                                                    pszFilePath,
                                                    NULL,
                                                    TDCBF_OK_BUTTON,
                                                    TD_INFORMATION_ICON,
                                                    NULL);
                                                CoTaskMemFree(pszFilePath);
                                            }
                                            psiResult->Release();
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                // Unhook the event handler.
                pfd->Unadvise(dwCookie);
            }
            pfde->Release();
        }
        pfd->Release();
    }
    return hr;
}
#endif
