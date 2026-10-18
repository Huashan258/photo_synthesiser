// 图片拼接工具 —— 主程序
// GUI: Slint（ui/app-window.slint 编译生成 app-window.h）
// 图像处理: OpenCV 5.0
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <thread>
#include <memory>
#include <cmath>

#include <windows.h>
#include <shobjidl.h>

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include <slint.h>
#include "app-window.h"

namespace fs = std::filesystem;

// 版本号由 CMakeLists.txt 的 project(... VERSION x.y.z) 注入
#ifndef APP_VERSION
#define APP_VERSION "1.1.0"
#endif

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

// ---------------- 图片读写（支持中文路径）----------------
static cv::Mat imreadWide(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::vector<char> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (buf.empty()) return {};
    return cv::imdecode(std::vector<uchar>(buf.begin(), buf.end()), cv::IMREAD_COLOR);
}

static bool imwriteWide(const std::wstring& path, const cv::Mat& img) {
    std::string ext = w2u(fs::path(path).extension().wstring());
    if (ext.empty()) ext = ".jpg";
    std::vector<uchar> buf;
    if (!cv::imencode(ext, img, buf)) return false;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    return f.good();
}

// 生成列表缩略图（居中裁剪后编码为 PNG，直接交给 Slint）
static slint::Image makeThumbnail(const std::wstring& path, int tw = 112, int th = 84) {
    cv::Mat img = imreadWide(path);
    if (img.empty()) return {};
    double scale = std::max(static_cast<double>(tw) / img.cols, static_cast<double>(th) / img.rows);
    cv::Mat small;
    cv::resize(img, small, cv::Size(std::max(1, static_cast<int>(img.cols * scale)),
                                    std::max(1, static_cast<int>(img.rows * scale))));
    int x = std::max(0, (small.cols - tw) / 2);
    int y = std::max(0, (small.rows - th) / 2);
    cv::Rect roi(x, y, std::min(tw, small.cols), std::min(th, small.rows));
    std::vector<uchar> png;
    if (!cv::imencode(".png", small(roi), png)) return {};
    return slint::Image::load_from_data(std::span<const uint8_t>(png.data(), png.size()), "png");
}

// ---------------- 文件筛选 ----------------
static bool isImageFile(const fs::path& p) {
    if (!fs::is_regular_file(p)) return false;
    std::wstring ext = p.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    static const std::vector<std::wstring> kExt = {
        L".jpg", L".jpeg", L".png", L".bmp", L".tif", L".tiff", L".webp"
    };
    return std::find(kExt.begin(), kExt.end(), ext) != kExt.end();
}

static std::string fileSizeText(const std::wstring& path) {
    std::error_code ec;
    auto bytes = fs::file_size(path, ec);
    if (ec || bytes == 0) return "—";
    char buf[32];
    if (bytes >= 1024 * 1024)
        std::snprintf(buf, sizeof(buf), "%.1f MB", bytes / 1048576.0);
    else
        std::snprintf(buf, sizeof(buf), "%.0f KB", bytes / 1024.0);
    return buf;
}

