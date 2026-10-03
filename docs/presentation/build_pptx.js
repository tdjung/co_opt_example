// Build the presentation as a native .pptx (two font variants).
// node build_pptx.js current  -> deck_current_fonts.pptx  (IBM Plex Sans / JetBrains Mono)
// node build_pptx.js basic    -> deck_basic_fonts.pptx    (Arial / Courier New)
const pptxgen = require("pptxgenjs");
const variant = process.argv[2] || "current";
const SANS = variant === "basic" ? "Arial" : "IBM Plex Sans";
const MONO = variant === "basic" ? "Courier New" : "JetBrains Mono";
const OUT = variant === "basic" ? "deck_basic_fonts.pptx" : "deck_current_fonts.pptx";

const C = { ink: "0F1B2D", body: "4A5568", mute: "6B7280", paper: "F2F4F7", line: "D9DEE6",
  orange: "E8A33D", orangeText: "B8791F", teal: "2FA6A0", tealText: "1F7F7A", band: "111111",
  white: "FFFFFF", navy2: "16263D", navyMute: "8FA3B8", navySoft: "BFD8D5", navyLine: "2A3B55" };

const pres = new pptxgen();
pres.layout = "LAYOUT_WIDE"; // 13.333 x 7.5 in  (1920x1080 @144dpi)
const PX = 13.333 / 1920;    // inch per px
const px = (v) => +(v * PX).toFixed(3);
const pt = (v) => +(v * 0.5).toFixed(1); // px -> pt at 1920px = 13.33in (72pt/in) => 1px = 0.5pt

// ---- helpers -------------------------------------------------------------
function T(slide, text, x, y, w, h, o = {}) {
  const base = { x: px(x), y: px(y), w: px(w), h: px(h), fontFace: o.mono ? MONO : SANS, fontSize: pt(o.size || 28),
    color: o.color || C.ink, bold: !!o.bold, align: o.align || "left", valign: o.valign || "top",
    margin: 0, isTextBox: true, fit: "none" };
  if (o.lineSpacing) base.lineSpacingMultiple = o.lineSpacing;
  if (o.charSpacing) base.charSpacing = o.charSpacing;
  if (o.fill) base.fill = { color: o.fill };
  if (o.italic) base.italic = true;
  slide.addText(text, base);
}
function R(slide, x, y, w, h, fill, o = {}) {
  const opt = { x: px(x), y: px(y), w: px(w), h: px(h), fill: fill ? { color: fill } : { type: "none" }, line: o.line ? { color: o.line, width: o.lw || 1, dashType: o.dash || "solid" } : { type: "none" } };
  if (o.radius !== undefined) { opt.rectRadius = px(o.radius); slide.addShape(pres.ShapeType.roundRect, opt); }
  else slide.addShape(pres.ShapeType.rect, opt);
}
function Ellipse(slide, x, y, w, h, fill, o = {}) {
  slide.addShape(pres.ShapeType.ellipse, { x: px(x), y: px(y), w: px(w), h: px(h), fill: { color: fill }, line: o.line ? { color: o.line, width: o.lw || 1 } : { type: "none" } });
}
function Line(slide, x1, y1, x2, y2, color, w = 2, o = {}) {
  const opt = { x: px(Math.min(x1, x2)), y: px(Math.min(y1, y2)), w: px(Math.abs(x2 - x1)), h: px(Math.abs(y2 - y1)), line: { color, width: w } };
  if (o.dash) opt.line.dashType = o.dash;
  if (o.end) opt.line.endArrowType = o.end;
  if ((x2 < x1) !== (y2 < y1)) opt.flipV = true;
  slide.addShape(pres.ShapeType.line, opt);
}
function Arrow(slide, x, y, w, h, fill) { slide.addShape(pres.ShapeType.rightArrow, { x: px(x), y: px(y), w: px(w), h: px(h), fill: { color: fill }, line: { type: "none" } }); }
function band(slide, title) {
  R(slide, 0, 0, 1920, 116, C.band);
  T(slide, title, 80, 0, 1760, 116, { size: 32, bold: true, color: C.white, valign: "middle" });
}
function base(title, notes) {
  const s = pres.addSlide(); s.background = { color: C.white }; band(s, title);
  if (notes) s.addNotes(notes); return s;
}
function H2(s, text, y = 160, size = 60, h = 140) { T(s, text, 80, y, 1760, h, { size, bold: true, lineSpacing: 1.1 }); }
function card(s, x, y, w, h, o = {}) { R(s, x, y, w, h, o.fill || C.paper, { line: o.line === null ? undefined : (o.line || C.line), radius: o.radius === undefined ? 16 : o.radius }); }
function pill(s, text, x, y, w, h, fill, color) { R(s, x, y, w, h, fill, { radius: h / 2 }); T(s, text, x, y, w, h, { size: 24, mono: true, color, align: "center", valign: "middle" }); }
function bullets(s, items, x, y, w, h, o = {}) {
  s.addText(items.map((t, i) => ({ text: t, options: { bullet: true, breakLine: i < items.length - 1, paraSpaceAfter: 6 } })),
    { x: px(x), y: px(y), w: px(w), h: px(h), fontFace: SANS, fontSize: pt(o.size || 27), color: o.color || C.body, margin: 0, isTextBox: true, valign: "top", lineSpacingMultiple: 1.25 });
}
function table(s, rows, x, y, w, colW, o = {}) {
  const fs = pt(o.size || 26);
  const data = rows.map((r, ri) => r.map((cell, ci) => {
    const isHead = ri === 0;
    const c = typeof cell === "string" ? { t: cell } : cell;
    return { text: c.t, options: { bold: isHead || !!c.bold, color: isHead ? C.white : (c.color || C.ink), fill: { color: isHead ? C.ink : (ri % 2 ? C.white : C.paper) }, fontFace: SANS, fontSize: fs, align: "left", valign: "middle", margin: [6, 10, 6, 10] } };
  }));
  s.addTable(data, { x: px(x), y: px(y), w: px(w), colW: colW.map(px), border: { type: "solid", color: C.line, pt: 0.75 }, rowH: px(o.rowH || 62) });
}

// =========================================================================
// 1. cover
{
  const s = pres.addSlide(); s.background = { color: C.white };
  band(s, "[System Expert 현업과제 발표] · [조직] · [발표 일자]");
  T(s, "PRE-SILICON · MODEL · PROFILE · OPTIMIZE", 80, 200, 1200, 40, { size: 26, mono: true, color: C.orangeText, charSpacing: 4 });
  T(s, "실리콘보다 먼저", 80, 260, 1600, 150, { size: 120, bold: true });
  T(s, "실리콘이 나오기 전에, HW 구조와 SW를 AI가 함께 최적화하는\nCycle-level 가상 플랫폼 루프", 80, 430, 1640, 150, { size: 52, color: C.body, lineSpacing: 1.2 });
  T(s, "[발표자 이름] · [소속]", 80, 900, 900, 44, { size: 32, bold: true });
  T(s, "[행사명]", 80, 950, 900, 40, { size: 26, color: C.mute });
  pill(s, "CM4 TLM", 1180, 930, 190, 56, C.orange, C.ink);
  pill(s, "Performance Profiler", 1386, 930, 300, 56, C.teal, C.ink);
  pill(s, "AI Co-Opt", 1702, 930, 138, 56, C.ink, C.white);
  s.addNotes("템플릿의 표지 레이아웃이 따로 있으면 그쪽을 따르고, 이 장은 제목·부제·배지 문구만 옮긴다.");
}


