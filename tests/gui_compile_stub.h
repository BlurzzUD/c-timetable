#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>
int gui_run(HINSTANCE instance,int show,const wchar_t *base_dir);
int gui_self_test(HINSTANCE instance,const wchar_t *base_dir,wchar_t *error,size_t error_cap);
