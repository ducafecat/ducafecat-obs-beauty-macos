# 猫哥美颜 OBS Beauty Filter for macOS (Apple Silicon / Intel)

一个 **OBS Studio 视频特效滤镜插件（源码项目）**，用 GPU Shader 为摄像头添加实时美颜。

> 当前为 **0.1.0 实验版**。源码和自动构建脚本已提供，但没有在本环境中的 macOS/OBS 上编译、加载或实测。它不是现成可安装的预编译 `.plugin`；需要在 Mac 上构建。也不是 AI 人脸跟踪 / 瘦脸 / 大眼插件。

## 功能

| 项目           | 参数范围 | 实现                             |
| -------------- | -------- | -------------------------------- |
| 磨皮           | 0–100    | GPU 边缘保留平滑（近似双边滤波） |
| 磨皮半径       | 1–6 px   | 控制采样范围                     |
| 美白 / 提亮    | 0–100    | 中间调温和提亮                   |
| 红润           | 0–100    | 轻微暖红色调整                   |
| 锐化           | 0–100    | 非锐化遮罩恢复细节               |
| 尽量只处理肤色 | 开 / 关  | 近似肤色遮罩，不是人脸分割       |

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

## 安装流程速览

适用于 M1 / M2 / M3 / M4 等 Apple Silicon Mac 和 Intel Mac，以下命令均在项目根目录运行。

1. 安装 CMake，并确认版本不低于 3.30：

   ```bash
   brew install cmake
   cmake --version
   ```

2. 进入解压或克隆得到的项目目录，构建插件（目录名以本机实际名称为准）：

   ```bash
   cd obs-beauty-macos
   bash scripts/build-macos.sh
   ```

   保留 `.work/`，以便修复后复用已下载、已编译的 OBS 依赖。如果遇到构建编号错误，请按下文的 `PLUGIN_BUILD_NUMBER` 修复步骤操作。

3. 安装插件：

   ```bash
   bash scripts/install-macos.sh
   ```

4. 完全退出并重启 OBS → 选中摄像头 → 滤镜 → 效果滤镜 → 添加 **猫哥美颜 · Beauty Filter**。

> 版本说明：当前仓库脚本中的版本仍为 0.1.0，配置命令尚未显式传入构建编号。如果使用另行发布的 V0.1.1 修复版，请确认其配置命令已包含 `-DPLUGIN_BUILD_NUMBER=1`；当前源码可按下文手动修复。

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

### 保留 OBS 构建缓存

`.work/obs-plugintemplate/` 保存模板源码、下载的依赖和构建目录。修复 CMake 安装、PATH 或插件配置参数后，直接重新运行构建脚本即可；脚本会复用已有模板检出和可用的构建缓存，通常无需重新编译整个 OBS。

不要为了重试而删除 `.work/`。更换 OBS 依赖版本、Xcode 或构建架构时，部分依赖可能需要重新构建；仅在确认缓存损坏或配置不兼容后再清理相关目录。`dist/` 是插件输出目录，不能替代 `.work/` 中的依赖缓存。

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

| 场景                  | 磨皮 | 半径 | 美白 | 红润 | 锐化 | 肤色限制 |
| --------------------- | ---: | ---: | ---: | ---: | ---: | -------- |
| 技术录屏 + 摄像头小窗 |   35 |    2 |   12 |    6 |   12 | 开       |
| 主播正脸特写          |   45 |    3 |   15 |   10 |   10 | 开       |
| 自然清爽              |   22 |    2 |    8 |    4 |    8 | 开       |

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

### `Missing dependency: cmake`

构建脚本无法从 PATH 找到 CMake。先安装并检查：

```bash
brew install cmake
command -v cmake
cmake --version
```

版本必须不低于 3.30；已安装但版本过低时运行 `brew upgrade cmake`。如果终端仍找不到命令，为当前终端加载 Homebrew 环境：

```bash
# Apple Silicon Mac（Homebrew 默认安装位置）
eval "$(/opt/homebrew/bin/brew shellenv)"

# Intel Mac（Homebrew 默认安装位置；与上一条按实际安装位置选用）
eval "$(/usr/local/bin/brew shellenv)"
```

只运行符合本机 Homebrew 安装位置的一条命令。需要永久生效时，将对应命令加入 `~/.zprofile`，重新打开终端（包括 IDE 内终端），再次检查 `cmake --version`，然后重跑构建脚本。

### `set_target_properties called with incorrect number of arguments`

本次遇到的原因是插件配置缺少 `PLUGIN_BUILD_NUMBER`，导致 Xcode 属性参数不完整。如果错误位置指向模板的 macOS 属性设置，可在 `scripts/build-macos.sh` 中将配置命令改为：

```bash
cmake --preset macos -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO -DPLUGIN_BUILD_NUMBER=1
```

保存后在项目根目录重试：

```bash
bash scripts/build-macos.sh
bash scripts/install-macos.sh
```

保留 `.work/`，无需为这个参数错误重新下载或全量构建 OBS。也可以先对已有模板目录手动配置，再运行项目构建脚本（构建编号会写入 CMake 缓存）：

```bash
cd .work/obs-plugintemplate
cmake --preset macos -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO -DPLUGIN_BUILD_NUMBER=1
cd ../..
bash scripts/build-macos.sh
```

如果添加参数后仍报同样错误，应查看报错文件和行号，排查其他空变量，不能仅凭错误文本认定原因相同。

### 日志中的 `Disabled Features: Plugin Support`

如果这条信息出现在构建 OBS 依赖时，通常是正常配置：官方模板以 `ENABLE_PLUGINS=OFF`、`ENABLE_FRONTEND=OFF` 构建插件所需的 OBS 开发依赖，不构建完整 OBS 前端和内置插件。它不表示你安装的 OBS 应用禁用了第三方插件，也不能单凭这条日志判断美颜插件构建失败。应以最终是否生成 `.plugin` 和 OBS 加载日志为准。


**OBS 的“效果滤镜”列表没有插件**

- 确认已在本机成功运行 `build-macos.sh` 和 `install-macos.sh`。
- 通过 Finder → 前往文件夹查看 `.plugin` 的完整路径。
- 必须彻底退出 OBS 后重启。
- OBS → 帮助 → 日志文件 → 查看当前日志，搜索 `obs-beauty-macos` 或 `beauty.effect`。
- 如果 OBS 提示二进制不兼容，重新构建与已安装 OBS 版本匹配的 SDK；插件二进制须包含 OBS 当前运行所用的架构（arm64 或 x86_64，包括 Rosetta 运行情况）。
- 若日志提示签名或资源校验失败，按下一节检查本地签名。

### 本地签名检查

构建脚本会对 `dist/obs-beauty-macos.plugin` 做 ad-hoc 本地签名，安装脚本随后复制整个 bundle。可以检查已安装插件：

```bash
codesign --verify --deep --strict --verbose=2 "$HOME/Library/Application Support/obs-studio/plugins/obs-beauty-macos.plugin"
```

签名后修改 bundle 内的二进制或资源会使签名失效。修改源码后应重新构建并安装；如果仅需重新签署本地构建产物，可运行后再安装：

```bash
codesign --force --deep --sign - dist/obs-beauty-macos.plugin
bash scripts/install-macos.sh
```

ad-hoc 签名用于本机测试，不等于 Developer ID 签名或 Apple 公证，也不保证从其他机器下载的插件能通过 Gatekeeper。出现安全拦截时先核实来源并检查系统提示及 OBS 日志。

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
