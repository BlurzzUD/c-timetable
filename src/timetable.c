#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <stdarg.h>
#include "timetable.h"
#include "json_parser.h"

static char *dupstr(const char *s) {size_t n;char *p;if(!s)return NULL;n=strlen(s);p=(char*)malloc(n+1);if(p)memcpy(p,s,n+1);return p;}
static const char *field(const Json *v,const char *key) {return json_string(json_get(v,key));}
static const char *nonempty(const char *s) {return s&&*s?s:NULL;}
static void errorf(wchar_t *out,size_t cap,const char *fmt,...) {char text[768];va_list ap;if(!out||!cap)return;va_start(ap,fmt);vsnprintf(text,sizeof(text),fmt,ap);va_end(ap);if(!MultiByteToWideChar(CP_UTF8,0,text,-1,out,(int)cap)) {wcsncpy(out,L"Timetable operation failed.",cap-1);out[cap-1]=0;}}
static int put(Json *o,const char *key,Json *child) {if(!child)return 0;if(!json_add(o,key,child)){json_free(child);return 0;}return 1;}
static int text(Json *o,const char *key,const char *s) {return put(o,key,json_text(nonempty(s)));}
static int number(Json *o,const char *key,int n) {return put(o,key,json_num(n));}
static const Json *table(const Json *tables,const char *id) {size_t i;if(tables&&tables->type==JSON_ARRAY)for(i=0;i<tables->count;i++)if(field(tables->items[i],"id")&&!strcmp(field(tables->items[i],"id"),id))return json_get(tables->items[i],"data_rows");return NULL;}
static const Json *row(const Json *rows,const char *id) {size_t i;if(rows&&rows->type==JSON_ARRAY&&id)for(i=0;i<rows->count;i++)if(field(rows->items[i],"id")&&!strcmp(field(rows->items[i],"id"),id))return rows->items[i];return NULL;}
static int valid_array(const Json *a) {return a&&a->type==JSON_ARRAY;}
static int valid_mask(const char *s,size_t count,int empty_ok) {size_t i;if(!s)return 0;if(!*s)return empty_ok;if(strlen(s)!=count)return 0;for(i=0;i<count;i++)if(s[i]!='0'&&s[i]!='1')return 0;return 1;}
static int contains_id(const Json *a,const char *id) {size_t i;if(valid_array(a))for(i=0;i<a->count;i++)if(json_string(a->items[i])&&!strcmp(json_string(a->items[i]),id))return 1;return 0;}
static char *join_names(const Json *a) {size_t i,n=0;char *s,*p;if(!valid_array(a)||!a->count)return NULL;for(i=0;i<a->count;i++){const char *name=field(a->items[i],"name");if(name)n+=strlen(name)+3;}if(!n)return NULL;s=(char*)malloc(n+1);if(!s)return NULL;p=s;for(i=0;i<a->count;i++){const char *name=field(a->items[i],"name");if(name&&*name){if(p!=s){memcpy(p," / ",3);p+=3;}memcpy(p,name,strlen(name));p+=strlen(name);}}*p=0;return s;}
/* Each resolved array retains IDs, full names when available, abbreviations, and raw entity rows. */
static Json *resolve(const Json *ids,const Json *rows,const char *classid,int groups,char *err,size_t cap) {
    Json *out=json_new(JSON_ARRAY);size_t i;const Json *r;const char *id,*name;Json *v;
    if(!out)return NULL;if(!valid_array(ids)){snprintf(err,cap,"Missing ID array");json_free(out);return NULL;}
    for(i=0;i<ids->count;i++) {id=json_string(ids->items[i]);r=row(rows,id);if(!r){snprintf(err,cap,"Unresolved timetable reference: %s",id?id:"(non-string ID)");json_free(out);return NULL;}
        if(groups&&classid&&field(r,"classid")&&strcmp(field(r,"classid"),classid))continue;
        name=nonempty(field(r,"name"));if(!name)name=nonempty(field(r,"short"));v=json_new(JSON_OBJECT);if(!v||!text(v,"id",id)||!text(v,"name",name)||!text(v,"short",field(r,"short"))||!put(v,"source",json_clone(r))||!put(out,NULL,v)){json_free(out);snprintf(err,cap,"Out of memory resolving references");return NULL;}
    }return out;
}
static Json *mask_labels(const char *mask,const Json *rows,char *err,size_t cap) {Json *a=json_new(JSON_ARRAY),*v;size_t i;if(!a)return NULL;if(!mask||!*mask)return a;if(!valid_mask(mask,rows->count,1)){snprintf(err,cap,"Invalid schedule mask %s",mask);json_free(a);return NULL;}for(i=0;i<rows->count;i++)if(mask[i]=='1'){v=json_new(JSON_OBJECT);if(!v||!text(v,"id",field(rows->items[i],"id"))||!text(v,"name",field(rows->items[i],"name"))||!text(v,"short",field(rows->items[i],"short"))||!put(a,NULL,v)){json_free(a);return NULL;}}return a;}
static const char *time_value(const Json *period,const Json *bell,const char *dayid,const char *key) {
    const char *value=nonempty(field(period,key)),*x;const Json *override,*day;
    day=json_get(json_get(period,"daydata"),dayid);x=nonempty(field(day,key));if(x)value=x;
    override=json_get(json_get(bell,"perioddata"),field(period,"id"));x=nonempty(field(override,key));if(x)value=x;
    day=json_get(json_get(override,"daydata"),dayid);x=nonempty(field(day,key));if(x)value=x;return value;
}
static void timestamp(char *s,size_t cap) {SYSTEMTIME t;TIME_ZONE_INFORMATION tz;DWORD z;LONG bias;char sign='+';GetLocalTime(&t);z=GetTimeZoneInformation(&tz);bias=tz.Bias+(z==TIME_ZONE_ID_DAYLIGHT?tz.DaylightBias:(z==TIME_ZONE_ID_STANDARD?tz.StandardBias:0));bias=-bias;if(bias<0){sign='-';bias=-bias;}snprintf(s,cap,"%04u-%02u-%02uT%02u:%02u:%02u%c%02ld:%02ld",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,sign,bias/60,bias%60);}
static int unique_rows(const Json *a,const char *tableid,char *err,size_t cap) {size_t i,j;if(!valid_array(a)){snprintf(err,cap,"Missing table %s",tableid);return 0;}for(i=0;i<a->count;i++){const char *id=field(a->items[i],"id");if(!id||!*id){snprintf(err,cap,"Missing row ID in %s",tableid);return 0;}for(j=0;j<i;j++)if(!strcmp(id,field(a->items[j],"id"))){snprintf(err,cap,"Duplicate row ID %s in %s",id,tableid);return 0;}}return 1;}
static int load_document(Json *doc,TTData **out,wchar_t *error,size_t cap);

