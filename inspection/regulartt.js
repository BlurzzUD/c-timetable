"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports._hotswap = exports.RegularTTView = void 0;
var tslib_1 = require("tslib");
var React = require("react");
var asc_react_1 = require("../asc/asc_react");
var dbi_1 = require("../rpr/dbi");
var edurequest_1 = require("../asc/edurequest");
var regulartt_1 = require("./server/regulartt");
var ttdoc_1 = require("./app/ttdoc");
var ttviewer_1 = require("./ttviewer");
var ttview_components_1 = require("./ttview_components");
var timetable_1 = require("../substitution/online/timetable");
var ttitem_1 = require("./ttitem");
var print_1 = require("./online/print");
var render_1 = require("./online/render");
var maindbi_ui_1 = require("../rpr/maindbi_ui");
function cardOnPage(card, table, id) {
    if (!card.umiestnena)
        return false;
    switch (table) {
        case 'classes': return card.classes.includes(id);
        case 'teachers': return card.teachers.includes(id);
        case 'classrooms': return card.classrooms.includes(id);
        case 'students': return card.students.includes(id);
    }
    return false;
}
function dozorOnPage(dozor, table, id) {
    if (table == 'teachers')
        return dozor.teacherid == id;
    if (table == 'classrooms')
        return dozor.classroomid == id;
    return false;
}
function viewerItemTime(dbi, uniperiod, durationperiods, day, classid, teacherid, classroomid, lrow) {
    var kind = dbi_1.DBITools.typeOfUniPeriod(uniperiod);
    if (kind != 'period' && kind != 'break')
        return null;
    var timeOf = function (up) {
        var isBreak = dbi_1.DBITools.typeOfUniPeriod(up) == 'break';
        var pb = {
            table: isBreak ? 'breaks' : 'periods',
            id: isBreak ? up.substring(1) : up,
        };
        if (!dbi.rowData(pb.table, pb.id))
            return null;
        return (0, print_1.getPeriodBreakTime)(dbi, pb, day, classid, teacherid, classroomid, lrow);
    };
    var t1 = timeOf(uniperiod);
    if (!(t1 === null || t1 === void 0 ? void 0 : t1.starttime))
        return null;
    var endtime = t1.endtime;
    var duration = durationperiods || 1;
    if (kind == 'period' && duration > 1) {
        var t2 = timeOf(dbi_1.DBITools.uniperiod_delta(uniperiod, duration - 1));
        if (t2 === null || t2 === void 0 ? void 0 : t2.endtime)
            endtime = t2.endtime;
    }
    return { starttime: t1.starttime, endtime: endtime };
}
function RegularTTView(props) {
    var tt_num = props.tt_num;
    var req = edurequest_1.client_req;
    var data = (0, asc_react_1.useAsyncMemo)(function () { return props.ttdata ? Promise.resolve(props.ttdata) : (0, regulartt_1.regularttGetData)(null, tt_num); }, [tt_num], { rights: {} }, { interactive: false });
    var dbi = React.useMemo(function () {
        if (data.dbiAccessorRes)
            return dbi_1.DBI.fromAccessorResult(req, data.dbiAccessorRes);
        return null;
    }, [data]);
    var mode = props.mode || 'svg';
    if (props.oblast.endsWith('_summary'))
        mode = 'svg';
    var onTTItemClick = props.onTTItemClick;
    if (req.isApp()) {
        onTTItemClick = function (ti) { return asc_react_1.RDialog.show(React.createElement(asc_react_1.RDialog, { header: req.ls(1920), width: 300, buttons: ['close'], noPadding: true },
            React.createElement(ttviewer_1.TTItemPopup, { ttitem: ti, dbi: dbi, addPadding: true }))); };
    }
    if (!dbi)
        return null;
    return mode == 'html'
        ? React.createElement(RegularTTView_HTML, tslib_1.__assign({}, props, { dbi: dbi, data: data, onTTItemClick: onTTItemClick }))
        : React.createElement(RegularTTView_SVG, tslib_1.__assign({}, props, { dbi: dbi, data: data }));
}
exports.RegularTTView = RegularTTView;
RegularTTView.npp = (0, dbi_1.neededPartProvider)(function () { return [
    RegularTTView_HTML,
    RegularTTView_SVG,
]; });
function RegularTTView_SVG(props) {
    var log_module = 'RegularTTView_SVG';
    var dbi = props.dbi, data = props.data, disableColors = props.disableColors, oblast = props.oblast, id = props.id, buildButtonsPortal = props.buildButtonsPortal, fixTTColors = props.fixTTColors;
    var _a = (0, asc_react_1.useClientSize)(), width = _a[0], height = _a[1], wrap_ref = _a[2];
    var sheet_ref = React.useRef();
    var _b = React.useState(0), page = _b[0], setPage = _b[1];
    var _c = React.useState(props.fit ? -1 : 4), zoom = _c[0], setZoom = _c[1];
    var req = (0, asc_react_1.useReq)();
    var printPages = React.useMemo(function () {
        if (!dbi)
            return [];
        var ttreportid = ttviewerOblast2ttreportid(dbi, oblast);
        if (!ttreportid)
            return [];
        var filter = {};
        if (id)
            filter[oblast] = [id];
        var year = dbi.rowData('globals', '1').edupage_year;
        return (0, print_1.renderPages)(dbi, ttreportid, {
            mode: 'viewer',
            year: year,
            filter: filter,
            fixTTColors: fixTTColors,
            disableColors: disableColors,
            hideDozory: !data.rights.classroomsupervision,
            unregistered: data.unregistered,
        });
    }, [dbi, disableColors, oblast, id, fixTTColors]);
    React.useEffect(function () {
        if (page >= printPages.length)
            setPage(0);
    }, [page, printPages]);
    var printPage = printPages[page];
    React.useEffect(function () {
        if (props.setViewData && data) {
            var ttreport = dbi ? dbi.rowData('ttreports', ttviewerOblast2ttreportid(dbi, oblast)) : null;
            props.setViewData({
                dbi: dbi,
                rights: data.rights,
                colorsEnabled: ttreport ? ttreport.cardcolorenabled : false,
                print: function () { return asc_react_1.RDialog.show(React.createElement(print_1.PrintingDlg, { pages: [printPage] })); },
            });
        }
    }, [data, dbi, oblast, printPage]);
    var realZoom = zoom;
    if (printPage && zoom < 0) {
        realZoom = Math.min(width / printPage.width, height / printPage.height) * 10;
    }
    var menu = [];
    if (printPages.length > 1) {
        menu.push({
            id: 'prevWeek',
            icon: '/timetable/pics/app/office/preview_back_32.png',
            onclick: page > 0 ? function () { return setPage(page - 1); } : undefined,
        });
        menu.push({
            id: 'nextWeek',
            icon: '/timetable/pics/app/office/preview_forward_32.png',
            onclick: page + 1 < printPages.length ? function () { return setPage(page + 1); } : undefined,
        });
    }
    {
        var zooms = [2, 3, 4, 6, 10];
        var z_plus_1 = -1;
        var z_minus_1 = -1;
        var min_ratio_1 = 1.1;
        zooms.forEach(function (z) {
            if (z < realZoom / min_ratio_1)
                z_minus_1 = z;
            if (z > realZoom * min_ratio_1 && z_plus_1 < 0)
                z_plus_1 = z;
        });
        if (menu.length > 0)
            menu.push([]);
        menu.push({
            icon: '/global/pics/ui/zoom_plus_32.png',
            onclick: z_plus_1 > 0
                ? function () { return setZoom(z_plus_1); }
                : undefined,
        });
        menu.push({
            icon: '/global/pics/ui/zoom_minus_32.png',
            onclick: z_minus_1 > 0
                ? function () { return setZoom(z_minus_1); }
                : undefined,
        });
        if (zoom > 0) {
            menu.push({
                text: req.ls(1915),
                onclick: function () { return setZoom(-1); }
            });
        }
    }
    var pp_el = React.useMemo(function () { return printPage && React.createElement(print_1.PrintPage, tslib_1.__assign({}, printPage)); }, [printPage]);
    var unsupported_browser = !Element.prototype.append;
    React.useEffect(function () {
        if (unsupported_browser) {
            (0, maindbi_ui_1.maindbiActionStat)(log_module + '.unsupported_browser', 'timetables', 'show');
        }
    }, [unsupported_browser]);
    if (unsupported_browser) {
        return React.createElement(React.Fragment, null, "Unsupported browser version (Error7123122586)");
    }
    return React.createElement(React.Fragment, null,
        React.createElement("div", { ref: wrap_ref, className: 'print-nobreak', style: tslib_1.__assign({ position: 'relative', overflow: 'auto' }, props.style || {}) },
            React.createElement("div", { ref: sheet_ref, style: {
                    marginLeft: 'auto',
                    marginRight: 'auto',
                    position: 'relative',
                } }, printPage && React.createElement(print_1.PrintPageOuterEls, tslib_1.__assign({}, printPage, { scale: realZoom }), pp_el))),
        buildButtonsPortal && buildButtonsPortal(menu));
}
RegularTTView_SVG.npp = (0, dbi_1.neededPartProvider)(function () { return [print_1.PrintPreview]; });
function RegularTTView_HTML(props) {
    var _a, _b, _c, _d, _e, _f;
    var dbi = props.dbi, data = props.data, oblast = props.oblast, id = props.id, fixTTColors = props.fixTTColors, buildButtonsPortal = props.buildButtonsPortal;
    var showHeader = (_a = props.showHeader) !== null && _a !== void 0 ? _a : true;
    var _g = (0, asc_react_1.useClientSize)(), clientWidth = _g[0], clientHeight = _g[1], ref = _g[2];
    clientWidth = (_b = props.clientWidth) !== null && _b !== void 0 ? _b : clientWidth;
    clientHeight = (_c = props.clientHeight) !== null && _c !== void 0 ? _c : clientHeight;
    var colors = (0, asc_react_1.useColors)();
    var _h = React.useState(0), week = _h[0], setWeek = _h[1];
    var _j = React.useState(0), term = _j[0], setTerm = _j[1];
    React.useEffect(function () {
        if (props.setViewData && data) {
            var ttreport_1 = dbi ? dbi.rowData('ttreports', ttviewerOblast2ttreportid(dbi, oblast)) : null;
            props.setViewData({
                dbi: dbi,
                rights: data.rights,
                colorsEnabled: ttreport_1 ? ttreport_1.cardcolorenabled : false,
            });
        }
    }, [data, dbi, oblast]);
    var table = oblast;
    var classid = '';
    if (table == 'classes')
        classid = id;
    if (table == 'students') {
        classid = ((_d = dbi === null || dbi === void 0 ? void 0 : dbi.rowData('students', id)) === null || _d === void 0 ? void 0 : _d.classid) || '';
    }
    var ttreport = dbi === null || dbi === void 0 ? void 0 : dbi.rowData('ttreports', ttviewerOblast2ttreportid(dbi, oblast));
    var color_table = !props.disableColors && (ttreport === null || ttreport === void 0 ? void 0 : ttreport.cardcolorenabled) && ((ttreport === null || ttreport === void 0 ? void 0 : ttreport.cardcolortable1) || (table == 'teachers' ? 'classes' : 'teachers'));
    var pageCards = React.useMemo(function () {
        if (!dbi)
            return [];
        return (0, render_1.dbi2renderCards)(dbi).filter(function (card) { return cardOnPage(card, table, id); });
    }, [dbi, table, id]);
    var pageDozory = React.useMemo(function () {
        if (!dbi || !data.rights.classroomsupervision)
            return [];
        if (table != 'teachers' && table != 'classrooms')
            return [];
        return (0, render_1.dbi2renderDozory)(dbi).filter(function (dozor) { return dozorOnPage(dozor, table, id); });
    }, [dbi, data, table, id]);
    var ttitemsByDay = React.useMemo(function () {
        if (!dbi)
            return [];
        var paint = function (ttitem) {
            if (color_table) {
                ttitem.colors = (0, ttitem_1.ttitemTableIds)(ttitem, color_table).map(function (cid) { var _a; return (_a = dbi.rowData(color_table, cid)) === null || _a === void 0 ? void 0 : _a['color']; }).filter(function (c) { return c; });
                if (fixTTColors)
                    ttitem.colors = ttitem.colors.map(function (c) { return (0, ttdoc_1.fixTTColor)(c, colors.darkMode); });
            }
            return ttitem;
        };
        return dbi.tableData('days').map(function (day) {
            var _a, _b, _c, _d;
            var dayIndex = parseInt(day.id);
            var ttitems = [];
            for (var _i = 0, pageCards_1 = pageCards; _i < pageCards_1.length; _i++) {
                var card = pageCards_1[_i];
                if (card.days[dayIndex] != '1')
                    continue;
                if (((_a = card.weeks) === null || _a === void 0 ? void 0 : _a[week]) == '0')
                    continue;
                if (((_b = card.terms) === null || _b === void 0 ? void 0 : _b[term]) == '0')
                    continue;
                var bellClass = classid || card.classes[0] || '';
                var time = viewerItemTime(dbi, card.crow.period, card.durationperiods, day.id, bellClass, bellClass ? '' : (card.teachers[0] || ''), '', card.lrow);
                var slices = table == 'classes' ? (_d = (_c = card.classdata) === null || _c === void 0 ? void 0 : _c[classid]) === null || _d === void 0 ? void 0 : _d.groups : '';
                ttitems.push(paint({
                    type: 'card',
                    days: card.days,
                    weeks: card.weeks,
                    terms: card.terms,
                    date: '',
                    uniperiod: card.crow.period,
                    starttime: (time === null || time === void 0 ? void 0 : time.starttime) || '',
                    endtime: (time === null || time === void 0 ? void 0 : time.endtime) || '',
                    subjectid: card.subjects[0] || '',
                    classids: card.classes,
                    teacherids: card.teachers,
                    groupnames: card.seminargroup ? ['seminargroup:' + card.seminargroup] : card.groupnames,
                    classroomids: card.classrooms,
                    studentids: card.students,
                    durationperiods: card.durationperiods,
                    lessonid: card.lessonid,
                    cellSlices: slices || undefined,
                }));
            }
            for (var _e = 0, pageDozory_1 = pageDozory; _e < pageDozory_1.length; _e++) {
                var dozor = pageDozory_1[_e];
                if (dozor.day >= 0 && dozor.day != dayIndex)
                    continue;
                if (dozor.week >= 0 && dozor.week != week)
                    continue;
                if (dozor.term >= 0 && dozor.term != term)
                    continue;
                var time = viewerItemTime(dbi, dozor.uniperiod, 1, day.id, '', table == 'teachers' ? id : '', table == 'classrooms' ? id : '');
                ttitems.push(paint({
                    type: 'classroomsupervision',
                    date: '',
                    uniperiod: dozor.uniperiod,
                    starttime: (time === null || time === void 0 ? void 0 : time.starttime) || '',
                    endtime: (time === null || time === void 0 ? void 0 : time.endtime) || '',
                    subjectid: '',
                    classids: [],
                    teacherids: dozor.teacherid ? [dozor.teacherid] : [],
                    groupnames: [],
                    classroomids: dozor.classroomid ? [dozor.classroomid] : [],
                }));
            }
            return ttitems;
        });
    }, [dbi, pageCards, pageDozory, classid, table, id, week, term, color_table, fixTTColors, colors.darkMode]);
    var showObdobie = React.useMemo(function () {
        var _a, _b;
        var showObdobie = {
            terms: false,
            weeks: false,
        };
        for (var _i = 0, pageCards_2 = pageCards; _i < pageCards_2.length; _i++) {
            var card = pageCards_2[_i];
            if ((_a = card.weeks) === null || _a === void 0 ? void 0 : _a.includes('0'))
                showObdobie.weeks = true;
            if ((_b = card.terms) === null || _b === void 0 ? void 0 : _b.includes('0'))
                showObdobie.terms = true;
        }
        if (dbi) {
            var nWeeks = dbi.tableSize('weeks');
            var nTerms = dbi.tableSize('terms');
            for (var _c = 0, pageDozory_2 = pageDozory; _c < pageDozory_2.length; _c++) {
                var dozor = pageDozory_2[_c];
                if (dozor.week >= 0 && nWeeks > 1)
                    showObdobie.weeks = true;
                if (dozor.term >= 0 && nTerms > 1)
                    showObdobie.terms = true;
            }
        }
        return showObdobie;
    }, [dbi, pageCards, pageDozory]);
    if (!(dbi && ttreport))
        return null;
    var periods = dbi.tableData('periods');
    var fromPeriod = dbi.rowData('periods', '0') ? 0 : 1;
    var nPeriods = periods.length || 1;
    var bell = '';
    if (classid)
        bell = ((_e = dbi.rowData('classes', classid)) === null || _e === void 0 ? void 0 : _e.bell) || '';
    else if (table == 'teachers')
        bell = ((_f = dbi.rowData('teachers', id)) === null || _f === void 0 ? void 0 : _f.bell) || '';
    if (bell) {
        var brow_1 = dbi.rowData('bells', bell);
        periods = periods.map(function (prow) {
            var pd = (0, dbi_1.patchmissingobj)(brow_1 === null || brow_1 === void 0 ? void 0 : brow_1.perioddata[prow.id], {});
            return tslib_1.__assign(tslib_1.__assign({}, prow), { starttime: pd.starttime || prow.starttime, endtime: pd.endtime || prow.endtime });
        });
    }
    var days = dbi.tableData('days');
    var swapAxis = ttreport.row_tables.includes('periods');
    var _k = (0, ttview_components_1.calcTTViewBoxes)({
        clientWidth: clientWidth,
        clientHeight: clientHeight,
        fit: false,
        swapAxis: swapAxis,
        showHeader: showHeader,
        time_mode: false,
        nDays: days.length || 1,
        nPeriods: nPeriods,
    }), gap = _k.gap, gap_day = _k.gap_day, ttWidth = _k.ttWidth, periodHeight = _k.periodHeight, rowHeight = _k.rowHeight, dayWidth = _k.dayWidth, headerWidth = _k.headerWidth, headerHeight = _k.headerHeight, ttBox = _k.ttBox, ttRowBox = _k.ttRowBox, weekBox = _k.weekBox;
    var tlr = new timetable_1.TimetableLayoutRenderer(ttWidth, periodHeight, fromPeriod, fromPeriod + nPeriods);
    tlr.qSide = 18;
    tlr.gap = gap;
    tlr.data2t = function (data) {
        var _a;
        var uniperiod = data['period'] || data['uniperiod'];
        if (uniperiod[0] == 'b') {
            var t1_1 = parseInt(uniperiod.substr(1));
            return {
                t1: t1_1,
                t2: t1_1,
            };
        }
        var t1 = parseInt(uniperiod);
        var durationperiods = (_a = data['durationperiods']) !== null && _a !== void 0 ? _a : 1;
        var res = {
            t1: t1,
            t2: t1 + durationperiods,
            wasAligned: false,
        };
        var itemPeriod = data['uniperiod'];
        if (itemPeriod && itemPeriod[0] != 'b') {
            var starttime = data['starttime'];
            var endtime = data['endtime'];
            if (starttime) {
                var prow = periods.find(function (p) { return p.id == itemPeriod; });
                if (prow && prow.starttime != starttime)
                    res.wasAligned = true;
            }
            if (endtime) {
                var last_1 = dbi_1.DBITools.uniperiod_delta(itemPeriod, (durationperiods || 1) - 1);
                var prow = periods.find(function (p) { return p.id == last_1; });
                if (prow && prow.endtime != endtime)
                    res.wasAligned = true;
            }
        }
        return res;
    };
    var menu = [];
    if (showObdobie.terms || showObdobie.weeks) {
        menu.push({
            id: 'prevWeek',
            icon: '/timetable/pics/app/office/preview_back_32.png',
            onclick: week > 0 || term > 0
                ? function () {
                    if (week > 0) {
                        setWeek(week - 1);
                    }
                    else {
                        setWeek(dbi.tableData('weeks').length - 1);
                        setTerm(term - 1);
                    }
                }
                : undefined,
        });
        menu.push({
            id: 'nextWeek',
            icon: '/timetable/pics/app/office/preview_forward_32.png',
            onclick: term + 1 < dbi.tableData('terms').length || week + 1 < dbi.tableData('weeks').length
                ? function () {
                    if (week + 1 < dbi.tableData('weeks').length) {
                        setWeek(week + 1);
                    }
                    else {
                        setWeek(0);
                        setTerm(term + 1);
                    }
                }
                : undefined,
        });
    }
    return React.createElement(React.Fragment, null,
        React.createElement("div", { ref: ref, style: tslib_1.__assign(tslib_1.__assign(tslib_1.__assign({}, ttview_components_1.ttview_div_style), { backgroundColor: colors.bgWhite, color: colors.fgBlack }), props.style || {}) },
            React.createElement(ttview_components_1.TTView_PageHeader, { dbi: dbi, table: table, id: id, headerWidth: headerWidth, headerHeight: headerHeight }),
            React.createElement(ttview_components_1.TTView_Boxes, { ttBox: ttBox, swapAxis: swapAxis, periods: React.createElement(ttview_components_1.TTView_Periods, { dbi: dbi, periodHeight: periodHeight, tlr: tlr, swapAxis: swapAxis, showTime: true, periods: (0, dbi_1.unpartial_array)(periods) }), days: days.map(function (day, i) {
                    var y1 = rowHeight * i;
                    var box = {
                        x1: 0,
                        x2: dayWidth,
                        y1: y1,
                        y2: y1 + rowHeight,
                    };
                    return React.createElement(ttview_components_1.TTView_Object, { key: day.id, box: box, dbi: dbi, table: 'days', id: day.id, splitLineTop: i > 0, swap: swapAxis });
                }), tt: days.map(function (day, i) { return React.createElement(ttview_components_1.TTView_TTDay, { key: i, dbi: dbi, box: ttRowBox(i), gap_day: gap_day, table: oblast, id: id, tlr: tlr, ttitems: ttitemsByDay[i] || [], onTTItemClick: props.onTTItemClick, selectedTTItem: props.selectedTTItem, swapAxis: swapAxis }); }), corner: React.createElement(asc_react_1.RTextBox, tslib_1.__assign({}, (0, asc_react_1.swapBox)(weekBox, swapAxis), { vAlign: 'center', text: [['terms', term], ['weeks', week]].map(function (_a) {
                        var t = _a[0], i = _a[1];
                        return showObdobie[t] && React.createElement("div", { key: t, style: { textAlign: 'center' } }, dbi.rowName(t, '' + i));
                    }) })) })),
        buildButtonsPortal && buildButtonsPortal(menu));
}
RegularTTView_HTML.needed_part = (0, dbi_1.typed)(dbi_1.DBITools.mergeNeededParts(render_1.dbi2renderCards, render_1.dbi2renderDozory, {
    classes: ['classroomid', 'bell'],
    teachers: ['bell'],
    classrooms: ['bell'],
    lessons: ['bell'],
    groups: ['name', 'entireclass'],
    periods: ['period', 'starttime', 'endtime', 'daydata'],
    breaks: ['starttime', 'endtime', 'daydata'],
    bells: ['perioddata', 'breakdata'],
    days: ['__name'],
    weeks: ['__name'],
    terms: ['__name'],
}));
function ttviewerOblast2ttreportid(dbi, oblast) {
    for (var _i = 0, _a = dbi.tableData('ttreports'); _i < _a.length; _i++) {
        var ttreport = _a[_i];
        if (oblast == 'teachers_summary' && ttreport.typ == 6)
            return ttreport.id;
        if (oblast == 'classes_summary' && ttreport.typ == 5)
            return ttreport.id;
        if (oblast == 'classrooms_summary' && ttreport.typ == 7)
            return ttreport.id;
        if (ttreport.page_tables.includes(oblast))
            return ttreport.id;
    }
    return '';
}
exports._hotswap = [
    RegularTTView_SVG,
];
