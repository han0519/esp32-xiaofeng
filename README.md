# 🎵 ESP32-S31 Korvo Bluetooth Audio Player

<div align="center">

**基于 ESP32-S31 的全功能蓝牙音乐播放器 | LVGL v9 图形界面 | A2DP Sink + AVRCP 封面**

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.2%2B-blue)](https://docs.espressif.com/projects/esp-idf/)
[![LVGL](https://img.shields.io/badge/LVGL-v9.2.2-green)](https://lvgl.io/)
[![License](https://img.shields.io/badge/License-Apache%202.0-orange)](LICENSE)

</div>

---

## 📖 项目简介

这是一个运行在 **ESP32-S31 Korvo1** 开发板上的全功能蓝牙音乐播放器。它通过蓝牙 A2DP 协议接收手机/电脑的音频流，在 800×480 RGB 触摸屏上显示精美的音乐播放界面，支持专辑封面显示、滚动歌词、FFT 频谱可视化、GIF 画廊和蓝牙通话等功能。

整套代码基于 Espressif 官方 **ESP-GMF (Generic Multimedia Framework)** 框架构建，音频编解码、蓝牙协议栈、UI 渲染等模块均通过 GMF 的 Pool/Pipeline 机制组装，架构清晰，易于扩展。

---

## ✨ 核心功能

### 🎧 蓝牙音频
- **A2DP Sink** — 接收来自手机/电脑的蓝牙立体声音频流
- **AVRCP 控制** — 支持播放/暂停、上一曲/下一曲、音量控制
- **AVRCP 元数据** — 自动获取歌曲标题、艺术家、专辑、流派信息
- **AVRCP 封面艺术** — 接收并实时显示专辑封面（JPEG 格式）
- **HFP 通话** — 支持蓝牙免提通话、来电显示、拨号
- **LE Audio (TMAP)** — 可选支持 BLE 音频（CIS/BIS 流）

### 🎨 LVGL 图形界面
- **音乐播放页** — 圆形裁剪专辑封面 + 旋转动画 + 歌曲信息 + 播放控制按钮
- **滚动歌词** — 最多 5 行实时歌词同步，当前行高亮显示
- **频谱可视化** — 16 段彩虹色 FFT 实时频谱柱状图（25fps 刷新）
- **GIF 画廊** — 14 张预置 GIF 表情包，支持左右滑动缩放浏览
- **拨号器** — 完整数字键盘 + 拨号/接听/挂断按钮
- **音量条** — 右侧浮动音量指示器，自动隐藏
- **启动画面** — 设备名称 + 蓝牙等待提示

### 🖱️ 交互体验
- **GT1151 电容触摸** — 灵敏的多点触控支持
- **手势滑动** — 左右滑动切换页面（画廊 ⇄ 音乐 ⇄ 拨号）
- **自适应布局** — 800×480 横屏布局，圆形封面 + 右侧信息面板
- **封面动画** — 封面淡入效果 + 持续旋转动画

---

## 🛠️ 硬件规格

| 组件 | 型号 / 规格 |
|------|-------------|
| **主控** | ESP32-S31 (RISC-V 双核) |
| **开发板** | ESP32-S31 Korvo1 |
| **LCD** | 800×480 RGB 接口 IPS 屏幕 |
| **触摸** | GT1151 I2C 电容触摸控制器 |
| **音频 DAC** | ES8311 / ES8388 编解码器 |
| **音频 ADC** | ES7210 / ES8388 编解码器 |
| **Flash** | 16MB（固件约 5.4MB） |
| **PSRAM** | 8MB（必须，LVGL 显示缓冲需要） |
| **接口** | I2S (音频), I2C (触摸), RGB (显示) |

---

## 📦 软件架构

```
┌─────────────────────────────────────────────────────┐
│                    Application Layer                  │
│  main.c ── bt_audio_event_cb (事件调度中心)          │
│     ├── bt_ui_t (LVGL 界面管理)                       │
│     ├── volume_ctrl_task (音量控制)                    │
│     ├── cli_init (命令行调试)                          │
│     └── stream_proc (音频流处理)                       │
├─────────────────────────────────────────────────────┤
│                  GMF Framework Layer                  │
│  esp_bt_audio (蓝牙音频托管)                          │
│     ├── A2DP Sink/Source Pipeline                     │
│     ├── HFP Pipeline                                  │
│     ├── LE Audio Pipeline                             │
│     └── GMF Pool (元素注册 & 管理)                    │
├─────────────────────────────────────────────────────┤
│                    Middleware Layer                    │
│  LVGL v9.2.2       │  esp_jpeg         │  FreeRTOS   │
│  esp_lvgl_port     │  esp_audio_codec  │  NimBLE/    │
│  esp_board_manager │  esp_codec_dev    │  BlueDroid  │
├─────────────────────────────────────────────────────┤
│                     Hardware Layer                     │
│  ESP32-S31 (RISC-V) │  LCD RGB  │  I2S  │  I2C       │
└─────────────────────────────────────────────────────┘
```

### 数据流（A2DP 音乐播放）

```
手机 ──[A2DP]──> ESP32-S31 ──[I2S]──> ES8311 DAC ──> 扬声器
                │
手机 ──[AVRCP]──┤── 元数据 (标题/艺术家/专辑/封面) ──> LVGL UI
                │
触摸屏 ──[I2C]──┤── 用户输入 ──> bt_audio_event_cb ──> AVRCP 命令
```

### 封面渲染管线

```
AVRCP COVER_ART ──> main.c (回调)
  └── bt_ui_post_cover() ──> cover_queue (FreeRTOS)
       └── bt_ui_cover_task() [独立线程，优先级 3]
            ├── lvgl_port_lock(0)     ← 获取 LVGL 锁
            ├── 隐藏旧封面              ← 瞬间完成
            ├── esp_jpeg 解码          ← JPEG → RGB565
            ├── 圆形裁剪 (软件 Alpha)   ← 创建圆形蒙版
            ├── 设置新封面 + 启动旋转动画
            └── lvgl_port_unlock()     ← 释放 LVGL 锁
```

---

## 🚀 快速开始

### 前置要求

- **ESP-IDF v6.2+** ([安装指南](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s31/get-started/))
- **RISC-V 工具链** (ESP-IDF 安装器会自动包含)
- **Python 3.8+**

### 1. 克隆仓库

```bash
git clone https://github.com/your-username/esp32-s31-korvo-bt.git
cd esp32-s31-korvo-bt
```

### 2. 激活 ESP-IDF 环境

```bash
# Linux / macOS
. $IDF_PATH/export.sh

# Windows PowerShell
. $env:IDF_PATH/export.ps1
```

### 3. 配置目标芯片

```bash
idf.py set-target esp32s31
```

### 4. 应用默认配置

```bash
# Windows
copy sdkconfig.defaults.esp32s31 sdkconfig

# Linux / macOS
cp sdkconfig.defaults.esp32s31 sdkconfig
```

### 5. 编译

```bash
idf.py build
```

首次编译约需 3-5 分钟（含 LVGL 全量编译），增量编译约 30 秒。

### 6. 烧录

```bash
# Windows
idf.py -p COM6 flash

# Linux / macOS
idf.py -p /dev/ttyUSB0 flash
```

### 7. 查看串口日志

```bash
idf.py -p COM6 monitor
```

### 8. 使用

1. **上电** → 看到启动画面，显示设备名称 "GMF_BT_UI_XXXX"
2. **配对** → 手机打开蓝牙，搜索并连接 "GMF_BT_UI_XXXX"
3. **播放** → 连接成功后自动显示音乐界面
4. **导航** → 左右滑动切换：画廊 | 音乐 | 拨号
5. **控制** → 点击播放/暂停、上一曲/下一曲按钮

---

## 📁 项目结构

```
esp32-s31-korvo-bt/
│
├── components/                      # 所有依赖组件（自包含）
│   ├── lvgl/                        # LVGL v9.2.2 图形库
│   ├── esp_lvgl_port/               # LVGL LCD 驱动端口 (RGB + 触摸)
│   ├── esp_jpeg/                    # JPEG 硬件/软件解码器
│   ├── esp_bt_audio/                # 蓝牙音频协议栈 (A2DP/HFP/LE)
│   ├── gmf_core/                    # ESP-GMF 核心框架
│   ├── gmf_io/                      # GMF IO 抽象层
│   ├── gmf_elements/                # GMF 音频处理元素 (编解码/混音/重采样)
│   ├── esp_audio_codec/             # 音频编解码驱动 (ES8311/ES8388/ES7210)
│   ├── esp_codec_dev/               # 编解码设备抽象层
│   ├── esp_muxer/                   # 音频复用/解复用
│   ├── esp_audio_effects/           # 音效处理
│   ├── esp_board_manager/           # 开发板管理器 (Korvo1 板级定义)
│   └── gen_bmgr_codes/              # 板级代码自动生成
│
├── main/                            # 应用代码
│   ├── main.c                       # 应用入口 + 蓝牙事件处理器
│   ├── CMakeLists.txt               # 主应用构建配置
│   ├── Kconfig.projbuild            # 应用 Kconfig 选项
│   ├── lv_conf.h                    # LVGL 本地配置
│   ├── cmd_reg.c / cmd_reg.h        # 命令行注册 (CLI 调试)
│   ├── pool_reg.c / pool_reg.h      # GMF Pool 元素注册
│   ├── stream_proc.c / .h           # 音频流处理管道
│   ├── codec_defs.h                 # 编解码器参数定义
│   │
│   └── bt_ui/                       # LVGL 用户界面模块
│       ├── bt_ui.c                  # 核心 UI 实现 (1700+ 行)
│       │   ├── 封面显示 & JPEG 解码
│       │   ├── 圆形裁剪 & 旋转动画
│       │   ├── 媒体播放页面
│       │   ├── 拨号器页面
│       │   ├── GIF 画廊页面
│       │   ├── 歌词滚动显示
│       │   ├── FFT 频谱可视化
│       │   └── 音量条指示器
│       ├── bt_ui.h                  # UI 公共 API
│       ├── gallery_images.h         # 预置 GIF 图片数据
│       ├── fonts/
│       │   ├── lv_font_notosanssc_regular_28.c  # 思源黑体 (中文)
│       │   └── simsunb.ttf                       # 原始字体文件
│       ├── images/                  # 图标资源 (流类型图标等)
│       └── __static/                # 静态资源 (原始 PNG)
│
├── partitions_esp32s31.csv          # ESP32-S31 分区表 (8MB factory)
├── sdkconfig.defaults.esp32s31      # 默认 Kconfig 配置
├── CMakeLists.txt                   # 顶层构建配置
├── convert_gif_frames.py            # GIF → LVGL 图片转换脚本
├── generate_font.py                 # TTF → LVGL 字体生成脚本
├── gen_cjk_font.py                  # CJK 中文字体生成脚本
├── .gitignore                       # Git 忽略规则
└── README.md                        # 本文件
```

---

## ⚙️ 关键配置说明

### 中文字体配置

```ini
# sdkconfig 必须包含以下设置
CONFIG_LV_FONT_FMT_TXT_LARGE=y      # 启用大 Unicode 范围（必须，否则中文无法渲染）
CONFIG_LV_USE_FONT_PLACEHOLDER=n     # 禁用字体占位符（避免显示方块）
CONFIG_LV_FONT_MONTSERRAT_14=y      # 小字体 (英文/数字)
CONFIG_LV_FONT_MONTSERRAT_28=y      # 中等字体 (播放控制图标)
CONFIG_LV_FONT_MONTSERRAT_32=y      # 大字体 (封面占位图标)
```

### 蓝牙配置

```ini
CONFIG_BT_ENABLED=y                           # 启用蓝牙
CONFIG_BT_BLUEDROID_ENABLED=y                 # 使用 BlueDroid 协议栈
CONFIG_BT_CLASSIC_ENABLED=y                   # 启用经典蓝牙
CONFIG_BT_A2DP_ENABLE=y                       # A2DP 音频分发
CONFIG_BT_AVRCP_ENABLED=y                     # AVRCP 远程控制
CONFIG_BT_AVRCP_CT_COVER_ART_ENABLED=y        # 封面艺术（关键！）
CONFIG_BT_HFP_ENABLE=y                        # HFP 免提通话
```

### 音频编解码

```ini
CONFIG_ESP_BOARD_DEV_AUDIO_CODEC_SUPPORT=y    # 启用编解码器支持
CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=y           # 使用外部 I2S 编解码器
CONFIG_BT_HFP_USE_EXTERNAL_CODEC=y            # HFP 外部编解码
```

### 内存/性能

```ini
CONFIG_SPIRAM=y                   # 启用 PSRAM（必须）
CONFIG_SPIRAM_SPEED_80M=y         # PSRAM 80MHz
CONFIG_FREERTOS_HZ=1000           # FreeRTOS 1kHz 滴答
CONFIG_ESPTOOLPY_FLASHSIZE_16MB   # 16MB Flash
```

---

## 🎨 自定义指南

### 替换 GIF 表情包

```bash
# 1. 将你的 GIF 文件放入 main/bt_ui/gifs/
# 2. 编辑 convert_gif_frames.py 中的文件列表
# 3. 运行转换
python convert_gif_frames.py

# 4. 编辑 gallery_images.h 调整图片引用
# 5. 重新编译
idfpy build
```

### 添加自定义字体

```bash
# 1. 准备 TTF/OTF 字体文件
# 2. 编辑 generate_font.py 中的字体路径和大小
# 3. 运行生成
python generate_font.py

# 4. 在 bt_ui.c 中声明并引用新字体
# 5. 重新编译
idf.py build
```

### 修改 UI 布局

所有布局常量定义在 `main/bt_ui/bt_ui.c` 的头部（第 62-87 行）：

```c
#define COVER_SIZE         400         // 封面大小
#define RIGHT_PANEL_X      420         // 右侧面板起始 X
#define LYRIC_AREA_H       280         // 歌词区域高度
#define SPECTRUM_BAR_COUNT 16          // 频谱柱子数量
#define SPECTRUM_UPDATE_MS 40          // 频谱刷新间隔 (25fps)
```

### 切换蓝牙协议栈

默认使用 **BlueDroid**（Classic BT）。如需使用 NimBLE：

```ini
# sdkconfig
CONFIG_BT_NIMBLE_ENABLED=y           # 启用 NimBLE
CONFIG_BT_BLUEDROID_ENABLED=n        # 禁用 BlueDroid
```

---

## 🔧 调试

### 串口命令

连接串口后可以输入以下命令：

| 命令 | 说明 |
|------|------|
| `play` | 开始播放 |
| `pause` | 暂停播放 |
| `next` | 下一曲 |
| `prev` | 上一曲 |
| `vol <0-100>` | 设置音量 |
| `conn <addr>` | 连接指定设备 |
| `disc` | 断开当前连接 |
| `scan on/off` | 开启/关闭扫描 |
| `pb <addr>` | 获取电话本 |

### 日志级别

```c
// 在 main.c 的 app_main() 中调整
esp_log_level_set("BT_AUD_AVRC_CT", ESP_LOG_DEBUG);  // AVRCP 封面调试
esp_log_level_set("BT_AUD_EXAMPLE", ESP_LOG_DEBUG);   // 应用层调试
```

### 常见问题

| 问题 | 原因 | 解决方案 |
|------|------|---------|
| 中文显示方块 | `LV_FONT_FMT_TXT_LARGE` 未启用 | 在 sdkconfig 中设置 `CONFIG_LV_FONT_FMT_TXT_LARGE=y` |
| 封面不显示 | AVRCP 封面未开启 | 检查 `CONFIG_BT_AVRCP_CT_COVER_ART_ENABLED=y` |
| 封面闪烁 | 异步清空队列竞态 | 已在 `bt_ui_media_set_cover_image()` 中同步隐藏解决 |
| 编译失败 (target) | 目标芯片未设置 | 运行 `idf.py set-target esp32s31` |
| 烧录失败 | COM 口被占用 | 重新插拔 USB，检查串口监视器是否关闭 |
| 音频卡顿 | PSRAM 带宽不足 | 减小 LVGL 绘制缓冲区行数 (`LVGL_DRAW_BUF_LINES`) |

---

## 🏗️ 编译输出

编译成功后，固件文件位于：

```
build/esp32_s31_korvo_bt.bin          # 应用固件 (~5.4MB)
build/bootloader/bootloader.bin       # 引导程序
build/partition_table/partition-table.bin  # 分区表
```

### 烧录地址

| 文件 | 偏移地址 |
|------|---------|
| bootloader.bin | `0x0000` |
| partition-table.bin | `0xA000` |
| esp32_s31_korvo_bt.bin | `0x10000` |

使用 esptool 直接烧录：

```bash
esptool.py --chip esp32s31 --port COM6 --baud 921600 \
  write_flash 0x0 build/bootloader/bootloader.bin \
              0xA000 build/partition_table/partition-table.bin \
              0x10000 build/esp32_s31_korvo_bt.bin
```

---

## 📸 界面预览

| 画廊页 | 音乐播放页 | 拨号器页 |
|:------:|:---------:|:--------:|
| GIF 表情包浏览 | 封面+歌词+频谱 | 蓝牙通话拨号 |
| 左右滑动导航 | 播放/暂停/切歌 | 数字键盘 |

---

## 🔑 技术亮点

### 1. 封面渲染优化
- 使用独立低优先级 FreeRTOS 任务进行 JPEG 解码，避免阻塞音频线程
- 同步隐藏旧封面 + 异步解码新封面，消除闪烁
- 软件圆形裁剪（Alpha 蒙版），配合旋转动画实现唱片效果

### 2. 频谱可视化
- 16 段柱状图，25fps 刷新
- 彩虹色渐变（低音红→中音绿→高音蓝紫）
- 使用 LVGL 定时器驱动，不阻塞 UI 线程

### 3. 内存管理
- PSRAM 分配显示缓冲区和封面图像缓冲区
- DRAM 保留给 FreeRTOS 任务栈和蓝牙协议栈
- 封面数据通过队列传递所有权，避免重复拷贝

### 4. 线程安全
- 所有 LVGL 操作必须在 `lvgl_port_lock(0)` / `lvgl_port_unlock()` 之间执行
- 蓝牙回调（高优先级）通过队列向 UI 任务（低优先级）投递消息
- 音量控制独立任务，避免死锁

---

## 📄 许可证

本项目采用 **Apache-2.0** 许可证。

包含的第三方组件许可证：

| 组件 | 许可证 |
|------|--------|
| LVGL | MIT |
| ESP-GMF | Apache-2.0 |
| Espressif 组件 | Apache-2.0 |
| Noto Sans SC 字体 | SIL Open Font License 1.1 |

---

## 🙏 致谢

- **[Espressif Systems](https://www.espressif.com/)** — ESP32-S31 芯片、ESP-IDF 框架
- **[LVGL](https://lvgl.io/)** — 优秀的嵌入式图形库
- **[Google Noto Fonts](https://fonts.google.com/noto)** — 开源中文字体

---

## 📮 联系方式

如有问题或建议，欢迎提交 Issue 或 Pull Request。

---

<p align="center">Made with ❤️ for Embedded Audio</p>