int tt_extract(const char *regular_json,const char *viewer_json,TTData **out,wchar_t *error,size_t error_cap) {
    Json *raw=NULL,*viewer=NULL,*doc=NULL,*classes=NULL,*meta=NULL,*stats=NULL,*days_copy=NULL,*periods_copy=NULL;
    const Json *tables,*srcclasses,*lessons,*cards,*days,*periods,*subjects,*teachers,*rooms,*groups,*weeks,*terms,*bells,*globals,*regular,*tts,*selected=NULL;
    const char *tid;char parseerr[768]={0},stamp[64];size_t i,j,k,di,records=0,scheduled=0,unplaced=0,processed=0;int success=0;
    if(out)*out=NULL;if(error&&error_cap)error[0]=0;
    raw=json_parse(regular_json,parseerr,sizeof(parseerr));if(!raw){errorf(error,error_cap,"Invalid timetable JSON: %s",parseerr);goto done;}
    viewer=json_parse(viewer_json,parseerr,sizeof(parseerr));if(!viewer){errorf(error,error_cap,"Invalid metadata JSON: %s",parseerr);goto done;}
    tables=json_get(json_get(json_get(raw,"r"),"dbiAccessorRes"),"tables");
    srcclasses=table(tables,"classes");lessons=table(tables,"lessons");cards=table(tables,"cards");days=table(tables,"days");periods=table(tables,"periods");subjects=table(tables,"subjects");teachers=table(tables,"teachers");rooms=table(tables,"classrooms");groups=table(tables,"groups");weeks=table(tables,"weeks");terms=table(tables,"terms");bells=table(tables,"bells");globals=table(tables,"globals");
    if(!unique_rows(srcclasses,"classes",parseerr,sizeof(parseerr))||!unique_rows(lessons,"lessons",parseerr,sizeof(parseerr))||!unique_rows(cards,"cards",parseerr,sizeof(parseerr))||!unique_rows(days,"days",parseerr,sizeof(parseerr))||!unique_rows(periods,"periods",parseerr,sizeof(parseerr))||!unique_rows(subjects,"subjects",parseerr,sizeof(parseerr))||!unique_rows(teachers,"teachers",parseerr,sizeof(parseerr))||!unique_rows(rooms,"classrooms",parseerr,sizeof(parseerr))||!unique_rows(groups,"groups",parseerr,sizeof(parseerr))||!unique_rows(weeks,"weeks",parseerr,sizeof(parseerr))||!unique_rows(terms,"terms",parseerr,sizeof(parseerr))||!valid_array(bells)||!valid_array(globals))goto extraction_error;
    if(!srcclasses->count||!days->count||days->count>31||!periods->count||periods->count>128||!weeks->count||!terms->count){snprintf(parseerr,sizeof(parseerr),"Invalid or empty class/day/period/week/term tables");goto extraction_error;}
    regular=json_get(json_get(viewer,"r"),"regular");tid=field(regular,"default_num");tts=json_get(regular,"timetables");if(!tid||!valid_array(tts)){snprintf(parseerr,sizeof(parseerr),"Metadata has no current public timetable");goto extraction_error;}for(i=0;i<tts->count;i++)if(field(tts->items[i],"tt_num")&&!strcmp(field(tts->items[i],"tt_num"),tid))selected=tts->items[i];if(!selected){snprintf(parseerr,sizeof(parseerr),"Default timetable missing from metadata");goto extraction_error;}
    doc=json_new(JSON_OBJECT);classes=json_new(JSON_ARRAY);meta=json_new(JSON_OBJECT);stats=json_new(JSON_OBJECT);days_copy=json_clone(days);periods_copy=json_new(JSON_ARRAY);if(!doc||!classes||!meta||!stats||!days_copy||!periods_copy)goto oom;
    timestamp(stamp,sizeof(stamp));
    if(!number(doc,"schema_version",1)||!text(doc,"source","https://pollak.edupage.org/timetable/")||!text(doc,"downloaded_at",stamp)||!text(meta,"id",tid)||!text(meta,"title",field(selected,"text"))||!text(meta,"valid_from",field(selected,"datefrom"))||!put(doc,"timetable",meta))goto oom;meta=NULL;
    if(!text(doc,"school",globals->count?field(globals->items[0],"reg_name"):NULL)||!put(doc,"days",days_copy))goto oom;days_copy=NULL;
    for(i=0;i<periods->count;i++){const Json *p=periods->items[i];Json *v=json_new(JSON_OBJECT);int num=json_integer(json_get(p,"period"),-1);if(num<0){json_free(v);snprintf(parseerr,sizeof(parseerr),"Non-numeric period ID");goto extraction_error;}if(!v||!number(v,"number",num)||!text(v,"name",field(p,"name"))||!text(v,"start",field(p,"starttime"))||!text(v,"end",field(p,"endtime"))||!put(periods_copy,NULL,v))goto oom;}
    if(!put(doc,"periods",periods_copy))goto oom;periods_copy=NULL;
    /* Validate all references, including unplaced cards and classes with no scheduled lessons. */
    for(i=0;i<lessons->count;i++){const Json *l=lessons->items[i],*ids=json_get(l,"classids"),*gs=json_get(l,"groupids"),*ts=json_get(l,"teacherids");if(!row(subjects,field(l,"subjectid"))||!valid_array(ids)||!valid_array(gs)||!valid_array(ts)||json_integer(json_get(l,"durationperiods"),0)<1){snprintf(parseerr,sizeof(parseerr),"Invalid lesson definition %s",field(l,"id"));goto extraction_error;}for(j=0;j<ids->count;j++)if(!row(srcclasses,json_string(ids->items[j]))){snprintf(parseerr,sizeof(parseerr),"Unresolved class in lesson %s",field(l,"id"));goto extraction_error;}for(j=0;j<gs->count;j++)if(!row(groups,json_string(gs->items[j]))){snprintf(parseerr,sizeof(parseerr),"Unresolved group in lesson %s",field(l,"id"));goto extraction_error;}for(j=0;j<ts->count;j++)if(!row(teachers,json_string(ts->items[j]))){snprintf(parseerr,sizeof(parseerr),"Unresolved teacher in lesson %s",field(l,"id"));goto extraction_error;}}
    for(i=0;i<cards->count;i++){const Json *card=cards->items[i],*l=row(lessons,field(card,"lessonid")),*ids=json_get(card,"classroomids");const char *dm=field(card,"days"),*wm=field(card,"weeks");if(!l||!valid_array(ids)||!valid_mask(dm,days->count,1)||!valid_mask(wm,weeks->count,1)){snprintf(parseerr,sizeof(parseerr),"Invalid card or reference %s",field(card,"id"));goto extraction_error;}for(j=0;j<ids->count;j++)if(!row(rooms,json_string(ids->items[j]))){snprintf(parseerr,sizeof(parseerr),"Unresolved room in card %s",field(card,"id"));goto extraction_error;}processed++;}
    for(i=0;i<srcclasses->count;i++) {
        const Json *cl=srcclasses->items[i];const char *cid=field(cl,"id"),*cname=field(cl,"name");Json *outclass=json_new(JSON_OBJECT),*outlessons=json_new(JSON_ARRAY);
        if(!cname||!*cname){json_free(outclass);json_free(outlessons);snprintf(parseerr,sizeof(parseerr),"Missing class name for ID %s",cid);goto extraction_error;}
        if(!outclass||!outlessons||!text(outclass,"id",cid)||!text(outclass,"name",cname)||!put(outclass,"source",json_clone(cl))||!put(outclass,"lessons",outlessons)||!put(classes,NULL,outclass))goto oom;
        for(j=0;j<cards->count;j++) {
            const Json *card=cards->items[j],*l=row(lessons,field(card,"lessonid")),*sub,*bell=NULL,*sp=NULL,*ep=NULL;
            const char *dm=field(card,"days"),*wm=field(card,"weeks"),*tm=field(l,"terms"),*bid;
            int period=json_integer(json_get(card,"period"),0),duration=json_integer(json_get(l,"durationperiods"),1),hasday=dm&&strchr(dm,'1')!=NULL;
            if(!contains_id(json_get(l,"classids"),cid))continue;sub=row(subjects,field(l,"subjectid"));
            bid=nonempty(field(l,"bell"));if(!bid)bid=nonempty(field(cl,"bell"));if(bid){bell=row(bells,bid);if(!bell){snprintf(parseerr,sizeof(parseerr),"Unknown bell schedule %s",bid);goto extraction_error;}}
            if(hasday&&period<=0){snprintf(parseerr,sizeof(parseerr),"Placed card %s lacks a valid period",field(card,"id"));goto extraction_error;}
            if(hasday){for(k=0;k<periods->count;k++){int n=json_integer(json_get(periods->items[k],"period"),-1);if(n==period)sp=periods->items[k];if(n==period+duration-1)ep=periods->items[k];}if(!sp||!ep){snprintf(parseerr,sizeof(parseerr),"Card %s extends outside period range",field(card,"id"));goto extraction_error;}}
            for(di=0;di<(hasday?days->count:1);di++) {
                Json *v,*a;char *joined;const Json *day;const char *dayid;
                if(hasday&&dm[di]!='1')continue;day=hasday?days->items[di]:NULL;dayid=field(day,"id");v=json_new(JSON_OBJECT);
                if(!v||!text(v,"id",field(l,"id"))||!text(v,"card_id",field(card,"id"))||!number(v,"day_index",hasday?(int)di:-1)||!text(v,"day",field(day,"name"))||!number(v,"period",period)||!number(v,"duration",duration)||!text(v,"start",hasday?time_value(sp,bell,dayid,"starttime"):NULL)||!text(v,"end",hasday?time_value(ep,bell,dayid,"endtime"):NULL)||!text(v,"subject",field(sub,"name"))||!text(v,"subject_short",field(sub,"short"))||!text(v,"color",field(sub,"color"))||!put(v,"days_mask",json_text(dm))||!put(v,"weeks_mask",json_text(wm))||!put(v,"terms_mask",json_text(tm))||!text(v,"schedule_status",hasday?"scheduled":"unplaced")||!put(v,"source_lesson",json_clone(l))||!put(v,"source_card",json_clone(card))){json_free(v);goto oom;}
                a=resolve(json_get(l,"teacherids"),teachers,NULL,0,parseerr,sizeof(parseerr));if(!a){json_free(v);goto extraction_error;}joined=join_names(a);if(!text(v,"teacher",joined)||!put(v,"teachers",a)){free(joined);json_free(v);goto oom;}free(joined);
                a=resolve(json_get(card,"classroomids"),rooms,NULL,0,parseerr,sizeof(parseerr));if(!a){json_free(v);goto extraction_error;}joined=join_names(a);if(!text(v,"room",joined)||!put(v,"rooms",a)){free(joined);json_free(v);goto oom;}free(joined);
                a=resolve(json_get(l,"groupids"),groups,cid,1,parseerr,sizeof(parseerr));if(!a){json_free(v);goto extraction_error;}joined=join_names(a);if(!text(v,"group",joined)||!put(v,"groups",a)){free(joined);json_free(v);goto oom;}free(joined);
                a=mask_labels(wm,weeks,parseerr,sizeof(parseerr));if(!a){json_free(v);goto extraction_error;}joined=join_names(a);if(!text(v,"week",joined)||!put(v,"weeks",a)){free(joined);json_free(v);goto oom;}free(joined);
                a=mask_labels(tm,terms,parseerr,sizeof(parseerr));if(!a){json_free(v);goto extraction_error;}if(!put(v,"terms",a)){json_free(v);goto oom;}
                /* Source texts may be a structured object. Preserve it and expose its exact JSON as notes. */
                {const Json *notes=json_get(l,"texts");char *ns=notes&&notes->type!=JSON_NULL?json_write(notes):NULL;if(ns){size_t n=strlen(ns);if(n&&ns[n-1]=='\n')ns[n-1]=0;}if(!text(v,"notes",ns)||!put(outlessons,NULL,v)){free(ns);goto oom;}free(ns);}
                records++;if(hasday)scheduled++;else unplaced++;
            }
        }
    }
    if(!put(doc,"classes",classes))goto oom;classes=NULL;
    if(!number(stats,"classes_discovered",(int)srcclasses->count)||!number(stats,"classes_processed",(int)srcclasses->count)||!number(stats,"classes_failed",0)||!number(stats,"source_cards",(int)cards->count)||!number(stats,"cards_processed",(int)processed)||!number(stats,"lesson_definitions",(int)lessons->count)||!number(stats,"lesson_records",(int)records)||!number(stats,"scheduled_records",(int)scheduled)||!number(stats,"unplaced_records",(int)unplaced)||!put(doc,"statistics",stats))goto oom;stats=NULL;
    if(!put(doc,"extraction_errors",json_new(JSON_ARRAY))||!put(doc,"source_tables",json_clone(tables)))goto oom;
    if(!load_document(doc,out,error,error_cap))goto done;doc=NULL;success=1;goto done;
extraction_error:errorf(error,error_cap,"Extraction stopped without replacing cached data: %s",parseerr[0]?parseerr:"Invalid source data");goto done;
oom:errorf(error,error_cap,"Out of memory extracting timetable data.");
done:json_free(raw);json_free(viewer);json_free(doc);json_free(classes);json_free(meta);json_free(stats);json_free(days_copy);json_free(periods_copy);return success;
}

