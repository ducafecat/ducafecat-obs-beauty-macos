# 猫哥美颜 OBS Beauty Filter for macOS (Apple Silicon / Intel)

一个 **OBS Studio 视频特效滤镜插件**，结合 GPU Shader 与 Apple Vision，为 macOS 摄像头画面添加实时美颜。

> 当前为 **0.2.0 实验版**。新增 Apple Vision 单人脸部美颜与五官保护，仍需完成 OBS 真机画面与性能验收。构建脚本生成本地测试 `.plugin`；不包含瘦脸、大眼或像素级皮肤分割。

## 功能

| 项目 | 参数范围 | 实现 |
| --- | --- | --- |
| 磨皮 | 0–100 | GPU 边缘保留平滑；同值混合强度为增强前的 2 倍 |
| 磨皮半径 | 1–6 | 实际基础采样半径为数值 × 2，即 2–12 px |
| 美白 / 提亮 | 0–100 | 中间调温和提亮 |
| 红润 | 0–100 | 轻微暖红色调整 |
| 锐化 | 0–100 | 非锐化遮罩恢复细节 |
| 尽量只处理肤色 | 开 / 关 | 近似肤色遮罩，不是人脸分割 |
| 仅处理主要人脸 | 开 / 关 | Apple Vision 五官点形成近似脸部蒙版 |
| 五官保护 | 开 / 关 | 眼、眉、鼻、口降低磨皮，嘴唇减弱调色 |
| 调试蒙版 | 开 / 关 | 绿：脸部，蓝：五官，红：嘴唇；默认关闭 |

**特点**：不下载额外模型、无网络推理，使用 macOS 自带 Vision。仅人脸模式会将最长边不超过 640 px 的图像从 GPU 读回 CPU，每秒最多检测 10 次；Vision 在后台串行执行，忙时丢帧，不阻塞渲染等待检测，但 GPU 回读和蒙版生成仍有开销。关闭“仅处理主要人脸”恢复原有 Shader 模式，不进行检测帧回读。

适合普通 SDR 摄像头。HDR/扩展色域输入会安全跳过滤镜。透明视频源并非主要测试对象。

### 磨皮增强说明

磨皮滑块范围与默认值保持不变：默认强度 40、半径 3，现在分别对应增强前强度 80、基础采样半径 6 px。已有场景在相同数值下也会增强，若想接近增强前效果，可先将强度和半径数值减半，再按画面微调；半径仍以整数调节，最小值为 1。

磨皮混合权重仍限制在 0–1，高强度达到上限后，视觉效果不会继续线性翻倍；强度 0 仍关闭磨皮。人脸蒙版、五官保护及其他美颜参数保持原有逻辑。半径控制采样间距，原有滤波核还包含两倍基础间距的外围采样。

### 人脸模式行为

- 新增滤镜默认开启“仅处理主要人脸”和“五官保护”；带旧版参数且没有新设置的场景按旧模式加载。仅使用默认值且未保存任何旧参数的场景无法与新建滤镜区分，会启用人脸模式。
- 首次选最大脸，之后优先跟随与上一位置重叠的脸；主要人脸短暂丢失时不会立即跳到旁人。
- 无脸时保持原图。检测结果在采集时间超过 200 ms 后开始淡出，500 ms 时停止美颜；分辨率变化立即清空蒙版并丢弃旧尺寸结果。
- “肤色限制”仍可叠加使用。复杂光线下可关闭它，检查脸部蒙版是否正确。
- 五官点不是皮肤分割：额头为保守补足，侧脸、遮挡、胡须、发际线以及运动时的检测延迟可能影响边界。
- 检测图与美颜来自同一滤镜输入；前置的裁剪、缩放、翻转滤镜会一起进入检测。OBS 场景项上的显示变换由 OBS 在后续统一应用。

## 兼容性与验证状态

| 项目 | 当前状态 |
| --- | --- |
| 版本 | 0.2.0 实验版 |
| 系统目标 | macOS 12+；实际兼容性还取决于所用 OBS 版本与开发依赖 |
| 二进制架构 | Universal：arm64 + x86_64，已编译并检查 |
| 本地签名 | ad-hoc 签名及完整性检查通过，未做 Developer ID 签名与公证 |
| Apple Silicon 集成验证 | M1 Pro + OBS 32.2.2，OpenGL / Metal 独立进程测试通过 |
| Intel 实机 | 待验证；双架构编译通过不代表已完成 Intel 运行验证 |
| 真实摄像头效果 / 30 分钟性能 | 待验收 |

