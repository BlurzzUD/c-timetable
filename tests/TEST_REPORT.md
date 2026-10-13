# Verification report

Project: H:\projects\ctimetable
Verified date: 2026-10-13, Europe/Oslo

## Public source and data

- Public timetable page accessed successfully without authentication.
- Website class dropdown independently lists 11 classes; relational response also has 11 classes.
- Current regular timetable discovered dynamically: 80, valid from 2026-09-21.
- 322 lesson definitions and 496 source cards processed. Two teacher-only definitions (12 cards) have no class assignment and remain in the preserved source tables.
- All 11 class timetables downloaded successfully; 0 class failures.
- 628 class lesson records: 627 scheduled, 1 explicitly unplaced.
- Combined JSON and all 11 individual files validated by the C loader and independent strict UTF-8/JSON validation.
- All subjects, teacher/room/group references, masks, periods, duration and start/end times matched independently against the public response.

Evidence: `website-class-list.txt`, `independent-source-counts.json`, `data-validation.txt`, `model-test-results.txt`.

## Native C download and storage tests

- WinHTTP actual public refresh: PASS (11 classes, 628 records, 496 cards).
- Actual HTTP 404 failure: PASS; SHA-256 checked all 13 data JSON files, none changed.
- Save/load round trip and repeated save: PASS.
- Real locked destination prevents publication: PASS; old combined bytes unchanged, class directory restored, cache still loads.
- Missing and malformed cache, invalid field types/counts, truncated JSON, malformed UTF-8 and duplicate keys: PASS with helpful errors.

Evidence: `../logs/download.log`, `model-test-results.txt`.

## Scope

The application downloads the latest public regular weekly timetable. Date-specific substitutions, cancellations, holidays and calendar events are not merged. Teacher abbreviations are the public source values. The RPC format is undocumented and may need maintenance if EduPage changes it.

## GUI verification method

The desktop helper could enumerate the real viewer and read its native accessibility controls, but Windows.Graphics.Capture timed out and coordinate input was unavailable. The app's in-process native tests therefore exercise its actual Win32 controls, paint handler, class-selection/lesson-click messages, filtering, resizing and refresh-button handler. A BMP export from the same GDI grid renderer is used for visual inspection. This avoids claiming that external mouse automation succeeded.

## Final executable build and GUI tests

Final build: PASS, MSVC 19.51.36256 x64 C17, /W4, zero compiler warnings. Native Windows GUI subsystem, static C runtime, no CLR, no browser/runtime dependency. See `../logs/build.log` and `binary-validation.json`.

Final `ctimetable.exe --self-test`: exit 0.

- 18 malformed JSON fixtures rejected; 8 valid/Unicode fixtures round-tripped.
- Cached JSON matches every class/card/day from the real source fixture.
- Synthetic variants preserve Saturday, week B, multiple teachers/rooms, and final-period end time.
- Every class selected in the actual native dropdown with its correct model.
- Lesson details, actual GDI painting and click hit tests, exact class filtering, and native resizing passed.
- `gui-grid.bmp` from the application's own renderer inspected: readable Hungarian accents, weekday headers, times, teachers, rooms, groups, and separate parallel lesson cards.
- Visible GUI startup logged `visible=1` and correct caption. The native accessibility tree reports selector `9.A`, 66 class lesson records and `11 classes / 628 records`.

Final `ctimetable.exe --test-gui-refresh`: exit 0. The actual Refresh button handler started the asynchronous worker, disabled duplicate requests, allowed native painting while downloading, processed completion messages, saved 11 classes/628 records/496 cards, updated status and preserved selected class.

Final `ctimetable.exe --test-failed-refresh`: exit 0. Actual HTTP 404 was handled; SHA-256 hashes of all 13 data JSON files were unchanged.

Final `ctimetable.exe --validate`: exit 0, cache loaded without network requests. See `final-runtime-results.json` and `../logs/download.log`.

Startup/window bugs found during verification were fixed: default window-caption initialization, inherited hidden-launch visibility, initial status-bar parts, stale grid click geometry, grid keyboard navigation, and repeated display of an unplaced lesson.
