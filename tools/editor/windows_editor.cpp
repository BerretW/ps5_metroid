#include "content_model.hpp"
#include "map_model.hpp"
#include "windows_storage.hpp"

#include <windows.h>
#include <commdlg.h>

#include <array>
#include <string>
#include <vector>

namespace {

constexpr int kOpen = 101;
constexpr int kSave = 102;
constexpr int kFields = 103;
constexpr int kValue = 104;
constexpr int kApply = 105;
constexpr int kLayer = 106;
constexpr int kBrush = 107;
constexpr int kUndo = 108;
constexpr int kCellSize = 20;

struct App {
    HWND window{}, open{}, save{}, fields{}, value{}, apply{}, label{}, path{}, status{};
    HWND canvas{}, layers{}, brush{}, layerLabel{}, brushLabel{}, undo{}, mapHint{};
    HFONT font{}, titleFont{};
    HBRUSH background{};
    echo::editor::ContentModel model;
    echo::editor::MapModel map;
    std::wstring filename;
    std::string original;
    int selected{-1};
    bool dirty{}, valueDirty{}, changing{}, mapMode{true};
    int cursorX{}, cursorY{};
};

std::wstring wide(std::string_view text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), result.data(), size) != size) return {};
    return result;
}

bool utf8(std::wstring_view text, std::string& result) {
    if (text.empty()) { result.clear(); return true; }
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return false;
    result.resize(static_cast<std::size_t>(size));
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                               static_cast<int>(text.size()), result.data(), size, nullptr, nullptr) == size;
}

void error(HWND window, const std::wstring& text) {
    MessageBoxW(window, text.c_str(), L"Echo Editor", MB_OK | MB_ICONERROR);
}

void systemError(HWND window, const wchar_t* action, DWORD code) {
    error(window, std::wstring(action) + L" (Windows error " + std::to_wstring(code) + L").");
}

HWND control(App& app, const wchar_t* type, const wchar_t* text, DWORD style, int id = 0,
             DWORD extended = 0) {
    HWND result = CreateWindowExW(extended, type, text, WS_CHILD | WS_VISIBLE | style,
                                  0, 0, 0, 0, app.window,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                  GetModuleHandleW(nullptr), nullptr);
    if (result) SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(app.font), TRUE);
    return result;
}

void layout(App& app, int width, int height) {
    const int left = 24;
    const int listWidth = (width - 72) * 55 / 100;
    const int right = left + listWidth + 24;
    MoveWindow(app.path, left, 65, width - 48, 23, TRUE);
    MoveWindow(app.fields, left, 111, listWidth, height - 187, TRUE);
    MoveWindow(app.canvas, left, 111, listWidth, height - 187, TRUE);
    MoveWindow(app.layerLabel, right, 111, width - right - 24, 24, TRUE);
    MoveWindow(app.layers, right, 140, width - right - 24, 200, TRUE);
    MoveWindow(app.brushLabel, right, 185, width - right - 24, 24, TRUE);
    MoveWindow(app.brush, right, 214, width - right - 24, 300, TRUE);
    MoveWindow(app.undo, right, 265, 125, 32, TRUE);
    MoveWindow(app.mapHint, right, 318, width - right - 24, 80, TRUE);
    MoveWindow(app.label, right, 111, width - right - 24, 58, TRUE);
    MoveWindow(app.value, right, 175, width - right - 24, 30, TRUE);
    MoveWindow(app.apply, right, 218, 125, 32, TRUE);
    MoveWindow(app.open, left, height - 58, 110, 32, TRUE);
    MoveWindow(app.save, left + 120, height - 58, 110, 32, TRUE);
    MoveWindow(app.status, left + 245, height - 53, width - left - 269, 26, TRUE);
}

void showMode(App& app, bool map) {
    app.mapMode = map;
    for (HWND item : {app.canvas, app.layers, app.brush, app.layerLabel, app.brushLabel, app.undo, app.mapHint})
        ShowWindow(item, map ? SW_SHOW : SW_HIDE);
    for (HWND item : {app.fields, app.value, app.apply, app.label})
        ShowWindow(item, map ? SW_HIDE : SW_SHOW);
}

