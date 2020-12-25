// FishWrapper.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
// This is more complex than it needs to be thanks to the Windows screensaver API.
// If a screensaver program pops up a window, the screensaver stops saving. But,
// we need to allocate a console for running fishnet because only processes with
// consoles can be send Ctrl-C events (which is the "nice" way of signaling fishnet
// to clean up and exit).
//
// So, enter the FishWrapper (this program) which is used to execute fishnet. This
// program allocates a pseudo console for running fishnet, runs it, and ferries
// the output from fishnet's stdout back to its own stdout. It allows fishnet
// to run until it senses EOF on its stdin (i.e. the screensaver
// has exited, died, etc.). It attaches to the fishnet pseudo console, sends it
// Ctrl-C and waits for up to N minutes for it to exit. If it hasn't exited at
// that point, we use the TerminateProcess() hammer as a last resort.
//
// The idea for this program is be created by the screensaver with DETACH_PROCESS
// or CREATE_NO_WINDOW.
//

#include <Windows.h>
#include <iostream>
#include <sstream>
#include <string>

// how long to wait after we have asked the child to exit before we kill it (milliseconds)
#define MAX_WAIT (5 * 60 * 1000)

void ShowError(LPCWSTR fun);
void ShowError(LPCWSTR fun, HRESULT);
void ShowError(LPCWSTR fun, LPCWSTR msg);

LPWSTR MakeCommand(const wchar_t*);
LPWSTR MakeCommandLine(int argc, wchar_t* argv[]);
bool GetSomeStdin(HANDLE hStdin, DWORD pid, HANDLE pProcess);
HRESULT PrepareStartupInformation(HPCON hpc, STARTUPINFOEX* psi);
void DiscardPipe(PHANDLE phPipe, HANDLE hOut);

int
wmain(int argc, wchar_t* argv[], wchar_t* envp[])
{
    HANDLE inputReadSide = INVALID_HANDLE_VALUE;
    HANDLE outputWriteSide = INVALID_HANDLE_VALUE;
    HANDLE outputReadSide = INVALID_HANDLE_VALUE;
    HANDLE inputWriteSide = INVALID_HANDLE_VALUE;
    HANDLE hStdin = INVALID_HANDLE_VALUE;
    HANDLE hStdout = INVALID_HANDLE_VALUE;
    HPCON hPC = INVALID_HANDLE_VALUE;
    HRESULT hr;
    LPWSTR command = NULL, commandLine = NULL;
    STARTUPINFOEX si;
    PROCESS_INFORMATION pi;
    int rc = 0;
    COORD size = { 80, 80 };
    SECURITY_ATTRIBUTES saAttr = { 0 };
    DWORD d = 0;

    saAttr.nLength = sizeof(saAttr);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    ZeroMemory(&saAttr, sizeof(saAttr));
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));

    pi.hProcess = INVALID_HANDLE_VALUE;
    pi.hThread = INVALID_HANDLE_VALUE;

    if (argc < 2) {
        std::wcerr << "usage: " << argv[0] << " prog [args]" << std::endl;
        return 2;
    }

    hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hStdout == INVALID_HANDLE_VALUE) {
        ShowError(TEXT("GetStdHandle(stdout)"));
        rc = 1;
        goto out;
    }

    d = PIPE_READMODE_BYTE | PIPE_NOWAIT;
    if (SetNamedPipeHandleState(hStdout, &d, NULL, NULL)) {
        ShowError(TEXT("SetNamedPipeHandleState(stdout)"));
        rc = 1;
        goto out;
    }

    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    if (hStdin == INVALID_HANDLE_VALUE) {
        ShowError(TEXT("GetStdHandle(stdin)"));
        rc = 1;
        goto out;
    }

    saAttr.nLength = sizeof(saAttr);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&inputReadSide, &inputWriteSide, &saAttr, 0)) {
        ShowError(TEXT("CreatePipe"));
        rc = 1;
        goto out;
    }

    if (!SetHandleInformation(inputWriteSide, HANDLE_FLAG_INHERIT, 0)) {
        // handle should not be inherited
        ShowError(TEXT("SetHandleInformation"));
        rc = 1;
        goto out;
    }

    if (!CreatePipe(&outputReadSide, &outputWriteSide, &saAttr, 0)) {
        ShowError(TEXT("CreatePipe"));
        rc = 1;
        goto out;
    }

    if (!SetHandleInformation(outputReadSide, HANDLE_FLAG_INHERIT, 0)) {
        // handle should not be inherited
        ShowError(TEXT("SetHandleInformation"));
        rc = 1;
        goto out;
    }

    hr = CreatePseudoConsole(size, inputReadSide, outputWriteSide, 0, &hPC);
    if (FAILED(hr)) {
        ShowError(TEXT("CreatePseudoConsole"), hr);
        rc = 1;
        goto out;
    }

    command = MakeCommand(argv[1]);
    commandLine = MakeCommandLine(argc - 1, &argv[1]);

    hr = PrepareStartupInformation(hPC, &si);
    if (FAILED(hr)) {
        ShowError(TEXT("CreatePseudoConsole"), hr);
        rc = 1;
        goto out;
    }

    si.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = inputReadSide;
    si.StartupInfo.hStdOutput = outputWriteSide;
    si.StartupInfo.hStdError = outputWriteSide;

    if (!CreateProcess(command, commandLine,
        NULL, NULL, TRUE, EXTENDED_STARTUPINFO_PRESENT,
        NULL, NULL, &si.StartupInfo, &pi)) {
        ShowError(TEXT("CreateProcess"));
        rc = 1;
        goto out;
    }

    // Close the handles we passed to child
    CloseHandle(inputReadSide);
    inputReadSide = INVALID_HANDLE_VALUE;
    CloseHandle(outputWriteSide);
    outputWriteSide = INVALID_HANDLE_VALUE;

    for (bool done = false; !done;) {
        HANDLE handles[3];
        DWORD wait, nHandles = 2;

        handles[0] = pi.hProcess;
        handles[1] = hStdin;
        if (outputReadSide != INVALID_HANDLE_VALUE) {
            handles[2] = outputReadSide;
            nHandles++;
        }

        wait = WaitForMultipleObjects(nHandles, handles, FALSE, INFINITE);
        switch (wait) {
        case WAIT_OBJECT_0 + 0:
            // process exited
            std::wcout << TEXT("process exited") << std::endl;
            done = true;
            break;
        case WAIT_OBJECT_0 + 1:
            // something on stdin
            done = GetSomeStdin(hStdin, pi.dwProcessId, pi.hProcess);
            break;
        case WAIT_OBJECT_0 + 2:
            DiscardPipe(&handles[2], hStdout);
            break;
        case WAIT_FAILED:
            ShowError(TEXT("WaitForMultipleObjects"));
            rc = 1;
            goto out;
        }
    }

    // If we're here, the process has exited (probably)