// 2. trend (배경 — 개발은 이미 실리콘 앞에)
{
  const s = base("1. 과제 배경 — 개발은 이미 실리콘 앞에 와 있다",
    "40초. '실리콘 전에 개발'은 새로운 얘기가 아니라는 것을 먼저 인정하고 시작한다 (심사위원 다수가 FPGA·에뮬레이터·벤더 도구를 안다). 포인트는 '못 본다'가 아니라 '한 번 보고 한 번 바꿔 보는 반복이 시간 단위'라는 것. 세 번째 줄에서 기존 도구가 사라지지 않고 검증 역할로 남는 것을 손으로 짚는다 — 8장 WaveScope가 이 약속을 받는다.");
  T(s, [{ text: "실리콘 전에 개발하는 것은 이미 당연합니다.", options: { breakLine: true } }, { text: "이제는 실리콘 전에 " }, { text: "최적화까지", options: { color: C.orangeText } }, { text: " — 더 빨리, 더 좋은 제품으로" }], 80, 150, 1760, 140, { size: 56, bold: true, lineSpacing: 1.1 });
  const X = 80, Y = 300; card(s, X, Y, 1760, 520);
  Line(s, X + 240, Y + 156, X + 1720, Y + 156, "DFE4EB", 1.5); Line(s, X + 240, Y + 306, X + 1720, Y + 306, "DFE4EB", 1.5);
  Line(s, X + 740, Y + 40, X + 740, Y + 470, C.orangeText, 1.5, { dash: "dash" });
  T(s, "실리콘", X + 754, Y + 18, 200, 30, { size: 21, color: C.orangeText });
  T(s, "TAT ↓ · AI로 개발 속도 ↑", X + 1300, Y + 16, 430, 30, { size: 22, mono: true, color: C.orangeText, align: "right", charSpacing: 2 });
  const rows = [["과거", 66, C.ink], ["현재", 196, C.ink], ["이 과제", 346, C.orangeText]];
  rows.forEach(([t, y, c]) => { T(s, t, X + 40, Y + y, 180, 52, { size: 30, bold: true, color: c, valign: "middle" }); R(s, X + 240, Y + y, 460, 52, C.ink, { radius: 8 }); T(s, "HW 설계", X + 240, Y + y, 460, 52, { size: 25, bold: true, color: C.white, align: "center", valign: "middle" }); });
  const bar = (x, y, w, h, fill, text, size, color) => { R(s, x, y, w, h, fill, { radius: 8 }); T(s, text, x, y, w, h, { size, bold: true, color, align: "center", valign: "middle" }); };
  const done = (x, y, sub, strong) => { Line(s, x, y, x, y + 52, C.orangeText, 3); T(s, [{ text: "완성", options: { bold: true, breakLine: true } }, { text: sub, options: { fontSize: pt(19), bold: !!strong, color: strong ? C.orangeText : C.mute } }], x + 10, y - 2, 150, 60, { size: 22, color: C.orangeText, lineSpacing: 1.15 }); };
  // 과거
  bar(X + 760, Y + 66, 320, 52, "B3B9C4", "SW 개발", 25, C.white); bar(X + 1100, Y + 66, 260, 52, "B3B9C4", "최적화 (SW만)", 25, C.white); done(X + 1382, Y + 66, "SW만 최적");
  T(s, "SW는 실리콘 뒤에\nHW는 이미 고정", X + 1520, Y + 64, 220, 60, { size: 23, color: C.body, lineSpacing: 1.3 });
  // 현재
  bar(X + 430, Y + 254, 270, 42, C.mute, "SW 개발 · FPGA/에뮬", 22, C.white); bar(X + 760, Y + 196, 260, 52, "B3B9C4", "최적화 (SW만)", 25, C.white); done(X + 1042, Y + 196, "SW만 최적");
  T(s, [{ text: "SW는 앞으로 왔지만 보고 바꿔 보는 1회 = " }, { text: "시간~일", options: { bold: true, breakLine: true } }, { text: "관찰마다 프로브 삽입 · 변경마다 재합성 · 장비 공유", options: { color: C.mute, fontSize: pt(21) } }], X + 1180, Y + 200, 560, 64, { size: 23, color: C.body, lineSpacing: 1.3 });
  // 이 과제
  bar(X + 250, Y + 404, 450, 52, C.orange, "가상 플랫폼: HW+SW 최적화 · AI 루프", 21, C.ink); bar(X + 400, Y + 462, 300, 40, C.mute, "검증: FPGA · 에뮬 · RTL", 19, C.white); bar(X + 760, Y + 346, 180, 52, "B3B9C4", "실리콘 최종 검증", 22, C.white); done(X + 962, Y + 346, "HW+SW 최적", true);
  T(s, [{ text: "HW 구조까지 함께, 1회 = " }, { text: "초", options: { bold: true, color: C.orangeText } }, { text: " · 하루 수백 번", options: { breakLine: true } }, { text: "기존 도구는 그대로, 역할은 " }, { text: "검증", options: { bold: true, breakLine: true } }, { text: "→ 같은 면적에 더 빠르게, 같은 성능에 더 작게", options: { bold: true, color: C.orangeText } }], X + 1120, Y + 346, 620, 96, { size: 23, color: C.body, lineSpacing: 1.3 });
  T(s, "가로축 = 시간 (개념도) · 세로 점선 = 실리콘 시점", X + 1120, Y + 462, 620, 30, { size: 21, color: C.mute });
  [["»", "TAT는 계속 줄고, AI가 개발 속도를 한 번 더 끌어올리는 중", C.tealText], ["○", "FPGA·에뮬레이터도 안을 볼 수는 있지만, 보고 바꾸는 반복이 느리다", C.tealText], ["✱", "대체가 아니라 앞단에 추가되는 옵션 — 여기서 찾은 답은 기존 도구로 검증", C.orangeText]].forEach((c, i) => {
    const x = 80 + i * 596, w = 568; card(s, x, 850, w, 90, { radius: 12 }); T(s, c[0], x + 24, 850, 44, 90, { size: 34, color: c[2], valign: "middle" }); T(s, c[1], x + 76, 850, w - 100, 90, { size: 25, valign: "middle", lineSpacing: 1.3 });
  });
}

