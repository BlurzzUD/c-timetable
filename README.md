# CTimetable

A native Windows 10/11 timetable viewer and downloader for [Pollák EduPage](https://pollak.edupage.org/timetable/). Application and refresh logic are pure C17, using Win32, GDI, common controls, and WinHTTP. No browser, Python, Node, .NET, or external runtime is required.

## Run

Double-click `ctimetable.exe`. The application reads `data/timetables.json` next to the executable, so it works offline after a successful download. Use the search box to filter classes, select a class, and click a lesson to read its details. Parallel groups remain separate lessons. The grid scrolls when the window is small. Use **Refresh** to fetch the latest published regular timetable in the background.

Data and logs are stored next to the executable. Keep the executable, `data` folder, and `logs` folder in a writable directory. 

## Rebuild

Run `build.bat` from this directory. It detects installed Visual Studio C build tools through `vswhere` and configures the compiler automatically. A MinGW-w64 GCC toolchain with `windres` on PATH is also supported. The project was built and tested with MSVC x64 C17. The output is `ctimetable.exe`, a native Windows GUI executable with static C runtime and an embedded DPI/common-controls manifest.

## Public data source

The website's public React viewer calls two JSON RPC endpoints:

1. `POST /timetable/server/ttviewer.js?__func=getTTViewerData`, with `{"__args":[null,SCHOOL_YEAR],"__gsh":"PUBLIC_PAGE_TOKEN"}`.
2. `POST /timetable/server/regulartt.js?__func=regularttGetData`, with `{"__args":[null,"DEFAULT_TIMETABLE_ID"],"__gsh":"PUBLIC_PAGE_TOKEN"}`.

The downloader first reads `/timetable/`, obtains its public request token, and discovers the current timetable identifier from the metadata response. It never hardcodes a class list or timetable identifier. It uses only publicly accessible data, with normal TLS certificate validation. There is no login bypass or credential storage.

The response contains relational tables for classes, subjects, teacher abbreviations, rooms, groups, periods, days, weeks, lesson definitions, and scheduled cards. Every published class is processed. Each card becomes one record per participating class and scheduled day. Multi-period cards remain one record with a duration and an end time at the final period; parallel cards remain separate records. Unscheduled cards remain explicit records without an assigned day or period.

## Saved data

- `data/timetables.json`: combined UTF-8 JSON, including provenance and source data.
- `data/classes/*.json`: individual class documents; filenames are sanitized and include stable source IDs.
- `logs/download.log`: UTF-8 activity and validation log.
- `logs/last_download_error.json`: last failed attempt, separate from usable timetable data.

Missing values use JSON `null`; no names or times are invented. Teacher names unavailable publicly are represented by the published abbreviations. The source's week and term masks are retained along with their labels. Hungarian characters are stored as UTF-8 and converted to UTF-16 for Windows controls. Download timestamps include the Windows local UTC offset (for this machine, `+02:00`). Unscheduled lessons use `day_index: -1` and `period: 0`, with null day/time/room fields.

A refresh validates the full dataset before saving. Files are staged before replacing the cache; a failed download or extraction keeps the previous valid cache. An error in a class or reference fails the refresh visibly instead of silently omitting a class.

## Maintenance and test commands

The executable has optional unattended commands. As a GUI executable it writes results to `logs/download.log` and returns an exit code; it does not open a console.

- `ctimetable.exe --refresh`: download, validate, and save using the same C implementation as the button.
- `ctimetable.exe --validate`: load and validate cached JSON without any network request.
- `ctimetable.exe --self-test`: strict parser/Unicode tests, source fixture comparison when available, native GUI checks, and a grid rendering export.
- `ctimetable.exe --test-gui-refresh`: test the actual native Refresh handler, background painting, saved result and class selection.
- `ctimetable.exe --extract-fixture`: regenerate data from public responses saved in `inspection` during development.
- `ctimetable.exe --test-failed-refresh`: request a deliberately missing public resource and verify that the failure is reported without replacing data.

`inspection` contains development evidence and public source responses; it is not needed to run the application. `tests` contains independent verification and results.

## Scope and limitations

The app displays the latest published **regular weekly timetable**. It does not merge date-specific substitutions, cancellations, holidays, or school events from EduPage's separate current-calendar viewer. Historical timetables marked hidden are not selected. The public source currently supplies teacher abbreviations rather than full names.

EduPage's public RPC format is undocumented and can change. If access requires login in future, the app reports the failure and keeps its offline cache; it does not currently implement authenticated sessions. The school must publish the regular timetable publicly for refresh to succeed.


Verified delivery: **11 classes discovered, 11 downloaded, 0 failed; 628 class lesson records (627 scheduled, 1 unplaced).** See [the verification report](tests/TEST_REPORT.md) for actual test results and the desktop capture limitation.

