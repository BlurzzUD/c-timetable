#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "downloader.h"
#include "json_parser.h"

#define MAX_BODY (32u * 1024u * 1024u)
static void win_error(wchar_t *error, size_t cap, const wchar_t *action, DWORD code) {
    wchar_t reason[512] = L"";
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0, reason, 512, NULL);
    swprintf_s(error, cap, L"%ls failed (%lu). %ls", action, code, reason);
}
void app_log(const wchar_t *base, const wchar_t *message) {
    wchar_t path[32768]; SYSTEMTIME now; FILE *f; char *utf8; int count;
    swprintf_s(path, 32768, L"%ls\\logs", base); CreateDirectoryW(path, NULL);
    wcscat_s(path, 32768, L"\\download.log");
    if (_wfopen_s(&f, path, L"ab") != 0) return;
    GetSystemTime(&now);
    fprintf(f, "%04u-%02u-%02uT%02u:%02u:%02uZ ", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    count = WideCharToMultiByte(CP_UTF8, 0, message, -1, NULL, 0, NULL, NULL);
    if (count <= 0) { fclose(f); return; }
    utf8 = (char *)malloc((size_t)count);
    if (utf8) { WideCharToMultiByte(CP_UTF8, 0, message, -1, utf8, count, NULL, NULL); fprintf(f, "%s\n", utf8); free(utf8); }
    fclose(f);
}
static void record_error(const wchar_t *base, const wchar_t *message) {
    Json *j = json_new(JSON_OBJECT); wchar_t path[32768]; char *buf, *serialized; FILE *f; int count;
    count = WideCharToMultiByte(CP_UTF8, 0, message, -1, NULL, 0, NULL, NULL);
    if (!j) return;
    if (count <= 0 || !(buf = (char *)malloc((size_t)count))) { json_free(j); return; }
    if (!WideCharToMultiByte(CP_UTF8, 0, message, -1, buf, count, NULL, NULL)) { free(buf); json_free(j); return; }
    if (!j) return;
    json_add(j, "stage", json_text("download_or_extraction"));
    json_add(j, "error", json_text(buf));
    json_add(j, "existing_data_preserved", json_bool(1));
    serialized = json_write(j); json_free(j); free(buf);
    swprintf_s(path, 32768, L"%ls\\logs\\last_download_error.json", base);
    if (serialized && _wfopen_s(&f, path, L"wb") == 0) { fwrite(serialized, 1, strlen(serialized), f); fclose(f); }
    free(serialized);
}
static int http(HINTERNET connection, const wchar_t *path, const char *body, char **out, wchar_t *error, size_t cap) {
    HINTERNET request = NULL; DWORD status = 0, status_len = sizeof(status), available, received; size_t length = 0;
    char *result = NULL; int ok = 0; DWORD bytes = body ? (DWORD)strlen(body) : 0;
    *out = NULL;
    request = WinHttpOpenRequest(connection, body ? L"POST" : L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) { win_error(error, cap, L"Creating HTTPS request", GetLastError()); goto done; }
    if (!WinHttpSendRequest(request, body ? L"Content-Type: application/json; charset=utf-8\r\n" : WINHTTP_NO_ADDITIONAL_HEADERS,
        body ? (DWORD)-1L : 0, (LPVOID)body, bytes, bytes, 0) || !WinHttpReceiveResponse(request, NULL)) {
        win_error(error, cap, L"Downloading from pollak.edupage.org", GetLastError()); goto done;
    }
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_len, WINHTTP_NO_HEADER_INDEX)) {
        win_error(error, cap, L"Reading HTTP status", GetLastError()); goto done;
    }
    if (status != 200) { swprintf_s(error, cap, L"EduPage returned HTTP %lu. The public timetable may be unavailable or require sign-in.", status); goto done; }
    result = (char *)malloc(1); if (!result) goto memory;
    for (;;) {
        if (!WinHttpQueryDataAvailable(request, &available)) { win_error(error, cap, L"Reading HTTPS response", GetLastError()); goto done; }
        if (!available) break;
        if (length + available > MAX_BODY) { swprintf_s(error, cap, L"EduPage response exceeded the 32 MiB limit."); goto done; }
        { char *next = (char *)realloc(result, length + available + 1); if (!next) goto memory; result = next; }
        if (!WinHttpReadData(request, result + length, available, &received)) { win_error(error, cap, L"Reading timetable data", GetLastError()); goto done; }
        if (!received) break;
        length += received;
    }
    result[length] = 0;
    if (!length) { swprintf_s(error, cap, L"EduPage returned an empty response."); goto done; }
    if (memchr(result, 0, length)) { swprintf_s(error, cap, L"EduPage response contains unexpected NUL bytes."); goto done; }
    *out = result; result = NULL; ok = 1; goto done;
memory:
    swprintf_s(error, cap, L"Not enough memory to download timetable data.");