// 3. problem
{
  const s = base("1. 과제 배경 — 그러려면 빠르면서 cycle을 아는 시뮬레이터가 필요하다",
    "30초. 앞 장의 '초 단위 반복'을 받아 '그러려면 시뮬레이터가 필요한데'로 시작. FPGA·에뮬레이터는 앞 장에서 정리했으므로 여기서는 시뮬레이터끼리만 비교한다고 한 마디. 그림의 빈 가운데를 가리키며 '여기가 비어 있었고 TLM이 채운다'.");
  H2(s, "그러려면 빠르면서 cycle을 아는 시뮬레이터가 필요한데,\n그 자리가 비어 있었습니다", 150, 50, 130);
  const X = 80, Y = 350, W = 1060, Hh = 580;
  card(s, X, Y, W, Hh);
  Line(s, X + 120, Y + 520, X + 1000, Y + 520, "B3B9C4", 2.5, { end: "triangle" });
  Line(s, X + 120, Y + 520, X + 120, Y + 60, "B3B9C4", 2.5, { end: "triangle" });
  Ellipse(s, X + 160, Y + 80, 200, 200, C.ink); Ellipse(s, X + 770, Y + 330, 200, 200, C.ink); Ellipse(s, X + 450, Y + 170, 230, 230, C.orange);
  T(s, "RTL\ncycle-level", X + 160, Y + 80, 200, 200, { size: 34, bold: true, color: C.white, align: "center", valign: "middle" });
  T(s, "C-model\n알고리즘", X + 770, Y + 330, 200, 200, { size: 34, bold: true, color: C.white, align: "center", valign: "middle" });
  T(s, "TLM", X + 450, Y + 170, 230, 230, { size: 44, bold: true, align: "center", valign: "middle" });
  T(s, "수 KIPS\n앱 동작을 볼 수 없음", X + 130, Y + 290, 260, 70, { size: 24, color: C.body, align: "center" });
  T(s, "HW 특성 없음\n성능을 볼 수 없음", X + 740, Y + 262, 260, 70, { size: 24, color: C.body, align: "center" });
  T(s, "수십~수백 배 빠르면서\ncycle을 안다", X + 400, Y + 412, 330, 70, { size: 26, bold: true, color: C.orangeText, align: "center" });
  T(s, "느림", X + 140, Y + 532, 200, 34, { size: 24, color: C.mute });
  T(s, "시뮬레이션 속도 → 빠름", X + 660, Y + 532, 340, 34, { size: 24, color: C.mute, align: "right" });
  T(s, "정확", X + 20, Y + 50, 96, 34, { size: 24, color: C.mute, align: "right" });
  T(s, "HW 없음", X + 20, Y + 470, 96, 34, { size: 24, color: C.mute, align: "right" });
  T(s, "시뮬레이터끼리 비교 — FPGA·에뮬레이터는 앞 장 (보고 바꾸는 반복이 느림)", X + 140, Y + 14, 900, 30, { size: 22, color: C.mute });
  const rx = 1190, rw = 650;
  [["RTL 시뮬레이션", "정확하지만 너무 느려 애플리케이션 동작을 제대로 보기 어렵다", C.ink, C.ink],
   ["C-model", "알고리즘만 있고 하드웨어 특성이 없어 성능을 볼 수 없다", C.ink, C.ink],
   ["TLM 모델", "그 사이를 메운다 — SW를 개발하면서 HW 구조까지 바꿔 보고 성능을 본다", C.orange, C.orangeText]].forEach((r, i) => {
    const y = 410 + i * 165;
    R(s, rx, y, 8, 130, r[2]);
    T(s, r[0], rx + 32, y, rw - 32, 46, { size: 36, bold: true, color: r[3] });
    T(s, r[1], rx + 32, y + 52, rw - 32, 80, { size: 28, color: C.body, lineSpacing: 1.3 });
  });
}

// 4. tlm
{
  const s = base("1. 과제 배경 — TLM 모델이란",
    "50초. 왼쪽 그림: 위는 RTL(클럭마다 신호 다 계산), 아래는 TLM(CPU→SRAM 화살표에 +7 cycle 뱃지). 오른쪽 네 줄이 곧 요구사항.");
  H2(s, "신호를 흉내 내지 않고, 트랜잭션에 시간을 붙입니다", 160, 60, 80);
  const X = 80, Y = 290, W = 1000, Hh = 620;
  R(s, X, Y, W, Hh, C.ink, { radius: 16 });
  T(s, "RTL · 클럭마다 모든 신호를 계산", X + 36, Y + 24, 600, 34, { size: 24, mono: true, color: C.navyMute });
  ["clk", "HADDR", "HWDATA", "HREADY"].forEach((n, i) => T(s, n, X + 70, Y + 72 + i * 52, 140, 34, { size: 24, mono: true, color: "5F7390" }));
  // clock + signal lines as segments
  for (let i = 0; i < 20; i++) { const x0 = X + 210 + i * 34; Line(s, x0, Y + 92 - (i % 2 ? 0 : 22), x0 + 34, Y + 92 - (i % 2 ? 0 : 22), C.navyMute, 2.5); Line(s, x0 + 34, Y + 70, x0 + 34, Y + 92, C.navyMute, 2.5); }
  [[144, [88, 132, 66, 176, 110, 88, 220]], [196, [154, 66, 242, 110, 66, 242]], [248, [66, 176, 132, 66, 220, 88, 132]]].forEach(([yy, segs]) => {
    let x0 = X + 210; segs.forEach((len, i) => { len = Math.round(len * 0.9); const yl = Y + yy - (i % 2 ? 18 : 0); Line(s, x0, yl, x0 + len, yl, C.navyMute, 2.5); if (i < segs.length - 1) Line(s, x0 + len, Y + yy - 18, x0 + len, Y + yy, C.navyMute, 2.5); x0 += len; });
  });
  Line(s, X + 60, Y + 300, X + 940, Y + 300, C.navyLine, 1.5);
  T(s, "TLM · 트랜잭션 하나 + cycle 숫자", X + 36, Y + 318, 700, 34, { size: 24, mono: true, color: C.teal });
  R(s, X + 70, Y + 380, 240, 120, C.navy2, { line: C.teal, lw: 2.5, radius: 12 }); T(s, "CPU", X + 70, Y + 380, 240, 120, { size: 34, bold: true, color: "F5F4EE", align: "center", valign: "middle" });
  R(s, X + 690, Y + 380, 240, 120, C.navy2, { line: C.teal, lw: 2.5, radius: 12 }); T(s, "SRAM", X + 690, Y + 380, 240, 120, { size: 34, bold: true, color: "F5F4EE", align: "center", valign: "middle" });
  Line(s, X + 310, Y + 440, X + 690, Y + 440, C.orange, 5, { end: "triangle" });
  pill(s, "+7 cycle", X + 380, Y + 393, 220, 94, C.orange, C.ink);
  T(s, "write(0x2400_0010, data)  →  버스 대기 2 + 메모리 5", X + 60, Y + 530, 880, 40, { size: 26, mono: true, color: C.navySoft, align: "center" });
  const items = [["RTL보다 수십~수백 배 빠르다", "애플리케이션은 물론 RTOS까지 함께 개발할 수 있는 속도", "»"], ["그래도 cycle을 안다", "기능만 맞추는 모델과 달리 \"얼마나 느린가\"에 답함", "○"], ["HW 구조가 파라미터다", "TCM·캐시·버스·DMA를 config로 바꿔 실험", "✱"], ["안이 전부 보인다", "C++ SW 플랫폼 — 프로브·재합성 없이 분석 데이터를 원하는 형태·양으로", "◎"]];
  items.forEach((it, i) => { const y = 300 + i * 158; R(s, 1128, y, 108, 108, C.ink, { radius: 22 }); T(s, it[2], 1128, y, 108, 108, { size: 52, color: C.orange, align: "center", valign: "middle" });
    T(s, it[0], 1260, y + 4, 580, 46, { size: 36, bold: true }); T(s, it[1], 1260, y + 56, 580, 60, { size: 26, color: C.body, lineSpacing: 1.3 }); });
}