集成验证包含模块加载、Shader 编译、固定蒙版混合、无脸透传、检测图回读、尺寸切换和新旧设置迁移，不等于已完成 OBS 界面下的直播实测。Metal 图形后端通过测试，也不代表本插件提供 Metal / Core ML 皮肤分割。

### 使用已构建的插件

需要兼容的 macOS 与 OBS Studio。安装已有 `.plugin` 无需 Xcode、CMake 或 Python；源码压缩包需要先构建。

当前本地构建产物位于 `dist/`：

- `obs-beauty-macos.plugin`：完整插件 bundle。
- `obs-beauty-macos-macos-universal.zip`：包含该 bundle 的二进制压缩包。

`dist/` 不纳入 Git；克隆源码后不会自动获得这些产物，需自行构建或使用另行提供的二进制包。

### 从源码构建

- macOS 12+，Apple Silicon 或 Intel Mac。
- **OBS Studio 与构建时使用的 OBS 开发依赖兼容**；OBS 更新后可能需要重编。
- Xcode 16+（包含 macOS SDK）及 Command Line Tools；仅安装 Command Line Tools 不能替代完整 Xcode。
- CMake **3.30+**，可通过 Homebrew 安装：`brew install cmake`。
- Git、Python 3、可下载 GitHub OBS 源码和编译依赖的网络，以及足够磁盘空间。

先检查构建工具：

```bash
xcodebuild -version
xcode-select -p
cmake --version
python3 --version
```

## 安装流程速览

以下为源码构建安装流程，命令均在项目根目录运行。已有二进制包可直接跳到“安装”。

1. 安装 CMake，并确认版本不低于 3.30：

   ```bash
   brew install cmake
   cmake --version
   ```

2. 进入解压或克隆得到的项目目录，构建插件（目录名以本机实际名称为准）：

   ```bash
   cd ducafecat-obs-beauty-macos
   bash scripts/build-macos.sh
   ```

   保留 `.work/`，后续构建可复用已下载、已编译的 OBS 依赖。

3. 安装插件：

   ```bash
   bash scripts/install-macos.sh
   ```

4. 完全退出并重启 OBS → 选中摄像头 → 滤镜 → 效果滤镜 → 添加 **猫哥美颜 · Beauty Filter**。

> 当前脚本已显式设置 `PLUGIN_BUILD_NUMBER=1`，并编译 Objective-C++ Vision 桥接模块。

## 构建（macOS）

```bash
# 解压下载的 zip，进入项目目录
cd ducafecat-obs-beauty-macos
bash scripts/build-macos.sh
```

脚本会：

1. 下载 [OBS 官方 obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) 到 `.work/`。
2. 注入 C / Objective-C++ 源码、Vision 框架构建配置、Shader 和本地化资源，并保留官方模板的 OBS / Qt 依赖版本与哈希。
3. 根据模板的 `macos` 预设构建 Universal（arm64 + x86_64）插件。
4. 把构建产物放到 `dist/obs-beauty-macos.plugin`，并生成 `dist/obs-beauty-macos-macos-universal.zip`。
5. 使用本机 **ad-hoc codesign** 为本地测试签名；公开分发仍需要独立的 Developer ID 签名与 Apple 公证。

> 第一次构建需要联网。官方模板依赖规格可能随时间升级，如出现 SDK / 构建错误，请优先检查 `.work/obs-plugintemplate/buildspec.json` 和 OBS 版本是否匹配。

### 保留 OBS 构建缓存

`.work/obs-plugintemplate/` 保存模板源码、下载的依赖和构建目录。修复 CMake 安装、PATH 或插件配置参数后，直接重新运行构建脚本即可；脚本会复用已有模板检出和可用的构建缓存，通常无需重新编译整个 OBS。

不要为了重试而删除 `.work/`。如果缓存来自另一个源码路径，脚本保留原缓存并改用 `build_macos-local/`。也可通过绝对路径环境变量 `BEAUTY_BUILD_DIR` 指定独立插件构建目录；下载的依赖仍会复用。更换 OBS 依赖版本、Xcode 或构建架构时，部分依赖可能需要重新构建；仅在确认缓存损坏或配置不兼容后再清理相关目录。`dist/` 是插件输出目录，不能替代 `.work/` 中的依赖缓存。