// ---------------- 核心拼接逻辑 ----------------
// files: 按顺序的图片路径（宽字符，支持中文）；返回 0 成功，errMsg 输出错误信息
static int stitchFiles(const std::vector<std::wstring>& files, const std::wstring& outputFile,
                       bool horizontal, std::string& errMsg) {
    if (files.empty()) {
        errMsg = "没有可拼接的图片";
        return 1;
    }

    std::vector<cv::Mat> images;
    for (const auto& f : files) {
        cv::Mat img = imreadWide(f);
        if (img.empty()) {
            std::cerr << "[警告] 无法读取，已跳过: " << w2u(f) << "\n";
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

    if (result.empty() || !imwriteWide(outputFile, result)) {
        errMsg = "保存失败: " + w2u(outputFile);
        return 2;
    }
    std::cout << "拼接完成 (" << (horizontal ? "横向" : "纵向") << "): "
              << images.size() << " 张 -> " << result.cols << "x" << result.rows
              << " -> " << w2u(outputFile) << "\n";
    return 0;
}

// ---------------- 系统文件对话框 ----------------
static std::vector<std::wstring> pickImages() {
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
    if (SUCCEEDED(pDlg->Show(nullptr))) {
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

static std::wstring pickSaveFile(bool horizontal) {
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
    if (SUCCEEDED(pDlg->Show(nullptr))) {
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

// ---------------- 可视化面板（Slint）----------------
static int runGui() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    auto ui = AppWindow::create();
    slint::ComponentWeakHandle<AppWindow> weak(ui);   // 供后台线程回传结果
    auto model = std::make_shared<slint::VectorModel<ImageItem>>();
    ui->set_images(model);

    std::vector<std::wstring> files;   // 图片路径，与 model 行一一对应
    std::vector<cv::Size>    dims;     // 原图尺寸，用于计算输出预览
    auto lastOutput = std::make_shared<std::wstring>();

    // 更新计数与输出尺寸预览
    auto refreshInfo = [&]() {
        bool vertical = ui->get_vertical();
        ui->set_count_text(slint::SharedString(std::to_string(files.size()) + " 张图片"));
        if (files.empty()) {
            ui->set_preview_text(slint::SharedString("—"));
            return;
        }
        int target = vertical ? dims[0].width : dims[0].height;
        for (const auto& d : dims)
            target = std::min(target, vertical ? d.width : d.height);
        long long total = 0;
        for (const auto& d : dims) {
            double scale = static_cast<double>(target) / (vertical ? d.width : d.height);
            total += static_cast<long long>(std::llround((vertical ? d.height : d.width) * scale));
        }
        std::string txt = vertical
            ? ("预计输出 " + std::to_string(target) + " × " + std::to_string(total))
            : ("预计输出 " + std::to_string(total) + " × " + std::to_string(target));
        ui->set_preview_text(slint::SharedString(txt));
    };

    // 重排后刷新序号
    auto renumber = [&]() {
        for (size_t i = 0; i < files.size(); ++i) {
            if (auto row = model->row_data(i)) {
                row->index_label = slint::SharedString(std::to_string(i + 1));
                model->set_row_data(i, *row);
            }
        }
    };

    ui->on_add_images([&]() {
        auto picked = pickImages();
        if (picked.empty()) return;
        size_t added = 0, failed = 0;
        for (const auto& p : picked) {
            cv::Mat img = imreadWide(p);
            if (img.empty()) { ++failed; continue; }
            files.push_back(p);
            dims.emplace_back(img.cols, img.rows);

            ImageItem item;
            item.index_label = slint::SharedString(std::to_string(files.size()));
            item.name = slint::SharedString(w2u(fs::path(p).filename().wstring()));
            item.detail = slint::SharedString(std::to_string(img.cols) + " × " +
                                              std::to_string(img.rows) + "  ·  " +
                                              fileSizeText(p));
            item.thumb = makeThumbnail(p);
            model->push_back(item);
            ++added;
        }
        renumber();
        refreshInfo();
        std::string msg = "已添加 " + std::to_string(added) + " 张图片";
        if (failed) msg += "（" + std::to_string(failed) + " 张无法读取已跳过）";
        ui->set_status(slint::SharedString(msg));
    });

    ui->on_remove_item([&](int index) {
        if (index < 0 || index >= static_cast<int>(files.size())) return;
        files.erase(files.begin() + index);
        dims.erase(dims.begin() + index);
        model->erase(static_cast<size_t>(index));
        int sel = ui->get_selected_index();
        if (sel > index) --sel;
        if (sel >= static_cast<int>(files.size())) sel = static_cast<int>(files.size()) - 1;
        ui->set_selected_index(sel);
        renumber();
        refreshInfo();
        ui->set_status(slint::SharedString("已删除第 " + std::to_string(index + 1) + " 张"));
    });

    auto moveItem = [&](int index, int delta) {
        int j = index + delta;
        if (index < 0 || j < 0 || index >= static_cast<int>(files.size()) ||
            j >= static_cast<int>(files.size()))
            return;
        std::swap(files[index], files[j]);
        std::swap(dims[index], dims[j]);
        auto a = model->row_data(index);
        auto b = model->row_data(j);
        if (a && b) {
            model->set_row_data(j, *a);
            model->set_row_data(index, *b);
        }
        ui->set_selected_index(j);
        renumber();
        refreshInfo();
    };
    ui->on_move_up([moveItem](int index) { moveItem(index, -1); });
    ui->on_move_down([moveItem](int index) { moveItem(index, 1); });

    ui->on_clear_all([&]() {
        files.clear();
        dims.clear();
        model->clear();
        ui->set_selected_index(-1);
        refreshInfo();
        ui->set_status(slint::SharedString("已清空列表"));
    });

    ui->on_generate([&, weak]() {
        if (files.empty()) {
            ui->set_status(slint::SharedString("请先添加图片"));
            return;
        }
        bool vertical = ui->get_vertical();
        std::wstring out = pickSaveFile(!vertical);
        if (out.empty()) {
            ui->set_status(slint::SharedString("已取消保存"));
            return;
        }

        ui->set_busy(true);
        ui->set_has_result(false);
        ui->set_status(slint::SharedString("正在拼接，请稍候…"));

        std::vector<std::wstring> snapshot = files;
        std::thread([weak, snapshot, out, vertical, lastOutput]() {
            std::string errMsg;
            int rc = stitchFiles(snapshot, out, !vertical, errMsg);
            bool ok = (rc == 0);
            std::string status = ok ? ("已保存：" + w2u(out)) : ("拼接失败：" + errMsg);
            slint::invoke_from_event_loop([weak, status, ok, out, lastOutput]() {
                *lastOutput = ok ? out : L"";
                if (auto handle = weak.lock()) {
                    auto w = *handle;                 // ComponentHandle -> 对象
                    w->set_busy(false);
                    w->set_status(slint::SharedString(status));
                    w->set_has_result(ok);
                }
            });
        }).detach();
    });

    ui->on_direction_changed([&]() {
        refreshInfo();
        ui->set_status(slint::SharedString(ui->get_vertical() ? "方向：纵向拼接" : "方向：横向拼接"));
    });

    ui->on_open_result([lastOutput]() {
        if (!lastOutput->empty())
            ShellExecuteW(nullptr, L"open", lastOutput->c_str(), nullptr, nullptr, SW_SHOW);
    });

    refreshInfo();
    ui->run();

    CoUninitialize();
    return 0;
}

// ---------------- 入口 ----------------
// Windows 子系统入口：双击启动为纯 GUI（无控制台黑框）；
// 从 cmd 带参数运行时附加到父控制台，保留命令行输出
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wargv) return 1;
    std::vector<std::wstring> args(wargv, wargv + argc);
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
    } else {
        SetConsoleOutputCP(CP_UTF8);
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    fs::path inputDir = args[1];
    std::wstring outputFile = (argc > 2) ? args[2] : L"result.jpg";
    bool horizontal = (argc > 3 && (args[3][0] == L'h' || args[3][0] == L'H'));

    if (!fs::exists(inputDir)) {
        std::cerr << "[错误] 目录不存在: " << w2u(inputDir.wstring()) << "\n";
        CoUninitialize();
        return 1;
    }

    std::vector<fs::path> paths;
    for (const auto& entry : fs::directory_iterator(inputDir))
        if (isImageFile(entry.path()))
            paths.push_back(entry.path());
    std::sort(paths.begin(), paths.end());

    std::vector<std::wstring> files;
    for (const auto& p : paths) files.push_back(p.wstring());

    std::string errMsg;
    int rc = stitchFiles(files, outputFile, horizontal, errMsg);
    if (rc != 0) {
        std::cerr << "[错误] " << errMsg << "\n";
        std::cerr << "用法: photo_synthesiser [图片目录] [输出文件] [v|h]\n";
    }

    CoUninitialize();
    return rc;
}
