#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show)
{
    (void)instance;
    (void)previous;
    (void)command;
    (void)show;
    return GetModuleHandleW(NULL) == NULL;
}
