#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/timetable.h"
#include "../src/json_parser.h"
static char *read_all(const wchar_t *path) {FILE *f=NULL;long n;char *s;if(_wfopen_s(&f,path,L"rb"))return NULL;fseek(f,0,SEEK_END);n=ftell(f);rewind(f);if(n<0){fclose(f);return NULL;}s=(char*)malloc((size_t)n+1);if(!s){fclose(f);return NULL;}if(fread(s,1,(size_t)n,f)!=(size_t)n){free(s);fclose(f);return NULL;}s[n]=0;fclose(f);return s;}
static int write_text(const wchar_t *path,const char *text) {FILE *f=NULL;size_t n=strlen(text);if(_wfopen_s(&f,path,L"wb"))return 0;if(fwrite(text,1,n,f)!=n){fclose(f);return 0;}return fclose(f)==0;}
static int invalid_cache_tests(const TTData *data) {
    wchar_t error[1024];TTData *invalid=NULL;Json *v=NULL,*stats;char *text=NULL;size_t i;int ok=0;
    if(tt_load(L"tests/model_output/does-not-exist.json",&invalid,error,1024)||invalid||!error[0])goto done;
    if(!write_text(L"tests/model_output/malformed.json","{\"classes\":[")||tt_load(L"tests/model_output/malformed.json",&invalid,error,1024)||invalid||!error[0])goto done;
    v=json_clone(data->document);if(!v)goto done;stats=(Json*)json_get(v,"statistics");if(!stats)goto done;
    for(i=0;i<stats->count;i++)if(!strcmp(stats->keys[i],"lesson_records")){json_free(stats->items[i]);stats->items[i]=json_num(1);}
    text=json_write(v);if(!text||!write_text(L"tests/model_output/wrong-count.json",text)||tt_load(L"tests/model_output/wrong-count.json",&invalid,error,1024)||invalid||!error[0])goto done;
    free(text);text=NULL;json_free(v);v=json_clone(data->document);if(!v)goto done;
    {Json *classes=(Json*)json_get(v,"classes"),*lessons;lessons=(Json*)json_get(classes->items[0],"lessons");for(i=0;i<lessons->items[0]->count;i++)if(!strcmp(lessons->items[0]->keys[i],"period")){json_free(lessons->items[0]->items[i]);lessons->items[0]->items[i]=json_text("not a number");}}
    text=json_write(v);if(!text||!write_text(L"tests/model_output/wrong-type.json",text)||tt_load(L"tests/model_output/wrong-type.json",&invalid,error,1024)||invalid||!error[0])goto done;
    ok=1;puts("PASS: missing, truncated, incorrect-count and incorrect-type caches fail gracefully.");
done:free(text);json_free(v);tt_free(invalid);return ok;
}
int main(int argc,char **argv) {
    char *raw=read_all(L"inspection/regular_data.json"),*meta=read_all(L"inspection/viewer_data.json"),*before,*after;
    TTData *data=NULL,*loaded=NULL;wchar_t error[2048];HANDLE locked;size_t i,total=0;int result=1;char parse_error[256];Json *value;
    const char *bad[]={"[1","{\"x\"","{\"x\":1","{\"x\":","\"abc\\","\"\xC0\x80\"","{\"x\":1,\"x\":2}","1e9999"};
    for(i=0;i<sizeof(bad)/sizeof(*bad);i++){value=json_parse(bad[i],parse_error,sizeof(parse_error));if(value){json_free(value);puts("FAIL malformed parser fixture");goto done;}}
    if(!raw||!meta||!tt_extract(raw,meta,&data,error,2048)){wprintf(L"FAIL extraction: %ls\n",error);goto done;}
    if(data->class_count!=11||data->source_card_count!=496||data->lesson_count!=628){puts("FAIL expected independent counts");goto done;}
    for(i=0;i<data->class_count;i++){printf("%s: %zu\n",data->classes[i].name,data->classes[i].lesson_count);total+=data->classes[i].lesson_count;}
    if(total!=628||!strstr(data->days[0].name,"H\xC3\xA9tf\xC5\x91")){puts("FAIL counts/UTF8");goto done;}
    CreateDirectoryW(L"tests/model_output",NULL);
    if(!tt_save(L"tests/model_output",data,error,2048)||!tt_save(L"tests/model_output",data,error,2048)||!tt_load(L"tests/model_output/timetables.json",&loaded,error,2048)){wprintf(L"FAIL saved JSON: %ls\n",error);goto done;}
    if(loaded->lesson_count!=data->lesson_count){puts("FAIL roundtrip counts");goto done;}
    before=read_all(L"tests/model_output/timetables.json");if(!before){puts("FAIL read saved");goto done;}
    locked=CreateFileW(L"tests/model_output/timetables.json",GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    if(locked==INVALID_HANDLE_VALUE){free(before);puts("FAIL test lock");goto done;}
    if(tt_save(L"tests/model_output",data,error,2048)){CloseHandle(locked);free(before);puts("FAIL expected publish failure while cache locked");goto done;}
    CloseHandle(locked);after=read_all(L"tests/model_output/timetables.json");if(!after||strcmp(before,after)){free(before);free(after);puts("FAIL previous cache changed after failed save");goto done;}free(before);free(after);
    tt_free(loaded);loaded=NULL;if(!tt_load(L"tests/model_output/timetables.json",&loaded,error,2048)){puts("FAIL rollback cache load");goto done;}
    if(!invalid_cache_tests(data)){puts("FAIL invalid-cache handling");goto done;}
    if(argc>1&&!strcmp(argv[1],"--save-production")&&!tt_save(L"data",data,error,2048)){wprintf(L"FAIL production save: %ls\n",error);goto done;}
    printf("PASS: C extraction 11 classes, 496 cards, 628 records; UTF8; strict parser; transactional save and rollback.\n");result=0;
done:tt_free(data);tt_free(loaded);free(raw);free(meta);return result;
}