out:
    free(command);
    free(commandLine);
    LocalFree(si.lpAttributeList);

    if (pi.hProcess != INVALID_HANDLE_VALUE)
        CloseHandle(pi.hProcess);
    if (pi.hThread != INVALID_HANDLE_VALUE)
        CloseHandle(pi.hThread);

    if (inputReadSide != INVALID_HANDLE_VALUE)
        CloseHandle(inputReadSide);
    if (outputWriteSide != INVALID_HANDLE_VALUE)
        CloseHandle(outputWriteSide);
    if (outputReadSide != INVALID_HANDLE_VALUE)
        CloseHandle(outputReadSide);
    if (inputWriteSide != INVALID_HANDLE_VALUE)
        CloseHandle(inputWriteSide);

    return rc;
}

LPWSTR
MakeCommand(const wchar_t* arg)
{
    return _wcsdup(arg);
}

LPWSTR
MakeCommandLine(int argc, wchar_t* argv[]) {
    std::wstringstream ss;

    for (int i = 0; i < argc; i++) {
        if (i != 0)
            ss << " ";
        ss << "\"" << argv[i] << "\"";
    }
    if (ss.str().length() == 0)
        return NULL;
    return _wcsdup(ss.str().c_str());
}

void
ShowError(LPCWSTR fun)
{
    LPCWSTR pInsertStrings[2] = { fun, NULL };

    DWORD buflen = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&pInsertStrings[1], 0, NULL);
    if (pInsertStrings[1] != NULL) {
        std::wcerr << pInsertStrings[0] << ": " << pInsertStrings[1] << std::endl;
        LocalFree((HLOCAL)pInsertStrings[1]);
    }
}

void
ShowError(LPCWSTR fun, HRESULT lr)
{
    LPCWSTR pInsertStrings[2] = { fun, NULL };

    DWORD buflen = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, lr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&pInsertStrings[1], 0, NULL);
    std::wcerr << pInsertStrings[0] << ": " << pInsertStrings[1] << std::endl;

    if (pInsertStrings[1] != NULL) {
        std::wcerr << pInsertStrings[0] << ": " << pInsertStrings[1] << std::endl;
        LocalFree((HLOCAL)pInsertStrings[1]);
    }
}

