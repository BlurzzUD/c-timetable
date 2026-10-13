#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "json_parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>
#include <errno.h>
#include <stdint.h>

typedef struct Buffer { char *s; size_t n, cap; int failed; } Buffer;
typedef struct Parser { const char *begin, *p; char *error; size_t cap; int failed; } Parser;
static char *copy_text(const char *s) { size_t n; char *p; if (!s) return NULL; n=strlen(s); p=(char*)malloc(n+1); if(p) memcpy(p,s,n+1); return p; }
static void append(Buffer *b,const char *s,size_t n) {
    char *p; size_t cap; if(b->failed || n>SIZE_MAX-b->n-1) {b->failed=1;return;}
    if(b->n+n+1>b->cap) {cap=b->cap?b->cap:128; while(cap<b->n+n+1) {if(cap>SIZE_MAX/2) {cap=b->n+n+1;break;} cap*=2;} p=(char*)realloc(b->s,cap); if(!p) {b->failed=1;return;} b->s=p;b->cap=cap;}
    memcpy(b->s+b->n,s,n); b->n+=n;b->s[b->n]=0;
}
static void ch(Buffer *b,char c) { append(b,&c,1); }
static void fail(Parser *p,const char *msg) {if(!p->failed && p->error && p->cap) snprintf(p->error,p->cap,"%s at byte %llu",msg,(unsigned long long)(p->p-p->begin));p->failed=1;}
Json *json_new(JsonType type) {Json *v=(Json*)calloc(1,sizeof(*v));if(v)v->type=type;return v;}
Json *json_text(const char *text) {Json *v=json_new(text?JSON_STRING:JSON_NULL);if(v&&text) {v->string=copy_text(text);if(!v->string) {free(v);return NULL;}}return v;}
Json *json_num(int value) {Json *v=json_new(JSON_NUMBER);if(v)v->number=(double)value;return v;}
Json *json_bool(int value) {Json *v=json_new(JSON_BOOL);if(v)v->boolean=!!value;return v;}
void json_free(Json *v) {size_t i;if(!v)return;for(i=0;i<v->count;i++) {json_free(v->items[i]);if(v->keys)free(v->keys[i]);}free(v->items);free(v->keys);free(v->string);free(v);}
int json_add(Json *v,const char *key,Json *child) {
    Json **items;char **keys;char *keycopy=NULL;size_t i;
    if(!v||!child||(v->type!=JSON_ARRAY&&v->type!=JSON_OBJECT))return 0;
    if(v->type==JSON_OBJECT) {if(!key)return 0;for(i=0;i<v->count;i++)if(!strcmp(v->keys[i],key))return 0;keycopy=copy_text(key);if(!keycopy)return 0;}
    items=(Json**)realloc(v->items,(v->count+1)*sizeof(*items));if(!items) {free(keycopy);return 0;}v->items=items;
    if(v->type==JSON_OBJECT) {keys=(char**)realloc(v->keys,(v->count+1)*sizeof(*keys));if(!keys) {free(keycopy);return 0;}v->keys=keys;v->keys[v->count]=keycopy;}
    v->items[v->count++]=child;return 1;
}
const Json *json_get(const Json *v,const char *key) {size_t i;if(v&&v->type==JSON_OBJECT&&key)for(i=0;i<v->count;i++)if(!strcmp(v->keys[i],key))return v->items[i];return NULL;}
const char *json_string(const Json *v) {return v&&v->type==JSON_STRING?v->string:NULL;}
int json_integer(const Json *v,int fallback) {
    char *end;long n;
    if(!v)return fallback;if(v->type==JSON_NUMBER&&v->number>=INT_MIN&&v->number<=INT_MAX&&floor(v->number)==v->number)return (int)v->number;
    if(v->type==JSON_STRING&&v->string[0]) {errno=0;n=strtol(v->string,&end,10);if(!errno&&!*end&&n>=INT_MIN&&n<=INT_MAX)return (int)n;}return fallback;
}
static void ws(Parser *p) {while(*p->p==' '||*p->p=='\t'||*p->p=='\r'||*p->p=='\n')p->p++;}
static int hex4(Parser *p,unsigned *out) {unsigned x=0;int i;for(i=0;i<4;i++) {unsigned c=(unsigned char)*p->p;if(!c) {fail(p,"Incomplete Unicode escape");return 0;}p->p++;if(c>='0'&&c<='9')c-='0';else if(c>='a'&&c<='f')c=c-'a'+10;else if(c>='A'&&c<='F')c=c-'A'+10;else {fail(p,"Invalid Unicode escape");return 0;}x=x*16+c;}*out=x;return 1;}
static void utf8(Buffer *b,unsigned x) {char t[4];size_t n;if(x<0x80) {t[0]=(char)x;n=1;}else if(x<0x800) {t[0]=(char)(0xC0|(x>>6));t[1]=(char)(0x80|(x&63));n=2;}else if(x<0x10000) {t[0]=(char)(0xE0|(x>>12));t[1]=(char)(0x80|((x>>6)&63));t[2]=(char)(0x80|(x&63));n=3;}else {t[0]=(char)(0xF0|(x>>18));t[1]=(char)(0x80|((x>>12)&63));t[2]=(char)(0x80|((x>>6)&63));t[3]=(char)(0x80|(x&63));n=4;}append(b,t,n);}
static char *parse_string(Parser *p) {
    Buffer b={0};unsigned c,x,y;int n,j;const char *start;if(*p->p++!='"') {fail(p,"Expected string");return NULL;}
    while(*p->p&&*p->p!='"') {
        c=(unsigned char)*p->p++;
        if(c<32) {fail(p,"Unescaped control character");break;}
        if(c=='\\') {c=(unsigned char)*p->p++;if(c=='"'||c=='\\'||c=='/')ch(&b,(char)c);else if(c=='b')ch(&b,'\b');else if(c=='f')ch(&b,'\f');else if(c=='n')ch(&b,'\n');else if(c=='r')ch(&b,'\r');else if(c=='t')ch(&b,'\t');else if(c=='u') {if(!hex4(p,&x))break;if(x>=0xD800&&x<=0xDBFF) {if(p->p[0]!='\\'||p->p[1]!='u') {fail(p,"Missing low surrogate");break;}p->p+=2;if(!hex4(p,&y))break;if(y<0xDC00||y>0xDFFF) {fail(p,"Invalid low surrogate");break;}x=0x10000+((x-0xD800)<<10)+(y-0xDC00);}else if(x>=0xDC00&&x<=0xDFFF) {fail(p,"Unexpected low surrogate");break;}if(x==0) {fail(p,"NUL is unsupported in JSON string fields");break;}utf8(&b,x);}else {if(!c)p->p--;fail(p,"Invalid escape");break;}}
        else if(c<128)ch(&b,(char)c);
        else {start=p->p-1;if(c>=0xC2&&c<=0xDF){n=1;x=c&31;}else if(c>=0xE0&&c<=0xEF){n=2;x=c&15;}else if(c>=0xF0&&c<=0xF4){n=3;x=c&7;}else {fail(p,"Invalid UTF-8");break;}for(j=0;j<n;j++) {y=(unsigned char)*p->p;if(y<0x80||y>0xBF) {fail(p,"Invalid UTF-8 continuation");break;}p->p++;x=(x<<6)|(y&63);}if(p->failed)break;if((n==1&&x<0x80)||(n==2&&x<0x800)||(n==3&&x<0x10000)||(x>=0xD800&&x<=0xDFFF)||x>0x10FFFF) {fail(p,"Noncanonical UTF-8");break;}append(&b,start,(size_t)n+1);}
        if(b.failed) {fail(p,"Out of memory");break;}
    }
    if(!p->failed&&*p->p!='"')fail(p,"Unterminated string");if(p->failed){free(b.s);return NULL;}p->p++;if(!b.s)b.s=copy_text("");return b.s;
}
static Json *parse_value(Parser *p,int depth) {
    Json *v=NULL,*child;char *key=NULL,*end;const char *start;
    if(depth>64) {fail(p,"JSON nesting exceeds 64");return NULL;}ws(p);
    if(*p->p=='{'||*p->p=='[') {int object=*p->p=='{';char close=object?'}':']';p->p++;v=json_new(object?JSON_OBJECT:JSON_ARRAY);if(!v){fail(p,"Out of memory");return NULL;}ws(p);if(*p->p==close){p->p++;return v;}for(;;){if(object) {if(*p->p!='"'){fail(p,"Expected object key");break;}key=parse_string(p);if(!key)break;ws(p);if(*p->p!=':'){fail(p,"Expected colon");break;}p->p++;if(json_get(v,key)){fail(p,"Duplicate object key");break;}}child=parse_value(p,depth+1);if(!child)break;if(!json_add(v,key,child)){json_free(child);fail(p,"Out of memory");break;}free(key);key=NULL;ws(p);if(*p->p==close){p->p++;return v;}if(*p->p!=','){fail(p,"Expected comma or closing delimiter");break;}p->p++;ws(p);}free(key);json_free(v);return NULL;}
    if(*p->p=='"') {v=json_new(JSON_STRING);if(v)v->string=parse_string(p);if(!v||!v->string){json_free(v);if(!p->failed)fail(p,"Out of memory");return NULL;}return v;}
    if(!strncmp(p->p,"null",4)){p->p+=4;return json_new(JSON_NULL);}if(!strncmp(p->p,"true",4)){p->p+=4;return json_bool(1);}if(!strncmp(p->p,"false",5)){p->p+=5;return json_bool(0);}
    start=p->p;if(*p->p=='-')p->p++;if(*p->p=='0')p->p++;else if(*p->p>='1'&&*p->p<='9')while(*p->p>='0'&&*p->p<='9')p->p++;else {fail(p,"Expected JSON value");return NULL;}if(*p->p=='.'){p->p++;if(*p->p<'0'||*p->p>'9'){fail(p,"Missing fractional digits");return NULL;}while(*p->p>='0'&&*p->p<='9')p->p++;}if(*p->p=='e'||*p->p=='E'){p->p++;if(*p->p=='+'||*p->p=='-')p->p++;if(*p->p<'0'||*p->p>'9'){fail(p,"Missing exponent digits");return NULL;}while(*p->p>='0'&&*p->p<='9')p->p++;}v=json_new(JSON_NUMBER);if(!v){fail(p,"Out of memory");return NULL;}errno=0;v->number=strtod(start,&end);if(end!=p->p||errno==ERANGE||!isfinite(v->number)){json_free(v);fail(p,"Number out of range");return NULL;}return v;
}
Json *json_parse(const char *text,char *error,size_t cap) {
    Parser p;Json *v;if(error&&cap)error[0]=0;if(!text){if(error&&cap)snprintf(error,cap,"No JSON input");return NULL;}p.begin=p.p=text;p.error=error;p.cap=cap;p.failed=0;if((unsigned char)p.p[0]==0xEF&&(unsigned char)p.p[1]==0xBB&&(unsigned char)p.p[2]==0xBF)p.p+=3;v=parse_value(&p,0);if(!v)return NULL;ws(&p);if(v&&*p.p){fail(&p,"Trailing JSON content");json_free(v);return NULL;}return v;
}
Json *json_clone(const Json *v) {Json *c,*n;size_t i;if(!v)return json_new(JSON_NULL);c=json_new(v->type);if(!c)return NULL;c->number=v->number;c->boolean=v->boolean;if(v->string){c->string=copy_text(v->string);if(!c->string){json_free(c);return NULL;}}for(i=0;i<v->count;i++){n=json_clone(v->items[i]);if(!n||!json_add(c,v->keys?v->keys[i]:NULL,n)){json_free(n);json_free(c);return NULL;}}return c;}
static void write_string(Buffer *b,const char *s) {unsigned char c;char t[7];ch(b,'"');while((c=(unsigned char)*s++)!=0){if(c=='"'||c=='\\'){ch(b,'\\');ch(b,(char)c);}else if(c=='\n')append(b,"\\n",2);else if(c=='\r')append(b,"\\r",2);else if(c=='\t')append(b,"\\t",2);else if(c<32){snprintf(t,sizeof(t),"\\u%04x",c);append(b,t,6);}else ch(b,(char)c);}ch(b,'"');}
static void write_value(Buffer *b,const Json *v) {size_t i;char t[48];int n;if(!v){append(b,"null",4);return;}switch(v->type){case JSON_NULL:append(b,"null",4);break;case JSON_BOOL:append(b,v->boolean?"true":"false",v->boolean?4:5);break;case JSON_NUMBER:n=snprintf(t,sizeof(t),"%.17g",v->number);if(n<0||!isfinite(v->number)){b->failed=1;return;}append(b,t,(size_t)n);break;case JSON_STRING:write_string(b,v->string);break;case JSON_ARRAY:case JSON_OBJECT:ch(b,v->type==JSON_ARRAY?'[':'{');for(i=0;i<v->count;i++){if(i)ch(b,',');if(v->type==JSON_OBJECT){write_string(b,v->keys[i]);ch(b,':');}write_value(b,v->items[i]);}ch(b,v->type==JSON_ARRAY?']':'}');break;}}
char *json_write(const Json *v) {Buffer b={0};write_value(&b,v);ch(&b,'\n');if(b.failed){free(b.s);return NULL;}return b.s;}

