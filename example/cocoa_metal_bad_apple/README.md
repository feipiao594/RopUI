# Cocoa + Metal Bad Apple 示例

这是一个使用 RopUI 框架在 macOS 上通过 Cocoa 和 Metal 渲染 Bad Apple 动画的示例程序。

## 功能特点

- 使用 Metal GPU 加速渲染
- 黑白像素风格的动画播放
- 基于 RopUI 的事件循环系统
- Cocoa 原生窗口支持

## 程序功能
1. 检查数据文件是否加载成功
2. 创建一个 Cocoa 窗口
3. 使用 Metal 渲染 Bad Apple 动画
4. 鼠标点击可以暂停/继续播放

## 编译前准备

由于数据文件较大（约 500MB），此示例默认不参与编译。如需使用，请按以下步骤操作：

### 1. 生成动画数据

首先需要从 Bad Apple 视频文件（也可以任意视频）生成帧数据。需要准备一个 MP4 格式的 Bad Apple 视频文件。

```bash
# 基本用法（使用默认分辨率 320x240）
python generate_bad_apple_data.py bad_apple.mp4

# 指定帧数限制和分辨率
python generate_bad_apple_data.py bad_apple.mp4 1000 320 240
```

参数说明：
- `video_file.mp4`: 输入的 Bad Apple 视频文件路径
- `max_frames` (可选): 最大帧数限制，默认处理全部帧
- `width` (可选): 输出分辨率宽度，默认使用视频原始宽度
- `height` (可选): 输出分辨率高度，默认使用视频原始高度

脚本会生成两个文件：
- `bad_apple_data.h`: C++ 头文件，包含帧数据常量和类定义
- `bad_apple_frames.bin`: 二进制数据文件，包含所有帧的像素数据

### 2. 启用编译

在 `example/CMakeLists.txt` 中取消注释以下行：

```cmake
# add_subdirectory(cocoa_metal_bad_apple)  # 需要手动启用，详见该目录下的README.md
```

改为：

```cmake
add_subdirectory(cocoa_metal_bad_apple)
```