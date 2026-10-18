# 图片拼接工具（photo_synthesiser）

一个用 C++ + OpenCV + Win32 写的轻量图片拼接工具：把多张截图按顺序**纵向**或**横向**直接拼接成一张长图。

## 功能

- 可视化面板：添加图片（可多选）、删除/上移/下移/清空、选择纵向或横向、一键生成
- 尺寸不一致的图片自动等比缩放对齐（纵向对齐宽度 / 横向对齐高度）
- 支持命令行批处理模式
- Windows 子系统程序，双击启动无控制台黑框

## 环境要求

- Windows 10/11（x64）
- CMake ≥ 3.24
- MSVC（Visual Studio 2019/2022）或 MinGW 工具链
- OpenCV 4.x / 5.x（Windows 预编译包，MSVC vc16 版本）

## 构建

```cmake
# 修改 CMakeLists.txt 中的 OpenCV_ROOT 为你的 OpenCV build 目录
# 例如 OpenCV 5.0.0 解压后通常是 <解压目录>/opencv/build
```

用 CLion 打开项目：

1. `Settings → Build, Execution, Deployment → Toolchains` 添加 **Visual Studio** 工具链
2. CMake 配置里把 Toolchain 选为该 Visual Studio，然后 Reload CMake Project
3. 编译目标 `photo_synthesiser`

命令行构建（MSVC 开发者命令行环境）：

```bash
cmake -G Ninja -S . -B cmake-build-msvc -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-msvc
```

## 使用

**可视化模式**：双击 `photo_synthesiser.exe`，面板中依次操作即可。

**命令行模式**：

```bash
photo_synthesiser.exe <图片目录> <输出文件> [v|h]
# v = 纵向拼接（默认），h = 横向拼接
photo_synthesiser.exe images out.jpg        # 纵向
photo_synthesiser.exe images out.jpg h      # 横向
```

## 目录说明

| 文件 | 说明 |
|---|---|
| `main.cpp` | 全部源码（拼接逻辑 + Win32 面板 + 命令行入口） |
| `CMakeLists.txt` | 构建配置，含 OpenCV 查找与 DLL 自动拷贝 |
| `make_test_images.py` | 生成本地测试用图片（可选，不影响构建） |

## 许可

MIT
