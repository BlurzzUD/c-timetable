#ifndef CT_GUI_H
#define CT_GUI_H
#include <windows.h>
#include <stddef.h>
int gui_run(HINSTANCE,int,const wchar_t *);
int gui_self_test(HINSTANCE,const wchar_t *,wchar_t *,size_t);
int gui_refresh_test(HINSTANCE,const wchar_t *,wchar_t *,size_t);
#endif