static int integer_field(const Json *o,const char *key,int *out) {const Json *v=json_get(o,key);if(!v||v->type!=JSON_NUMBER)return 0;*out=json_integer(v,INT_MIN);return *out!=INT_MIN;}
static int string_fields(const Json *o,const char *const *keys,size_t count) {size_t i;for(i=0;i<count;i++){const Json *v=json_get(o,keys[i]);if(!v||(v->type!=JSON_STRING&&v->type!=JSON_NULL))return 0;}return 1;}
static int known_period(const TTData *d,int p) {size_t i;for(i=0;i<d->period_count;i++)if(d->periods[i].number==p)return 1;return 0;}
static int load_document(Json *doc,TTData **out,wchar_t *error,size_t cap) {
    TTData *d=NULL;const Json *cs,*ps,*ds,*meta,*stats;size_t i,j,k;int n,recorded;const char *const fields[]={"day","start","end","subject","teacher","room","group","week","notes","id","card_id","subject_short","color","days_mask","weeks_mask","terms_mask"};
    if(!out){errorf(error,cap,"Missing output parameter.");return 0;}*out=NULL;
    if(!doc||doc->type!=JSON_OBJECT||!integer_field(doc,"schema_version",&n)||n!=1||!field(doc,"source")||!field(doc,"downloaded_at"))goto malformed;
    cs=json_get(doc,"classes");ps=json_get(doc,"periods");ds=json_get(doc,"days");meta=json_get(doc,"timetable");stats=json_get(doc,"statistics");
    if(!valid_array(cs)||!cs->count||cs->count>10000||!valid_array(ps)||!ps->count||ps->count>128||!valid_array(ds)||!ds->count||ds->count>31||!field(meta,"id")||!field(meta,"title")||!field(meta,"valid_from"))goto malformed;
    d=(TTData*)calloc(1,sizeof(*d));if(!d)goto memory;
    d->classes=(TTClass*)calloc(cs->count,sizeof(TTClass));d->periods=(TTPeriod*)calloc(ps->count,sizeof(TTPeriod));d->days=(TTDay*)calloc(ds->count,sizeof(TTDay));if(!d->classes||!d->periods||!d->days)goto memory;
    d->class_count=cs->count;d->period_count=ps->count;d->day_count=ds->count;
    d->downloaded_at=(char*)field(doc,"downloaded_at");d->timetable_id=(char*)field(meta,"id");d->title=(char*)field(meta,"title");d->valid_from=(char*)field(meta,"valid_from");
    for(i=0;i<ds->count;i++){if(!field(ds->items[i],"id")||!field(ds->items[i],"name"))goto malformed;d->days[i].id=(char*)field(ds->items[i],"id");d->days[i].name=(char*)field(ds->items[i],"name");d->days[i].short_name=(char*)field(ds->items[i],"short");}
    for(i=0;i<ps->count;i++){const Json *p=ps->items[i];if(!integer_field(p,"number",&n)||n<0||n>1000)goto malformed;for(j=0;j<i;j++)if(d->periods[j].number==n)goto malformed;d->periods[i].number=n;d->periods[i].name=(char*)field(p,"name");d->periods[i].start=(char*)field(p,"start");d->periods[i].end=(char*)field(p,"end");}
    for(i=0;i<cs->count;i++){TTClass *c=&d->classes[i];const Json *source=cs->items[i],*ls=json_get(source,"lessons");if(!field(source,"id")||!field(source,"name")||!valid_array(ls)||ls->count>1000000)goto malformed;c->id=(char*)field(source,"id");c->name=(char*)field(source,"name");for(k=0;k<i;k++)if(!strcmp(c->id,d->classes[k].id))goto malformed;
        c->lesson_count=ls->count;if(ls->count)c->lessons=(TTLesson*)calloc(ls->count,sizeof(TTLesson));if(ls->count&&!c->lessons)goto memory;
        for(j=0;j<ls->count;j++){const Json *v=ls->items[j];TTLesson *l=&c->lessons[j];if(!string_fields(v,fields,sizeof(fields)/sizeof(fields[0]))||!integer_field(v,"day_index",&l->day_index)||l->day_index< -1||l->day_index>=(int)d->day_count||!integer_field(v,"period",&l->period)||!integer_field(v,"duration",&l->duration)||l->duration<1||l->duration>128)goto malformed;
            if(l->day_index>=0&&(!known_period(d,l->period)||!known_period(d,l->period+l->duration-1)||!field(v,"day")))goto malformed;
            l->day=(char*)field(v,"day");l->start=(char*)field(v,"start");l->end=(char*)field(v,"end");l->subject=(char*)field(v,"subject");l->teacher=(char*)field(v,"teacher");l->room=(char*)field(v,"room");l->group=(char*)field(v,"group");l->week=(char*)field(v,"week");l->notes=(char*)field(v,"notes");l->id=(char*)field(v,"id");l->card_id=(char*)field(v,"card_id");l->subject_short=(char*)field(v,"subject_short");l->color=(char*)field(v,"color");l->days_mask=(char*)field(v,"days_mask");l->weeks_mask=(char*)field(v,"weeks_mask");l->terms_mask=(char*)field(v,"terms_mask");
            if(!l->id||!l->card_id||!l->subject||!l->days_mask||!l->weeks_mask)goto malformed;
            d->lesson_count++;
        }
    }
    if(!integer_field(stats,"lesson_records",&recorded)||recorded!=(int)d->lesson_count||!integer_field(stats,"classes_processed",&recorded)||recorded!=(int)d->class_count||!integer_field(stats,"classes_discovered",&recorded)||recorded!=(int)d->class_count||!integer_field(stats,"classes_failed",&recorded)||recorded!=0||!integer_field(stats,"source_cards",&recorded)||recorded<0)goto malformed;d->source_card_count=(size_t)recorded;
    d->document=doc;*out=d;return 1;
malformed:errorf(error,cap,"The timetable JSON is missing required fields, contains invalid periods/references, or its counts disagree. The previous valid data remains available.");tt_free(d);return 0;
memory:errorf(error,cap,"Not enough memory to load timetable data.");tt_free(d);return 0;
}
void tt_free(TTData *d) {size_t i;if(!d)return;if(d->classes)for(i=0;i<d->class_count;i++)free(d->classes[i].lessons);free(d->classes);free(d->days);free(d->periods);json_free(d->document);free(d);}
static char *read_file(const wchar_t *path,wchar_t *error,size_t cap) {HANDLE h;LARGE_INTEGER size;DWORD got;char *s;h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);if(h==INVALID_HANDLE_VALUE){errorf(error,cap,"Cannot open timetable file (Windows error %lu).",GetLastError());return NULL;}if(!GetFileSizeEx(h,&size)||size.QuadPart<=0||size.QuadPart>64*1024*1024){CloseHandle(h);errorf(error,cap,"Timetable file is empty or exceeds 64 MiB.");return NULL;}s=(char*)malloc((size_t)size.QuadPart+1);if(!s){CloseHandle(h);errorf(error,cap,"Not enough memory reading timetable file.");return NULL;}if(!ReadFile(h,s,(DWORD)size.QuadPart,&got,NULL)||got!=(DWORD)size.QuadPart){free(s);CloseHandle(h);errorf(error,cap,"Cannot read complete timetable file.");return NULL;}CloseHandle(h);s[got]=0;if(memchr(s,0,got)){free(s);errorf(error,cap,"Timetable file contains embedded NUL bytes.");return NULL;}return s;}
int tt_load(const wchar_t *path,TTData **out,wchar_t *error,size_t error_cap) {char *s,parseerr[768];Json *v;int ok;if(out)*out=NULL;s=read_file(path,error,error_cap);if(!s)return 0;v=json_parse(s,parseerr,sizeof(parseerr));free(s);if(!v){errorf(error,error_cap,"Invalid JSON: %s",parseerr);return 0;}ok=load_document(v,out,error,error_cap);if(!ok)json_free(v);return ok;}
static int write_json_file(const wchar_t *path,const Json *v,wchar_t *error,size_t cap) {char *s=json_write(v),parseerr[256];Json *check;HANDLE h;DWORD wrote;size_t n;if(!s){errorf(error,cap,"Out of memory writing JSON.");return 0;}check=json_parse(s,parseerr,sizeof(parseerr));if(!check){free(s);errorf(error,cap,"Generated JSON did not pass validation: %s",parseerr);return 0;}json_free(check);n=strlen(s);if(n>MAXDWORD){free(s);return 0;}h=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);if(h==INVALID_HANDLE_VALUE){free(s);errorf(error,cap,"Cannot create staged JSON (Windows error %lu).",GetLastError());return 0;}if(!WriteFile(h,s,(DWORD)n,&wrote,NULL)||wrote!=(DWORD)n||!FlushFileBuffers(h)){CloseHandle(h);free(s);DeleteFileW(path);errorf(error,cap,"Cannot write and flush complete JSON.");return 0;}CloseHandle(h);free(s);return 1;}
/* Only removes the application-created temporary/backup directory; never follows reparse points. */
static void remove_tree(const wchar_t *dir) {wchar_t find[2048],child[2048];WIN32_FIND_DATAW item;HANDLE h;swprintf(find,2048,L"%ls\\*",dir);h=FindFirstFileW(find,&item);if(h!=INVALID_HANDLE_VALUE){do{if(!wcscmp(item.cFileName,L".")||!wcscmp(item.cFileName,L".."))continue;swprintf(child,2048,L"%ls\\%ls",dir,item.cFileName);if(item.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){if(item.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)RemoveDirectoryW(child);else remove_tree(child);}else DeleteFileW(child);}while(FindNextFileW(h,&item));FindClose(h);}RemoveDirectoryW(dir);}
static int class_filename(const TTClass *c,wchar_t *name,size_t cap) {wchar_t base[256],id[128];size_t i;if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,c->name,-1,base,256)||!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,c->id,-1,id,128))return 0;for(i=0;base[i];i++)if(base[i]<32||wcschr(L"<>:\"/\\|?*",base[i]))base[i]='_';for(i=0;id[i];i++)if(!((id[i]>='0'&&id[i]<='9')||(id[i]>='A'&&id[i]<='Z')||(id[i]>='a'&&id[i]<='z')||id[i]=='-'))id[i]='_';return swprintf(name,cap,L"%ls_%ls.json",base,id)>0;}
int tt_save(const wchar_t *dir,const TTData *data,wchar_t *error,size_t error_cap) {
    wchar_t stage[1024],stageclasses[1152],combined[1152],staged[1152],classes[1152],backup[1152],path[1536],name[512],errors[1152];
    size_t i;int moved_old=0,moved_new=0,ok=0;const Json *cs;TTData *validated=NULL;DWORD pid=GetCurrentProcessId();ULONGLONG tick=GetTickCount64();
    if(!dir||wcslen(dir)>800||!data||!data->document){errorf(error,error_cap,"Invalid save directory or timetable.");return 0;}
    if(!CreateDirectoryW(dir,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS){errorf(error,error_cap,"Cannot create timetable data directory.");return 0;}
    swprintf(stage,1024,L"%ls\\.pending-%lu-%llu",dir,pid,(unsigned long long)tick);swprintf(stageclasses,1152,L"%ls\\classes",stage);swprintf(staged,1152,L"%ls\\timetables.json",stage);swprintf(combined,1152,L"%ls\\timetables.json",dir);swprintf(classes,1152,L"%ls\\classes",dir);swprintf(backup,1152,L"%ls\\.previous-classes-%lu-%llu",dir,pid,(unsigned long long)tick);
    if(!CreateDirectoryW(stage,NULL)||!CreateDirectoryW(stageclasses,NULL)){errorf(error,error_cap,"Cannot stage timetable files.");goto done;}
    if(!write_json_file(staged,data->document,error,error_cap)||!tt_load(staged,&validated,error,error_cap))goto done;tt_free(validated);validated=NULL;
    cs=json_get(data->document,"classes");if(!valid_array(cs)||cs->count!=data->class_count){errorf(error,error_cap,"Class count mismatch while saving.");goto done;}
    for(i=0;i<data->class_count;i++) {Json *v=json_clone(cs->items[i]);if(!v||!text(v,"source_url",field(data->document,"source"))||!text(v,"downloaded_at",data->downloaded_at)||!put(v,"timetable",json_clone(json_get(data->document,"timetable")))){json_free(v);errorf(error,error_cap,"Cannot build individual class JSON.");goto done;}if(!class_filename(&data->classes[i],name,512)){json_free(v);errorf(error,error_cap,"Class name is not valid UTF-8 or is too long.");goto done;}swprintf(path,1536,L"%ls\\%ls",stageclasses,name);if(!write_json_file(path,v,error,error_cap)){json_free(v);goto done;}json_free(v);}
    /* Commit only after complete validation. Roll back the class directory if publishing combined data fails. */
    if(GetFileAttributesW(classes)!=INVALID_FILE_ATTRIBUTES){if(!MoveFileExW(classes,backup,MOVEFILE_WRITE_THROUGH)){errorf(error,error_cap,"Cannot preserve previous class JSON files (Windows error %lu).",GetLastError());goto done;}moved_old=1;}
    if(!MoveFileExW(stageclasses,classes,MOVEFILE_WRITE_THROUGH)){errorf(error,error_cap,"Cannot publish class JSON files (Windows error %lu).",GetLastError());goto rollback;}moved_new=1;
    if(!MoveFileExW(staged,combined,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){errorf(error,error_cap,"Cannot replace combined timetable JSON (Windows error %lu).",GetLastError());goto rollback;}
    ok=1;if(moved_old)remove_tree(backup);moved_old=0;moved_new=0;
    /* Separate diagnostics file; failed extraction is returned to the downloader's error log. */
    swprintf(errors,1152,L"%ls\\extraction_errors.json",stage);if(write_json_file(errors,json_get(data->document,"extraction_errors"),NULL,0)){swprintf(path,1536,L"%ls\\extraction_errors.json",dir);MoveFileExW(errors,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);}
    goto done;
rollback:if(moved_new){if(MoveFileExW(classes,stageclasses,MOVEFILE_WRITE_THROUGH))moved_new=0;}if(moved_old&&!moved_new){if(MoveFileExW(backup,classes,MOVEFILE_WRITE_THROUGH))moved_old=0;}
done:tt_free(validated);remove_tree(stage);return ok;
}



