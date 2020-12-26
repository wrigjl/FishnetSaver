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
#include "messages.h"

// how long to wait after we have asked the child to exit before we kill it (milliseconds)
#define MAX_WAIT (5 * 60 * 1000)

void ShowError(LPCWSTR fun);
void ShowError(LPCWSTR fun, HRESULT);
void ShowError(LPCWSTR fun, LPCWSTR msg);

void LogError(LPCWSTR fun);
void LogError(LPCWSTR fun, LPCWSTR str);
void LogError(LPCWSTR fun, LRESULT errcode);
void LogInfo(LPCWSTR msg);

LPWSTR MakeCommand(const wchar_t*);
LPWSTR MakeCommandLine(int argc, wchar_t* argv[]);
bool GetSomeStdin(HANDLE hStdin, DWORD pid, HANDLE pProcess);
HRESULT PrepareStartupInformation(HPCON hpc, STARTUPINFOEX* psi);
void DiscardPipe(PHANDLE phPipe, HANDLE hOut);

#define PROVIDER_NAME L"FishnetProvider"
HANDLE ghEventLog = INVALID_HANDLE_VALUE;

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

    if (ghEventLog == NULL)
        RegisterEventSource(NULL, PROVIDER_NAME);

    saAttr.nLength = sizeof(saAttr);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    ZeroMemory(&saAttr, sizeof(saAttr));
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));

    pi.hProcess = INVALID_HANDLE_VALUE;
    pi.hThread = INVALID_HANDLE_VALUE;

    ghEventLog = RegisterEventSource(NULL, PROVIDER_NAME);
    if (ghEventLog == NULL) {
        LogError(TEXT("RegisterEventSource"));
        ghEventLog = INVALID_HANDLE_VALUE;
    }

    if (argc < 2) {
        LogError(TEXT("Usage: "), TEXT("args"));
        return 2;
    }

    hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hStdout == INVALID_HANDLE_VALUE) {
        LogError(TEXT("GetStdHandle(stdout)"));
        rc = 1;
        goto out;
    }

    d = PIPE_READMODE_BYTE | PIPE_NOWAIT;
    if (!SetNamedPipeHandleState(hStdout, &d, NULL, NULL)) {
        LogError(TEXT("SetNamedPipeHandleState(stdout)"));
        rc = 1;
        goto out;
    }

    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    if (hStdin == INVALID_HANDLE_VALUE) {
        LogError(TEXT("GetStdHandle(stdin)"));
        rc = 1;
        goto out;
    }

    saAttr.nLength = sizeof(saAttr);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&inputReadSide, &inputWriteSide, &saAttr, 0)) {
        LogError(TEXT("CreatePipe"));
        rc = 1;
        goto out;
    }

    if (!SetHandleInformation(inputWriteSide, HANDLE_FLAG_INHERIT, 0)) {
        // handle should not be inherited
        LogError(TEXT("SetHandleInformation"));
        rc = 1;
        goto out;
    }

    if (!CreatePipe(&outputReadSide, &outputWriteSide, &saAttr, 0)) {
        LogError(TEXT("CreatePipe"));
        rc = 1;
        goto out;
    }

    if (!SetHandleInformation(outputReadSide, HANDLE_FLAG_INHERIT, 0)) {
        // handle should not be inherited
        LogError(TEXT("SetHandleInformation"));
        rc = 1;
        goto out;
    }

    hr = CreatePseudoConsole(size, inputReadSide, outputWriteSide, 0, &hPC);
    if (FAILED(hr)) {
        LogError(TEXT("CreatePseudoConsole"), hr);
        rc = 1;
        goto out;
    }

    command = MakeCommand(argv[1]);
    commandLine = MakeCommandLine(argc - 1, &argv[1]);

    hr = PrepareStartupInformation(hPC, &si);
    if (FAILED(hr)) {
        LogError(TEXT("CreatePseudoConsole"), hr);
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
        LogError(TEXT("CreateProcess"));
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
            LogInfo(TEXT("process exited"));
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
            LogError(TEXT("WaitForMultipleObjects"));
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
    DWORD dwBytesRead = 0;

    bResult = ReadFile(hStdin, buf, sizeof(buf), &dwBytesRead, NULL);
    if (!bResult) {
        if (GetLastError() == ERROR_BROKEN_PIPE) {
            LogInfo(TEXT("Pipe broken"));
            goto out;
        }
        LogError(TEXT("ReadConsole"));
        return true;
    }

out:
    LogInfo(TEXT("EOF detected"));

    OutputDebugString(TEXT("Freeing console\n"));
    if (GetConsoleWindow() != NULL && !FreeConsole())
        LogError(TEXT("FreeConsole"));

    OutputDebugString(TEXT("attaching console\n"));
    if (!AttachConsole(pid))
        LogError(TEXT("AttachConsole"));

    OutputDebugString(TEXT("sethandler\n"));
    if (!SetConsoleCtrlHandler(NULL, TRUE))
        LogError(TEXT("SetConsoleCtrlHandler"));

    OutputDebugString(TEXT("generate\n"));
    if (!GenerateConsoleCtrlEvent(CTRL_C_EVENT, pid))
        LogError(TEXT("GenerateConsoleCtrlEvent"));

    OutputDebugString(TEXT("Freeing console 2"));
    if (!FreeConsole())
        LogError(TEXT("FreeConsole(sub)"));

    LogInfo(TEXT("Waiting for process to end..."));

    DWORD wait = WaitForSingleObject(hProcess, MAX_WAIT);

    if (wait == WAIT_OBJECT_0) {
        LogInfo(TEXT("Waiting done"));
    }
    else if (wait == WAIT_FAILED) {
        LogError(TEXT("WaitForSingleObject(proc)"));
    }
    else if (wait == WAIT_TIMEOUT) {
        LogInfo(TEXT("We tried being nice..."));
        if (!TerminateProcess(hProcess, 1)) {
            LogError(TEXT("TerminateProcess"));
            return true;
        }

        wait = WaitForSingleObject(hProcess, INFINITE);
        if (wait == WAIT_OBJECT_0) {
            // good, it exited
            LogInfo(TEXT("Exit after terminate..."));
            return true;
        }
        if (wait == WAIT_FAILED) {
            LogError(TEXT("WaitForSingleObject(term)"));
            return true;
        }
    }
    else {
        LogError(TEXT("Weird return value from WFMO"));
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


void
LogInfo(LPCWSTR msg)
{
    if (ghEventLog == INVALID_HANDLE_VALUE)
        return;

    LPCWSTR pInsertStrings[1] = { msg };
    ReportEvent(ghEventLog, EVENTLOG_INFORMATION_TYPE, GENERAL_CATEGORY, MSG_FUNCTION_GENERIC, NULL, 1, 0, pInsertStrings, NULL);
}

void
LogError(LPCWSTR fun)
{
    if (ghEventLog == INVALID_HANDLE_VALUE)
        return;

    LPCWSTR pInsertStrings[2] = { fun, NULL };

    DWORD buflen = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&pInsertStrings[1], 0, NULL);
    if (pInsertStrings[1] != NULL) {
        ReportEvent(ghEventLog, EVENTLOG_ERROR_TYPE, GENERAL_CATEGORY, MSG_FUNCTION_ERROR, NULL, 2, 0, pInsertStrings, NULL);
        LocalFree((HLOCAL)pInsertStrings[1]);
    }
}

void
LogError(LPCWSTR fun, LRESULT lr)
{
    if (ghEventLog == INVALID_HANDLE_VALUE)
        return;

    LPCWSTR pInsertStrings[2] = { fun, NULL };

    DWORD buflen = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, (DWORD)lr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&pInsertStrings[1], 0, NULL);
    if (pInsertStrings[1] != NULL) {
        ReportEvent(ghEventLog, EVENTLOG_ERROR_TYPE, GENERAL_CATEGORY, MSG_FUNCTION_ERROR, NULL, 2, 0, pInsertStrings, NULL);
        LocalFree((HLOCAL)pInsertStrings[1]);
    }
}

void LogError(LPCWSTR fun, LPCWSTR str)
{
    if (ghEventLog == INVALID_HANDLE_VALUE)
        return;

    LPCWSTR pInsertStrings[2] = { fun, str };
    ReportEvent(ghEventLog, EVENTLOG_ERROR_TYPE, GENERAL_CATEGORY, MSG_FUNCTION_ERROR, NULL, 2, 0, pInsertStrings, NULL);
}
