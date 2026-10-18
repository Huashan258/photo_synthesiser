#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace fs = std::filesystem;

// 版本号由 CMakeLists.txt 的 project(... VERSION x.y.z) 注入
#ifndef APP_VERSION
#define APP_VERSION L"1.0.1"
#endif
// 窗口/标题使用的完整名称
#define APP_TITLE L"图片拼接工具 v" APP_VERSION

// 支持的图片扩展名
static const std::vector<std::string> kExtensions = {
    ".jpg", ".jpeg", ".png", ".bmp", ".tif", ".tiff", ".webp"
};

bool isImageFile(const fs::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return std::find(kExtensions.begin(), kExtensions.end(), ext) != kExtensions.end();
}

// ---------------- 编码转换 ----------------
static std::string w2u(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}
static std::wstring u2w(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

// ---------------- 核心拼接逻辑 ----------------
// files: 按顺序的图片路径（UTF-8）；返回 0 成功，errMsg 输出错误信息
int stitchFiles(const std::vector<std::string>& files, const std::string& outputFile,
                bool horizontal, std::string& errMsg) {
    if (files.empty()) {
        errMsg = "没有可拼接的图片";
        return 1;
    }

    std::vector<cv::Mat> images;
    for (const auto& f : files) {
        cv::Mat img = cv::imread(f);
        if (img.empty()) {
            std::cerr << "[警告] 无法读取，已跳过: " << f << "\n";
            continue;
        }
        images.push_back(img);
    }
    if (images.empty()) {
        errMsg = "所有图片都无法读取";
        return 1;
    }

    // 尺寸对齐：纵向拼接统一缩放到最小宽度，横向拼接统一缩放到最小高度
    int target = horizontal ? images[0].rows : images[0].cols;
    for (const auto& img : images)
        target = std::min(target, horizontal ? img.rows : img.cols);

    for (auto& img : images) {
        if ((horizontal ? img.rows : img.cols) != target) {
            double scale = static_cast<double>(target) / (horizontal ? img.rows : img.cols);
            cv::resize(img, img, cv::Size(static_cast<int>(img.cols * scale),
                                          static_cast<int>(img.rows * scale)));
        }
    }

    cv::Mat result;
    if (horizontal)
        cv::hconcat(images, result);
    else
        cv::vconcat(images, result);

    if (!cv::imwrite(outputFile, result)) {
        errMsg = "保存失败: " + outputFile;
        return 2;
    }
    std::cout << "拼接完成 (" << (horizontal ? "横向" : "纵向") << "): "
              << images.size() << " 张 -> " << result.cols << "x" << result.rows
              << " -> " << outputFile << "\n";
    return 0;
}

// ---------------- 可视化面板 ----------------

#define IDC_LIST      101
#define IDC_ADD       102
#define IDC_REMOVE    103
#define IDC_UP        104
#define IDC_DOWN      105
#define IDC_CLEAR     106
#define IDC_RADIO_V   107
#define IDC_RADIO_H   108
#define IDC_GENERATE  109
#define IDC_TITLE     110
#define IDC_SUBTITLE  111
#define IDC_STATUS    112
#define IDC_GROUP     113

// 配色
static const COLORREF C_BG      = RGB(245, 247, 250);  // 窗口背景
static const COLORREF C_ACCENT  = RGB(0, 120, 212);    // 主题蓝
static const COLORREF C_ACCENT_D= RGB(16, 110, 190);   // 主题蓝（按下）
static const COLORREF C_TEXT    = RGB(32, 32, 32);
static const COLORREF C_TEXT_SUB= RGB(115, 115, 115);
static const COLORREF C_BORDER  = RGB(200, 205, 212);

struct GuiState {
    HWND hList = nullptr;
    HWND hRadioV = nullptr;
    HWND hStatus = nullptr;
    std::vector<std::wstring> files;   // 与列表框同步的图片顺序
};
static GuiState g;

static HBRUSH g_bgBrush = nullptr;
static HBRUSH g_whiteBrush = nullptr;
static HFONT g_font = nullptr;        // 正文
static HFONT g_fontTitle = nullptr;   // 标题
static HFONT g_fontGen = nullptr;     // 生成按钮

// 刷新列表框显示和状态文字
static void refreshList() {
    SendMessageW(g.hList, LB_RESETCONTENT, 0, 0);
    for (const auto& f : g.files) {
        std::wstring name = fs::path(f).filename().wstring();
        SendMessageW(g.hList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    }
    std::wstring status = L"已添加 " + std::to_wstring(g.files.size()) + L" 张图片";
    SetWindowTextW(g.hStatus, status.c_str());
}

// 多选图片对话框
static std::vector<std::wstring> pickImages(HWND owner) {
    std::vector<std::wstring> result;
    IFileOpenDialog* pDlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                IID_IFileOpenDialog, reinterpret_cast<void**>(&pDlg))))
        return result;
    COMDLG_FILTERSPEC filters[] = {
        { L"图片文件", L"*.jpg;*.jpeg;*.png;*.bmp;*.tif;*.tiff;*.webp" },
        { L"所有文件", L"*.*" },
    };
    pDlg->SetFileTypes(2, filters);
    DWORD opts = 0;
    pDlg->GetOptions(&opts);
    pDlg->SetOptions(opts | FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST);
    pDlg->SetTitle(L"选择要拼接的图片（可多选）");
    if (SUCCEEDED(pDlg->Show(owner))) {
        IShellItemArray* pItems = nullptr;
        if (SUCCEEDED(pDlg->GetResults(&pItems))) {
            DWORD count = 0;
            pItems->GetCount(&count);
            for (DWORD i = 0; i < count; ++i) {
                IShellItem* pItem = nullptr;
                if (SUCCEEDED(pItems->GetItemAt(i, &pItem))) {
                    PWSTR path = nullptr;
                    if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                        result.push_back(path);
                        CoTaskMemFree(path);
                    }
                    pItem->Release();
                }
            }
            pItems->Release();
        }
    }
    pDlg->Release();
    return result;
}