// 5. platform
{
  const s = base("2. 개발 및 설계 — Cortex-M4 TLM 가상 플랫폼", "40초. 세 숫자만 말한다. 아래는 '코어·메모리·버스는 기존 자산, 이번에 더한 것은 MAC 가속기·PoC 주변장치·분석 출력'을 한 문장으로. gem5/Fast Model 질문: gem5는 Cortex-M 미지원, Fast Model은 LT라 cycle 분석 불가.");
  H2(s, "TLM으로 만든 Cortex-M4 SoC: 빠르고, 정확하고, 간편합니다", 160, 60, 80);
  [["빠르다", "50배+", C.teal, "RTL 시뮬 대비 (벤치마크) · 실제 워크로드 수백 배 예상"], ["정확하다", "99.7%", C.orange, "cycle 정합성, RTL 시뮬 대비"], ["간편하다", "JSON 1장", C.white, "모델을 조립해 SoC 구성", 84]].forEach((c, i) => {
    const x = 80 + i * 596, w = 568; R(s, x, 290, w, 300, C.ink, { radius: 16 });
    T(s, c[0], x + 44, 320, w - 88, 36, { size: 28, color: C.navyMute }); T(s, c[1], x + 44, 360, w - 88, 130, { size: c[4] || 112, bold: true, color: c[2], valign: "middle" }); T(s, c[3], x + 44, 500, w - 88, 70, { size: 26, color: C.navySoft, lineSpacing: 1.25 });
  });
  card(s, 80, 640, 420, 300, { fill: C.white, radius: 14 });
  T(s, "기존 자산 · 그대로 사용", 110, 668, 360, 32, { size: 24, bold: true, color: C.mute });
  T(s, "CM4 코어 (NVIC·FPU)\nTCM · SRAM · Flash · I-Cache\nAHB 매트릭스 · DMA · Timer", 110, 712, 370, 130, { size: 25, color: C.body, lineSpacing: 1.5 });
  T(s, "이번 과제에서 추가 모델링", 528, 640, 800, 36, { size: 28, bold: true, color: C.orangeText });
  [["MAC 가속기", "INT8 dot-product 버스 마스터 · 레지스터 맵 · 완료 IRQ · 면적 비용"], ["PoC 주변장치", "오디오 입력(16 kHz → DMA) · 센서/액추에이터 · 결과·마커 출력"], ["분석 출력 확장", "callgrind 이벤트: 메모리 영역별 접근·스톨·버스 대기 · FST: PC·함수·버스 신호"]].forEach((c, i) => {
    const x = 528 + i * 444, w = 424; card(s, x, 690, w, 250, { radius: 14 }); R(s, x, 690, w, 6, C.orange);
    T(s, c[0], x + 28, 716, w - 56, 40, { size: 30, bold: true }); T(s, c[1], x + 28, 766, w - 56, 150, { size: 24, color: C.body, lineSpacing: 1.35 });
  });
}

// 6. loop
{
  const s = base("2. 개발 및 설계 — Pre-Silicon Co-Optimization 루프", "60초. 이 장이 과제의 정의. 점선 되돌이 화살표를 가리키며 '실리콘 없이 HW와 SW를 같이 돌리는 루프'. 아래 두 줄이 신뢰 장치.");
  H2(s, "그래서 하나의 루프가 됩니다: 돌리고, 읽고, 고치고, 다시", 160, 60, 80);
  T(s, "사람이 며칠 걸릴 HW·SW 시행착오를 AI가 하루에 수백 번 — 사람은 검토와 승인", 80, 250, 1760, 40, { size: 30, color: C.body });
  const Y = 320;
  const boxes = [["돌린다", "CM4 가상 플랫폼", "config + 펌웨어를 실행하면\n프로파일(callgrind)과\n파형(FST)이 나온다", C.orange, C.orangeText], ["읽는다", "AI 분석 + 사람 검토", "AI는 API로 병목·원인을 읽고\n가설을 세운다. 사람은\nPerformance Profiler로 검토", C.teal, C.tealText], ["고친다", "SW knob · HW knob", "데이터 배치, DMA, 오프로드,\n메모리·캐시·버스 파라미터.\n소스가 아닌 config를 바꾼다", C.ink, C.ink]];
  boxes.forEach((b, i) => { const x = 80 + i * 620; R(s, x, Y, 520, 300, C.paper, { line: b[3], lw: 3, radius: 20 });
    T(s, b[0], x + 44, Y + 28, 440, 32, { size: 24, mono: true, color: b[4] }); T(s, b[1], x + 44, Y + 68, 440, 56, { size: 42, bold: true }); T(s, b[2], x + 44, Y + 140, 440, 130, { size: 27, color: C.body, lineSpacing: 1.35 }); });
  Arrow(s, 610, Y + 130, 80, 40, C.orange); Arrow(s, 1230, Y + 130, 80, 40, C.teal);
  // return path
  Line(s, 1580, Y + 300, 1580, Y + 420, C.orange, 5, { dash: "dash" }); Line(s, 1580, Y + 420, 340, Y + 420, C.orange, 5, { dash: "dash" }); Line(s, 340, Y + 420, 340, Y + 300, C.orange, 5, { dash: "dash", end: "triangle" });
  T(s, "한 바퀴 수 초~수십 초 · 하루 수백 회", 660, Y + 398, 600, 44, { size: 28, bold: true, color: C.orangeText, align: "center", fill: C.white });
  [["◉", "Human-on-the-loop: 5회마다 근거와 함께 보고, HW 변경은 항상 사람이 승인"], ["■", "출력이 골든과 한 비트라도 다르면 실험 무효 — 정확도를 깎아 빨라질 수 없다"]].forEach((c, i) => {
    const x = 80 + i * 892, w = 868; card(s, x, 810, w, 110); T(s, c[0], x + 28, 810, 44, 110, { size: 36, valign: "middle" }); T(s, c[1], x + 84, 810, w - 110, 110, { size: 26, valign: "middle", lineSpacing: 1.3 });
  });
}