void
ShowError(LPCWSTR fun, LPCWSTR msg)
{
    std::wcerr << fun << ": " << msg << std::endl;
}

HRESULT
PrepareStartupInformation(HPCON hpc, STARTUPINFOEX* psi)
{
    // Prepare Startup Information structure
    STARTUPINFOEX si;
    ZeroMemory(&si, sizeof(si));
    si.StartupInfo.cb = sizeof(STARTUPINFOEX);
    HRESULT hr = S_OK;

    // Discover the size required for the list
    SIZE_T bytesRequired = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &bytesRequired);

    // Allocate memory to represent the list
    si.lpAttributeList = (PPROC_THREAD_ATTRIBUTE_LIST)LocalAlloc(LPTR, bytesRequired);
    if (!si.lpAttributeList) {
        hr = E_OUTOFMEMORY;
        goto errout;
    }

    // Initialize the list memory location
    if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &bytesRequired))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        goto errout;
    }

    // Set the pseudoconsole information into the list
    if (!UpdateProcThreadAttribute(si.lpAttributeList,
        0,
        PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
        hpc,
        sizeof(hpc),
        NULL,
        NULL))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        goto errout;
    }

    *psi = si;
    return S_OK;

errout:
    LocalFree(si.lpAttributeList);
    return hr;
}

bool
GetSomeStdin(HANDLE hStdin, DWORD pid, HANDLE hProcess)
{
    TCHAR buf[80];
    BOOL bResult;
    DWORD dwCharsRead = 0;
    CONSOLE_READCONSOLE_CONTROL inputControl;

    ZeroMemory(&inputControl, sizeof(inputControl));
    inputControl.nLength = sizeof(inputControl);
    inputControl.dwCtrlWakeupMask |= 1 << ('D' - '@');
    inputControl.dwCtrlWakeupMask |= 1 << ('Z' - '@');
    inputControl.dwControlKeyState |= LEFT_CTRL_PRESSED;

    bResult = ReadConsole(hStdin, buf, sizeof(buf) / sizeof(buf[0]), &dwCharsRead, &inputControl);
    if (!bResult) {
        ShowError(TEXT("ReadConsole"));
        return true;
    }

    std::wcout << TEXT("Eof detected") << std::endl;

    OutputDebugString(TEXT("Freeing console\n"));
    if (GetConsoleWindow() != NULL && !FreeConsole())
        ShowError(TEXT("FreeConsole"));

    OutputDebugString(TEXT("attaching console\n"));
    if (!AttachConsole(pid))
        ShowError(TEXT("AttachConsole"));

    OutputDebugString(TEXT("sethandler\n"));
    if (!SetConsoleCtrlHandler(NULL, TRUE))
        ShowError(TEXT("SetConsoleCtrlHandler"));

    OutputDebugString(TEXT("generate\n"));
    if (!GenerateConsoleCtrlEvent(CTRL_C_EVENT, pid))
        ShowError(TEXT("GenerateConsoleCtrlEvent"));

    OutputDebugString(TEXT("Freeing console 2"));
    if (!FreeConsole())
        ShowError(TEXT("FreeConsole(sub)"));

    std::wcout << "Waiting for process to end..." << std::endl;

    DWORD wait = WaitForSingleObject(hProcess, MAX_WAIT);

    if (wait == WAIT_OBJECT_0) {
        std::cout << "done" << std::endl;
    }
    else if (wait == WAIT_FAILED) {
        ShowError(TEXT("WaitForSingleObject(proc)"));
    }
    else if (wait == WAIT_TIMEOUT) {
        std::wcout << "We tried being nice..." << std::endl;
        if (!TerminateProcess(hProcess, 1)) {
            ShowError(TEXT("TerminateProcess"));
            return true;
        }

        wait = WaitForSingleObject(hProcess, INFINITE);
        if (wait == WAIT_OBJECT_0) {
            // good, it exited
            return true;
        }
        if (wait == WAIT_FAILED) {
            ShowError(TEXT("WaitForSingleObject(term)"));
            return true;
        }
    }
    else {
        std::wcout << "Wait: " << wait << std::endl;
    }

    return true;
}

void
DiscardPipe(PHANDLE phPipe, HANDLE hOut)
{
    BOOL bStatus;
    DWORD nRead = 0, nWrite = 0;
    BYTE buf[100];

    bStatus = ReadFile(*phPipe, buf, sizeof(buf), &nRead, NULL);
    if (bStatus && nRead == 0) {
        // EOF
        CloseHandle(*phPipe);
        *phPipe = INVALID_HANDLE_VALUE;
        return;
    }

    WriteFile(hOut, buf, nRead, &nWrite, NULL);
}