## 安装

### 从项目构建产物安装

完全退出 OBS，在项目根目录运行：

```bash
bash scripts/install-macos.sh
```

安装脚本将 `dist/obs-beauty-macos.plugin` 复制到当前用户的 OBS 插件目录。修改源码后需重新构建、安装，并重启 OBS。

### 从二进制压缩包安装

解压 `obs-beauty-macos-macos-universal.zip`，将其中完整的 `obs-beauty-macos.plugin` 复制到下方目录；如果目录不存在，先创建 `plugins` 文件夹。完全退出 OBS 后再替换旧 bundle，随后重新打开 OBS。

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

建议先打开正常的面部补光，以 720p / 30 fps 起步，保持“仅处理主要人脸”和“五官保护”开启，再轻量调整上表参数。

从 0.1.0 升级后，已有滤镜通常保留旧模式；需要在其设置中手动开启“仅处理主要人脸”。无人脸或检测结果过期时没有美颜效果是预期行为。

磨皮过大仍可能让细节发软。肤色限制在旧模式下可能误伤木色或暖色衣服，也可能在彩色灯光下漏掉皮肤；人脸模式下它只进一步约束预测脸部区域内的处理。

## 项目结构

```text
obs-beauty-macos/
├── README.md
├── LICENSE
├── src/
│   ├── plugin-main.c          # OBS 接口 / 检测回读 / 蒙版上传
│   ├── face-tracker.h         # C 桥接与检测结果
│   ├── face-tracker.mm        # 异步 Vision 检测与跟踪
│   └── face-mask.c            # 坐标、时效、主脸选择与蒙版
├── data/
│   ├── beauty.effect          # GPU Shader
│   └── locale/
│       ├── en-US.ini
│       └── zh-CN.ini
├── scripts/
│   ├── build-macos.sh         # 构建 .plugin + 二进制 zip
│   ├── install-macos.sh       # 安装插件
│   ├── uninstall-macos.sh     # 卸载插件
│   └── face-tracking.cmake    # Objective-C++ 与 Apple 框架链接
└── tests/
    ├── source_smoke.py       # 静态文件 / 参数校验
    ├── face_behavior.c       # 生产几何与蒙版逻辑的行为测试
    ├── run_face_tests.py     # 编译运行行为与可选原生集成测试
    ├── vision_lifecycle.mm   # Vision 空白帧 / 检测中销毁
    └── obs_runtime_smoke.mm  # 独立 OBS GPU 渲染与兼容测试
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

0.1.0 的构建脚本曾遗漏 `PLUGIN_BUILD_NUMBER`，导致 Xcode 属性参数不完整。当前脚本已显式传入 `-DPLUGIN_BUILD_NUMBER=1`；先确认使用最新脚本，再保留 `.work/` 重跑构建。

若最新脚本仍报错，请检查具体报错文件和行号，排查其他空变量；不能仅凭这段错误文本认定原因相同。

### 移动项目后提示 CMake 缓存路径不匹配

当前脚本会保留原插件缓存，并切换到 `build_macos-local/`。如果该目录也来自旧路径，可以指定新的绝对构建路径：

```bash
BEAUTY_BUILD_DIR="$PWD/.work/obs-plugintemplate/build_macos-fresh" bash scripts/build-macos.sh
```

无需为插件配置目录的路径问题删除整个 `.work/`；依赖是否需要重新构建，由其缓存兼容情况决定。

### 日志中的 `Disabled Features: Plugin Support`

如果这条信息出现在构建 OBS 依赖时，通常是正常配置：官方模板以 `ENABLE_PLUGINS=OFF`、`ENABLE_FRONTEND=OFF` 构建插件所需的 OBS 开发依赖，不构建完整 OBS 前端和内置插件。它不表示你安装的 OBS 应用禁用了第三方插件，也不能单凭这条日志判断美颜插件构建失败。应以最终是否生成 `.plugin` 和 OBS 加载日志为准。

### OBS 的“效果滤镜”列表没有插件

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

### 运行慢 / CPU 或 GPU 占用增加

- 磨皮半径先从 1–2 开始（实际基础半径 2–4 px），摄像头使用 720p / 30 fps。
- 将磨皮和锐化滑块降为 0 可以跳过对应的 GPU 采样分支，但人脸模式仍会执行检测回读。
- 临时关闭“仅处理主要人脸”，对照 CPU / GPU 占用和 OBS 渲染丢帧，判断检测路径的额外成本。

### 效果不明显 / 人脸模式没有变化

- 确认“仅处理主要人脸”的状态；升级后的已有滤镜可能仍使用旧模式。
- 人脸模式下先打开“调试蒙版”，检查脸部与五官是否覆盖正确；无脸、严重遮挡或过期结果会保持原图。
- 尽量正脸入镜并改善补光。复杂彩光下可临时关闭“肤色限制”，逐个调整磨皮 40 / 半径 3 等参数。
- 检查后关闭调试蒙版，恢复正常画面。

### 编译时报 GitHub 下载失败

- 属于获取 OBS 构建依赖的网络问题，需保证 Mac 可以访问 GitHub 和依赖下载地址。

## 当前局限 / 下一阶段

本版本已接入 **Apple Vision 人脸关键点、近似脸部蒙版与五官保护**。仍不含瘦脸、大眼、眼袋去除、LUT、Metal / Core ML 皮肤分割；不承诺像素级皮肤边界。下一阶段根据真机回读成本与效果决定是否增加模型分割或跟踪优化。

## 测试与验收

### 自动化验证

```bash
python3 tests/source_smoke.py
python3 tests/run_face_tests.py
# macOS 原生 Vision 生命周期测试
python3 tests/run_face_tests.py --native
# 完成构建后，使用本机 /Applications/OBS.app 的图形后端运行独立测试进程
# 不打开摄像头、不加载或修改用户场景；需要可用的图形会话
python3 tests/run_face_tests.py --obs-bundle dist/obs-beauty-macos.plugin
# OBS 32 的 Metal 后端（若当前安装版本支持）
BEAUTY_TEST_GRAPHICS=metal python3 tests/run_face_tests.py --obs-bundle dist/obs-beauty-macos.plugin
```

行为测试使用生产代码验证 Vision 坐标转换、上下翻转、非方形蒙版、羽化、五官与嘴唇位置、主脸选择、结果过期以及尺寸代次隔离。可选 OBS 集成测试验证模块加载、GPU Shader 编译、固定蒙版的脸部限制 / 五官磨皮保护 / 嘴唇调色保护 / 过期透传、无脸透传、检测图回读、输入尺寸切换及设置迁移。这些测试不替代真实人脸检测与摄像头效果验收。

本次开发验证：M1 Pro 上完成 Universal（arm64 + x86_64）编译，使用 OBS 32.2.2 的 OpenGL 和 Metal 后端通过独立进程集成测试。Intel 实机、真实摄像头视觉效果和 30 分钟性能仍待验收。

### OBS 真机验收（待完成）

- Apple Silicon 和 Intel 分别确认加载、Shader 编译，以及旧场景保持旧模式、新增滤镜默认人脸模式。
- 720p / 30 fps，测试正脸、侧脸、眼镜、说话、转头、遮挡、出入画和暖色背景；打开蒙版检查五官与翻转方向对齐，脸外不被美颜。
- 验证前置裁剪 / 缩放 / 翻转、无人脸、多脸进入、切换场景、切换分辨率、反复开关与删除滤镜，以及 HDR 跳过行为。
- 与旧模式对照记录 CPU / GPU、平均与峰值渲染时间及 OBS 统计窗口的渲染丢帧；连续运行 30 分钟检查内存增长和新增丢帧。目标是 720p / 30 fps 无明显新增渲染丢帧，尚未完成该性能验收。
- 1080p 和 60 fps 为扩展测试，不提前承诺性能。

## 参考

- [OBS 官方插件模板](https://github.com/obsproject/obs-plugintemplate)
- [OBS 图形与 Effect Filter 文档](https://docs.obsproject.com/graphics)
- [OBS 官方插件安装说明](https://obsproject.com/kb/plugins-guide)

## 授权

本项目代码采用 GPL-2.0-or-later，见 LICENSE。上游 OBS 插件模板按其原始许可单独下载，不包含在本源码压缩包内。
