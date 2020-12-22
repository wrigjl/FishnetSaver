// DummyFish.cpp
// This is a stand-in for the actual fishnet executable. The idea is to have
// a little I/O w/out actually interacting with lichess
//

#include <iostream>
#include <Windows.h>
#include <synchapi.h>

#define SIGNAL_WAIT_SECONDS 15
#define MESSAGE_WAIT_SECONDS 2

HANDLE ctrlEventHandle;

BOOL WINAPI
CtrlHandler(_In_ DWORD dwCtrlType) {
    SetEvent(ctrlEventHandle);
    return TRUE;
}

void
DebugError(LPCWSTR name) {
    std::wcerr << name << L": X";
    DWORD error = GetLastError();
    if (error == 0) {
        std::wcerr << " no error" << std::endl;
        return;
    }
    LPWSTR lpMsgBuf = NULL;
    DWORD bufLen = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&lpMsgBuf, 0, NULL);
    if (bufLen) {
        std::wcerr << lpMsgBuf << std::endl;
        LocalFree(lpMsgBuf);
    }
    else
        std::wcerr << "FormatMessage: " << bufLen << " error: " << error << std::endl;
}

int
wmain(int argc, wchar_t *argv[], wchar_t *envp[])
{
    DWORD wait;
    int ntimer = 0;

    for (int i = 0; argv[i]; i++)
        std::wcout << L"Arg(" << i << L"): " << argv[i] << std::endl;

    ctrlEventHandle = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (ctrlEventHandle == NULL) {
        DebugError(L"CreateEvent");
        return 1;
    }

    if (!SetConsoleCtrlHandler(CtrlHandler, TRUE)) {
        DebugError(L"SetConsoleCtrlHandler");
        return 1;
    }

    // Wait for Control event
    for (;;) {
        std::wcout << L"Message: " << ntimer++ << std::endl;

        wait = WaitForSingleObject(ctrlEventHandle, MESSAGE_WAIT_SECONDS * 1000);
        if (wait == WAIT_TIMEOUT)
            continue;

        if (wait == WAIT_OBJECT_0)
            break;

        if (wait == WAIT_ABANDONED) {
            std::wcerr << "WaitForSingleObject: abandoned" << std::endl;
            return 1;
        } else if (wait == WAIT_FAILED) {
            DebugError(L"WaitForSingleObject");
            return 1;
        } else {
            std::wcerr << L"WaitForSingleObject: unhandled: " << wait << std::endl;
            return 1;
        }
    }

    std::wcout << "Control event received, exit in " << SIGNAL_WAIT_SECONDS << " seconds." << std::endl;

    // restore the normal Ctrl-C handler
    if (!SetConsoleCtrlHandler(NULL, FALSE)) {
        DebugError(L"SetConsoleCtrlHandler");
        return 1;
    }

    // Reset the event
    if (!ResetEvent(ctrlEventHandle)) {
        DebugError(L"ResetHandle");
        return 1;
    }

    // now wait for the handle (which should not be signal-able)
    wait = WaitForSingleObject(ctrlEventHandle, SIGNAL_WAIT_SECONDS * 1000);
    if (wait == WAIT_OBJECT_0) {
        std::wcerr << "WaitForSingleObject: event wasn't supposed to be signaled" << std::endl;
        return 1;
    }
    if (wait == WAIT_ABANDONED) {
        std::wcerr << "WaitForSingleObject: abandoned" << std::endl;
        return 1;
    }
    if (wait == WAIT_FAILED) {
        DebugError(L"WaitForSingleObject");
        return 1;
    }
    if (wait != WAIT_TIMEOUT) {
        std::wcerr << L"WaitForSingleObject: unhandled: " << wait << std::endl;
        return 1;
    }

    if (!CloseHandle(ctrlEventHandle)) {
        DebugError(L"CloseHandle");
        return 1;
    }

    std::wcout << L"All done." << std::endl;
    return 0;
}