// 保存位置对话框
static std::wstring pickSaveFile(HWND owner, bool horizontal) {
    std::wstring result;
    IFileSaveDialog* pDlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL,
                                IID_IFileSaveDialog, reinterpret_cast<void**>(&pDlg))))
        return result;
    COMDLG_FILTERSPEC filters[] = {
        { L"JPEG 图片", L"*.jpg" },
        { L"PNG 图片",  L"*.png" },
    };
    pDlg->SetFileTypes(2, filters);
    pDlg->SetDefaultExtension(L"jpg");
    pDlg->SetFileName(horizontal ? L"result_h.jpg" : L"result_v.jpg");
    pDlg->SetTitle(L"选择拼接结果保存位置");
    if (SUCCEEDED(pDlg->Show(owner))) {
        IShellItem* pItem = nullptr;
        if (SUCCEEDED(pDlg->GetResult(&pItem))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = path;
                CoTaskMemFree(path);
            }
            pItem->Release();
        }
    }
    pDlg->Release();
    return result;
}

static void moveSelected(int delta) {
    LRESULT sel = SendMessageW(g.hList, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) return;
    int i = static_cast<int>(sel);
    int j = i + delta;
    if (j < 0 || j >= static_cast<int>(g.files.size())) return;
    std::swap(g.files[i], g.files[j]);
    refreshList();
    SendMessageW(g.hList, LB_SETCURSEL, j, 0);
}

static void doGenerate(HWND hWnd) {
    if (g.files.empty()) {
        MessageBoxW(hWnd, L"请先点击【添加图片】选择要拼接的图片。", L"提示", MB_OK | MB_ICONWARNING);
        return;
    }
    bool horizontal = SendMessageW(g.hRadioV, BM_GETCHECK, 0, 0) != BST_CHECKED;

    std::wstring output = pickSaveFile(hWnd, horizontal);
    if (output.empty()) return;  // 用户取消

    std::vector<std::string> files;
    for (const auto& f : g.files) files.push_back(w2u(f));

    std::string errMsg;
    int rc = stitchFiles(files, w2u(output), horizontal, errMsg);
    if (rc == 0) {
        int open = MessageBoxW(hWnd,
            (L"拼接完成！\n\n已保存到：\n" + output + L"\n\n是否立即查看？").c_str(),
            L"完成", MB_YESNO | MB_ICONINFORMATION);
        if (open == IDYES)
            ShellExecuteW(nullptr, L"open", output.c_str(), nullptr, nullptr, SW_SHOW);
    } else {
        MessageBoxW(hWnd, (L"拼接失败：\n" + u2w(errMsg)).c_str(),
                    L"错误", MB_OK | MB_ICONERROR);
    }
}