void updateScroll(App& app) {
    if (!app.map.room() || app.map.layers().empty()) return;
    RECT rect{};
    GetClientRect(app.canvas, &rect);
    const auto& layer = app.map.layers()[0];
    SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE};
    info.nMax = static_cast<int>(layer.rows.empty() ? 0 : layer.rows[0].size() * kCellSize) - 1;
    info.nPage = static_cast<UINT>(rect.right);
    SetScrollInfo(app.canvas, SB_HORZ, &info, TRUE);
    info.nMax = static_cast<int>(layer.rows.size() * kCellSize) - 1;
    info.nPage = static_cast<UINT>(rect.bottom);
    SetScrollInfo(app.canvas, SB_VERT, &info, TRUE);
}

void paintCell(App& app, int x, int y) {
    const int layer = static_cast<int>(SendMessageW(app.layers, CB_GETCURSEL, 0, 0));
    const int brush = static_cast<int>(SendMessageW(app.brush, CB_GETCURSEL, 0, 0));
    if (layer < 0 || brush < 0 || x < 0 || y < 0) return;
    const char symbol = static_cast<char>(SendMessageW(app.brush, CB_GETITEMDATA, brush, 0));
    const auto& layers = app.map.layers();
    if (static_cast<std::size_t>(layer) >= layers.size() ||
        static_cast<std::size_t>(y) >= layers[layer].rows.size() ||
        static_cast<std::size_t>(x) >= layers[layer].rows[y].size()) return;
    if (layers[layer].rows[y][x] == symbol) return;
    std::string problem;
    if (!app.map.paint(static_cast<std::size_t>(layer), static_cast<std::size_t>(x),
                       static_cast<std::size_t>(y), symbol, problem)) {
        error(app.window, wide(problem));
        return;
    }
    app.cursorX = x;
    app.cursorY = y;
    app.dirty = true;
    SetWindowTextW(app.status, L"Unsaved map changes");
    InvalidateRect(app.canvas, nullptr, FALSE);
}