// 7. profiler
{
  const s = base("2. 개발 및 설계 — Performance Profiler (Human-on-the-loop)", "30초. '시뮬레이터가 callgrind 하나를 내고, AI는 API로, 사람은 GUI로 같은 파일을 본다.' 왼쪽에 30초 무음 자동재생 영상(반복) 권장.");
  H2(s, "하나의 프로파일을 AI와 사람이 같이 읽습니다", 160, 60, 80);
  R(s, 80, 290, 1080, 500, C.ink, { radius: 16 });
  T(s, "[스크린샷 또는 30초 자동재생 영상 — Performance Profiler\n함수 목록 → 라인 분석 + 점프 화살표 → 어셈블리 → 시나리오 비교]", 240, 290, 760, 500, { size: 30, color: C.navyMute, align: "center", valign: "middle", lineSpacing: 1.4 });
  const X = 1200, Y = 290;
  R(s, X + 160, Y, 320, 120, C.ink, { radius: 16 }); T(s, "CM4 가상 플랫폼\n실행 1회", X + 160, Y, 320, 120, { size: 30, bold: true, color: "F5F4EE", align: "center", valign: "middle" });
  Line(s, X + 320, Y + 120, X + 320, Y + 210, C.ink, 5);
  pill(s, "callgrind", X + 180, Y + 210, 280, 96, C.orange, C.ink);
  Line(s, X + 240, Y + 306, X + 110, Y + 460, C.teal, 5, { end: "triangle" }); Line(s, X + 400, Y + 306, X + 530, Y + 460, C.teal, 5, { end: "triangle" });
  card(s, X, Y + 460, 240, 100); T(s, "AI\n질의 API", X, Y + 460, 240, 100, { size: 28, bold: true, align: "center", valign: "middle" });
  card(s, X + 400, Y + 460, 240, 100); T(s, "사람\nProfiler GUI", X + 400, Y + 460, 240, 100, { size: 28, bold: true, align: "center", valign: "middle" });
  T(s, "summary · stalls\nregions · diff", X, Y + 340, 150, 70, { size: 22, color: C.body, align: "center" });
  T(s, "함수 · 줄 · 콜 트리\n시나리오 비교", X + 490, Y + 340, 150, 70, { size: 22, color: C.body, align: "center" });
  [["✓", "callgrind는 정립된 공개 포맷 — AI도 사람도 새 파서 없이 바로 읽음"], ["◉", "AI의 결정을 사람이 같은 데이터로 검토 → human-on-the-loop"], ["★", "이번 과제: 다중 시나리오·비교 분석·Call Tree·UI/UX 대규모 개편"]].forEach((c, i) => {
    const x = 80 + i * 596, w = 568; card(s, x, 826, w, 100, { radius: 12 }); T(s, c[0], x + 24, 826, 40, 100, { size: 32, color: i == 2 ? C.orangeText : C.tealText, valign: "middle" }); T(s, c[1], x + 72, 826, w - 96, 100, { size: 25, valign: "middle", lineSpacing: 1.3 });
  });
}

// 8. wavescope
{
  const s = base("2. 개발 및 설계 — WaveScope (신규): 기존 도구와의 연결", "45초. 이번 과제에서 새로 만든 것. 2장에서 '여기서 찾은 답은 기존 도구로 검증한다'고 했으므로 방어가 아니라 '그 검증을 같은 형식으로 하게 만들었다'로 시작. '왼쪽 네 곳 어디서 나온 파형이든 오른쪽 같은 표가 된다'.");
  H2(s, "파형만 있으면 어떤 플랫폼이든 같은 프로파일이 나옵니다", 160, 60, 80);
  const Y = 270;
  ["가상 플랫폼", "RTL 시뮬레이터", "에뮬레이터", "FPGA 프로토타입"].forEach((t, i) => { card(s, 80, Y + i * 120, 320, 90, { radius: 14 }); T(s, t, 80, Y + i * 120, 320, 90, { size: 30, bold: true, align: "center", valign: "middle" }); Line(s, 400, Y + i * 120 + 45, 590, Y + 225, "B3B9C4", 3); });
  R(s, 590, Y + 130, 260, 190, C.ink, { radius: 16 });
  { // clk + two data lanes, square waves with vertical edges
    const x0 = 612, W = 216, hi = 16;
    T(s, "clk", x0, Y + 142, 60, 24, { size: 17, mono: true, color: "5F7390" }); T(s, "PC", x0, Y + 205, 60, 24, { size: 17, mono: true, color: "5F7390" }); T(s, "addr", x0, Y + 262, 60, 24, { size: 17, mono: true, color: "5F7390" });
    const sx = x0 + 48, sw = W - 48;
    for (let i = 0; i < 8; i++) { const a = sx + i * (sw / 8), b = a + sw / 16, c = a + sw / 8, y = Y + 166; Line(s, a, y - hi, b, y - hi, C.teal, 2); Line(s, b, y - hi, b, y, C.teal, 2); Line(s, b, y, c, y, C.teal, 2); if (i < 7) Line(s, c, y, c, y - hi, C.teal, 2); }
    [[Y + 226, [0.18, 0.12, 0.25, 0.15, 0.3]], [Y + 284, [0.3, 0.1, 0.2, 0.25, 0.15]]].forEach(([y, segs]) => { let x = sx; segs.forEach((f, i) => { const len = f * sw, yl = i % 2 ? y : y - hi; Line(s, x, yl, x + len, yl, C.navyMute, 2.5); if (i < segs.length - 1) Line(s, x + len, y - hi, x + len, y, C.navyMute, 2.5); x += len; }); });
  }
  T(s, "파형 (clk + PC)", 590, Y + 96, 260, 30, { size: 23, mono: true, color: C.body, align: "center" });
  T(s, "트레이스 IP 불필요", 590, Y + 328, 260, 30, { size: 23, color: C.body, align: "center" });
  Arrow(s, 860, Y + 205, 80, 40, C.teal);
  R(s, 970, Y + 155, 320, 140, C.teal, { radius: 20 }); T(s, "WaveScope", 970, Y + 155, 320, 140, { size: 40, bold: true, align: "center", valign: "middle" });
  T(s, "PC 신호 × ELF 디스어셈블리\n수 GB 파형 → 약 3분", 970, Y + 310, 320, 60, { size: 23, color: C.body, align: "center" });
  Arrow(s, 1300, Y + 205, 80, 40, C.orange);
  R(s, 1410, Y, 430, 450, C.ink, { radius: 16 });
  T(s, "callgrind · 같은 함수 이름", 1432, Y + 24, 390, 32, { size: 24, mono: true, color: C.orange });
  T(s, "fir_filter    58,904\nmemcpy        31,878\ndma_wait      15,488\nirq_handler    9,134", 1432, Y + 72, 390, 180, { size: 26, mono: true, color: "F5F4EE", lineSpacing: 1.6 });
  T(s, "→ Profiler에서 플랫폼 간\n함수별로 나란히 비교", 1432, Y + 270, 390, 70, { size: 25, color: C.navySoft, lineSpacing: 1.3 });
  T(s, "(숫자는 형식 예시)", 1432, Y + 380, 390, 30, { size: 22, color: C.navyMute });
  [["✓", "2장의 약속 — TLM에서 찾은 최적점을 RTL·에뮬레이터 파형으로 검증, 모델 정합성을 루프 안에서 관리"], ["◎", "단독으로도 사용 — 설계·시뮬레이터 수정 없이 기존 파형에서 바로 프로파일"]].forEach((c, i) => {
    const x = 80 + i * 892, w = 868; card(s, x, 800, w, 120, { radius: 12 }); T(s, c[0], x + 28, 800, 44, 120, { size: 34, color: C.tealText, valign: "middle" }); T(s, c[1], x + 84, 800, w - 110, 120, { size: 27, valign: "middle", lineSpacing: 1.3 });
  });
}

