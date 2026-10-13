#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <limits.h>
#include "gui.h"
#include "timetable.h"
#include "downloader.h"
#define ID_SEARCH 101
#define ID_CLASS 102
#define ID_REFRESH 103
#define ID_DETAILS 104
#define ID_STATUS 105
#define ID_GRID 106
#define WM_REFRESH_DONE (WM_APP+1)
#define WM_TEST_SELECT_CLASS (WM_APP+20)
#define WM_TEST_CLASS_COUNT (WM_APP+21)
#define WM_TEST_SELECT_LESSON (WM_APP+22)
#define COL_NAVY RGB(24,43,66)
#define COL_TEAL RGB(0,116,120)
#define COL_BG RGB(242,246,250)
#define COL_LINE RGB(216,225,233)
#define COL_MUTED RGB(80,99,117)
typedef struct WBuf {wchar_t *s;size_t len,cap;} WBuf;
typedef struct GridRow {int period,top,height;} GridRow;
typedef struct GridHit {RECT rect;size_t lesson;} GridHit;
typedef struct RefreshJob {HWND window;wchar_t base[4096],error[2048];TTData *data;int ok;} RefreshJob;
typedef struct App {
 HINSTANCE instance;HWND window,search,classes,refresh,details,status,grid,search_label,class_label,detail_label;
 HFONT font,bold,title,small;HBRUSH white,background;TTData *data;wchar_t base[4096],message[2048];
 int dpi,current,scroll_x,scroll_y,content_w,content_h,column_w,label_w,header_h;
 int day_count,unplaced_day,refreshing,closing,layout_failed,testing,last_refresh_ok;GridRow *rows;size_t row_count,selected_lesson;
 GridHit *hits;size_t hit_count,hit_cap;HANDLE thread;RefreshJob *job;
} App;
static LRESULT CALLBACK main_proc(HWND,UINT,WPARAM,LPARAM);
static LRESULT CALLBACK grid_proc(HWND,UINT,WPARAM,LPARAM);
static int px(const App *a,int n){return MulDiv(n,a->dpi,96);}
static void copy_w(wchar_t *d,size_t cap,const wchar_t *s){if(!cap)return;wcsncpy(d,s?s:L"",cap-1);d[cap-1]=0;}
static wchar_t *wide(const char *s){int n;wchar_t *r;if(!s||!*s)s="Unavailable";n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,NULL,0);if(!n)return NULL;r=(wchar_t *)malloc((size_t)n*sizeof(wchar_t));if(r)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,r,n);return r;}
static int wb_add(WBuf *b,const wchar_t *s){size_t n=s?wcslen(s):0,cap;wchar_t *p;if(n>SIZE_MAX/sizeof(wchar_t)-b->len-1)return 0;if(b->len+n+1>b->cap){cap=b->cap?b->cap:256;while(cap<b->len+n+1){if(cap>SIZE_MAX/2)return 0;cap*=2;}p=(wchar_t *)realloc(b->s,cap*sizeof(wchar_t));if(!p)return 0;b->s=p;b->cap=cap;}if(n)memcpy(b->s+b->len,s,n*sizeof(wchar_t));b->len+=n;b->s[b->len]=0;return 1;}
static void wb_utf(WBuf *b,const char *s){wchar_t *p=wide(s);wb_add(b,p?p:L"Unavailable");free(p);}
static void wb_field(WBuf *b,const wchar_t *label,const char *s){wb_add(b,label);wb_utf(b,s);wb_add(b,L"\r\n");}
static char *copy_utf(const char *s){size_t n;char *p;if(!s)return NULL;n=strlen(s)+1;p=(char *)malloc(n);if(p)memcpy(p,s,n);return p;}
static TTClass *current_class(App *a){return a->data&&a->current>=0&&(size_t)a->current<a->data->class_count?&a->data->classes[a->current]:NULL;}
static int duration(const TTLesson *l){return l->duration>0?l->duration:1;}
static int lesson_day(const App *a,const TTLesson *l){return l->day_index>=0&&l->day_index<a->unplaced_day?l->day_index:a->unplaced_day;}
static int in_row(const TTLesson *l,int p){if(l->day_index<0)return p==l->period;return p>=l->period&&(long long)p<(long long)l->period+duration(l);}
static const TTPeriod *period_meta(const App *a,int n){size_t i;if(a->data)for(i=0;i<a->data->period_count;++i)if(a->data->periods[i].number==n)return &a->data->periods[i];return NULL;}
static wchar_t *card_subject(const TTLesson *l,int cont){WBuf b={0};if(cont)wb_add(&b,L"\x21B3 ");wb_utf(&b,l->subject);if(cont)wb_add(&b,L" (continues)");return b.s;}
static wchar_t *card_info(const TTLesson *l){WBuf b={0};wb_utf(&b,l->start);wb_add(&b,L" \x2013 ");wb_utf(&b,l->end);wb_add(&b,L"\r\n");wb_field(&b,L"Teacher: ",l->teacher);wb_field(&b,L"Room: ",l->room);if(l->group&&*l->group)wb_field(&b,L"Group: ",l->group);if(l->week&&*l->week)wb_field(&b,L"Week: ",l->week);if(l->notes&&*l->notes)wb_field(&b,L"Note: ",l->notes);if(b.len>=2&&b.s[b.len-2]==L'\r')b.s[b.len-=2]=0;return b.s;}
static int text_height(HDC dc,HFONT font,const wchar_t *s,int width){RECT r={0,0,width>0?width:1,0};HGDIOBJ old=SelectObject(dc,font);DrawTextW(dc,s?s:L"",-1,&r,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX|DT_EDITCONTROL);SelectObject(dc,old);return r.bottom>0?r.bottom:1;}
static int card_height(App *a,HDC dc,const TTLesson *l,int cont){wchar_t *s=card_subject(l,cont),*info=card_info(l);int width=a->column_w-px(a,34),h=text_height(dc,a->bold,s,width)+text_height(dc,a->small,info,width)+px(a,29);free(s);free(info);return h;}
static void status_update(App *a){wchar_t counts[160],updated[512],*stamp=a->data?wide(a->data->downloaded_at):NULL;swprintf(counts,160,L"%zu classes  \x2022  %zu records",a->data?a->data->class_count:0,a->data?a->data->lesson_count:0);swprintf(updated,512,L"Last update: %ls",stamp?stamp:L"none");SendMessageW(a->status,SB_SETTEXTW,0,(LPARAM)counts);SendMessageW(a->status,SB_SETTEXTW,1,(LPARAM)updated);SendMessageW(a->status,SB_SETTEXTW,2,(LPARAM)a->message);free(stamp);}
static void detail_class(App *a){TTClass *c=current_class(a);WBuf b={0};wchar_t n[128];a->selected_lesson=SIZE_MAX;if(c){wb_field(&b,L"Class: ",c->name);swprintf(n,128,L"%zu lesson records\r\n\r\n",c->lesson_count);wb_add(&b,n);wb_add(&b,L"Click a lesson for complete details.\r\n\r\nParallel lessons are stacked in each cell. Longer lessons also appear as continuations in their following periods.\r\n\r\nUse the scrollbars or mouse wheel to explore the complete week. In the grid, Up and Down select lessons; Enter opens their details.");}else{wb_add(&b,a->data?L"No classes match your search.":L"No saved timetable is available.\r\n\r\nChoose Refresh to download the public timetable. A successful download is saved for offline use.");if(!a->data&&*a->message){wb_add(&b,L"\r\n\r\n");wb_add(&b,a->message);}}SetWindowTextW(a->details,b.s?b.s:L"");free(b.s);}
static void select_lesson(App *a,size_t index){TTClass *c=current_class(a);TTLesson *l;WBuf b={0};wchar_t nums[180];if(!c||index>=c->lesson_count)return;l=&c->lessons[index];a->selected_lesson=index;wb_field(&b,L"Class: ",c->name);wb_field(&b,L"Day: ",l->day_index<0?"Unplaced / not assigned":l->day);swprintf(nums,180,L"Period: %d\r\nDuration: %d period%ls\r\n",l->period,duration(l),duration(l)==1?L"":L"s");wb_add(&b,nums);wb_field(&b,L"Starts: ",l->start);wb_field(&b,L"Ends: ",l->end);wb_add(&b,L"\r\n");wb_field(&b,L"Subject: ",l->subject);wb_field(&b,L"Abbreviation: ",l->subject_short);wb_field(&b,L"Teacher(s): ",l->teacher);wb_field(&b,L"Room(s): ",l->room);wb_field(&b,L"Group(s): ",l->group);wb_field(&b,L"Week / term: ",l->week);wb_field(&b,L"Notes: ",l->notes);wb_add(&b,L"\r\nSource details\r\n");wb_field(&b,L"Lesson ID: ",l->id);wb_field(&b,L"Card ID: ",l->card_id);wb_field(&b,L"Days mask: ",l->days_mask);wb_field(&b,L"Weeks mask: ",l->weeks_mask);wb_field(&b,L"Terms mask: ",l->terms_mask);wb_field(&b,L"Source color: ",l->color);wb_field(&b,L"Timetable ID: ",a->data->timetable_id);SetWindowTextW(a->details,b.s?b.s:L"");free(b.s);InvalidateRect(a->grid,NULL,FALSE);}
static int int_compare(const void *l,const void *r){int a=*(const int *)l,b=*(const int *)r;return(a>b)-(a<b);}
static void scroll_update(App *a){RECT rc;SCROLLINFO si={sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS,0,0,0,0,0};int max;GetClientRect(a->grid,&rc);max=a->content_h-(rc.bottom-a->header_h);if(max<0)max=0;if(a->scroll_y>max)a->scroll_y=max;if(a->scroll_y<0)a->scroll_y=0;si.nMin=0;si.nMax=a->content_h>0?a->content_h-1:0;si.nPage=(UINT)(rc.bottom>a->header_h?rc.bottom-a->header_h:1);si.nPos=a->scroll_y;SetScrollInfo(a->grid,SB_VERT,&si,TRUE);max=a->content_w-rc.right;if(max<0)max=0;if(a->scroll_x>max)a->scroll_x=max;if(a->scroll_x<0)a->scroll_x=0;si.nMax=a->content_w>0?a->content_w-1:0;si.nPage=(UINT)(rc.right>0?rc.right:1);si.nPos=a->scroll_x;SetScrollInfo(a->grid,SB_HORZ,&si,TRUE);}
static void grid_layout(App *a){
 RECT rc;TTClass *c=current_class(a);size_t i,j,cap=a->data?a->data->period_count:0,count=0,unique=0;int *numbers=NULL,has_unplaced=0,top=0,d;HDC dc;
 free(a->rows);a->rows=NULL;a->row_count=0;a->hit_count=0;a->layout_failed=0;a->unplaced_day=a->data?(int)a->data->day_count:5;if(a->unplaced_day<5)a->unplaced_day=5;
 if(c)for(i=0;i<c->lesson_count;++i){int len=duration(&c->lessons[i]);if(c->lessons[i].day_index<0||c->lessons[i].day_index>=a->unplaced_day)has_unplaced=1;if((size_t)len>SIZE_MAX-cap){a->layout_failed=1;return;}cap+=(size_t)len;}
 a->day_count=a->unplaced_day+has_unplaced;GetClientRect(a->grid,&rc);a->label_w=px(a,96);a->header_h=px(a,42);a->column_w=(rc.right-a->label_w)/a->day_count;if(a->column_w<px(a,190))a->column_w=px(a,190);a->content_w=a->label_w+a->column_w*a->day_count;
 if(cap)numbers=(int *)malloc(cap*sizeof(int));if(cap&&!numbers){a->layout_failed=1;return;}
 if(a->data)for(i=0;i<a->data->period_count;++i)numbers[count++]=a->data->periods[i].number;
 if(c)for(i=0;i<c->lesson_count;++i)for(j=0;j<(size_t)duration(&c->lessons[i]);++j)numbers[count++]=c->lessons[i].period+(int)j;
 if(count){qsort(numbers,count,sizeof(int),int_compare);for(i=0;i<count;++i)if(!i||numbers[i]!=numbers[i-1])numbers[unique++]=numbers[i];a->rows=(GridRow *)calloc(unique,sizeof(GridRow));if(!a->rows){free(numbers);a->layout_failed=1;return;}}
 a->row_count=unique;dc=GetDC(a->grid);
 for(i=0;i<unique;++i){int max_h=px(a,96);for(d=0;d<a->day_count;++d){int h=px(a,12);if(c)for(j=0;j<c->lesson_count;++j){const TTLesson *l=&c->lessons[j];if(lesson_day(a,l)==d&&in_row(l,numbers[i]))h+=card_height(a,dc,l,l->period!=numbers[i])+px(a,7);}if(h>max_h)max_h=h;}a->rows[i].period=numbers[i];a->rows[i].top=top;a->rows[i].height=max_h;if(max_h>INT_MAX-top){a->layout_failed=1;break;}top+=max_h;}
 ReleaseDC(a->grid,dc);free(numbers);a->content_h=top;scroll_update(a);InvalidateRect(a->grid,NULL,TRUE);
}
static int contains_i(const wchar_t *name,const wchar_t *query){size_t n=wcslen(name),q=wcslen(query),i;if(!q)return 1;if(q>n||q>INT_MAX)return 0;for(i=0;i+q<=n;++i)if(CompareStringOrdinal(name+i,(int)q,query,(int)q,TRUE)==CSTR_EQUAL)return 1;return 0;}
static void classes_rebuild(App *a){wchar_t query[512];size_t i;int first=-1,chosen=-1;GetWindowTextW(a->search,query,512);SendMessageW(a->classes,WM_SETREDRAW,FALSE,0);SendMessageW(a->classes,CB_RESETCONTENT,0,0);if(a->data)for(i=0;i<a->data->class_count;++i){wchar_t *name=wide(a->data->classes[i].name);if(name&&contains_i(name,query)){LRESULT row=SendMessageW(a->classes,CB_ADDSTRING,0,(LPARAM)name);if(row!=CB_ERR&&row!=CB_ERRSPACE){SendMessageW(a->classes,CB_SETITEMDATA,(WPARAM)row,(LPARAM)i);if(first<0)first=(int)row;if((int)i==a->current)chosen=(int)row;}}free(name);}if(chosen<0)chosen=first;SendMessageW(a->classes,CB_SETCURSEL,(WPARAM)chosen,0);a->current=chosen>=0?(int)SendMessageW(a->classes,CB_GETITEMDATA,(WPARAM)chosen,0):-1;SendMessageW(a->classes,WM_SETREDRAW,TRUE,0);InvalidateRect(a->classes,NULL,TRUE);a->scroll_x=a->scroll_y=0;detail_class(a);grid_layout(a);}
static COLORREF lesson_accent(const TTLesson *l){unsigned int h=0;const unsigned char *p=(const unsigned char *)(l->subject?l->subject:"");static const COLORREF c[]={RGB(0,116,120),RGB(68,101,171),RGB(132,84,153),RGB(170,103,46),RGB(64,126,85),RGB(168,78,105)};for(;*p;++p)h=h*33+*p;return c[h%(sizeof(c)/sizeof(*c))];}
static void hit_add(App *a,RECT r,size_t lesson){GridHit *p;size_t cap;if(a->hit_count==a->hit_cap){cap=a->hit_cap?a->hit_cap*2:64;p=(GridHit *)realloc(a->hits,cap*sizeof(GridHit));if(!p)return;a->hits=p;a->hit_cap=cap;}a->hits[a->hit_count].rect=r;a->hits[a->hit_count++].lesson=lesson;}
static void draw_text(HDC dc,HFONT font,COLORREF color,RECT r,const wchar_t *s,UINT flags){HGDIOBJ old=SelectObject(dc,font);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);DrawTextW(dc,s?s:L"",-1,&r,flags|DT_NOPREFIX);SelectObject(dc,old);}
static void grid_draw(App *a,HDC dc){
 RECT rc,r;TTClass *c=current_class(a);size_t i,j;int d;HPEN line=CreatePen(PS_SOLID,1,COL_LINE),old_pen=(HPEN)SelectObject(dc,line);HBRUSH navy=CreateSolidBrush(COL_NAVY),label_bg=CreateSolidBrush(RGB(232,239,245));
 a->hit_count=0;GetClientRect(a->grid,&rc);FillRect(dc,&rc,a->background);
 if(!c||a->layout_failed){r=rc;InflateRect(&r,-px(a,32),-px(a,70));draw_text(dc,a->font,COL_MUTED,r,a->layout_failed?L"There is not enough memory to lay out this timetable.":(a->data?L"No classes match the current filter.":L"Choose Refresh to download the timetable.\nSaved timetables can be viewed offline."),DT_CENTER|DT_WORDBREAK);}
 SaveDC(dc);IntersectClipRect(dc,0,a->header_h,rc.right,rc.bottom);
 for(i=0;c&&i<a->row_count;++i){int y=a->header_h+a->rows[i].top-a->scroll_y;if(y+a->rows[i].height<a->header_h||y>rc.bottom)continue;
  for(d=0;d<a->day_count;++d){int x=a->label_w+d*a->column_w-a->scroll_x,cy=y+px(a,7),found=0;if(x+a->column_w<0||x>rc.right)continue;MoveToEx(dc,x,y,NULL);LineTo(dc,x,y+a->rows[i].height);
   for(j=0;j<c->lesson_count;++j){const TTLesson *l=&c->lessons[j];if(lesson_day(a,l)==d&&in_row(l,a->rows[i].period)){
    int h=card_height(a,dc,l,l->period!=a->rows[i].period),sh;wchar_t *subject=card_subject(l,l->period!=a->rows[i].period),*info=card_info(l);HBRUSH accent=CreateSolidBrush(lesson_accent(l)),bg=CreateSolidBrush(j==a->selected_lesson?RGB(222,242,242):RGB(255,255,255));RECT card={x+px(a,7),cy,x+a->column_w-px(a,7),cy+h};RECT bar=card,text;
    FillRect(dc,&card,bg);FrameRect(dc,&card,j==a->selected_lesson?accent:label_bg);bar.right=bar.left+px(a,4);FillRect(dc,&bar,accent);text=card;text.left+=px(a,12);text.right-=px(a,8);text.top+=px(a,8);sh=text_height(dc,a->bold,subject,text.right-text.left);text.bottom=text.top+sh;draw_text(dc,a->bold,COL_NAVY,text,subject,DT_WORDBREAK|DT_EDITCONTROL);text.top=text.bottom+px(a,5);text.bottom=card.bottom-px(a,7);draw_text(dc,a->small,COL_MUTED,text,info,DT_WORDBREAK|DT_EDITCONTROL);
    if(card.bottom>a->header_h&&card.top<rc.bottom)hit_add(a,card,j);cy+=h+px(a,7);++found;DeleteObject(accent);DeleteObject(bg);free(subject);free(info);
   }}
   if(!found){r.left=x;r.right=x+a->column_w;r.top=y+px(a,22);r.bottom=y+px(a,55);draw_text(dc,a->small,RGB(150,166,181),r,L"\x2014",DT_CENTER|DT_SINGLELINE);}
  }
  MoveToEx(dc,-a->scroll_x,y+a->rows[i].height,NULL);LineTo(dc,a->content_w-a->scroll_x,y+a->rows[i].height);r.left=-a->scroll_x;r.right=a->label_w-a->scroll_x;r.top=y;r.bottom=y+a->rows[i].height;FillRect(dc,&r,label_bg);
  {wchar_t period[80];const TTPeriod *p=period_meta(a,a->rows[i].period);if(a->rows[i].period==0&&!p)copy_w(period,80,L"Unplaced");else swprintf(period,80,L"Period %d",a->rows[i].period);r.top+=px(a,12);r.bottom=r.top+px(a,26);draw_text(dc,a->bold,COL_NAVY,r,period,DT_CENTER|DT_SINGLELINE);if(p){WBuf times={0};wb_utf(&times,p->start);wb_add(&times,L"\n");wb_utf(&times,p->end);r.top=r.bottom;r.bottom=r.top+px(a,48);draw_text(dc,a->small,COL_MUTED,r,times.s,DT_CENTER);free(times.s);}}
 }
 RestoreDC(dc,-1);r=rc;r.bottom=a->header_h;FillRect(dc,&r,navy);
 for(d=0;d<a->day_count;++d){wchar_t *name;static const wchar_t *defaults[7]={L"Monday",L"Tuesday",L"Wednesday",L"Thursday",L"Friday",L"Saturday",L"Sunday"};r.left=a->label_w+d*a->column_w-a->scroll_x;r.right=r.left+a->column_w;r.top=0;r.bottom=a->header_h;if(a->data&&(size_t)d<a->data->day_count){name=wide(a->data->days[d].name);draw_text(dc,a->bold,RGB(255,255,255),r,name,DT_CENTER|DT_VCENTER|DT_SINGLELINE);free(name);}else draw_text(dc,a->bold,RGB(255,255,255),r,d==a->unplaced_day?L"Unplaced":(d<7?defaults[d]:L"Day"),DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
 r.left=-a->scroll_x;r.right=a->label_w-a->scroll_x;r.top=0;r.bottom=a->header_h;FillRect(dc,&r,navy);draw_text(dc,a->small,RGB(205,226,237),r,L"LESSON",DT_CENTER|DT_VCENTER|DT_SINGLELINE);if(GetFocus()==a->grid){r=rc;InflateRect(&r,-1,-1);DrawFocusRect(dc,&r);}SelectObject(dc,old_pen);DeleteObject(line);DeleteObject(navy);DeleteObject(label_bg);
}
static void scroll_grid(App *a,int bar,int action){SCROLLINFO si={sizeof(si),SIF_ALL,0,0,0,0,0};int *p=bar==SB_VERT?&a->scroll_y:&a->scroll_x;GetScrollInfo(a->grid,bar,&si);switch(action){case SB_LINEUP:*p-=px(a,36);break;case SB_LINEDOWN:*p+=px(a,36);break;case SB_PAGEUP:*p-=(int)si.nPage;break;case SB_PAGEDOWN:*p+=(int)si.nPage;break;case SB_THUMBTRACK:case SB_THUMBPOSITION:*p=si.nTrackPos;break;case SB_TOP:*p=0;break;case SB_BOTTOM:*p=si.nMax;break;default:return;}scroll_update(a);InvalidateRect(a->grid,NULL,FALSE);}
static void ensure_lesson_visible(App *a,size_t index){TTClass *c=current_class(a);RECT rc;size_t i,j;int y=0,x;HDC dc;if(!c||index>=c->lesson_count)return;GetClientRect(a->grid,&rc);dc=GetDC(a->grid);for(i=0;i<a->row_count;++i)if(a->rows[i].period==c->lessons[index].period){y=a->rows[i].top+px(a,7);for(j=0;j<index;++j)if(lesson_day(a,&c->lessons[j])==lesson_day(a,&c->lessons[index])&&in_row(&c->lessons[j],a->rows[i].period))y+=card_height(a,dc,&c->lessons[j],c->lessons[j].period!=a->rows[i].period)+px(a,7);break;}ReleaseDC(a->grid,dc);x=a->label_w+lesson_day(a,&c->lessons[index])*a->column_w;if(y<a->scroll_y||y>a->scroll_y+rc.bottom-a->header_h-px(a,100))a->scroll_y=y;if(x<a->scroll_x||x+a->column_w>a->scroll_x+rc.right)a->scroll_x=x-a->label_w;scroll_update(a);InvalidateRect(a->grid,NULL,FALSE);}
static LRESULT CALLBACK grid_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
 App *a=(App *)GetWindowLongPtrW(w,GWLP_USERDATA);if(msg==WM_NCCREATE){a=(App *)((CREATESTRUCTW *)lp)->lpCreateParams;SetWindowLongPtrW(w,GWLP_USERDATA,(LONG_PTR)a);return DefWindowProcW(w,msg,wp,lp);}if(!a)return DefWindowProcW(w,msg,wp,lp);
 switch(msg){
 case WM_ERASEBKGND:return 1;
 case WM_SIZE:if(a->font)grid_layout(a);return 0;
 case WM_PAINT:{PAINTSTRUCT ps;RECT rc;HDC target=BeginPaint(w,&ps),mem=CreateCompatibleDC(target);HBITMAP bitmap,old;GetClientRect(w,&rc);bitmap=CreateCompatibleBitmap(target,rc.right>0?rc.right:1,rc.bottom>0?rc.bottom:1);if(mem&&bitmap){old=(HBITMAP)SelectObject(mem,bitmap);grid_draw(a,mem);BitBlt(target,0,0,rc.right,rc.bottom,mem,0,0,SRCCOPY);SelectObject(mem,old);}else grid_draw(a,target);if(bitmap)DeleteObject(bitmap);if(mem)DeleteDC(mem);EndPaint(w,&ps);return 0;}
 case WM_PRINTCLIENT:grid_draw(a,(HDC)wp);return 0;
 case WM_VSCROLL:scroll_grid(a,SB_VERT,LOWORD(wp));return 0;
 case WM_HSCROLL:scroll_grid(a,SB_HORZ,LOWORD(wp));return 0;
 case WM_MOUSEWHEEL:{int amount=GET_WHEEL_DELTA_WPARAM(wp);if(GET_KEYSTATE_WPARAM(wp)&MK_SHIFT)a->scroll_x-=MulDiv(amount,px(a,100),WHEEL_DELTA);else a->scroll_y-=MulDiv(amount,px(a,100),WHEEL_DELTA);scroll_update(a);InvalidateRect(w,NULL,FALSE);return 0;}
 case WM_MOUSEHWHEEL:a->scroll_x+=MulDiv(GET_WHEEL_DELTA_WPARAM(wp),px(a,100),WHEEL_DELTA);scroll_update(a);InvalidateRect(w,NULL,FALSE);return 0;
 case WM_LBUTTONDOWN:{POINT p={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};size_t i;UpdateWindow(w);SetFocus(w);if(p.y>=a->header_h)for(i=0;i<a->hit_count;++i)if(PtInRect(&a->hits[i].rect,p)){select_lesson(a,a->hits[i].lesson);break;}return 0;}
 case WM_GETDLGCODE:if(lp&&(((MSG *)lp)->wParam==VK_RETURN||((MSG *)lp)->wParam==VK_SPACE))return DLGC_WANTARROWS|DLGC_WANTALLKEYS;return DLGC_WANTARROWS;
 case WM_KEYDOWN:{TTClass *c=current_class(a);size_t index=a->selected_lesson;if(wp==VK_LEFT)scroll_grid(a,SB_HORZ,SB_LINEUP);else if(wp==VK_RIGHT)scroll_grid(a,SB_HORZ,SB_LINEDOWN);else if(wp==VK_PRIOR)scroll_grid(a,SB_VERT,SB_PAGEUP);else if(wp==VK_NEXT)scroll_grid(a,SB_VERT,SB_PAGEDOWN);else if(wp==VK_HOME)scroll_grid(a,SB_VERT,SB_TOP);else if(wp==VK_END)scroll_grid(a,SB_VERT,SB_BOTTOM);else if(c&&c->lesson_count&&(wp==VK_UP||wp==VK_DOWN||wp==VK_RETURN||wp==VK_SPACE)){if(index==SIZE_MAX)index=0;else if(wp==VK_UP&&index)--index;else if(wp==VK_DOWN&&index+1<c->lesson_count)++index;select_lesson(a,index);ensure_lesson_visible(a,index);}return 0;}
 case WM_SETFOCUS:case WM_KILLFOCUS:InvalidateRect(w,NULL,FALSE);return 0;
 default:return DefWindowProcW(w,msg,wp,lp);
 }
}
static void fonts_create(App *a){
 if(a->font)DeleteObject(a->font);if(a->bold)DeleteObject(a->bold);if(a->title)DeleteObject(a->title);if(a->small)DeleteObject(a->small);
 a->font=CreateFontW(-px(a,15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");a->small=CreateFontW(-px(a,13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");a->bold=CreateFontW(-px(a,14),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");a->title=CreateFontW(-px(a,25),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
 if(a->search){HWND controls[8]={a->search,a->classes,a->refresh,a->details,a->status,a->search_label,a->class_label,a->detail_label};size_t i;for(i=0;i<8;++i)SendMessageW(controls[i],WM_SETFONT,(WPARAM)a->font,TRUE);}
}
static void main_layout(App *a){RECT rc,sr;int w,h,m=px(a,18),gap=px(a,14),dw=px(a,310),top=px(a,132),bottom,gw;int parts[3];GetClientRect(a->window,&rc);w=rc.right;h=rc.bottom;SendMessageW(a->status,WM_SIZE,0,0);GetWindowRect(a->status,&sr);bottom=h-(sr.bottom-sr.top)-m;MoveWindow(a->search_label,m,px(a,70),px(a,210),px(a,20),TRUE);MoveWindow(a->search,m,px(a,92),px(a,210),px(a,29),TRUE);MoveWindow(a->class_label,m+px(a,230),px(a,70),px(a,280),px(a,20),TRUE);MoveWindow(a->classes,m+px(a,230),px(a,92),px(a,300),px(a,420),TRUE);MoveWindow(a->refresh,w-m-px(a,160),px(a,88),px(a,160),px(a,34),TRUE);gw=w-2*m-dw-gap;if(gw<px(a,300))gw=px(a,300);MoveWindow(a->grid,m,top,gw,bottom>top?bottom-top:1,TRUE);MoveWindow(a->detail_label,m+gw+gap,top,dw,px(a,24),TRUE);MoveWindow(a->details,m+gw+gap,top+px(a,30),dw,bottom>top+px(a,30)?bottom-top-px(a,30):1,TRUE);parts[0]=px(a,205);parts[1]=w>px(a,1100)?px(a,605):px(a,510);parts[2]=-1;SendMessageW(a->status,SB_SETPARTS,3,(LPARAM)parts);status_update(a);InvalidateRect(a->window,NULL,TRUE);}
static DWORD WINAPI refresh_thread(void *param){RefreshJob *job=(RefreshJob *)param;job->ok=downloader_refresh(job->base,&job->data,job->error,sizeof(job->error)/sizeof(*job->error));PostMessageW(job->window,WM_REFRESH_DONE,0,(LPARAM)job);return 0;}
static void refresh_start(App *a){RefreshJob *job;if(a->refreshing)return;job=(RefreshJob *)calloc(1,sizeof(*job));if(!job){copy_w(a->message,2048,L"Not enough memory to start the download.");status_update(a);return;}job->window=a->window;copy_w(job->base,4096,a->base);a->job=job;a->refreshing=1;a->last_refresh_ok=0;EnableWindow(a->refresh,FALSE);SetWindowTextW(a->refresh,L"Downloading\x2026");copy_w(a->message,2048,L"Downloading latest timetable\x2026");status_update(a);a->thread=CreateThread(NULL,0,refresh_thread,job,0,NULL);if(!a->thread){a->job=NULL;a->refreshing=0;free(job);EnableWindow(a->refresh,TRUE);SetWindowTextW(a->refresh,L"Refresh timetable");copy_w(a->message,2048,L"Windows could not start the download worker.");status_update(a);}}
static void refresh_done(App *a,RefreshJob *job){char *id=NULL,*name=NULL;TTClass *c=current_class(a);size_t i;if(a->thread){WaitForSingleObject(a->thread,INFINITE);CloseHandle(a->thread);a->thread=NULL;}a->refreshing=0;a->job=NULL;a->last_refresh_ok=job->ok&&job->data;if(job->ok&&job->data){if(c){id=copy_utf(c->id);name=copy_utf(c->name);}tt_free(a->data);a->data=job->data;job->data=NULL;a->current=0;for(i=0;i<a->data->class_count;++i)if((id&&a->data->classes[i].id&&!strcmp(id,a->data->classes[i].id))||(!id&&name&&a->data->classes[i].name&&!strcmp(name,a->data->classes[i].name))){a->current=(int)i;break;}classes_rebuild(a);copy_w(a->message,2048,L"Timetable updated and saved for offline use.");}else{copy_w(a->message,2048,*job->error?job->error:L"The download failed. Existing saved data was retained.");if(!a->closing&&!a->testing)MessageBoxW(a->window,a->message,L"Timetable refresh failed",MB_OK|MB_ICONWARNING);}free(id);free(name);tt_free(job->data);free(job);EnableWindow(a->refresh,TRUE);SetWindowTextW(a->refresh,L"Refresh timetable");status_update(a);if(a->closing)DestroyWindow(a->window);}
static LRESULT CALLBACK main_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp){
 App *a=(App *)GetWindowLongPtrW(w,GWLP_USERDATA);if(msg==WM_NCCREATE){a=(App *)((CREATESTRUCTW *)lp)->lpCreateParams;SetWindowLongPtrW(w,GWLP_USERDATA,(LONG_PTR)a);a->window=w;return DefWindowProcW(w,msg,wp,lp);}if(!a)return DefWindowProcW(w,msg,wp,lp);
 switch(msg){
 case WM_CREATE:{DWORD child=WS_CHILD|WS_VISIBLE;
 a->search_label=CreateWindowExW(0,L"STATIC",L"Filter classes",child,0,0,0,0,w,NULL,a->instance,NULL);a->class_label=CreateWindowExW(0,L"STATIC",L"Class timetable",child,0,0,0,0,w,NULL,a->instance,NULL);a->detail_label=CreateWindowExW(0,L"STATIC",L"LESSON DETAILS",child,0,0,0,0,w,NULL,a->instance,NULL);
 a->search=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",child|WS_TABSTOP|ES_AUTOHSCROLL,0,0,0,0,w,(HMENU)(INT_PTR)ID_SEARCH,a->instance,NULL);SendMessageW(a->search,EM_SETCUEBANNER,TRUE,(LPARAM)L"Type a class name\x2026");SendMessageW(a->search,EM_SETLIMITTEXT,510,0);
 a->classes=CreateWindowExW(0,L"COMBOBOX",L"",child|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,0,0,0,0,w,(HMENU)(INT_PTR)ID_CLASS,a->instance,NULL);SendMessageW(a->classes,CB_SETMINVISIBLE,16,0);
 a->refresh=CreateWindowExW(0,L"BUTTON",L"Refresh timetable",child|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,w,(HMENU)(INT_PTR)ID_REFRESH,a->instance,NULL);
 a->details=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",child|WS_TABSTOP|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,0,0,0,0,w,(HMENU)(INT_PTR)ID_DETAILS,a->instance,NULL);SendMessageW(a->details,EM_SETLIMITTEXT,0x7FFFFFFE,0);SendMessageW(a->details,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(px(a,12),px(a,12)));
 a->status=CreateWindowExW(0,STATUSCLASSNAMEW,L"",child|SBARS_SIZEGRIP,0,0,0,0,w,(HMENU)(INT_PTR)ID_STATUS,a->instance,NULL);a->grid=CreateWindowExW(WS_EX_CLIENTEDGE,L"CTimetableGrid",L"Weekly timetable",child|WS_TABSTOP|WS_VSCROLL|WS_HSCROLL,0,0,0,0,w,(HMENU)(INT_PTR)ID_GRID,a->instance,a);
 if(!a->search||!a->classes||!a->refresh||!a->details||!a->status||!a->grid)return -1;fonts_create(a);classes_rebuild(a);status_update(a);main_layout(a);return 0;}
 case WM_SIZE:if(a->status)main_layout(a);return 0;
 case WM_GETMINMAXINFO:((MINMAXINFO *)lp)->ptMinTrackSize.x=px(a,970);((MINMAXINFO *)lp)->ptMinTrackSize.y=px(a,620);return 0;
 case WM_DPICHANGED:{RECT *r=(RECT *)lp;a->dpi=HIWORD(wp);fonts_create(a);SetWindowPos(w,NULL,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);main_layout(a);grid_layout(a);return 0;}
 case WM_COMMAND:if(LOWORD(wp)==ID_SEARCH&&HIWORD(wp)==EN_CHANGE){if(a->classes)classes_rebuild(a);return 0;}if(LOWORD(wp)==ID_CLASS&&HIWORD(wp)==CBN_SELCHANGE){LRESULT row=SendMessageW(a->classes,CB_GETCURSEL,0,0);a->current=row>=0?(int)SendMessageW(a->classes,CB_GETITEMDATA,(WPARAM)row,0):-1;a->scroll_x=a->scroll_y=0;detail_class(a);grid_layout(a);return 0;}if(LOWORD(wp)==ID_REFRESH&&HIWORD(wp)==BN_CLICKED){refresh_start(a);return 0;}break;
 case WM_REFRESH_DONE:refresh_done(a,(RefreshJob *)lp);return 0;
 case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:SetTextColor((HDC)wp,COL_NAVY);SetBkColor((HDC)wp,RGB(255,255,255));return (LRESULT)a->white;
 case WM_DRAWITEM:{DRAWITEMSTRUCT *di=(DRAWITEMSTRUCT *)lp;if(di->CtlID==ID_REFRESH){RECT r=di->rcItem;wchar_t text[80];HBRUSH b=CreateSolidBrush(di->itemState&ODS_DISABLED?RGB(125,157,158):(di->itemState&ODS_SELECTED?RGB(0,91,95):COL_TEAL));FillRect(di->hDC,&r,b);GetWindowTextW(a->refresh,text,80);draw_text(di->hDC,a->bold,RGB(255,255,255),r,text,DT_CENTER|DT_VCENTER|DT_SINGLELINE);if(di->itemState&ODS_FOCUS){InflateRect(&r,-3,-3);DrawFocusRect(di->hDC,&r);}DeleteObject(b);return TRUE;}break;}
 case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);RECT r;GetClientRect(w,&r);FillRect(dc,&r,a->white);r.left=px(a,18);r.top=px(a,13);r.right-=px(a,18);r.bottom=px(a,46);draw_text(dc,a->title,COL_NAVY,r,L"Poll\x00E1k timetable",DT_SINGLELINE);r.top=px(a,47);r.bottom=px(a,67);draw_text(dc,a->small,COL_MUTED,r,L"Every class. Full lesson details. Available offline.",DT_SINGLELINE);EndPaint(w,&ps);return 0;}
 case WM_ERASEBKGND:return 1;
 case WM_TEST_CLASS_COUNT:return a->data?(LRESULT)a->data->class_count:0;
 case WM_TEST_SELECT_CLASS:{LRESULT count=SendMessageW(a->classes,CB_GETCOUNT,0,0),i;for(i=0;i<count;++i)if(SendMessageW(a->classes,CB_GETITEMDATA,(WPARAM)i,0)==(LRESULT)wp){SendMessageW(a->classes,CB_SETCURSEL,(WPARAM)i,0);SendMessageW(w,WM_COMMAND,MAKEWPARAM(ID_CLASS,CBN_SELCHANGE),(LPARAM)a->classes);return a->current==(int)wp;}return 0;}
 case WM_TEST_SELECT_LESSON:select_lesson(a,(size_t)wp);ensure_lesson_visible(a,(size_t)wp);return a->selected_lesson==(size_t)wp;
 case WM_CLOSE:if(a->refreshing){a->closing=1;ShowWindow(w,SW_HIDE);}else DestroyWindow(w);return 0;
 case WM_DESTROY:PostQuitMessage(0);return 0;
 default:break;
 }
 return DefWindowProcW(w,msg,wp,lp);
}
static int app_initialize(App *a,HINSTANCE instance,const wchar_t *base){WNDCLASSEXW wc={sizeof(wc)};INITCOMMONCONTROLSEX common={sizeof(common),ICC_BAR_CLASSES|ICC_STANDARD_CLASSES};wchar_t path[4096];HDC dc;memset(a,0,sizeof(*a));a->instance=instance;a->current=0;a->selected_lesson=SIZE_MAX;a->dpi=96;copy_w(a->base,4096,base);dc=GetDC(NULL);if(dc){a->dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(NULL,dc);}a->white=CreateSolidBrush(RGB(255,255,255));a->background=CreateSolidBrush(COL_BG);InitCommonControlsEx(&common);wc.style=CS_HREDRAW|CS_VREDRAW;wc.lpfnWndProc=main_proc;wc.hInstance=instance;wc.hCursor=LoadCursorW(NULL,IDC_ARROW);wc.hIcon=LoadIconW(NULL,IDI_APPLICATION);wc.hIconSm=wc.hIcon;wc.hbrBackground=a->white;wc.lpszClassName=L"CTimetableMain";if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 0;wc.lpfnWndProc=grid_proc;wc.hbrBackground=a->background;wc.lpszClassName=L"CTimetableGrid";if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 0;swprintf(path,4096,L"%ls\\data\\timetables.json",base);if(!tt_load(path,&a->data,a->message,2048))a->current=-1;else copy_w(a->message,2048,L"Ready \x2014 saved timetables loaded.");a->window=CreateWindowExW(0,L"CTimetableMain",L"Poll\x00E1k \x2014 Class Timetables",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,px(a,1460),px(a,900),NULL,NULL,instance,a);return a->window!=NULL;}
static void app_dispose(App *a){tt_free(a->data);free(a->rows);free(a->hits);if(a->font)DeleteObject(a->font);if(a->bold)DeleteObject(a->bold);if(a->title)DeleteObject(a->title);if(a->small)DeleteObject(a->small);if(a->white)DeleteObject(a->white);if(a->background)DeleteObject(a->background);UnregisterClassW(L"CTimetableMain",a->instance);UnregisterClassW(L"CTimetableGrid",a->instance);}
int gui_run(HINSTANCE instance,int show,const wchar_t *base_dir){App app;MSG msg;int result=0,r;if(!app_initialize(&app,instance,base_dir)){MessageBoxW(NULL,L"Windows could not create the timetable viewer.",L"Timetable viewer",MB_OK|MB_ICONERROR);app_dispose(&app);return 1;}ShowWindow(app.window,show==SW_HIDE?SW_SHOWNORMAL:show);if(!IsWindowVisible(app.window))ShowWindow(app.window,SW_SHOW);UpdateWindow(app.window);{wchar_t startup[768],caption[256];GetWindowTextW(app.window,caption,256);swprintf(startup,768,L"GUI startup: show=%d, visible=%d, window=%p, title=%ls",show,IsWindowVisible(app.window),(void *)app.window,caption);app_log(base_dir,startup);}while((r=(int)GetMessageW(&msg,NULL,0,0))>0){if(!IsDialogMessageW(app.window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}if(r<0)result=1;app_dispose(&app);return result;}
/* Export the same native painting used on screen for reproducible visual QA. */
static int save_grid_snapshot(App *a){
 RECT r;HDC dc,mem;HBITMAP image,old;BITMAPINFO info;BITMAPFILEHEADER header;unsigned char *bits=NULL;FILE *file=NULL;wchar_t path[4096];DWORD size;int ok=0;
 GetClientRect(a->grid,&r);if(r.right<1||r.bottom<1||r.right>16000||r.bottom>16000)return 0;
 size=(DWORD)r.right*(DWORD)r.bottom*4u;dc=GetDC(a->grid);mem=CreateCompatibleDC(dc);image=CreateCompatibleBitmap(dc,r.right,r.bottom);
 if(!mem||!image)goto done;
 old=(HBITMAP)SelectObject(mem,image);SendMessageW(a->grid,WM_PRINTCLIENT,(WPARAM)mem,PRF_CLIENT);SelectObject(mem,old);
 bits=(unsigned char *)malloc(size);if(!bits)goto done;memset(&info,0,sizeof(info));info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=r.right;info.bmiHeader.biHeight=-r.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;info.bmiHeader.biSizeImage=size;
 if(GetDIBits(dc,image,0,(UINT)r.bottom,bits,&info,DIB_RGB_COLORS)!=(int)r.bottom)goto done;
 memset(&header,0,sizeof(header));header.bfType=0x4D42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+size;
 swprintf(path,4096,L"%ls\\tests",a->base);CreateDirectoryW(path,NULL);swprintf(path,4096,L"%ls\\tests\\gui-grid.bmp",a->base);file=_wfopen(path,L"wb");if(!file)goto done;
 ok=fwrite(&header,1,sizeof(header),file)==sizeof(header)&&fwrite(&info.bmiHeader,1,sizeof(BITMAPINFOHEADER),file)==sizeof(BITMAPINFOHEADER)&&fwrite(bits,1,size,file)==size;
done:
 if(file)fclose(file);free(bits);if(image)DeleteObject(image);if(mem)DeleteDC(mem);if(dc)ReleaseDC(a->grid,dc);return ok;
}
static int test_fail(wchar_t *error,size_t cap,const wchar_t *s){copy_w(error,cap,s);return 0;}
int gui_self_test(HINSTANCE instance,const wchar_t *base_dir,wchar_t *error,size_t error_cap){
 App a;size_t i;int ok=0;HDC dc=NULL,mem=NULL;HBITMAP bitmap=NULL,old=NULL;RECT rc;wchar_t detail[8192];
 if(!app_initialize(&a,instance,base_dir)){app_dispose(&a);return test_fail(error,error_cap,L"Native GUI window creation failed.");}if(!a.data||!a.data->class_count){test_fail(error,error_cap,L"GUI self-test needs a valid downloaded timetable.");goto done;}
 if(SendMessageW(a.classes,CB_GETCOUNT,0,0)!=(LRESULT)a.data->class_count){test_fail(error,error_cap,L"Dropdown does not contain every class.");goto done;}
 for(i=0;i<a.data->class_count;++i){if(!SendMessageW(a.window,WM_TEST_SELECT_CLASS,(WPARAM)i,0)||current_class(&a)!=&a.data->classes[i]){test_fail(error,error_cap,L"Selecting a class did not select its own timetable.");goto done;}if(a.data->classes[i].lesson_count){if(!SendMessageW(a.window,WM_TEST_SELECT_LESSON,0,0)){test_fail(error,error_cap,L"Lesson selection failed.");goto done;}GetWindowTextW(a.details,detail,8192);if(!wcsstr(detail,L"Subject:")||!wcsstr(detail,L"Teacher(s):")||!wcsstr(detail,L"Card ID:")){test_fail(error,error_cap,L"Lesson details are incomplete.");goto done;}}if(a.layout_failed){test_fail(error,error_cap,L"Timetable layout failed.");goto done;}}
 SendMessageW(a.window,WM_TEST_SELECT_CLASS,0,0);SetWindowPos(a.window,NULL,0,0,px(&a,1000),px(&a,650),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);main_layout(&a);grid_layout(&a);GetClientRect(a.grid,&rc);dc=GetDC(a.grid);mem=CreateCompatibleDC(dc);bitmap=CreateCompatibleBitmap(dc,rc.right,rc.bottom);if(!mem||!bitmap){test_fail(error,error_cap,L"Could not create a native painting surface.");goto done;}old=(HBITMAP)SelectObject(mem,bitmap);SendMessageW(a.grid,WM_PRINTCLIENT,(WPARAM)mem,PRF_CLIENT);
 if(current_class(&a)->lesson_count&&!a.hit_count){test_fail(error,error_cap,L"Painting produced no selectable lessons.");goto done;}if(a.hit_count){POINT point={a.hits[0].rect.left+px(&a,12),a.hits[0].rect.top+px(&a,12)};size_t expected=a.hits[0].lesson;if(point.y<a.header_h)point.y=a.header_h+1;SendMessageW(a.grid,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(point.x,point.y));if(a.selected_lesson!=expected){test_fail(error,error_cap,L"Clicking a painted lesson did not open its details.");goto done;}}
 {wchar_t *query=wide(a.data->classes[a.data->class_count-1].name);SetWindowTextW(a.search,query?query:L"");free(query);if(SendMessageW(a.classes,CB_GETCOUNT,0,0)<1){test_fail(error,error_cap,L"Class filtering lost an exact class match.");goto done;}SetWindowTextW(a.search,L"");}
 SetWindowPos(a.window,NULL,0,0,px(&a,1500),px(&a,920),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);main_layout(&a);grid_layout(&a);SendMessageW(a.window,WM_TEST_SELECT_CLASS,0,0);if(!save_grid_snapshot(&a)){test_fail(error,error_cap,L"Native grid bitmap export failed.");goto done;}copy_w(error,error_cap,L"Native GUI: every class selected, lesson details, painting/clicks, filtering and resizing passed.");ok=1;
done:
 if(old&&mem)SelectObject(mem,old);if(bitmap)DeleteObject(bitmap);if(mem)DeleteDC(mem);if(dc)ReleaseDC(a.grid,dc);DestroyWindow(a.window);app_dispose(&a);return ok;
}








/* Exercise the actual button handler and message pump against the public source. */
int gui_refresh_test(HINSTANCE instance,const wchar_t *base_dir,wchar_t *error,size_t error_cap){
 App a;MSG msg;ULONGLONG started;char *selected=NULL;int ok=0,timed_out=0;wchar_t status[512];HDC paint;TTClass *c;
 if(!app_initialize(&a,instance,base_dir)){app_dispose(&a);return test_fail(error,error_cap,L"Refresh test could not create the native window.");}
 a.testing=1;
 if(!a.data||!a.data->class_count){test_fail(error,error_cap,L"Refresh test needs a valid initial saved timetable.");goto done;}
 SendMessageW(a.window,WM_TEST_SELECT_CLASS,a.data->class_count>1?1:0,0);c=current_class(&a);selected=copy_utf(c->id);
 if(!selected){test_fail(error,error_cap,L"Refresh test could not copy the selected class ID.");goto done;}
 if(c->lesson_count)SendMessageW(a.window,WM_TEST_SELECT_LESSON,0,0);
 started=GetTickCount64();SendMessageW(a.window,WM_COMMAND,MAKEWPARAM(ID_REFRESH,BN_CLICKED),(LPARAM)a.refresh);
 if(!a.refreshing||!a.thread||IsWindowEnabled(a.refresh)){test_fail(error,error_cap,L"Refresh button did not start its asynchronous worker and disable duplicate requests.");goto done;}
 paint=GetDC(a.grid);SendMessageW(a.grid,WM_PRINTCLIENT,(WPARAM)paint,PRF_CLIENT);ReleaseDC(a.grid,paint);
 if(GetTickCount64()-started>5000){test_fail(error,error_cap,L"Native painting was blocked while the refresh worker was active.");goto done;}
 while(a.refreshing&&GetTickCount64()-started<120000){
  while(PeekMessageW(&msg,NULL,0,0,PM_REMOVE)){if(msg.message!=WM_QUIT){TranslateMessage(&msg);DispatchMessageW(&msg);}}
  if(a.refreshing)MsgWaitForMultipleObjects(0,NULL,FALSE,50,QS_ALLINPUT);
 }
 if(a.refreshing){timed_out=1;test_fail(error,error_cap,L"The refresh did not finish within the 120 second test limit.");goto done;}
 if(!a.last_refresh_ok||!a.data||!a.data->class_count||!a.data->lesson_count||!a.data->downloaded_at||!*a.data->downloaded_at||!IsWindowEnabled(a.refresh)){test_fail(error,error_cap,*a.message?a.message:L"Refresh did not publish a valid updated timetable.");goto done;}
 c=current_class(&a);if(!c||!c->id||strcmp(c->id,selected)){test_fail(error,error_cap,L"Refresh changed the selected class.");goto done;}
 SendMessageW(a.status,SB_GETTEXTW,2,(LPARAM)status);
 if(wcscmp(status,L"Timetable updated and saved for offline use.")||LOWORD(SendMessageW(a.status,SB_GETTEXTLENGTHW,1,0))==0){test_fail(error,error_cap,L"Refresh status and last successful update were not displayed.");goto done;}
 copy_w(error,error_cap,L"PASS: actual Refresh button handler, disabled duplicate requests, concurrent native painting, message pump, saved result, status and selected class preservation.");ok=1;
done:
 /* A failed test still owns the worker until it finishes; never free its window state early. */
 if(a.refreshing){
  a.testing=1;
  while(a.refreshing){while(PeekMessageW(&msg,NULL,0,0,PM_REMOVE)){if(msg.message!=WM_QUIT){TranslateMessage(&msg);DispatchMessageW(&msg);}}if(a.refreshing)MsgWaitForMultipleObjects(0,NULL,FALSE,50,QS_ALLINPUT);}
 }
 if(timed_out)ok=0;
 free(selected);DestroyWindow(a.window);app_dispose(&a);return ok;
}

