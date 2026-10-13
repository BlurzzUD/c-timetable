#ifndef CT_DOWNLOADER_H
#define CT_DOWNLOADER_H
#include "timetable.h"
int downloader_refresh(const wchar_t *base_dir, TTData **out, wchar_t *error, size_t error_cap);
int downloader_test_failure(const wchar_t *base_dir, wchar_t *error, size_t error_cap);
void app_log(const wchar_t *base_dir, const wchar_t *message);
#endif
