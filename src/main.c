#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "timetable.h"
#include "json_parser.h"
#include "downloader.h"
#include "gui.h"

static char *read_utf8(const wchar_t *path) {
    FILE *f; long length; char *s;
    if (_wfopen_s(&f, path, L"rb") != 0) return NULL;
    if (fseek(f, 0, SEEK_END) || (length = ftell(f)) < 0 || length > 32 * 1024 * 1024) { fclose(f); return NULL; }
    rewind(f); s = (char *)malloc((size_t)length + 1);
    if (!s || fread(s, 1, (size_t)length, f) != (size_t)length) { free(s); fclose(f); return NULL; }
    s[length] = 0; fclose(f); return s;
}
static int json_tests(const wchar_t *base) {
    const char *invalid[] = {"", "{", "[1,]", "{\"x\":1,}", "{\"x\":1 \"y\":2}", "[true false]", "01", "1.", "1e", "NaN", "\"a\nb\"", "\"\\q\"", "\"\\uD800\"", "\"\\uDC00\"", "null null", "[1", "{\"x\"", "{\"x\":1"};
    const char *valid[] = {"null", "true", "false", "-1.25e+3", "[]", "{}", "{\"x\":[1,2,null,false,\"Árvíztűrő tükörfúrógép\"]}", "\"\\u00e1\\u0151\\u0171\\uD83D\\uDE00\""};
    char error[256]; size_t i; Json *j; char *out;
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        j = json_parse(invalid[i], error, sizeof(error));
        if (j) { json_free(j); app_log(base, L"FAIL: strict JSON parser accepted malformed fixture."); return 0; }
    }
    for (i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i) {
        j = json_parse(valid[i], error, sizeof(error));
        if (!j) { app_log(base, L"FAIL: JSON parser rejected valid Unicode fixture."); return 0; }
        out = json_write(j); json_free(j); j = out ? json_parse(out, error, sizeof(error)) : NULL;
        free(out); if (!j) { app_log(base, L"FAIL: JSON writer round-trip."); return 0; } json_free(j);
    }
    app_log(base, L"PASS: 18 malformed JSON cases rejected; 8 valid/Unicode cases round-tripped."); return 1;
}
static int validate_source(const wchar_t *base, const TTData *data) {
    wchar_t path[32768], error[512]; char *raw, *meta; TTData *from_source = NULL; size_t c,l; int ok = 1;
    swprintf_s(path, 32768, L"%ls\\inspection\\regular_data.json", base); raw = read_utf8(path);
    swprintf_s(path, 32768, L"%ls\\inspection\\viewer_data.json", base); meta = read_utf8(path);
    if (!raw || !meta) { free(raw); free(meta); app_log(base, L"Source fixture unavailable; skipping fixture comparison."); return 1; }
    if (!tt_extract(raw, meta, &from_source, error, 512)) { app_log(base, error); ok = 0; goto done; }
    if (from_source->class_count != data->class_count || from_source->lesson_count != data->lesson_count) ok = 0;
    for (c = 0; ok && c < data->class_count; ++c) {
        const TTClass *a = &data->classes[c], *b = &from_source->classes[c];
        if (strcmp(a->id,b->id) || strcmp(a->name,b->name) || a->lesson_count != b->lesson_count) { ok=0; break; }
        for(l=0;l<a->lesson_count;++l) {
            const TTLesson *x=&a->lessons[l],*y=&b->lessons[l];
            if(x->day_index!=y->day_index || x->period!=y->period || x->duration!=y->duration || strcmp(x->card_id,y->card_id)) { ok=0; break; }
        }
    }
    app_log(base, ok ? L"PASS: cached JSON matches extraction of every class/card/day in the public source fixture." : L"FAIL: cached JSON differs from public source fixture.");
done: tt_free(from_source); free(raw); free(meta); return ok;
}
/* Synthetic variants of real source entities test uncommon extraction features. */
static Json *source_table(Json *root, const char *name) {
    const Json *tables=json_get(json_get(json_get(root,"r"),"dbiAccessorRes"),"tables"); size_t i;
    if(!tables || tables->type!=JSON_ARRAY) return NULL;
    for(i=0;i<tables->count;++i) { const char *id=json_string(json_get(tables->items[i],"id")); if(id && !strcmp(id,name)) return (Json*)json_get(tables->items[i],"data_rows"); }
    return NULL;
}
static int replace_string(Json *object,const char *key,const char *value) {
    size_t i; Json *s=json_text(value); if(!s) return 0;
    for(i=0;i<object->count;++i) if(!strcmp(object->keys[i],key)) { json_free(object->items[i]); object->items[i]=s; return 1; }
    json_free(s); return 0;
}
static int extraction_variant_tests(const wchar_t *base) {
    wchar_t path[32768], error[1024] = L""; char *raw=NULL,*metadata=NULL,*variant=NULL; char parse_error[256];
    Json *root=NULL,*cards,*days,*weeks,*teachers,*rooms,*lessons,*card,*lesson=NULL,*entry,*ids; TTData *data=NULL;
    size_t i,c,l; const char *lessonid,*cardid; int matched=0,ok=0;
    swprintf_s(path,32768,L"%ls\\inspection\\regular_data.json",base); raw=read_utf8(path);
    swprintf_s(path,32768,L"%ls\\inspection\\viewer_data.json",base); metadata=read_utf8(path);
    if(!raw||!metadata) {ok=1;goto done;}
    root=json_parse(raw,parse_error,sizeof(parse_error)); if(!root) goto done;
    cards=source_table(root,"cards");days=source_table(root,"days");weeks=source_table(root,"weeks");teachers=source_table(root,"teachers");rooms=source_table(root,"classrooms");lessons=source_table(root,"lessons");
    if(!cards||!cards->count||!days||days->count!=5||!weeks||weeks->count!=1||!teachers||teachers->count<2||!rooms||rooms->count<2||!lessons) {app_log(base,L"Variant fixture assumptions changed; update development tests.");goto done;}
    card=cards->items[0]; lessonid=json_string(json_get(card,"lessonid"));cardid=json_string(json_get(card,"id"));
    for(i=0;i<lessons->count;i++) if(!strcmp(json_string(json_get(lessons->items[i],"id")),lessonid)) lesson=lessons->items[i];
    if(!lesson)goto done;
    entry=json_new(JSON_OBJECT); if(!entry||!json_add(entry,"id",json_text("test-saturday"))||!json_add(entry,"name",json_text("Szombat"))||!json_add(entry,"short",json_text("Szo"))||!json_add(days,NULL,entry))goto done;
    entry=json_new(JSON_OBJECT); if(!entry||!json_add(entry,"id",json_text("test-week-B"))||!json_add(entry,"name",json_text("Hét B"))||!json_add(entry,"short",json_text("B"))||!json_add(weeks,NULL,entry))goto done;
    for(i=0;i<cards->count;i++) {
        Json *v=cards->items[i];const char *dm=json_string(json_get(v,"days")),*wm=json_string(json_get(v,"weeks")); char extended[64];
        if(dm&&*dm) {sprintf_s(extended,sizeof(extended),"%s%c",dm,i==0?'1':'0');if(!replace_string(v,"days",extended))goto done;}
        if(wm&&*wm) {sprintf_s(extended,sizeof(extended),"%s0",wm);if(!replace_string(v,"weeks",i==0?"01":extended))goto done;}
    }
    ids=(Json*)json_get(lesson,"teacherids");if(!json_add(ids,NULL,json_clone(json_get(teachers->items[1],"id"))))goto done;
    ids=(Json*)json_get(card,"classroomids");if(!json_add(ids,NULL,json_clone(json_get(rooms->items[1],"id"))))goto done;
    variant=json_write(root); if(!variant||!tt_extract(variant,metadata,&data,error,1024)) {app_log(base,error);goto done;}
    for(c=0;c<data->class_count;c++)for(l=0;l<data->classes[c].lesson_count;l++) {
        const TTLesson *v=&data->classes[c].lessons[l];
        if(!strcmp(v->card_id,cardid)) {
            if(!v->teacher||!strstr(v->teacher," / ")||!v->room||!strstr(v->room," / ")||!v->week||strcmp(v->week,"Hét B")||v->duration!=2||!v->end||strcmp(v->end,"11:30"))goto done;
            if(v->day_index==5&&v->day&&!strcmp(v->day,"Szombat"))matched++;
        }
    }
    ok=matched==(int)json_get(lesson,"classids")->count;
    if(ok)app_log(base,L"PASS: synthetic source variant preserved Saturday, week B, multiple teachers, multiple rooms and multi-period end time.");
done:
    if(!ok)app_log(base,L"FAIL: synthetic extraction variant.");tt_free(data);json_free(root);free(raw);free(metadata);free(variant);return ok;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show) {
    wchar_t base[32768], path[32768], error[2048] = L"", summary[1024]; int argc, result=0; wchar_t **argv; wchar_t *slash; TTData *data=NULL;
    (void)previous; (void)command;
    if (!GetModuleFileNameW(NULL,base,32768)) return 1;
    slash=wcsrchr(base,L'\\'); if(!slash) return 1; *slash=0; SetCurrentDirectoryW(base);
    argv=CommandLineToArgvW(GetCommandLineW(),&argc); if(!argv) return 1;
    if(argc>1) {
        swprintf_s(path,32768,L"%ls\\data\\timetables.json",base);
        if(!wcscmp(argv[1],L"--refresh")) result=downloader_refresh(base,&data,error,2048)?0:1;
        else if(!wcscmp(argv[1],L"--validate") || !wcscmp(argv[1],L"--self-test")) {
            result=tt_load(path,&data,error,2048)?0:1;
            if(!result && !wcscmp(argv[1],L"--self-test")) {
                if(!json_tests(base) || !validate_source(base,data) || !extraction_variant_tests(base)) result=1;
                if(!result) { if(!gui_self_test(instance,base,error,2048)) result=1; else app_log(base,error); }
            }
        } else if(!wcscmp(argv[1],L"--extract-fixture")) {
            char *raw,*meta; wchar_t dir[32768];
            swprintf_s(path,32768,L"%ls\\inspection\\regular_data.json",base); raw=read_utf8(path);
            swprintf_s(path,32768,L"%ls\\inspection\\viewer_data.json",base); meta=read_utf8(path);
            swprintf_s(dir,32768,L"%ls\\data",base);
            if(!raw || !meta) { wcscpy_s(error,2048,L"Source fixture files missing."); result=1; }
            else if(!tt_extract(raw,meta,&data,error,2048) || !tt_save(dir,data,error,2048)) result=1;
            free(raw); free(meta);
        } else if(!wcscmp(argv[1],L"--test-gui-refresh")) {
            result=gui_refresh_test(instance,base,error,2048)?0:1;
            app_log(base,error);
        } else if(!wcscmp(argv[1],L"--test-failed-refresh")) {
            /* Return success when the deliberately missing resource fails as expected. */
            result=downloader_test_failure(base,error,2048)?1:0;
            if(!result) app_log(base,L"PASS: deliberate request failure reported without saving timetable data.");
        } else { wcscpy_s(error,2048,L"Unknown option. Supported: --refresh --validate --self-test --extract-fixture --test-failed-refresh"); result=1; }
        if(data) { swprintf_s(summary,1024,L"Loaded %zu classes, %zu lesson records, %zu source cards; timetable %hs.",data->class_count,data->lesson_count,data->source_card_count,data->timetable_id); app_log(base,summary); }
        if(result && error[0]) app_log(base,error);
        tt_free(data); LocalFree(argv); return result;
    }
    LocalFree(argv); return gui_run(instance,show,base);
}