done:
    free(result); if (request) WinHttpCloseHandle(request); return ok;
}
static int refresh_impl(const wchar_t *base, TTData **out, wchar_t *error, size_t cap, int fail_route) {
    HINTERNET session = NULL, connection = NULL; char *page = NULL, *meta = NULL, *raw = NULL;
    char gsh[129] = "", body[512], parse_error[256]; Json *metadata = NULL; const char *id; const char *hash; size_t n;
    SYSTEMTIME now; TTData *data = NULL; wchar_t dir[32768], message[512]; int ok = 0; int year;
    *out = NULL; error[0] = 0;
    app_log(base, fail_route ? L"Starting expected-failure network test." : L"Starting public timetable refresh.");
    session = WinHttpOpen(L"CTimetable/1.0 (native Windows timetable viewer)", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { win_error(error, cap, L"Opening WinHTTP", GetLastError()); goto done; }
    WinHttpSetTimeouts(session, 15000, 15000, 30000, 30000);
    { DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE; WinHttpSetOption(session, WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof(decompress)); }
    connection = WinHttpConnect(session, L"pollak.edupage.org", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) { win_error(error, cap, L"Connecting to EduPage", GetLastError()); goto done; }
    if (!http(connection, fail_route ? L"/timetable/__ctimetable_missing_test_resource__.json" : L"/timetable/", NULL, &page, error, cap)) goto done;
    if (fail_route) { swprintf_s(error, cap, L"Expected-failure route unexpectedly returned HTTP 200; no data saved."); goto done; }
    hash = strstr(page, "ASC.gsechash=");
    if (!hash) { swprintf_s(error, cap, L"The timetable page no longer exposes the public request token. Inspect the changed EduPage format."); goto done; }
    hash += strlen("ASC.gsechash="); while (*hash == ' ') ++hash;
    if (*hash != '\"' && *hash != '\'') { swprintf_s(error, cap, L"Invalid public request token in timetable page."); goto done; }
    { char quote = *hash++; for (n = 0; hash[n] && hash[n] != quote && n < 128; ++n) {
        if (!((hash[n] >= '0' && hash[n] <= '9') || (hash[n] >= 'a' && hash[n] <= 'z') || (hash[n] >= 'A' && hash[n] <= 'Z') || hash[n] == '_' || hash[n] == '-')) break;
        gsh[n] = hash[n];
      }
      if (!n || n >= 128 || hash[n] != quote) { swprintf_s(error, cap, L"Unsupported public request token format."); goto done; } gsh[n] = 0;
    }
    GetLocalTime(&now); year = now.wYear - (now.wMonth < 9 ? 1 : 0);
    sprintf_s(body, sizeof(body), "{\"__args\":[null,%d],\"__gsh\":\"%s\"}", year, gsh);
    if (!http(connection, L"/timetable/server/ttviewer.js?__func=getTTViewerData", body, &meta, error, cap)) goto done;
    metadata = json_parse(meta, parse_error, sizeof(parse_error));
    id = json_string(json_get(json_get(json_get(metadata, "r"), "regular"), "default_num"));
    if (!id || !*id || strlen(id) > 32) { swprintf_s(error, cap, L"No current public timetable was returned by EduPage. Sign-in or a published timetable may be needed."); goto done; }
    for (n = 0; id[n]; ++n) if (id[n] < '0' || id[n] > '9') { swprintf_s(error, cap, L"Unexpected timetable identifier."); goto done; }
    sprintf_s(body, sizeof(body), "{\"__args\":[null,\"%s\"],\"__gsh\":\"%s\"}", id, gsh);
    if (!http(connection, L"/timetable/server/regulartt.js?__func=regularttGetData", body, &raw, error, cap)) goto done;
    if (!tt_extract(raw, meta, &data, error, cap)) goto done;
    swprintf_s(dir, 32768, L"%ls\\data", base);
    if (!tt_save(dir, data, error, cap)) goto done;
    swprintf_s(message, 512, L"Refresh succeeded: %zu classes, %zu lesson records, %zu source cards.", data->class_count, data->lesson_count, data->source_card_count);
    app_log(base, message); *out = data; data = NULL; ok = 1;
done:
    if (!ok) { if (!error[0]) swprintf_s(error, cap, L"Refresh failed. Existing timetable data was preserved."); app_log(base, error); record_error(base, error); }
    tt_free(data); json_free(metadata); free(page); free(meta); free(raw);
    if (connection) WinHttpCloseHandle(connection); if (session) WinHttpCloseHandle(session);
    return ok;
}
int downloader_refresh(const wchar_t *base, TTData **out, wchar_t *error, size_t cap) { return refresh_impl(base, out, error, cap, 0); }
int downloader_test_failure(const wchar_t *base, wchar_t *error, size_t cap) { TTData *unused = NULL; int result = refresh_impl(base, &unused, error, cap, 1); tt_free(unused); return result; }