LRESULT CALLBACK mapProc(HWND window, UINT id, WPARAM wParam, LPARAM lParam) {
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (id == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(window, id, wParam, lParam);
    switch (id) {
    case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_SIZE: updateScroll(*app); return 0;
    case WM_HSCROLL:
    case WM_VSCROLL: {
        const int bar = id == WM_HSCROLL ? SB_HORZ : SB_VERT;
        SCROLLINFO info{sizeof(info), SIF_ALL};
        GetScrollInfo(window, bar, &info);
        int position = info.nPos;
        switch (LOWORD(wParam)) {
        case SB_LINELEFT: position -= kCellSize; break;
        case SB_LINERIGHT: position += kCellSize; break;
        case SB_PAGELEFT: position -= static_cast<int>(info.nPage); break;
        case SB_PAGERIGHT: position += static_cast<int>(info.nPage); break;
        case SB_THUMBTRACK: position = info.nTrackPos; break;
        }
        info.fMask = SIF_POS;
        info.nPos = position;
        SetScrollInfo(window, bar, &info, TRUE);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN:
        SetFocus(window);
        SetCapture(window);
        paintCell(*app, (static_cast<short>(LOWORD(lParam)) + GetScrollPos(window, SB_HORZ)) / kCellSize,
                  (static_cast<short>(HIWORD(lParam)) + GetScrollPos(window, SB_VERT)) / kCellSize);
        return 0;
    case WM_MOUSEMOVE:
        if ((wParam & MK_LBUTTON) != 0 && GetCapture() == window) {
            const int x = static_cast<short>(LOWORD(lParam));
            const int y = static_cast<short>(HIWORD(lParam));
            if (x >= 0 && y >= 0)
                paintCell(*app, (x + GetScrollPos(window, SB_HORZ)) / kCellSize,
                          (y + GetScrollPos(window, SB_VERT)) / kCellSize);
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == window) ReleaseCapture();
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_LEFT && app->cursorX > 0) --app->cursorX;
        else if (wParam == VK_UP && app->cursorY > 0) --app->cursorY;
        else if (wParam == VK_RIGHT && app->map.room() && app->cursorX + 1 < app->map.room()->columns()) ++app->cursorX;
        else if (wParam == VK_DOWN && app->map.room() && app->cursorY + 1 < app->map.room()->rows()) ++app->cursorY;
        else if (wParam == VK_SPACE || wParam == VK_RETURN) paintCell(*app, app->cursorX, app->cursorY);
        if (app->map.room()) {
            RECT client{};
            GetClientRect(window, &client);
            int horizontal = GetScrollPos(window, SB_HORZ);
            int vertical = GetScrollPos(window, SB_VERT);
            if (app->cursorX * kCellSize < horizontal) horizontal = app->cursorX * kCellSize;
            if ((app->cursorX + 1) * kCellSize > horizontal + client.right)
                horizontal = (app->cursorX + 1) * kCellSize - client.right;
            if (app->cursorY * kCellSize < vertical) vertical = app->cursorY * kCellSize;
            if ((app->cursorY + 1) * kCellSize > vertical + client.bottom)
                vertical = (app->cursorY + 1) * kCellSize - client.bottom;
            SetScrollPos(window, SB_HORZ, horizontal, TRUE);
            SetScrollPos(window, SB_VERT, vertical, TRUE);
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT rect{};
        GetClientRect(window, &rect);
        HBRUSH background = CreateSolidBrush(RGB(27, 47, 62));
        FillRect(dc, &rect, background);
        DeleteObject(background);
        SetBkMode(dc, TRANSPARENT);
        const auto* room = app->map.room();
        if (!room) {
            SetTextColor(dc, RGB(225, 235, 239));
            const wchar_t* text = L"Open a room to edit its map.";
            TextOutW(dc, 16, 16, text, static_cast<int>(wcslen(text)));
        } else {
            const int offsetX = GetScrollPos(window, SB_HORZ);
            const int offsetY = GetScrollPos(window, SB_VERT);
            const auto& layers = app->map.layers();
            for (std::size_t y = 0; y < layers[0].rows.size(); ++y) {
                for (std::size_t x = 0; x < layers[0].rows[y].size(); ++x) {
                    RECT cell{static_cast<LONG>(x * kCellSize) - offsetX,
                              static_cast<LONG>(y * kCellSize) - offsetY,
                              static_cast<LONG>((x + 1) * kCellSize) - offsetX,
                              static_cast<LONG>((y + 1) * kCellSize) - offsetY};
                    if (cell.right < 0 || cell.bottom < 0 || cell.left >= rect.right || cell.top >= rect.bottom) continue;
                    char symbol = ' ';
                    auto collision = echo::framework::TileCollision::None;
                    for (const auto& layer : layers) {
                        if (y < layer.rows.size() && x < layer.rows[y].size() &&
                            layer.rows[y][x] != ' ' && layer.rows[y][x] != '.') {
                            symbol = layer.rows[y][x];
                            const auto* layerTile = room->findTileType(symbol);
                            if (layerTile && layerTile->collision == echo::framework::TileCollision::Hazard)
                                collision = echo::framework::TileCollision::Hazard;
                            else if (layerTile && layerTile->collision == echo::framework::TileCollision::Solid &&
                                     collision == echo::framework::TileCollision::None)
                                collision = echo::framework::TileCollision::Solid;
                        }
                    }
                    const auto* tile = room->findTileType(symbol);
                    COLORREF color = RGB(37, 61, 74);
                    if (collision == echo::framework::TileCollision::Solid) color = RGB(71, 107, 124);
                    else if (collision == echo::framework::TileCollision::Hazard) color = RGB(173, 75, 58);
                    else if (tile) color = RGB(48, 101, 105);
                    HBRUSH brush = CreateSolidBrush(color);
                    FillRect(dc, &cell, brush);
                    DeleteObject(brush);
                    FrameRect(dc, &cell, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                    if (tile) {
                        wchar_t text = static_cast<unsigned char>(symbol);
                        SetTextColor(dc, RGB(235, 245, 245));
                        TextOutW(dc, cell.left + 5, cell.top + 1, &text, 1);
                    }
                    if (static_cast<int>(x) == app->cursorX && static_cast<int>(y) == app->cursorY &&
                        GetFocus() == window) FrameRect(dc, &cell, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
                }
            }
        }
        EndPaint(window, &paint);
        return 0;
    }
    }
    return DefWindowProcW(window, id, wParam, lParam);
}

bool discard(App& app) {
    return (!app.dirty && !app.valueDirty) ||
           MessageBoxW(app.window, L"Discard unsaved changes?", L"Echo Editor",
                       MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}

void selectField(App& app, int index) {
    app.selected = index;
    app.changing = true;
    if (index >= 0 && static_cast<std::size_t>(index) < app.model.fields().size()) {
        const auto& field = app.model.fields()[static_cast<std::size_t>(index)];
        std::wstring description = wide(field.label);
        if (field.kind == echo::editor::FieldKind::Script)
            description += L"\r\nLua source, 1-4096 bytes";
        else if (field.kind != echo::editor::FieldKind::Objective)
            description += L"\r\nAllowed: " + std::to_wstring(field.minimum) + L" - " +
                           std::to_wstring(field.maximum);
        else description += L"\r\nGame font, maximum 48 characters";
        SetWindowTextW(app.label, description.c_str());
        SetWindowTextW(app.value, wide(field.value).c_str());
    } else {
        SetWindowTextW(app.label, L"Select a property.");
        SetWindowTextW(app.value, L"");
    }
    app.valueDirty = false;
    app.changing = false;
}

bool applyValue(App& app) {
    if (!app.valueDirty) return true;
    const int size = GetWindowTextLengthW(app.value);
    if (size > 4096) { error(app.window, L"Value is too long."); return false; }
    std::vector<wchar_t> buffer(static_cast<std::size_t>(size) + 1);
    if (GetWindowTextW(app.value, buffer.data(), size + 1) != size) {
        error(app.window, L"Could not read the property value.");
        return false;
    }
    std::string value, problem;
    if (!utf8(std::wstring_view(buffer.data(), static_cast<std::size_t>(size)), value)) {
        error(app.window, L"Property value contains invalid text.");
        return false;
    }
    if (app.selected < 0 || !app.model.edit(static_cast<std::size_t>(app.selected), value, problem)) {
        error(app.window, wide(problem.empty() ? "Select a field first." : problem));
        return false;
    }
    app.dirty = true;
    selectField(app, app.selected);
    SetWindowTextW(app.status, L"Unsaved changes");
    return true;
}

void openFile(App& app) {
    if (!discard(app)) return;
    std::array<wchar_t, 32768> selected{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = app.window;
    dialog.lpstrFilter = L"JSON files (*.json)\0*.json\0All files\0*.*\0";
    dialog.lpstrFile = selected.data();
    dialog.nMaxFile = static_cast<DWORD>(selected.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameW(&dialog)) {
        DWORD problem = CommDlgExtendedError();
        if (problem) systemError(app.window, L"Could not open the file dialog", problem);
        return;
    }
    std::string data, problem;
    std::wstring path;
    DWORD code{};
    if (!echo::editor::readContent(selected.data(), data, &path, code)) {
        systemError(app.window, L"Could not read the selected file", code);
        return;
    }
    echo::framework::JsonDocument document;
    echo::framework::JsonError jsonError{};
    if (!echo::framework::JsonDocument::parse(data.data(), data.size(), document, jsonError)) {
        error(app.window, L"The selected file is not valid JSON.");
        return;
    }
    std::string_view documentId;
    const bool room = document.root().find("id").string(documentId) &&
                      documentId.find(":room/") != std::string_view::npos;
    if (room) {
        echo::editor::MapModel candidate;
        if (!candidate.open(path, data, problem)) {
            error(app.window, wide(problem));
            return;
        }
        app.map = std::move(candidate);
        SendMessageW(app.layers, CB_RESETCONTENT, 0, 0);
        SendMessageW(app.brush, CB_RESETCONTENT, 0, 0);
        for (const auto& layer : app.map.layers()) {
            const std::wstring name = wide(layer.name);
            SendMessageW(app.layers, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        }
        auto addBrush = [&](const std::wstring& label, char symbol) {
            const LRESULT index = SendMessageW(app.brush, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            if (index >= 0) SendMessageW(app.brush, CB_SETITEMDATA, index, static_cast<LPARAM>(symbol));
        };
        addBrush(L"Erase", ' ');
        for (const auto& tile : app.map.room()->tileTypes()) {
            std::wstring label(1, static_cast<wchar_t>(tile.symbol));
            label += tile.collision == echo::framework::TileCollision::Solid ? L"  solid" :
                     tile.collision == echo::framework::TileCollision::Hazard ? L"  hazard" : L"  decoration";
            addBrush(label, tile.symbol);
        }
        SendMessageW(app.layers, CB_SETCURSEL, 0, 0);
        SendMessageW(app.brush, CB_SETCURSEL, 0, 0);
        app.cursorX = app.cursorY = 0;
        showMode(app, true);
        updateScroll(app);
        InvalidateRect(app.canvas, nullptr, FALSE);
    } else {
        echo::editor::ContentModel candidate;
        if (!candidate.open(data, problem)) {
            error(app.window, wide(problem));
            return;
        }
        app.model = std::move(candidate);
        SendMessageW(app.fields, LB_RESETCONTENT, 0, 0);
        for (const auto& field : app.model.fields()) {
            const std::wstring label = wide(field.label);
            if (SendMessageW(app.fields, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())) < 0) {
                error(app.window, L"Could not display the property list.");
                return;
            }
        }
        SendMessageW(app.fields, LB_SETCURSEL, 0, 0);
        selectField(app, 0);
        showMode(app, false);
    }
    app.filename = std::move(path);
    app.original = std::move(data);
    app.dirty = false;
    app.valueDirty = false;
    SetWindowTextW(app.path, app.filename.c_str());
    SetWindowTextW(app.status, L"File opened");
    EnableWindow(app.save, TRUE);
}

void saveFile(App& app) {
    if (app.filename.empty() || (!app.mapMode && !applyValue(app)) || !app.dirty) return;
    std::string output, validation;
    if (app.mapMode) {
        if (!app.map.serialize(output, validation) || !app.map.validate(output, validation)) {
            error(app.window, wide(validation));
            return;
        }
    } else {
        output = app.model.source();
    }
    if (MessageBoxW(app.window, L"Replace the opened game content file with these changes?",
                    L"Save game content", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
    std::wstring problem;
    if (!echo::editor::replaceContent(app.filename, app.original, output, problem)) {
        error(app.window, problem);
        return;
    }
    app.original = output;
    app.dirty = false;
    SetWindowTextW(app.status, L"Saved");
}

LRESULT CALLBACK windowProc(HWND window, UINT id, WPARAM wParam, LPARAM lParam) {
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (id == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(window, id, wParam, lParam);
    switch (id) {
    case WM_CREATE:
        app->font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH, L"Segoe UI");
        app->titleFont = CreateFontW(-25, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH, L"Segoe UI");
        app->background = CreateSolidBrush(RGB(238, 245, 248));
        if (!app->font || !app->titleFont || !app->background) return -1;
        app->path = control(*app, L"STATIC", L"Open a room in assets\\world\\rooms to edit its map.", 0);
        app->canvas = CreateWindowExW(WS_EX_CLIENTEDGE, L"EchoMapView", L"Map tile grid",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_HSCROLL | WS_VSCROLL,
                                      0, 0, 0, 0, window, nullptr, GetModuleHandleW(nullptr), app);
        app->layerLabel = control(*app, L"STATIC", L"Layer", 0);
        app->layers = control(*app, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, kLayer);
        app->brushLabel = control(*app, L"STATIC", L"Tile brush", 0);
        app->brush = control(*app, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, kBrush);
        app->undo = control(*app, L"BUTTON", L"Undo tile", WS_TABSTOP, kUndo);
        app->mapHint = control(*app, L"STATIC",
            L"Symbol grid: blue is solid, red is hazardous, teal is decoration.\r\n"
            L"Arrow keys move; Space paints.", 0);
        app->fields = control(*app, L"LISTBOX", L"", LBS_NOTIFY | WS_TABSTOP | WS_VSCROLL, kFields, WS_EX_CLIENTEDGE);
        app->label = control(*app, L"STATIC", L"Select a property.", 0);
        app->value = control(*app, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, kValue, WS_EX_CLIENTEDGE);
        app->apply = control(*app, L"BUTTON", L"Apply value", WS_TABSTOP, kApply);
        app->open = control(*app, L"BUTTON", L"Open...", WS_TABSTOP, kOpen);
        app->save = control(*app, L"BUTTON", L"Save", WS_TABSTOP, kSave);
        app->status = control(*app, L"STATIC", L"", 0);
        if (!app->path || !app->canvas || !app->layers || !app->brush || !app->undo || !app->mapHint || !app->layerLabel ||
            !app->brushLabel || !app->fields || !app->label || !app->value || !app->apply ||
            !app->open || !app->save || !app->status) return -1;
        SendMessageW(app->value, EM_SETLIMITTEXT, 1024, 0);
        EnableWindow(app->save, FALSE);
        showMode(*app, true);
        return 0;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = {850, 500};
        return 0;
    case WM_SIZE:
        if (app->fields) layout(*app, LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == kValue && HIWORD(wParam) == EN_CHANGE && !app->changing) {
            app->valueDirty = true;
            SetWindowTextW(app->status, L"Unapplied value");
        } else if (LOWORD(wParam) == kFields && HIWORD(wParam) == LBN_SELCHANGE) {
            const int next = static_cast<int>(SendMessageW(app->fields, LB_GETCURSEL, 0, 0));
            if (applyValue(*app)) selectField(*app, next);
            else SendMessageW(app->fields, LB_SETCURSEL, app->selected, 0);
        } else if (LOWORD(wParam) == kLayer && HIWORD(wParam) == CBN_SELCHANGE) {
            InvalidateRect(app->canvas, nullptr, FALSE);
        } else if (HIWORD(wParam) == BN_CLICKED) {
            if (LOWORD(wParam) == kOpen) openFile(*app);
            else if (LOWORD(wParam) == kSave) saveFile(*app);
            else if (LOWORD(wParam) == kApply) applyValue(*app);
            else if (LOWORD(wParam) == kUndo && app->mapMode && app->map.undo()) {
                app->dirty = true;
                SetWindowTextW(app->status, L"Unsaved map changes");
                InvalidateRect(app->canvas, nullptr, FALSE);
            }
        }
        return 0;
    case WM_CTLCOLORSTATIC:
        SetBkColor(reinterpret_cast<HDC>(wParam), RGB(238, 245, 248));
        SetTextColor(reinterpret_cast<HDC>(wParam), RGB(32, 54, 76));
        return reinterpret_cast<LRESULT>(app->background);
    case WM_ERASEBKGND: {
        RECT area{};
        GetClientRect(window, &area);
        FillRect(reinterpret_cast<HDC>(wParam), &area, app->background);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(32, 54, 76));
        HGDIOBJ previous = SelectObject(dc, app->titleFont);
        TextOutW(dc, 24, 22, L"Echo Editor", 11);
        SelectObject(dc, previous);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_CLOSE:
        if (discard(*app)) DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        if (app->font) DeleteObject(app->font);
        if (app->titleFont) DeleteObject(app->titleFont);
        if (app->background) DeleteObject(app->background);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, id, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    App app{};
    WNDCLASSW type{};
    type.lpfnWndProc = windowProc;
    type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.lpszClassName = L"EchoEditor";
    if (!RegisterClassW(&type)) {
        systemError(nullptr, L"Could not register the editor window", GetLastError());
        return 1;
    }
    WNDCLASSW mapType{};
    mapType.lpfnWndProc = mapProc;
    mapType.hInstance = instance;
    mapType.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    mapType.lpszClassName = L"EchoMapView";
    if (!RegisterClassW(&mapType)) {
        systemError(nullptr, L"Could not register the map view", GetLastError());
        return 1;
    }
    HWND window = CreateWindowExW(0, type.lpszClassName, L"Echo Editor",
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                  1100, 650, nullptr, nullptr, instance, &app);
    if (!window) {
        systemError(nullptr, L"Could not create the editor window", GetLastError());
        return 1;
    }
    ShowWindow(window, show);
    MSG event{};
    int result{};
    while ((result = GetMessageW(&event, nullptr, 0, 0)) > 0) {
        if (!IsDialogMessageW(window, &event)) {
            TranslateMessage(&event);
            DispatchMessageW(&event);
        }
    }
    if (result < 0) {
        systemError(nullptr, L"The editor event loop failed", GetLastError());
        return 1;
    }
    return static_cast<int>(event.wParam);
}
