# OBS Beauty Filter for macOS (Apple Silicon / Intel)

一个 **OBS Studio 视频特效滤镜插件（源码项目）**，用 GPU Shader 为摄像头添加实时美颜。

> 当前为 **0.1.0 实验版**。源码和自动构建脚本已提供，但没有在本环境中的 macOS/OBS 上编译、加载或实测。它不是现成可安装的预编译 `.plugin`；需要在 Mac 上构建。也不是 AI 人脸跟踪 / 瘦脸 / 大眼插件。

## 功能

| 项目 | 参数范围 | 实现 |
| --- | --- | --- |
| 磨皮 | 0–100 | GPU 边缘保留平滑（近似双边滤波） |
| 磨皮半径 | 1–6 px | 控制采样范围 |
| 美白 / 提亮 | 0–100 | 中间调温和提亮 |
| 红润 | 0–100 | 轻微暖红色调整 |
| 锐化 | 0–100 | 非锐化遮罩恢复细节 |
| 尽量只处理肤色 | 开 / 关 | 近似肤色遮罩，不是人脸分割 |

**特点**：没有 Python、没有额外模型、没有 CPU 帧传输、无网络调用。适合普通 SDR 摄像头。HDR/扩展色域输入会安全跳过滤镜。透明视频源并非主要测试对象。

## 你需要

- macOS 12+，M 系列芯片或 Intel Mac
- **OBS Studio 与构建时使用的 OBS 开发依赖兼容**（OBS 更新后可能需要重编）
- Xcode 16 或更高版本、Xcode Command Line Tools
- CMake **3.30 或更高版本**（可通过 Homebrew 安装：`brew install cmake`）
- Git、Python 3，以及能够下载 GitHub OBS 源码和编译依赖的网络
- 足够的磁盘空间用于首次构建时下载 OBS 的 SDK 依赖

先打开终端运行：

```bash
xcode-select --install   # 如果已安装会提示无需再次安装
xcodebuild -version
cmake --version
```

## 构建（macOS）

```bash
# 解压下载的 zip，进入项目目录
cd obs-beauty-macos
bash scripts/build-macos.sh
```

脚本会：

1. 下载 [OBS 官方 obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) 到 `.work/`。
2. 注入 `src/plugin-main.c`、`data/beauty.effect` 和本地化资源，并保留官方模板的 OBS / Qt 依赖版本与哈希。
3. 根据模板的 `macos` 预设构建 Universal（arm64 + x86_64）插件。
4. 把构建产物放到 `dist/obs-beauty-macos.plugin`，并生成 `dist/obs-beauty-macos-macos-universal.zip`。
5. 使用本机 **ad-hoc codesign** 为本地测试签名；公开分发仍需要独立的 Developer ID 签名与 Apple 公证。

> 第一次构建需要联网。官方模板依赖规格可能随时间升级，如出现 SDK / 构建错误，请优先检查 `.work/obs-plugintemplate/buildspec.json` 和 OBS 版本是否匹配。

## 安装

```bash
bash scripts/install-macos.sh
```

等价的手动安装路径：

```text
~/Library/Application Support/obs-studio/plugins/obs-beauty-macos.plugin
```

**注意**：复制的是整个 `.plugin` bundle，而非只复制其内部二进制；不能把源码 zip 直接拖入插件文件夹。

完全退出 OBS 后重新打开。选择 **视频采集设备（摄像头） → 滤镜 → 效果滤镜下的 + → 猫哥美颜 · Beauty Filter**（英文 OBS 界面下为 Beauty Filter (GPU)）。

若需要彻底卸载：

```bash
bash scripts/uninstall-macos.sh
```

## 推荐起步参数

| 场景 | 磨皮 | 半径 | 美白 | 红润 | 锐化 | 肤色限制 |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| 技术录屏 + 摄像头小窗 | 35 | 2 | 12 | 6 | 12 | 开 |
| 主播正脸特写 | 45 | 3 | 15 | 10 | 10 | 开 |
| 自然清爽 | 22 | 2 | 8 | 4 | 8 | 开 |

建议先开正常的面部补光，再进行轻量调整。磨皮太大可能让眼镜、嘴唇等细节发软；肤色遮罩可能误伤木色或暖色衣服，也可能漏掉受彩色灯光影响的脸部。

## 项目结构

```text
obs-beauty-macos/
├── README.md
├── LICENSE
├── src/
│   └── plugin-main.c          # OBS C 接口 / 过滤器设置
├── data/
│   ├── beauty.effect          # GPU Shader
│   └── locale/
│       ├── en-US.ini
│       └── zh-CN.ini
├── scripts/
│   ├── build-macos.sh         # 构建 .plugin + 二进制 zip
│   ├── install-macos.sh       # 安装插件
│   └── uninstall-macos.sh     # 卸载插件
└── tests/
    └── source_smoke.py       # 静态文件 / 参数校验，不等于运行测试
```

## 故障排查

**OBS 的“效果滤镜”列表没有插件**

- 确认已在本机成功运行 `build-macos.sh` 和 `install-macos.sh`。
- 通过 Finder → 前往文件夹查看 `.plugin` 的完整路径。
- 必须彻底退出 OBS 后重启。
- OBS → 帮助 → 日志文件 → 查看当前日志，搜索 `obs-beauty-macos` 或 `beauty.effect`。
- 如果 OBS 提示二进制不兼容，重新构建与已安装 OBS 版本匹配的 SDK。

**运行慢 / CPU 或 GPU 占用增加**

- 磨皮半径先从 2 开始，摄像头使用 720p / 30 fps。
- 将磨皮和锐化滑块降为 0 可以跳过对应的 GPU 采样分支。

**效果不明显**

- 逐个调整，先观察磨皮 40 / 半径 3；复杂彩光环境下可临时关闭“肤色限制”确认算法是否作用。

**编译时报 GitHub 下载失败**

- 属于获取 OBS 构建依赖的网络问题，需保证 Mac 可以访问 GitHub 和依赖下载地址。

## 当前局限 / 下一阶段

本版本 **不含人脸检测、五官保护专用 mask、瘦脸、大眼、眼袋去除、LUT、Metal/Core ML 人脸分割**。如需精准仅脸部美颜，下一步可加 Apple Vision / Core ML 人脸区域预测、口鼻眼保护蒙版，再与 Shader 混合。此版本也尚未进行 OBS macOS 真机兼容性验证。

## 参考

- [OBS 官方插件模板](https://github.com/obsproject/obs-plugintemplate)
- [OBS 图形与 Effect Filter 文档](https://docs.obsproject.com/graphics)
- [OBS 官方插件安装说明](https://obsproject.com/kb/plugins-guide)

## 授权

本项目代码采用 GPL-2.0-or-later，见 LICENSE。上游 OBS 插件模板按其原始许可单独下载，不包含在本源码压缩包内。