// 9. poc
{
  const s = base("3. 검증 및 결과 — PoC 구성: Keyword Spotting", "50초. 왼쪽: Base(검정)에 예산 280부터 TCM(청록 점선), 470부터 MAC(주황 점선)이 더해진다. 주황 점이 AI가 고르는 HW knob. 오른쪽: '소리가 들어와서 yes라는 답이 나오는 데 몇 cycle 걸리나'가 측정값. 아래 세 칸이 일부러 심은 상충 관계. 면적 예산 3단계(≤170/≤280/≤470), SW 소스 동일.");
  H2(s, "PoC: 음성 키워드 인식(KWS)을 세 가지 면적 예산에서 최적화", 160, 60, 80);
  { // architecture diagram (Base / +TCM / +MAC)
    const X = 80, Y = 290; card(s, X, Y, 840, 570);
    const box = (x, y, w, h, fill, o = {}) => R(s, X + x, Y + y, w, h, fill, { radius: o.r === undefined ? 12 : o.r, line: o.line, lw: o.lw, dash: o.dash });
    const V = (x, y1, y2) => Line(s, X + x, Y + y1, X + x, Y + y2, C.mute, 3.5);
    const dot = (cx, cy) => Ellipse(s, X + cx - 10, Y + cy - 10, 20, 20, C.orange);
    T(s, "CM4 가상 플랫폼 · 면적 예산 3단계", X + 24, Y + 18, 440, 30, { size: 22, mono: true, color: C.mute });
    R(s, X + 470, Y + 22, 22, 22, C.ink, { radius: 4 }); T(s, "Base", X + 500, Y + 16, 90, 32, { size: 22, bold: true });
    R(s, X + 590, Y + 22, 22, 22, C.white, { radius: 4, line: C.tealText, lw: 2, dash: "dash" }); T(s, "+TCM", X + 620, Y + 16, 90, 32, { size: 22, bold: true, color: C.tealText });
    R(s, X + 700, Y + 22, 22, 22, C.white, { radius: 4, line: C.orangeText, lw: 2, dash: "dash" }); T(s, "+MAC", X + 730, Y + 16, 90, 32, { size: 22, bold: true, color: C.orangeText });
    [310, 545, 730].forEach(x => V(x, 204, 270)); [140, 425, 705].forEach(x => V(x, 322, 390));
    Line(s, X + 180, Y + 150, X + 200, Y + 150, C.tealText, 6);
    box(40, 96, 140, 108, C.white, { line: C.tealText, lw: 3, dash: "dash" }); T(s, "TCM", X + 40, Y + 108, 140, 40, { size: 28, bold: true, color: C.tealText, align: "center" }); T(s, "ITCM + DTCM\n예산 ≤280부터", X + 40, Y + 152, 140, 50, { size: 20, color: C.body, align: "center", lineSpacing: 1.15 }); dot(180, 96);
    box(200, 96, 220, 108, C.ink); T(s, "Cortex-M4", X + 200, Y + 106, 220, 44, { size: 32, bold: true, color: C.white, align: "center" }); T(s, "FPU · NVIC", X + 200, Y + 158, 220, 30, { size: 21, color: C.navySoft, align: "center" });
    box(470, 96, 150, 108, C.ink); T(s, "DMA", X + 470, Y + 106, 150, 44, { size: 32, bold: true, color: C.white, align: "center" }); T(s, "burst 1–16", X + 470, Y + 158, 150, 30, { size: 21, color: C.navySoft, align: "center" }); dot(620, 96);
    box(660, 96, 140, 108, C.white, { line: C.orangeText, lw: 3, dash: "dash" }); T(s, "MAC", X + 660, Y + 108, 140, 40, { size: 28, bold: true, color: C.orangeText, align: "center" }); T(s, "INT8 가속기\n예산 ≤470부터", X + 660, Y + 152, 140, 50, { size: 20, color: C.body, align: "center", lineSpacing: 1.15 }); dot(800, 96);
    box(40, 270, 760, 52, C.body, { r: 10 }); T(s, "AHB 매트릭스 — 마스터 간 경합을 cycle 단위로", X + 40, Y + 270, 760, 52, { size: 26, bold: true, color: C.white, align: "center", valign: "middle" });
    box(40, 390, 200, 100, C.white, { line: C.ink, lw: 1.5 }); T(s, "Flash", X + 40, Y + 402, 200, 40, { size: 28, bold: true, align: "center" }); T(s, "512 KB · WS/prefetch", X + 40, Y + 446, 200, 30, { size: 20, color: C.body, align: "center" }); dot(240, 390);
    box(290, 390, 270, 100, C.white, { line: C.ink, lw: 1.5 }); Line(s, X + 425, Y + 390, X + 425, Y + 490, C.line, 1.5);
    T(s, "SRAM0", X + 290, Y + 402, 135, 40, { size: 26, bold: true, align: "center" }); T(s, "SRAM1", X + 425, Y + 402, 135, 40, { size: 26, bold: true, align: "center" }); T(s, "64 KB × 2 · CPU·DMA 공유", X + 290, Y + 446, 270, 30, { size: 20, color: C.body, align: "center" }); dot(560, 390);
    box(610, 390, 190, 100, C.white, { line: C.ink, lw: 1.5 }); T(s, "주변장치", X + 610, Y + 402, 190, 40, { size: 28, bold: true, align: "center" }); T(s, "Timer · Audio · I/O", X + 610, Y + 446, 190, 30, { size: 20, color: C.body, align: "center" });
    T(s, [{ text: "● ", options: { color: C.orange } }, { text: "config knob — 크기·개수·burst·WS를 AI가 예산 안에서 선택" }], X + 24, Y + 520, 800, 36, { size: 21, color: C.body });
  }
  const X = 960, Y = 290;
  R(s, X, Y, 880, 160, C.ink, { radius: 16 }); T(s, "마이크 · 16 kHz · 1초 클립", X + 22, Y + 14, 500, 30, { size: 23, mono: true, color: C.navyMute });
  { let x0 = X + 40; [-40, 20, -50, 30, -10, 55, -25, 5, -40, 15, -45, 20, -8, 30, -12, 50, -20, 6, -35, 10, -30, 12].forEach(a => { const h = Math.abs(a); Line(s, x0, Y + 90, x0 + 18, Y + 90 + a / 1.6, C.teal, 2.5); Line(s, x0 + 18, Y + 90 + a / 1.6, x0 + 36, Y + 90, C.teal, 2.5); x0 += 36; }); }
  [["DMA", "20 ms 프레임 수집"], ["MFCC", "FFT → 음향 특징"], ["CNN 추론", "INT8 · 2.7 M MAC"]].forEach((b, i) => { const x = X + i * 307; card(s, x, Y + 215, 266, 120, { radius: 14 }); T(s, b[0] + "\n" + b[1], x, Y + 215, 266, 120, { size: 28, bold: true, align: "center", valign: "middle", lineSpacing: 1.25 }); if (i < 2) Arrow(s, x + 266, Y + 262, 41, 26, C.orangeText); });
  Line(s, X + 747, Y + 335, X + 747, Y + 400, C.orangeText, 5, { end: "triangle" });
  R(s, X, Y + 410, 880, 170, C.ink, { radius: 16 }); T(s, "결과: 12개 키워드 중 하나", X + 22, Y + 424, 600, 30, { size: 23, mono: true, color: C.navyMute });
  pill(s, "yes", X + 32, Y + 462, 140, 54, C.orange, C.ink); T(s, "no · up · down · left · right · on · off · stop · go · silence · unknown", X + 190, Y + 472, 680, 40, { size: 22, color: C.navySoft });
  T(s, "+ 동시에 1 kHz 제어 루프 ISR (응답 ≤ 20 µs)가 돌아야 함", X + 22, Y + 528, 840, 34, { size: 25, color: "F5F4EE" });
  [["배치", "가중치 29 KB + 버퍼가 TCM 16 KB에 다 안 들어감", C.orange], ["경합", "DMA와 CNN이 같은 SRAM 뱅크를 다툼", C.teal], ["오프로드", "가속기는 큰 레이어만 이득", C.ink]].forEach((c, i) => {
    const x = 80 + i * 596, w = 568; card(s, x, 870, w, 70, { radius: 10 }); R(s, x, 870, 8, 70, c[2]); T(s, [{ text: c[0] + " — ", options: { bold: true } }, { text: c[1] }], x + 30, 870, w - 50, 70, { size: 25, valign: "middle" });
  });
}

