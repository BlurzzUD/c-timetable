#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include "../src/downloader.h"
int wmain(int argc, wchar_t **argv) {
    TTData *data=NULL; wchar_t error[2048]=L""; int ok;
    if(argc>1 && !wcscmp(argv[1],L"--failure")) {
        ok=downloader_test_failure(L"H:\\projects\\ctimetable",error,2048);
        wprintf(L"Expected failure: %ls\n",error);return ok?1:0;
    }
    ok=downloader_refresh(L"H:\\projects\\ctimetable",&data,error,2048);
    if(!ok) {wprintf(L"FAIL: %ls\n",error);return 1;}
    printf("PASS: native WinHTTP refresh: %zu classes, %zu lesson records, %zu source cards\n",data->class_count,data->lesson_count,data->source_card_count);
    tt_free(data);return 0;
}