// 自绘按钮：生成键为主题蓝实心，其余为白底描边，均圆角
static void drawButton(const DRAWITEMSTRUCT* dis) {
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    bool isGen   = (dis->CtlID == IDC_GENERATE);

    HDC dc = dis->hDC;
    RECT r = dis->rcItem;
    COLORREF fill   = isGen ? (pressed ? C_ACCENT_D : C_ACCENT)
                            : (pressed ? RGB(232, 236, 240) : RGB(255, 255, 255));
    COLORREF border = isGen ? C_ACCENT : C_BORDER;

    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen  = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBr = SelectObject(dc, br);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right - 1, r.bottom - 1, 10, 10);

    wchar_t text[128] = {};
    GetWindowTextW(dis->hwndItem, text, 127);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, isGen ? RGB(255, 255, 255) : C_TEXT);
    HFONT oldFont = reinterpret_cast<HFONT>(
        SelectObject(dc, isGen ? g_fontGen : g_font));
    DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(dc, oldFont);
    SelectObject(dc, oldBr);
    SelectObject(dc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // 标题与副标题
        CreateWindowExW(0, L"STATIC", APP_TITLE,
            WS_CHILD | WS_VISIBLE,
            24, 16, 300, 34, hWnd, reinterpret_cast<HMENU>(IDC_TITLE), nullptr, nullptr);
        CreateWindowExW(0, L"STATIC", L"把多张截图按顺序纵向或横向拼接成长图",
            WS_CHILD | WS_VISIBLE,
            24, 52, 340, 20, hWnd, reinterpret_cast<HMENU>(IDC_SUBTITLE), nullptr, nullptr);
        g.hStatus = CreateWindowExW(0, L"STATIC", L"已添加 0 张图片",
            WS_CHILD | WS_VISIBLE | SS_RIGHT,
            384, 54, 136, 18, hWnd, reinterpret_cast<HMENU>(IDC_STATUS), nullptr, nullptr);

        // 图片列表
        g.hList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            24, 82, 496, 218, hWnd, reinterpret_cast<HMENU>(IDC_LIST), nullptr, nullptr);

        // 操作按钮（自绘）
        struct Btn { int id; LPCWSTR text; int x, w; };
        const Btn btns[] = {
            { IDC_ADD,    L"添加图片",  24,  108 },
            { IDC_REMOVE, L"删除选中",  140, 108 },
            { IDC_UP,     L"上移",      256, 80 },
            { IDC_DOWN,   L"下移",      344, 80 },
            { IDC_CLEAR,  L"清空",      432, 88 },
        };
        for (const auto& b : btns)
            CreateWindowExW(0, L"BUTTON", b.text,
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                b.x, 314, b.w, 32, hWnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(b.id)), nullptr, nullptr);

        // 方向分组
        CreateWindowExW(0, L"BUTTON", L"拼接方向",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            24, 358, 496, 58, hWnd, reinterpret_cast<HMENU>(IDC_GROUP), nullptr, nullptr);
        g.hRadioV = CreateWindowExW(0, L"BUTTON", L"纵向拼接（上下排列）",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
            44, 382, 190, 26, hWnd, reinterpret_cast<HMENU>(IDC_RADIO_V), nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"横向拼接（左右排列）",
            WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            290, 382, 190, 26, hWnd, reinterpret_cast<HMENU>(IDC_RADIO_H), nullptr, nullptr);
        SendMessageW(g.hRadioV, BM_SETCHECK, BST_CHECKED, 0);  // 默认纵向

        // 生成按钮（主题蓝实心）
        CreateWindowExW(0, L"BUTTON", L"生成拼接图",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            24, 428, 496, 42, hWnd, reinterpret_cast<HMENU>(IDC_GENERATE), nullptr, nullptr);

        // 字体
        g_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        g_fontTitle = CreateFontW(-24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        g_fontGen = CreateFontW(-17, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");

        EnumChildWindows(hWnd, [](HWND child, LPARAM) -> BOOL {
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
            return TRUE;
        }, 0);
        SendMessageW(GetDlgItem(hWnd, IDC_TITLE), WM_SETFONT,
                     reinterpret_cast<WPARAM>(g_fontTitle), TRUE);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        // 标题/副标题/状态/单选框/分组框 背景统一为窗口背景色
        HDC dc = reinterpret_cast<HDC>(wParam);
        HWND h = reinterpret_cast<HWND>(lParam);
        SetBkMode(dc, TRANSPARENT);
        int id = GetDlgCtrlID(h);
        if (id == IDC_SUBTITLE || id == IDC_STATUS)
            SetTextColor(dc, C_TEXT_SUB);
        else if (id == IDC_TITLE)
            SetTextColor(dc, C_ACCENT);
        else
            SetTextColor(dc, C_TEXT);
        return reinterpret_cast<LRESULT>(g_bgBrush);
    }
    case WM_CTLCOLORBTN:
        return reinterpret_cast<LRESULT>(g_bgBrush);
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, C_TEXT);
        SetBkColor(dc, RGB(255, 255, 255));
        return reinterpret_cast<LRESULT>(g_whiteBrush);
    }
    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (dis->CtlType == ODT_BUTTON) {
            drawButton(dis);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_ADD: {
            auto picked = pickImages(hWnd);
            for (auto& p : picked) g.files.push_back(std::move(p));
            if (!picked.empty()) refreshList();
            return 0;
        }
        case IDC_REMOVE: {
            LRESULT sel = SendMessageW(g.hList, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) {
                g.files.erase(g.files.begin() + sel);
                refreshList();
            }
            return 0;
        }
        case IDC_UP:   moveSelected(-1); return 0;
        case IDC_DOWN: moveSelected(+1); return 0;
        case IDC_CLEAR:
            g.files.clear();
            refreshList();
            return 0;
        case IDC_GENERATE:
            doGenerate(hWnd);
            return 0;
        }
        break;
    case WM_DESTROY:
        if (g_font) DeleteObject(g_font);
        if (g_fontTitle) DeleteObject(g_fontTitle);
        if (g_fontGen) DeleteObject(g_fontGen);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int runGui() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    InitCommonControls();

    g_bgBrush = CreateSolidBrush(C_BG);
    g_whiteBrush = CreateSolidBrush(RGB(255, 255, 255));

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = g_bgBrush;
    wc.lpszClassName = L"PhotoSynthesiserWnd";
    // 从资源加载程序图标（app.rc 中 ID=1，由 icon.png 生成）
    wc.hIcon = reinterpret_cast<HICON>(LoadImageW(wc.hInstance,
        MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    wc.hIconSm = reinterpret_cast<HICON>(LoadImageW(wc.hInstance,
        MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, LR_DEFAULTSIZE));
    RegisterClassExW(&wc);

    // 客户区 544x486
    RECT rc = { 0, 0, 544, 486 };
    DWORD style = WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX;
    AdjustWindowRect(&rc, style, FALSE);
    HWND hWnd = CreateWindowExW(0, wc.lpszClassName, APP_TITLE,
        style | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!hWnd) {
        MessageBoxW(nullptr, L"窗口创建失败", L"错误", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }
    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    // 刷新标题栏大图标（任务栏会用 class 的 hIcon）
    if (HICON hBig = reinterpret_cast<HICON>(LoadImageW(wc.hInstance,
            MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE)))
        SendMessageW(hWnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hBig));

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    DeleteObject(g_bgBrush);
    DeleteObject(g_whiteBrush);
    CoUninitialize();
    return 0;
}

// ---------------- 入口 ----------------
// Windows 子系统入口：双击启动为纯 GUI（无控制台黑框）；
// 从 cmd 带参数运行时附加到父控制台，保留命令行输出
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i)
        args.push_back(w2u(wargv[i]));
    LocalFree(wargv);

    // 不带参数 -> 可视化面板
    if (argc <= 1)
        return runGui();

    // 命令行模式: photo_synthesiser [图片目录] [输出文件] [v|h]
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        SetConsoleOutputCP(CP_UTF8);
    }

    std::string inputDir   = args[1];
    std::string outputFile = (argc > 2) ? args[2] : "result.jpg";
    bool horizontal = (argc > 3 && (args[3][0] == 'h' || args[3][0] == 'H'));

    if (!fs::exists(inputDir)) {
        std::cerr << "[错误] 目录不存在: " << inputDir << "\n";
        return 1;
    }
    std::vector<fs::path> paths;
    for (const auto& entry : fs::directory_iterator(inputDir)) {
        if (entry.is_regular_file() && isImageFile(entry.path()))
            paths.push_back(entry.path());
    }
    std::sort(paths.begin(), paths.end());

    std::vector<std::string> files;
    for (const auto& p : paths) files.push_back(p.string());

    std::string errMsg;
    int rc = stitchFiles(files, outputFile, horizontal, errMsg);
    if (rc != 0) {
        std::cerr << "[错误] " << errMsg << "\n";
        std::cerr << "用法: photo_synthesiser [图片目录] [输出文件] [v|h]\n";
    }
    return rc;
}