// 10. results
{
  const s = base("3. 검증 및 결과 — PoC 결과", "60초. 회색 점(사람 출발점)→주황 점(AI 도착점) 화살표가 'AI가 HW도 SW도 바꿨다'는 증거. 점선은 헤드라인. 실측 후 그림 교체. 검은 박스의 HW 변경 목록이 co-optimization의 실체.");
  H2(s, "[헤드라인 — 예) 예산 280에서 AI가 고른 HW+SW가 예산 470의 사람 출발점보다 빠름: 면적 −40%, 같은 성능]", 160, 42, 120);
  const X = 80, Y = 300; card(s, X, Y, 960, 520);
  Line(s, X + 100, Y + 450, X + 920, Y + 450, "B3B9C4", 2); Line(s, X + 100, Y + 450, X + 100, Y + 50, "B3B9C4", 2);
  [300, 500, 830].forEach(x => Line(s, X + x, Y + 60, X + x, Y + 450, "CBD2DC", 2, { dash: "dash" }));
  [[280, 150], [480, 250], [810, 345]].forEach(([x, y]) => Ellipse(s, X + x - 14, Y + y - 14, 28, 28, C.mute));
  [[272, 166, 212, 282], [472, 266, 402, 382], [802, 360, 734, 426]].forEach(([a, b, c, d]) => { Line(s, X + a, Y + b, X + c, Y + d, C.orange, 5, { end: "triangle" }); Ellipse(s, X + c - 16, Y + d - 16, 32, 32, C.orange); });
  Line(s, X + 402, Y + 382, X + 810, Y + 345, C.tealText, 2.5, { dash: "dash" });
  T(s, "추론 cycle ↑", X + 20, Y + 18, 300, 30, { size: 23, color: C.mute }); T(s, "면적 (상대) →", X + 630, Y + 480, 300, 30, { size: 23, color: C.mute, align: "right" });
  [["≤170", 250], ["≤280", 450], ["≤470", 780]].forEach(([t, x]) => T(s, t, X + x, Y + 458, 100, 30, { size: 23, color: C.mute, align: "center" }));
  T(s, [{ text: "● ", options: { color: C.mute } }, { text: "사람이 정한 HW + 기본 SW\n" }, { text: "● ", options: { color: C.orangeText } }, { text: "AI가 HW·SW 함께 선택\n" }, { text: "- - ", options: { color: C.tealText } }, { text: "같은 성능, 면적 −__%" }], X + 600, Y + 60, 340, 110, { size: 23, color: C.body, lineSpacing: 1.4 });
  T(s, "[예시 — 실측으로 교체]", X + 120, Y + 60, 400, 30, { size: 22, color: C.mute });
  const RX = 1080;
  table(s, [["면적 예산", "사람 HW + 기본 SW", "AI · SW만", "AI · HW+SW"], ["≤ 170", "[___] M", "−__%", "−__% · 면적 [___]"], ["≤ 280", "[___]", "−__%", "−__% · 면적 [___]"], ["≤ 470", "[___]", "−__%", "−__% · 면적 [___]"]], RX, Y, 760, [200, 190, 165, 205], { size: 24, rowH: 56 });
  R(s, RX, Y + 250, 760, 130, C.ink, { radius: 12 }); T(s, "AI가 실제로 바꾼 HW (예산 ≤280 예시)", RX + 28, Y + 266, 700, 30, { size: 23, color: C.navyMute });
  T(s, "[SRAM 3뱅크 → 2뱅크 · I-cache 4K → 없음\nFlash prefetch on · DMA burst 1 → 8]", RX + 28, Y + 300, 700, 70, { size: 25, mono: true, color: "F5F4EE", lineSpacing: 1.35 });
  [["AI 반복 · 소요", "[__]회 · [_]h"], ["사람 대비", "[_]일 → [_]h"], ["골든 불일치 폐기", "[__] / [__]"]].forEach((k, i) => { const x = RX + i * 258, w = 244; card(s, x, Y + 410, w, 110, { radius: 10 }); T(s, k[0], x + 20, Y + 424, w - 40, 28, { size: 21, color: C.mute }); T(s, k[1], x + 20, Y + 456, w - 40, 50, { size: 32, bold: true }); });
  T(s, [{ text: "신뢰성", options: { bold: true } }, { text: " — 모든 점은 출력 bit-exact · ISR ≤ 20 µs · overrun 0 통과. 모델 정합성 99.7%, 추론 커널 TFLite와 bit-exact, 실험마다 git rev·config 해시 기록. 예산 안의 HW 구성은 AI가 선택." }], 80, 850, 1760, 80, { size: 24, color: C.body, lineSpacing: 1.35 });
}

// 11. impact
{
  const s = base("4. 기대효과 · 마무리", "45초. 정량 칸은 PoC 수치로. 교육·실무 칸의 대괄호는 제안 — 실제 교과목명·적용 프로젝트로 교체.\n\n제목 후보:\n1. 실리콘보다 먼저 — Cycle-level 가상 플랫폼과 파형 기반 프로파일러 위에서 AI가 도는 HW-SW Co-Optimization\n2. 파형에서 최적화까지 — 실리콘을 기다리지 않는 HW·SW 공동 최적화 루프\n3. 돌리고, 읽고, 고친다 — Pre-Silicon 성능 분석 툴체인과 AI Co-Optimization\n4. Shift-Left, Close the Loop — 실리콘 없이 완성하는 HW-SW 최적화\n5. 실리콘 없이 답하는 세 가지 질문: 얼마나 느린가, 왜 느린가, 어떻게 고치는가");
  H2(s, "실리콘을 기다리지 않습니다. 돌리고, 읽고, 고칩니다.", 160, 68, 90);
  [["정성적 효과", ["HW-SW 최적화까지 실리콘 전에 — 기존 FPGA·에뮬 흐름 앞에 추가되는 단계", "\"왜 느린가\"에 근거(프로파일·경합 창)로 답함", "HW 결정에 면적 비용이 항상 따라붙음", "사람은 검토자로, 반복은 AI로 역할 분리"], C.orange],
   ["정량적 효과", ["탐색 시간 [사람 __일 → AI __시간]", "추론 cycle [베이스라인 대비 −__%]", "같은 성능에 면적 [−__%]", "실험 1회 [__초], 하루 [__]회"], C.teal],
   ["교육 · 실무 연계", ["[교과목: 컴퓨터구조 · 임베디드 · SoC 설계 — 메모리 계층·버스 경합을 실험으로 체득]", "[실무: 신규 SoC 펌웨어 선개발, RTL 파형(WaveScope) 교차 검증에 즉시 적용]", "[다음: RISC-V·멀티코어, 실제 프로젝트 워크로드 투입]"], C.ink]].forEach((c, i) => {
    const x = 80 + i * 596, w = 568; card(s, x, 300, w, 520); R(s, x, 300, w, 6, c[2]);
    T(s, c[0], x + 36, 332, w - 72, 46, { size: 34, bold: true }); bullets(s, c[1], x + 36, 396, w - 72, 400, { size: 26 });
  });
  T(s, "감사합니다 · Q&A", 80, 880, 800, 50, { size: 34, bold: true }); T(s, "[발표자 이름] · [이메일]", 1040, 890, 800, 40, { size: 26, color: C.mute, align: "right" });
}

