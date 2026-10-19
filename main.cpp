// Pane - Photos-style image viewer. Win32 + GDI+, no dependencies.
#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <algorithm>
using std::min; using std::max;
#include <gdiplus.h>
#include <commdlg.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <filesystem>
#include <vector>
#include <map>
#include <string>
#include <cmath>
using namespace Gdiplus; namespace fs = std::filesystem;

struct Pal { Color bg, nav, card, fg, dim, acc, onacc, hov; } P;
struct File { std::wstring path, month; ULONGLONG key; };
struct Hit { RECT r; int id, a; };
HWND W; int dpi = 96, mode = 0, idx = -1, scrollY = 0, totalH = 0, sz = 1, drag = -1; bool dark = true, desc = true, sf = false, pan = false;
POINT mp, pp; std::wstring query; std::vector<File> files; std::vector<int> vis; std::vector<Hit> hits; std::vector<int> q;
std::vector<std::pair<std::wstring, fs::path>> places; std::vector<fs::path> roots; std::map<int, Bitmap*> th;
Bitmap* img = nullptr; float zoom = 1, panx = 0, pany = 0, adj[3] = {0, 0, 0}; bool fitm = true; std::wstring info;
Font *fN, *fB, *fH, *fI; int sx0 = 0, sx1 = 0; const wchar_t* AN[3] = {L"Brightness", L"Contrast", L"Saturation"};

int S(int v) { return MulDiv(v, dpi, 96); }
std::wstring lower(std::wstring s) { for (auto& c : s) c = towlower(c); return s; }
void inv() { InvalidateRect(W, nullptr, FALSE); }

void txt(Graphics& g, const std::wstring& s, int x, int y, Font* f, Color c, StringAlignment al = StringAlignmentNear, int w = 2000, int h = 0) {
    SolidBrush b(c); StringFormat sf; sf.SetAlignment(al); sf.SetLineAlignment(StringAlignmentCenter); sf.SetTrimming(StringTrimmingEllipsisCharacter); sf.SetFormatFlags(StringFormatFlagsNoWrap);
    g.DrawString(s.c_str(), -1, f, RectF((REAL)x, (REAL)y, (REAL)w, (REAL)(h ? h : S(30))), &sf, &b);
}
void rr(Graphics& g, RECT r, int rad, Color c) {
    GraphicsPath p; REAL d = (REAL)S(rad) * 2, x = (REAL)r.left, y = (REAL)r.top, w = (REAL)(r.right - r.left), h = (REAL)(r.bottom - r.top);
    p.AddArc(x, y, d, d, 180, 90); p.AddArc(x + w - d, y, d, d, 270, 90); p.AddArc(x + w - d, y + h - d, d, d, 0, 90); p.AddArc(x, y + h - d, d, d, 90, 90); p.CloseFigure();
    SolidBrush b(c); g.FillPath(&b, &p);
}
// style 0 flat, 1 card, 2 accent
void btn(Graphics& g, RECT r, int id, int a, const wchar_t* s, int style = 0, bool icon = true, bool sel = false) {
    hits.push_back({r, id, a}); bool o = PtInRect(&r, mp);
    if (style == 2) rr(g, r, 4, P.acc); else { if (style == 1) rr(g, r, 4, P.card); if (o || sel) rr(g, r, 4, P.hov); }
    txt(g, s, r.left, r.top, icon ? fI : fN, style == 2 ? P.onacc : P.fg, StringAlignmentCenter, r.right - r.left, r.bottom - r.top);
}

