#include <windows.h>

#define DEMO_PATH        TEXT("\\Storage Card\\CerfDemo.exe")
#define LAUNCH_ATTEMPTS  10
#define RETRY_WAIT_MS    500

#define ERROR_TEXT \
    TEXT("CE Runtime Foundation Demo App is not found. ") \
    TEXT("Check your installation or upgrade the emulator.")

static BOOL LaunchDemo(void) {
    PROCESS_INFORMATION pi;
    if (GetFileAttributes(DEMO_PATH) == 0xFFFFFFFF)
        return FALSE;
    if (!CreateProcess(DEMO_PATH, NULL, NULL, NULL, FALSE, 0,
                       NULL, NULL, NULL, &pi))
        return FALSE;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return TRUE;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPTSTR cmd, int show) {
    int attempt;

    (void)hInst; (void)hPrev; (void)cmd; (void)show;

    for (attempt = 0; attempt < LAUNCH_ATTEMPTS; attempt++) {
        if (attempt) Sleep(RETRY_WAIT_MS);
        if (LaunchDemo()) return 0;
    }

    MessageBox(NULL, ERROR_TEXT, TEXT("CE Runtime Foundation"),
               MB_OK | MB_ICONERROR);
    return 1;
}