// 12. appendix-req
{
  const s = base("부록 — 요구사항 분석: 기존 수단과의 비교", "Q&A 대비용. '왜 기존 도구로 안 되나', '왜 gem5 안 썼나'가 오면 이 장을 띄운다.");
  H2(s, "초 단위 루프에 필요한 것 다섯 가지, 기존 수단은 하나씩 빠져 있습니다", 160, 60, 80);
  const o = C.orangeText, g = C.tealText;
  table(s, [["필요한 것", "RTL 시뮬", "FPGA · 에뮬레이터", "기능(LT) 시뮬", "이 과제"],
    ["R1 · SW를 개발할 수 있는 속도", { t: "✕ 수 KIPS", color: o }, "○", "○", { t: "○ 50배+ vs RTL", color: g, bold: true }],
    ["R2 · cycle 수준 정확도", "○", "○", { t: "✕ 타이밍 없음", color: o }, { t: "○ 99.7%", color: g, bold: true }],
    ["R3 · 원인을 보여주는 분석 데이터", "△ 파형뿐", "△ 프로브·재합성 필요", "△", { t: "○ callgrind + 파형", color: g, bold: true }],
    ["R4 · HW 구조를 바꿔 볼 수 있음", { t: "✕ RTL 재작성", color: o }, { t: "✕ 재합성 수 시간", color: o }, "△", { t: "○ config 한 줄", color: g, bold: true }],
    ["R5 · 반복을 사람 대신 돌릴 수 있음", "–", "–", "–", { t: "○ AI 루프 + 사람 검토", color: g, bold: true }]], 80, 290, 1760, [493, 317, 317, 317, 316], { size: 28, rowH: 74 });
  T(s, "R1~R4를 한 플랫폼이 동시에 만족해야 R5(자동 최적화 루프)가 가능합니다. FPGA·에뮬레이터는 대체 대상이 아니라 루프 결과의 검증 수단(WaveScope). 공개 도구 참고: gem5는 Cortex-M 미지원, GVSoC는 RISC-V 전용, Arm Fast Model은 기능 수준(LT).", 80, 790, 1700, 100, { size: 30, color: C.body, lineSpacing: 1.4 });
}

// 13. appendix-loopfit
{
  const s = base("부록 — FPGA · 에뮬레이터 대비 반복 실험 적합성", "Q&A용. 'FPGA나 에뮬레이터 쓰면 되지 않나': 빠르고 정확하고 안을 볼 수도 있지만, 관찰 지점을 바꾸거나 HW 구조를 바꾸면 재합성이라 한 바퀴가 시간 단위입니다. 하루 수백 번 도는 루프에는 맞지 않고, 대체가 아니라 앞단 옵션입니다. 그 파형은 WaveScope로 가져와 검증에 씁니다.");
  H2(s, "빠르고 정확한 것만으로는 부족합니다 — 보고 바꾸는 한 바퀴가 초 단위여야 루프가 돕니다", 160, 54, 130);
  const X = 80, Y = 320, W = 1060, Hh = 580; card(s, X, Y, W, Hh);
  Line(s, X + 120, Y + 480, X + 1000, Y + 480, "B3B9C4", 2.5, { end: "triangle" }); Line(s, X + 120, Y + 480, X + 120, Y + 50, "B3B9C4", 2.5, { end: "triangle" });
  Ellipse(s, X + 160, Y + 70, 220, 220, C.orange); T(s, "TLM", X + 160, Y + 70, 220, 220, { size: 40, bold: true, align: "center", valign: "middle" });
  Ellipse(s, X + 680, Y + 90, 190, 190, C.ink); T(s, "RTL\n시뮬", X + 680, Y + 90, 190, 190, { size: 32, bold: true, color: C.white, align: "center", valign: "middle" });
  Ellipse(s, X + 740, Y + 290, 190, 190, C.ink); T(s, "FPGA\n에뮬레이터", X + 740, Y + 290, 190, 190, { size: 30, bold: true, color: C.white, align: "center", valign: "middle" });
  T(s, "모든 이벤트 관측 ·\nHW 변경 = config 한 줄", X + 120, Y + 302, 300, 60, { size: 23, color: C.body, align: "center" });
  T(s, "파형 전부 · 그러나 느림 · RTL 수정", X + 560, Y + 52, 420, 30, { size: 23, color: C.body, align: "center" });
  T(s, "FPGA·에뮬: 프로브 · 재합성\n신호 수 제한 · 장비 공유", X + 420, Y + 330, 300, 60, { size: 23, color: C.body, align: "center", lineSpacing: 1.3 });
  T(s, "C-model: HW가 없어 축 밖", X + 420, Y + 250, 300, 30, { size: 23, color: C.mute, align: "center" });
  T(s, "초 (config)", X + 140, Y + 540, 300, 30, { size: 23, color: C.mute }); T(s, "HW 구조 변경 1회 → 시간~일", X + 520, Y + 540, 480, 30, { size: 23, color: C.mute, align: "right" });
  T(s, "관측 비용\n없음 ↑", X + 14, Y + 40, 100, 60, { size: 23, color: C.mute, align: "right" }); T(s, "프로브\n재합성", X + 14, Y + 426, 100, 60, { size: 23, color: C.mute, align: "right" });
  [["관측성", "프로브·트레이스 IP를 심고 재합성해야 보임. 신호 수 제한, 스톨 원인은 바로 안 나옴"], ["HW 변경 비용", "RTL이 있어야 하고, 구조를 바꾸면 재합성 수 시간. 하루 수백 회 루프에 들어갈 수 없음"], ["가용 시점 · 비용", "RTL이 어느 정도 완성된 뒤에야 가능. 장비가 비싸 여러 실험을 동시에 돌리기 어려움"]].forEach((r, i) => {
    const y = 330 + i * 150; R(s, 1190, y, 8, 120, C.ink); T(s, r[0], 1226, y, 614, 42, { size: 32, bold: true }); T(s, r[1], 1226, y + 46, 614, 80, { size: 25, color: C.body, lineSpacing: 1.35 });
  });
  R(s, 1190, 790, 650, 110, C.ink, { radius: 12 }); T(s, [{ text: "그래서 경쟁이 아니라 연동: 이들의 파형을 " }, { text: "WaveScope", options: { bold: true } }, { text: "로 가져와 TLM 결과를 검증" }], 1216, 790, 600, 110, { size: 25, color: "F5F4EE", valign: "middle", lineSpacing: 1.3 });
}

pres.writeFile({ fileName: OUT }).then(f => console.log("wrote", f));
