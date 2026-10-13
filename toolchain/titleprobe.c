#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
static LRESULT CALLBACK skip_default(HWND w, UINT m, WPARAM a, LPARAM b) { if(m==WM_NCCREATE)return TRUE;return DefWindowProcW(w,m,a,b); }
static LRESULT CALLBACK use_default(HWND w, UINT m, WPARAM a, LPARAM b) { return DefWindowProcW(w,m,a,b); }
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show) {
 WNDCLASSW wc={0}; HWND first,second; wchar_t text1[100],text2[100]; FILE *output; int n1,n2;
 (void)previous;(void)command;(void)show;wc.hInstance=instance;wc.lpfnWndProc=skip_default;wc.lpszClassName=L"TitleProbeSkipDefault";RegisterClassW(&wc);first=CreateWindowExW(0,wc.lpszClassName,L"Expected caption",WS_OVERLAPPEDWINDOW,0,0,100,100,NULL,NULL,instance,NULL);
 wc.lpfnWndProc=use_default;wc.lpszClassName=L"TitleProbeUseDefault";RegisterClassW(&wc);second=CreateWindowExW(0,wc.lpszClassName,L"Expected caption",WS_OVERLAPPEDWINDOW,0,0,100,100,NULL,NULL,instance,NULL);
 n1=GetWindowTextW(first,text1,100);n2=GetWindowTextW(second,text2,100);if(fopen_s(&output,"toolchain/titleprobe-result.txt","wb")==0){fprintf(output,"WM_NCCREATE return TRUE: caption length %d\nWM_NCCREATE DefWindowProc: caption length %d\n",n1,n2);fclose(output);}DestroyWindow(first);DestroyWindow(second);return n2==16?0:1;
}