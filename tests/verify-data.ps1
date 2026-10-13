param([string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = 'Stop'
$lines = [Collections.Generic.List[string]]::new()
$utf8Strict = [Text.UTF8Encoding]::new($false, $true)
function Record([string]$Text) { $script:lines.Add($Text); Write-Output $Text }
function Check([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Read-Json([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $text = $script:utf8Strict.GetString($bytes)
    Check (-not $text.Contains([string][char]0xfffd)) ('Replacement character in ' + $Path)
    return ($text | ConvertFrom-Json)
}
function Source-Table($Root, [string]$Id) { return @($Root.r.dbiAccessorRes.tables | Where-Object id -CEQ $Id).data_rows }
function Index-Rows($Rows) { $index = @{}; foreach ($row in $Rows) { Check (-not $index.ContainsKey([string]$row.id)) ('Duplicate source ID: ' + $row.id); $index[[string]$row.id] = $row }; return ,$index }
function Nullable-String($Value) { if ($null -eq $Value -or [string]$Value -eq '') { return $null }; return [string]$Value }
function Resolve-Time($Period, $Bell, [string]$DayId, [string]$Field) {
    $value = Nullable-String $Period.$Field
    $day = $Period.daydata.$DayId
    if ($null -ne (Nullable-String $day.$Field)) { $value = [string]$day.$Field }
    $override = $Bell.perioddata.([string]$Period.id)
    if ($null -ne (Nullable-String $override.$Field)) { $value = [string]$override.$Field }
    $day = $override.daydata.$DayId
    if ($null -ne (Nullable-String $day.$Field)) { $value = [string]$day.$Field }
    return $value
}
function Id-Set($Ids) { return (@($Ids | ForEach-Object { [string]$_ } | Sort-Object) -join '|') }
function Record-Key([string]$ClassId, [string]$CardId, [int]$Day) { return $ClassId + '|' + $CardId + '|' + $Day }
try {
    $ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
    $raw = Read-Json (Join-Path $ProjectRoot 'inspection\regular_data.json')
    $priorCounts = Read-Json (Join-Path $ProjectRoot 'tests\independent-source-counts.json')
    $combined = Read-Json (Join-Path $ProjectRoot 'data\timetables.json')
    $sourceClasses = @(Source-Table $raw 'classes'); $lessons = Index-Rows (Source-Table $raw 'lessons')
    $cards = @(Source-Table $raw 'cards'); $days = @(Source-Table $raw 'days'); $periods = @(Source-Table $raw 'periods')
    $bells = Index-Rows (Source-Table $raw 'bells'); $subjects = Index-Rows (Source-Table $raw 'subjects')
    $groups = Index-Rows (Source-Table $raw 'groups'); $sourceClassIndex = Index-Rows $sourceClasses
    $periodIndex = @{}; foreach ($period in $periods) { $periodIndex[[int]$period.period] = $period }
    $expected = @{}; $expectedPerClass = @{}; $scheduled = 0; $unplaced = 0
    foreach ($class in $sourceClasses) { $expectedPerClass[[string]$class.id] = 0 }
    foreach ($card in $cards) {
        $lesson = $lessons[[string]$card.lessonid]
        Check ($null -ne $lesson) ('Unresolved source lesson: ' + $card.lessonid)
        $dayIndexes = @(); for ($dayIndex=0; $dayIndex -lt ([string]$card.days).Length; $dayIndex++) { if (([string]$card.days)[$dayIndex] -eq '1') { $dayIndexes += $dayIndex } }
        if ($dayIndexes.Count -eq 0) { $dayIndexes = @(-1) }
        foreach ($classId in $lesson.classids) {
            Check ($sourceClassIndex.ContainsKey([string]$classId)) ('Unresolved source class: ' + $classId)
            $class = $sourceClassIndex[[string]$classId]; $bellId = Nullable-String $lesson.bell
            if ($null -eq $bellId) { $bellId = Nullable-String $class.bell }; $bell = if ($null -ne $bellId) { $bells[$bellId] } else { $null }
            foreach ($dayIndex in $dayIndexes) {
                $key = Record-Key $classId $card.id $dayIndex
                Check (-not $expected.ContainsKey($key)) ('Duplicate expected record: ' + $key)
                $start=$null; $end=$null; $dayName=$null; $periodNumber=0; $groupIds=@()
                if ($dayIndex -ge 0) {
                    $periodNumber = [int]$card.period; $duration = [int]$lesson.durationperiods
                    $startPeriod = $periodIndex[$periodNumber]; $endPeriod = $periodIndex[($periodNumber + $duration - 1)]
                    Check ($null -ne $startPeriod -and $null -ne $endPeriod) ('Invalid source period range: ' + $key)
                    $start = Resolve-Time $startPeriod $bell ([string]$days[$dayIndex].id) 'starttime'
                    $end = Resolve-Time $endPeriod $bell ([string]$days[$dayIndex].id) 'endtime'
                    $dayName = [string]$days[$dayIndex].name; $scheduled++
                } else { $unplaced++ }
                foreach ($groupId in $lesson.groupids) { $group=$groups[[string]$groupId]; if ($null -eq $group.classid -or [string]$group.classid -eq [string]$classId) { $groupIds += [string]$groupId } }
                $expected[$key]=[pscustomobject]@{lesson=$lesson;card=$card;day=$dayName;period=$periodNumber;duration=[int]$lesson.durationperiods;start=$start;end=$end;subject=$subjects[[string]$lesson.subjectid].name;groups=$groupIds}
                $expectedPerClass[[string]$classId]++
            }
        }
    }
    Check ($sourceClasses.Count -eq $priorCounts.discovered_classes) 'Source class count changed from independent count report.'
    Check ($expected.Count -eq $priorCounts.total_records -and $scheduled -eq $priorCounts.scheduled_class_day_records -and $unplaced -eq $priorCounts.unplaced_class_records) 'Source record count changed from independent count report.'
    Check (@($combined.classes).Count -eq $sourceClasses.Count) 'Combined JSON does not contain every source class.'
    Check (@($combined.extraction_errors).Count -eq 0) 'Combined JSON has extraction errors.'
    Check ($combined.statistics.classes_discovered -eq $sourceClasses.Count -and $combined.statistics.classes_processed -eq $sourceClasses.Count -and $combined.statistics.classes_failed -eq 0) 'Class statistics disagree.'
    Check ($combined.statistics.source_cards -eq $cards.Count -and $combined.statistics.cards_processed -eq $cards.Count) 'Some source cards were not processed.'
    Check ($combined.statistics.lesson_records -eq $expected.Count -and $combined.statistics.scheduled_records -eq $scheduled -and $combined.statistics.unplaced_records -eq $unplaced) 'Record statistics disagree.'
    $seenClasses=@{}; $seenRecords=@{}; $recordTotal=0
    foreach ($class in $combined.classes) {
        $classId=[string]$class.id; Check ($sourceClassIndex.ContainsKey($classId)) ('Unexpected class: ' + $classId)
        Check (-not $seenClasses.ContainsKey($classId)) ('Duplicate class: ' + $classId); $seenClasses[$classId]=$class
        Check ([string]$class.name -ceq [string]$sourceClassIndex[$classId].name) ('Class name mismatch: ' + $classId)
        Check (@($class.lessons).Count -eq $expectedPerClass[$classId]) ('Missing class records: ' + $class.name)
        foreach ($record in $class.lessons) {
            $key=Record-Key $classId $record.card_id ([int]$record.day_index)
            Check ($expected.ContainsKey($key)) ('Unexpected class/card/day record: ' + $key)
            Check (-not $seenRecords.ContainsKey($key)) ('Duplicate record: ' + $key); $seenRecords[$key]=$true; $recordTotal++
            $item=$expected[$key]
            Check ([string]$record.id -ceq [string]$item.lesson.id) ('Lesson ID mismatch: ' + $key)
            Check ($record.period -eq $item.period -and $record.duration -eq $item.duration) ('Period/duration mismatch: ' + $key)
            Check ((Nullable-String $record.day) -ceq $item.day) ('Day mismatch: ' + $key)
            Check ((Nullable-String $record.start) -ceq $item.start -and (Nullable-String $record.end) -ceq $item.end) ('Start/end time mismatch: ' + $key)
            Check ([string]$record.subject -ceq [string]$item.subject) ('Subject mismatch: ' + $key)
            Check ([string]$record.days_mask -ceq [string]$item.card.days -and [string]$record.weeks_mask -ceq [string]$item.card.weeks -and [string]$record.terms_mask -ceq [string]$item.lesson.terms) ('Day/week/term masks changed: ' + $key)
            Check ((Id-Set @($record.teachers | ForEach-Object {$_.id})) -ceq (Id-Set $item.lesson.teacherids)) ('Teacher references changed: ' + $key)
            Check ((Id-Set @($record.rooms | ForEach-Object {$_.id})) -ceq (Id-Set $item.card.classroomids)) ('Room references changed: ' + $key)
            Check ((Id-Set @($record.groups | ForEach-Object {$_.id})) -ceq (Id-Set $item.groups)) ('Group references changed: ' + $key)
            if ($record.day_index -lt 0) { Check ($record.schedule_status -ceq 'unplaced' -and $null -eq $record.day -and $null -eq $record.start -and $null -eq $record.end) ('Unplaced data was invented: ' + $key) }
        }
        Record ('PASS class ' + $class.name + ': ' + @($class.lessons).Count + ' records')
    }
    Check ($seenRecords.Count -eq $expected.Count) 'Expected source records were silently dropped.'
    $classFiles=@(Get-ChildItem -LiteralPath (Join-Path $ProjectRoot 'data\classes') -Filter '*.json' -File)
    Check ($classFiles.Count -eq $sourceClasses.Count) 'Individual class JSON file count differs from source class count.'
    $seenFiles=@{}
    foreach ($file in $classFiles) {
        $individual=Read-Json $file.FullName; $id=[string]$individual.id
        Check ($seenClasses.ContainsKey($id) -and -not $seenFiles.ContainsKey($id)) ('Unexpected/duplicate individual class file: ' + $file.Name); $seenFiles[$id]=$true
        $class=$seenClasses[$id]
        Check ([string]$individual.name -ceq [string]$class.name -and @($individual.lessons).Count -eq @($class.lessons).Count) ('Individual file class/count mismatch: ' + $file.Name)
        Check (($individual.lessons | ConvertTo-Json -Depth 100 -Compress) -ceq ($class.lessons | ConvertTo-Json -Depth 100 -Compress)) ('Individual lesson content differs: ' + $file.Name)
        Check ($individual.downloaded_at -ceq $combined.downloaded_at) ('Individual timestamp differs: ' + $file.Name)
    }
    $monday='H'+[char]0x00e9+'tf'+[char]0x0151
    Check (@($combined.days | Where-Object name -CEQ $monday).Count -eq 1) 'Hungarian Monday name was not preserved.'
    $errors=Read-Json (Join-Path $ProjectRoot 'data\extraction_errors.json')
    Check (@($errors).Count -eq 0) 'Separate extraction error file is nonempty.'
    Record ('PASS strict UTF-8 and JSON: combined + ' + $classFiles.Count + ' individual class files + diagnostics')
    Record ('PASS Hungarian H' + [char]0x00e9 + 'tf' + [char]0x0151 + ' preserved exactly')
    Record ('PASS all ' + $sourceClasses.Count + ' public classes, ' + $cards.Count + ' source cards, ' + $recordTotal + ' class/day records (' + $scheduled + ' scheduled, ' + $unplaced + ' unplaced)')
    Record 'PASS independent source mapping, all subjects/teacher/room/group references, masks, periods, durations and end times'
    $exitCode=0
} catch { Record ('FAIL: ' + $_.Exception.Message); $exitCode=1 }
[IO.File]::WriteAllLines((Join-Path $ProjectRoot 'tests\data-validation.txt'), $lines, [Text.UTF8Encoding]::new($false))
exit $exitCode