void orient(Bitmap* b) {
    UINT n = b->GetPropertyItemSize(0x0112); if (!n) return; auto* pi = (PropertyItem*)malloc(n);
    if (b->GetPropertyItem(0x0112, n, pi) == Ok) {
        int o = *(short*)pi->value; RotateFlipType t[] = {RotateNoneFlipNone, RotateNoneFlipNone, RotateNoneFlipX, Rotate180FlipNone, Rotate180FlipX, Rotate90FlipX, Rotate90FlipNone, Rotate270FlipX, Rotate270FlipNone};
        if (o > 1 && o <= 8) b->RotateFlip(t[o]);
    } free(pi);
}
Bitmap* loadFull(const std::wstring& p) {
    Bitmap s(p.c_str()); if (s.GetLastStatus() != Ok) return nullptr; orient(&s); int w = s.GetWidth(), h = s.GetHeight();
    auto* b = new Bitmap(w, h, PixelFormat32bppPARGB); Graphics g(b); g.DrawImage(&s, Rect(0, 0, w, h), 0, 0, w, h, UnitPixel); return b;
}
void loadThumb(int i) {
    Bitmap s(files[i].path.c_str()); if (s.GetLastStatus() != Ok) { th[i] = nullptr; return; } orient(&s); int w = s.GetWidth(), h = s.GetHeight(), m = min(w, h);
    auto* t = new Bitmap(256, 256, PixelFormat32bppPARGB); Graphics g(t); g.SetInterpolationMode(InterpolationModeHighQualityBilinear);
    g.DrawImage(&s, Rect(0, 0, 256, 256), (w - m) / 2, (h - m) / 2, m, m, UnitPixel); th[i] = t;
}
void colorMat(ImageAttributes& ia) {
    float b = adj[0], cs = 1 + adj[1] * .8f, sat = 1 + adj[2], lr = .3086f, lg = .6094f, lb = .0820f, sr = (1 - sat) * lr, sg = (1 - sat) * lg, sb = (1 - sat) * lb, t = (1 - cs) * .5f + b * .4f;
    ColorMatrix m = {{{cs * (sr + sat), cs * sr, cs * sr, 0, 0}, {cs * sg, cs * (sg + sat), cs * sg, 0, 0}, {cs * sb, cs * sb, cs * (sb + sat), 0, 0}, {0, 0, 0, 1, 0}, {t, t, t, 0, 1}}};
    ia.SetColorMatrix(&m);
}

