// tools/abconv.cpp —— A/B 对拍工具：走 ImageEditor 的真实 IO 路径（imageio::load）
// 把任意输入图转成无损 PNG，供与 vips CLI 解码结果逐像素比对。
//
// 命令行用法：abconv <input> <output.png>（脚本/批处理用）
// 双击打开（无参数）：弹窗选输入与输出文件，转完弹窗显示结果 —— 不再"闪退"。
#include "core/ImageIo.h"

#include <cstdio>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

namespace {

#ifdef _WIN32
// 弹窗选一个存在的文件（打开对话框）
bool pickInput(std::string& path)
{
    char buf[MAX_PATH] = {0};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "图像文件\0*.png;*.jpg;*.jpeg;*.webp;*.gif;*.tif;*.tiff\0所有文件\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameA(&ofn)) return false;
    path = buf;
    return true;
}

// 弹窗选输出路径（默认 output.png，存在则提示覆盖）
bool pickOutput(std::string& path)
{
    char buf[MAX_PATH] = {0};
    std::snprintf(buf, sizeof(buf), "output.png");
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "PNG 无损\0*.png\0";
    ofn.lpstrDefExt = "png";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameA(&ofn)) return false;
    path = buf;
    return true;
}
#endif

} // namespace

int main(int argc, char** argv)
{
    if (argc >= 3) {
        // 命令行模式（A/B 对拍脚本用）：行为与之前完全一致
        ie::Image img;
        if (!ie::imageio::load(argv[1], img)) {
            std::fprintf(stderr, "load failed: %s\n", ie::imageio::lastError().c_str());
            return 1;
        }
        if (!ie::imageio::save(argv[2], img, 95)) {
            std::fprintf(stderr, "save failed: %s\n", ie::imageio::lastError().c_str());
            return 1;
        }
        std::printf("%s -> %s (%dx%d)\n", argv[1], argv[2], img.width(), img.height());
        return 0;
    }

    // 双击打开：无参数 = 弹窗交互模式，先讲清楚这是命令行工具
    std::fprintf(stderr, "usage: abconv <input> <output.png>\n（无参数 = 弹窗交互模式）\n");
#ifdef _WIN32
    std::string in, out;
    if (!pickInput(in)) return 0; // 用户取消
    if (!pickOutput(out)) return 0;

    ie::Image img;
    if (!ie::imageio::load(in, img)) {
        MessageBoxA(nullptr, ie::imageio::lastError().c_str(), "转换失败", MB_OK | MB_ICONERROR);
        std::fprintf(stderr, "load failed: %s\n", ie::imageio::lastError().c_str());
        return 1;
    }
    if (!ie::imageio::save(out, img, 95)) {
        MessageBoxA(nullptr, ie::imageio::lastError().c_str(), "转换失败", MB_OK | MB_ICONERROR);
        std::fprintf(stderr, "save failed: %s\n", ie::imageio::lastError().c_str());
        return 1;
    }
    const std::string msg = "已转换\n" + in + "\n→\n" + out + "\n(" + std::to_string(img.width()) + " × " +
                            std::to_string(img.height()) + ")";
    MessageBoxA(nullptr, msg.c_str(), "转换完成", MB_OK | MB_ICONINFORMATION);
    std::printf("%s -> %s (%dx%d)\n", in.c_str(), out.c_str(), img.width(), img.height());
    return 0;
#else
    return 2;
#endif
}
