#ifndef CT_TIMETABLE_H
#define CT_TIMETABLE_H
#include <stddef.h>
#include <wchar.h>
typedef struct TTPeriod { int number; char *name, *start, *end; } TTPeriod;
typedef struct TTDay { char *id, *name, *short_name; } TTDay;
typedef struct TTLesson {
    int day_index, period, duration;
    char *day, *start, *end, *subject, *teacher, *room, *group, *week, *notes;
    char *id, *card_id, *subject_short, *color, *days_mask, *weeks_mask, *terms_mask;
} TTLesson;
typedef struct TTClass { char *id, *name; TTLesson *lessons; size_t lesson_count; } TTClass;
/* All UTF-8 pointers below borrow storage from document; callers must not free or modify them. */
typedef struct TTData {
    char *downloaded_at, *timetable_id, *title, *valid_from;
    TTClass *classes; size_t class_count, lesson_count, source_card_count;
    TTDay *days; size_t day_count;
    TTPeriod *periods; size_t period_count;
    struct Json *document;
} TTData;
/* UTF-8 input and strings; paths/errors are UTF-16. All public functions return 1 on success. */
int tt_extract(const char *regular_json, const char *viewer_json, TTData **out, wchar_t *error, size_t error_cap);
int tt_load(const wchar_t *path, TTData **out, wchar_t *error, size_t error_cap);
/* dir is the data directory. Stages and validates every file before committing. */
int tt_save(const wchar_t *dir, const TTData *data, wchar_t *error, size_t error_cap);
void tt_free(TTData *data);
#endif