// ---- library
void rebuild() {
    vis.clear(); std::wstring ql = lower(query);
    for (int i = 0; i < (int)files.size(); i++) if (ql.empty() || lower(fs::path(files[i].path).filename().wstring()).find(ql) != std::wstring::npos) vis.push_back(i);
    std::sort(vis.begin(), vis.end(), [](int a, int b) { return desc ? files[a].key > files[b].key : files[a].key < files[b].key; }); scrollY = 0;
}
void scanAll() {
    files.clear(); for (auto& kv : th) delete kv.second; th.clear(); std::error_code ec;
    for (auto& root : roots) for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
        std::wstring e = lower(it->path().extension().wstring());
        if (e != L".jpg" && e != L".jpeg" && e != L".png" && e != L".bmp" && e != L".gif" && e != L".tif" && e != L".tiff" && e != L".jfif") continue;
        WIN32_FILE_ATTRIBUTE_DATA d; if (!GetFileAttributesExW(it->path().c_str(), GetFileExInfoStandard, &d)) continue;
        FILETIME lf; SYSTEMTIME st; FileTimeToLocalFileTime(&d.ftLastWriteTime, &lf); FileTimeToSystemTime(&lf, &st); wchar_t buf[64]; GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, L"MMMM yyyy", buf, 64, nullptr);
        files.push_back({it->path().wstring(), buf, ((ULONGLONG)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime});
    }
    rebuild();
}
void openAt(int i) {
    if (vis.empty()) return; idx = (i % (int)vis.size() + (int)vis.size()) % (int)vis.size(); delete img; img = loadFull(files[vis[idx]].path);
    if (!img) { mode = 0; return; } fitm = true; panx = pany = 0; adj[0] = adj[1] = adj[2] = 0; if (mode == 0) mode = 1;
    std::error_code ec; auto p = fs::path(files[vis[idx]].path); wchar_t b[128]; swprintf(b, 128, L"%s    %d × %d    %.1f KB", lower(p.extension().wstring()).substr(1).c_str(), (int)img->GetWidth(), (int)img->GetHeight(), fs::file_size(p, ec) / 1024.0);
    for (auto& c : std::wstring(b)) (void)c; info = b; for (int k = 0; k < 4 && k < (int)info.size(); k++) info[k] = towupper(info[k]);
    SetWindowTextW(W, (p.filename().wstring() + L" - Pane").c_str()); inv();
}
bool enc(const wchar_t* mime, CLSID* c) {
    UINT n, z; GetImageEncodersSize(&n, &z); auto* p = (ImageCodecInfo*)malloc(z); GetImageEncoders(n, z, p); bool ok = false;
    for (UINT i = 0; i < n; i++) if (!wcscmp(p[i].MimeType, mime)) { *c = p[i].Clsid; ok = true; break; } free(p); return ok;
}
void save(bool copy) {
    std::wstring path = files[vis[idx]].path;
    if (copy) {
        wchar_t buf[MAX_PATH]; wcscpy_s(buf, (fs::path(path).stem().wstring() + L"_edited.jpg").c_str()); OPENFILENAMEW o{}; o.lStructSize = sizeof o; o.hwndOwner = W; o.lpstrFile = buf; o.nMaxFile = MAX_PATH;
        o.lpstrFilter = L"JPEG\0*.jpg\0PNG\0*.png\0\0"; o.lpstrDefExt = L"jpg"; o.Flags = OFN_OVERWRITEPROMPT; if (!GetSaveFileNameW(&o)) return; path = buf;
    }
    std::wstring e = lower(fs::path(path).extension().wstring()); CLSID c; if (!enc(e == L".png" ? L"image/png" : e == L".bmp" ? L"image/bmp" : L"image/jpeg", &c)) return;
    int w = img->GetWidth(), h = img->GetHeight(); Bitmap out(w, h, PixelFormat32bppARGB); { Graphics g(&out); ImageAttributes ia; colorMat(ia); g.DrawImage(img, Rect(0, 0, w, h), 0, 0, w, h, UnitPixel, &ia); }
    out.Save(path.c_str(), &c, nullptr); std::wstring keep = path; scanAll(); mode = 1;
    for (int k = 0; k < (int)vis.size(); k++) if (files[vis[k]].path == keep) { openAt(k); return; } mode = 0; inv();
}
void del() {
    if (mode == 0 || idx < 0) return; std::wstring p = files[vis[idx]].path; if (MessageBoxW(W, (L"Move " + fs::path(p).filename().wstring() + L" to the Recycle Bin?").c_str(), L"Delete photo", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    p.push_back(0); SHFILEOPSTRUCTW op{}; op.wFunc = FO_DELETE; op.pFrom = p.c_str(); op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT; SHFileOperationW(&op); scanAll(); mode = 0; inv();
}
// ---- print: the standard Windows print dialog -> printer driver settings
void doPrint() {
    if (!img) return; PRINTDLGW pd{}; pd.lStructSize = sizeof pd; pd.hwndOwner = W; pd.Flags = PD_RETURNDC | PD_NOSELECTION | PD_NOPAGENUMS | PD_USEDEVMODECOPIESANDCOLLATE; pd.nCopies = 1;
    if (!PrintDlgW(&pd)) return; HDC d = pd.hDC; DOCINFOW di{sizeof di, L"Pane photo"}; StartDocW(d, &di); StartPage(d);
    int pw = GetDeviceCaps(d, HORZRES), ph = GetDeviceCaps(d, VERTRES), iw = img->GetWidth(), ih = img->GetHeight(); float f = min((float)pw / iw, (float)ph / ih); int dw = (int)(iw * f), dh = (int)(ih * f);
    { Graphics g(d); g.SetPageUnit(UnitPixel); g.SetInterpolationMode(InterpolationModeHighQualityBicubic); ImageAttributes ia; colorMat(ia); g.DrawImage(img, Rect((pw - dw) / 2, (ph - dh) / 2, dw, dh), 0, 0, iw, ih, UnitPixel, &ia); }
    EndPage(d); EndDoc(d); DeleteDC(d); if (pd.hDevMode) GlobalFree(pd.hDevMode); if (pd.hDevNames) GlobalFree(pd.hDevNames);
}

// ---- drawing
void gallery(Graphics& g, int w, int h) {
    int nw = S(280), x0 = nw + S(44), top = S(156); SolidBrush nb(P.nav); g.FillRectangle(&nb, 0, 0, nw, h);
    RECT gr = {S(8), S(16), nw - S(8), S(60)}; rr(g, gr, 4, P.hov); SolidBrush ab(P.acc); g.FillRectangle(&ab, S(8), S(28), S(3), S(20));
    hits.push_back({gr, 56, 0}); txt(g, L"\uE91B", S(24), S(16), fI, P.fg, StringAlignmentNear, S(30), S(44)); txt(g, L"Gallery", S(56), S(16), fN, P.fg, StringAlignmentNear, S(200), S(44));
    txt(g, L"This PC", S(24), S(76), fB, P.dim, StringAlignmentNear, S(200), S(36));
    for (int i = 0; i < (int)places.size(); i++) { RECT r = {S(8), S(112) + i * S(44), nw - S(8), S(152) + i * S(44)}; btn(g, r, 57, i, L"", 0, false); txt(g, L"\uE8B7", S(36), r.top, fI, P.dim, StringAlignmentNear, S(30), S(40)); txt(g, places[i].first, S(68), r.top, fN, P.fg, StringAlignmentNear, S(180), S(40)); }
    RECT sb = {nw + S(20), S(10), w - S(260), S(46)}; rr(g, sb, 4, P.card); hits.push_back({sb, 50, 0});
    txt(g, L"\uE721", sb.left + S(12), sb.top, fI, P.dim, StringAlignmentNear, S(24), S(36)); txt(g, query.empty() ? L"Search in Gallery" : query + (sf ? L"|" : L""), sb.left + S(42), sb.top, fN, query.empty() ? P.dim : P.fg, StringAlignmentNear, sb.right - sb.left - S(50), S(36));
    btn(g, {w - S(240), S(10), w - S(110), S(46)}, 51, 0, L"  Add folder", 1, false); btn(g, {w - S(100), S(10), w - S(60), S(46)}, 52, 0, L"\uE713");
    txt(g, L"Gallery", x0, S(70), fH, P.fg, StringAlignmentNear, S(300), S(46)); txt(g, std::to_wstring(vis.size()) + L" photos", x0, S(114), fN, P.dim, StringAlignmentNear, S(300), S(26));
    btn(g, {w - S(190), S(78), w - S(150), S(114)}, 53, 0, L"\uE8CB"); btn(g, {w - S(146), S(78), w - S(106), S(114)}, 54, 0, L"\uE8A9"); btn(g, {w - S(102), S(78), w - S(62), S(114)}, 55, 0, L"\uE768");
    int tile = S(sz == 0 ? 120 : sz == 1 ? 176 : 240), gap = S(4), cols = max(1, (w - x0 - S(24) + gap) / (tile + gap)); g.SetClip(Rect(nw, top, w - nw, h - top)); int y = top - scrollY; size_t i = 0; q.clear();
    while (i < vis.size()) {
        size_t j = i; const std::wstring& m = files[vis[i]].month; while (j < vis.size() && files[vis[j]].month == m) j++;
        if (y + S(44) > top && y < h) txt(g, m, x0, y, fB, P.fg, StringAlignmentNear, S(400), S(40)); y += S(44);
        for (size_t k = i; k < j; k++) {
            int tx = x0 + (int)((k - i) % cols) * (tile + gap), ty = y + (int)((k - i) / cols) * (tile + gap); if (ty + tile < top || ty > h) continue; int fi = vis[k]; auto it = th.find(fi);
            if (it != th.end() && it->second) g.DrawImage(it->second, Rect(tx, ty, tile, tile)); else { rr(g, {tx, ty, tx + tile, ty + tile}, 0, P.card); if (it == th.end()) q.push_back(fi); }
            RECT hr = {tx, max(ty, top), tx + tile, min(ty + tile, h)}; hits.push_back({hr, 1, (int)k}); if (PtInRect(&hr, mp)) { Pen p(P.acc, (REAL)S(2)); g.DrawRectangle(&p, tx + S(1), ty + S(1), tile - S(2), tile - S(2)); }
        }
        y += (int)((j - i + cols - 1) / cols) * (tile + gap) + S(10); i = j;
    }
    totalH = y + scrollY - top; g.ResetClip();
}
void viewer(Graphics& g, int w, int h) {
    int top = S(52), bot = S(40), pw = mode == 2 ? S(320) : 0; btn(g, {S(8), S(6), S(48), S(46)}, 10, 0, L"\uE72B");
    if (mode == 1) { btn(g, {S(56), S(8), S(130), S(44)}, 11, 0, L"\uE70F  Edit", 1, false); btn(g, {S(138), S(8), S(178), S(44)}, 12, 0, L"\uE7AD"); btn(g, {S(182), S(8), S(222), S(44)}, 13, 0, L"\uE74D"); btn(g, {S(226), S(8), S(266), S(44)}, 14, 0, L"\uE749"); }
    else txt(g, L"Adjust", S(64), S(6), fB, P.fg, StringAlignmentNear, S(200), S(40));
    txt(g, fs::path(files[vis[idx]].path).filename().wstring(), S(300), 0, fN, P.fg, StringAlignmentCenter, w - S(600) - pw, top);
    int aw = w - pw, ah = h - top - bot, iw = img->GetWidth(), ih = img->GetHeight(); float fit = min(1.0f, min((float)aw / iw, (float)ah / ih)); if (fitm) { zoom = fit; panx = pany = 0; }
    int dw = (int)(iw * zoom), dh = (int)(ih * zoom), dx = (aw - dw) / 2 + (int)panx, dy = top + (ah - dh) / 2 + (int)pany; g.SetClip(Rect(0, top, aw, ah)); g.SetInterpolationMode(zoom > 2 ? InterpolationModeNearestNeighbor : InterpolationModeHighQualityBicubic);
    ImageAttributes ia; colorMat(ia); g.DrawImage(img, Rect(dx, dy, dw, dh), 0, 0, iw, ih, UnitPixel, &ia); g.ResetClip();
    if (mode == 1 && vis.size() > 1) { btn(g, {S(10), top + ah / 2 - S(36), S(50), top + ah / 2 + S(36)}, 20, 0, L"\uE76B", 1); btn(g, {aw - S(50), top + ah / 2 - S(36), aw - S(10), top + ah / 2 + S(36)}, 21, 0, L"\uE76C", 1); }
    txt(g, info, S(20), h - bot, fN, P.dim, StringAlignmentNear, S(500), bot);
    if (mode == 1) { sx0 = w - S(220); sx1 = w - S(40); float t = logf(zoom / .1f) / logf(80.f); t = max(0.f, min(1.f, t)); int ty = h - bot / 2; hits.push_back({{sx0 - S(8), h - bot, sx1 + S(8), h}, 60, 0});
        txt(g, std::to_wstring((int)(zoom * 100 + .5f)) + L"%", sx0 - S(70), h - bot, fN, P.dim, StringAlignmentFar, S(60), bot); SolidBrush tb(P.hov); g.FillRectangle(&tb, sx0, ty - S(1), sx1 - sx0, S(2)); SolidBrush ab(P.acc); g.FillEllipse(&ab, sx0 + (int)(t * (sx1 - sx0)) - S(8), ty - S(8), S(16), S(16)); }
    if (mode == 2) {
        SolidBrush nb(P.nav); g.FillRectangle(&nb, aw, top, pw, h - top); sx0 = aw + S(24); sx1 = w - S(24);
        for (int i = 0; i < 3; i++) { int y = top + S(24) + i * S(70); txt(g, AN[i], sx0, y, fN, P.fg, StringAlignmentNear, S(200), S(28)); txt(g, std::to_wstring((int)(adj[i] * 100)), sx1 - S(60), y, fN, P.dim, StringAlignmentFar, S(60), S(28));
            SolidBrush tb(P.hov); g.FillRectangle(&tb, sx0, y + S(40), sx1 - sx0, S(3)); SolidBrush ab(P.acc); int hx = sx0 + (int)((adj[i] + 1) / 2 * (sx1 - sx0)); g.FillEllipse(&ab, hx - S(9), y + S(32), S(18), S(18)); hits.push_back({{sx0 - S(9), y + S(26), sx1 + S(9), y + S(58)}, 30 + i, i}); }
        int by = h - bot - S(150); btn(g, {sx0, by, sx1, by + S(36)}, 12, 0, L"Rotate", 1, false); btn(g, {sx0, by + S(44), sx1, by + S(80)}, 40, 0, L"Reset", 1, false); btn(g, {sx0, by + S(88), (sx0 + sx1) / 2 - S(4), by + S(124)}, 41, 0, L"Save a copy", 1, false); btn(g, {(sx0 + sx1) / 2 + S(4), by + S(88), sx1, by + S(124)}, 42, 0, L"Save", 2, false);
    }
}
void paint(HDC hdc, RECT rc) {
    int w = rc.right, h = rc.bottom; HDC m = CreateCompatibleDC(hdc); HBITMAP bm = CreateCompatibleBitmap(hdc, w, h); SelectObject(m, bm); { Graphics g(m); g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        g.Clear(P.bg); hits.clear(); if (mode == 0 || !img) gallery(g, w, h); else viewer(g, w, h); } BitBlt(hdc, 0, 0, w, h, m, 0, 0, SRCCOPY); DeleteObject(bm); DeleteDC(m);
}
void setPal() {
    DWORD v = 0, n = 4; RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD, 0, &v, &n); (void)v;
    if (dark) P = {Color(39, 39, 39), Color(32, 32, 32), Color(58, 58, 58), Color(255, 255, 255), Color(172, 172, 172), Color(96, 205, 255), Color(0, 0, 0), Color(24, 255, 255, 255)};
    else P = {Color(249, 249, 249), Color(238, 238, 238), Color(236, 236, 236), Color(26, 26, 26), Color(96, 94, 92), Color(0, 95, 184), Color(255, 255, 255), Color(16, 0, 0, 0)};
    BOOL d = dark; DwmSetWindowAttribute(W, 20, &d, sizeof d);
}
void act(int id, int a, POINT p) {
    switch (id) {
    case 1: mode = 0; openAt(a); break;
    case 10: if (mode == 2) { mode = 1; adj[0] = adj[1] = adj[2] = 0; } else { mode = 0; KillTimer(W, 2); SetWindowTextW(W, L"Pane"); } break;
    case 11: mode = 2; break;
    case 12: img->RotateFlip(Rotate90FlipNone); fitm = true; break;
    case 13: del(); break; case 14: doPrint(); break;
    case 20: openAt(idx - 1); break; case 21: openAt(idx + 1); break;
    case 30: case 31: case 32: drag = a; break;
    case 40: adj[0] = adj[1] = adj[2] = 0; break; case 41: save(true); break; case 42: save(false); break;
    case 50: sf = true; break; case 52: dark = !dark; setPal(); break; case 53: desc = !desc; rebuild(); break; case 54: sz = (sz + 1) % 3; break;
    case 55: if (!vis.empty()) { openAt(0); SetTimer(W, 2, 3000, nullptr); } break;
    case 51: { BROWSEINFOW bi{}; bi.hwndOwner = W; bi.ulFlags = BIF_NEWDIALOGSTYLE | BIF_RETURNONLYFSDIRS; if (auto* l = SHBrowseForFolderW(&bi)) { wchar_t b[MAX_PATH]; if (SHGetPathFromIDListW(l, b)) { roots.push_back(b); scanAll(); } CoTaskMemFree(l); } break; }
    case 56: roots = {places[0].second}; scanAll(); break; case 57: roots = {places[a].second}; scanAll(); break;
    case 60: { float t = (float)(p.x - sx0) / (sx1 - sx0); t = max(0.f, min(1.f, t)); zoom = .1f * powf(80.f, t); fitm = false; break; }
    }
    if (id >= 30 && id <= 32) { adj[a] = max(-1.f, min(1.f, (float)(p.x - sx0) / (sx1 - sx0) * 2 - 1)); }
    inv();
}
LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_PAINT: { PAINTSTRUCT ps; HDC d = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc); paint(d, rc); EndPaint(h, &ps); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: inv(); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO*)lp)->ptMinTrackSize = {S(900), S(600)}; return 0;
    case WM_LBUTTONDOWN: { POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)}; mp = p; sf = false; SetCapture(h);
        for (int i = (int)hits.size() - 1; i >= 0; i--) if (PtInRect(&hits[i].r, p)) { int id = hits[i].id, a = hits[i].a; act(id, a, p); if (id == 60) drag = 60; return 0; }
        if (mode >= 1) { pan = true; pp = p; fitm = false; } inv(); return 0; }
    case WM_MOUSEMOVE: { POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)}; mp = p;
        if (drag >= 30 && drag <= 32) adj[drag - 30] = max(-1.f, min(1.f, (float)(p.x - sx0) / (sx1 - sx0) * 2 - 1)); else if (drag == 60) act(60, 0, p);
        else if (pan) { panx += p.x - pp.x; pany += p.y - pp.y; pp = p; } inv(); return 0; }
    case WM_LBUTTONUP: drag = -1; pan = false; ReleaseCapture(); return 0;
    case WM_MOUSEWHEEL: { int d = GET_WHEEL_DELTA_WPARAM(wp);
        if (mode == 0) { RECT rc; GetClientRect(h, &rc); scrollY = max(0, min(scrollY - d, max(0, totalH - (rc.bottom - S(156))))); }
        else { zoom = max(.05f, min(16.f, zoom * (d > 0 ? 1.15f : 1 / 1.15f))); fitm = false; } inv(); return 0; }
    case WM_CHAR: if (mode == 0 && sf) { if (wp == 8) { if (!query.empty()) query.pop_back(); } else if (wp >= 32) query.push_back((wchar_t)wp); rebuild(); inv(); } return 0;
    case WM_KEYDOWN: if (sf && mode == 0) { if (wp == VK_ESCAPE) sf = false; inv(); return 0; }
        if (wp == VK_ESCAPE) { KillTimer(h, 2); act(10, 0, mp); } else if (mode == 1 && wp == VK_LEFT) openAt(idx - 1); else if (mode == 1 && wp == VK_RIGHT) openAt(idx + 1); else if (wp == VK_DELETE) del();
        else if (wp == 'P' && GetKeyState(VK_CONTROL) < 0) doPrint(); else if (wp == 'E' && mode == 1) mode = 2; inv(); return 0;
    case WM_TIMER: if (wp == 1) { for (int k = 0; k < 3 && !q.empty(); k++) { int i = q.front(); q.erase(q.begin()); if (!th.count(i)) loadThumb(i); } if (mode == 0) inv(); } else if (wp == 2 && mode == 1) openAt(idx + 1); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    } return DefWindowProcW(h, m, wp, lp);
}
int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int) {
    CoInitialize(nullptr); GdiplusStartupInput gi; ULONG_PTR tok; GdiplusStartup(&tok, &gi, nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = hi; wc.lpszClassName = L"PaneWnd"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hIcon = LoadIcon(hi, MAKEINTRESOURCE(1)); RegisterClassW(&wc);
    W = CreateWindowW(L"PaneWnd", L"Pane", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 1360, 860, nullptr, nullptr, hi, nullptr);
    HDC d = GetDC(W); dpi = GetDeviceCaps(d, LOGPIXELSX); ReleaseDC(W, d); const wchar_t* ff = FontFamily(L"Segoe Fluent Icons").IsAvailable() ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    fN = new Font(L"Segoe UI Variable Text", (REAL)S(14), FontStyleRegular, UnitPixel); fB = new Font(L"Segoe UI Variable Text", (REAL)S(14), FontStyleBold, UnitPixel);
    fH = new Font(L"Segoe UI Variable Display", (REAL)S(30), FontStyleBold, UnitPixel); fI = new Font(ff, (REAL)S(16), FontStyleRegular, UnitPixel); setPal();
    struct { const GUID* id; const wchar_t* n; } kf[] = {{&FOLDERID_Pictures, L"Pictures"}, {&FOLDERID_Downloads, L"Downloads"}, {&FOLDERID_Desktop, L"Desktop"}, {&FOLDERID_Documents, L"Documents"}};
    for (auto& k : kf) { PWSTR p; if (SHGetKnownFolderPath(*k.id, 0, nullptr, &p) == S_OK) { places.push_back({k.n, p}); CoTaskMemFree(p); } }
    int ac; LPWSTR* av = CommandLineToArgvW(GetCommandLineW(), &ac); fs::path arg = ac > 1 ? fs::path(av[1]) : fs::path(); std::error_code ec;
    roots = {ac > 1 && fs::is_regular_file(arg, ec) ? arg.parent_path() : places[0].second}; scanAll(); SetTimer(W, 1, 30, nullptr);
    if (ac > 1 && fs::is_regular_file(arg, ec)) for (int k = 0; k < (int)vis.size(); k++) if (fs::equivalent(files[vis[k]].path, arg, ec)) { openAt(k); break; }
    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); } GdiplusShutdown(tok); return 0;
}
