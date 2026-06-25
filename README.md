# 🎵 ESP32-S31 Korvo Bluetooth Audio Player

基于 **ESP32-S31 Korvo1** 开发板的全功能蓝牙音乐播放器，LVGL v9 图形界面，支持专辑封面、滚动歌词、频谱可视化和 GIF 画廊。

## ✨ 功能

- 🎧 **蓝牙 A2DP Sink** — 接收手机/电脑蓝牙音频流
- 🎨 **专辑封面显示** — JPEG 解码 + 圆形裁剪 + 旋转动画
- 📝 **滚动歌词** — 实时多行歌词同步
- 📊 **频谱可视化** — 16段彩虹色 FFT 频谱柱状图
- 🖼️ **GIF 画廊** — 14 张动态 GIF 表情包，支持缩放浏览
- 📞 **拨号器界面** — HFP 蓝牙通话
- 🎚️ **音量控制** — 浮动音量条，自动隐藏
- 🖱️ **触摸交互** — GT1151 I2C 电容触摸

## 🛠️ 硬件

| 组件 | 型号 |
|------|------|
| 开发板 | ESP32-S31 Korvo1 |
| LCD | 800×480 RGB 接口触摸屏 |
| 触摸 | GT1151 I2C 电容触摸控制器 |
| 音频 | ES8388 音频编解码器 |
| Flash | ≥ 8MB（约 5MB 固件） |

## 📦 软件依赖

| 组件 | 版本 |
|------|------|
| ESP-IDF | v6.2+ |
| LVGL | v9.2.2（已内置） |
| ESP-GMF | 多媒体框架（已内置） |
| ESP-Board-Manager | 开发板管理（已内置） |

> 所有组件均已内置在 `components/` 目录中，无需额外下载。

## 🚀 快速开始

### 1. 设置 ESP-IDF

```bash
# Linux / macOS
. $IDF_PATH/export.sh

# Windows PowerShell
. $env:IDF_PATH/export.ps1
```

### 2. 配置 & 编译

```bash
cd esp32-s31-korvo-bt
idf.py set-target esp32s31
cp sdkconfig.defaults.esp32s31 sdkconfig
idf.py build
```

### 3. 烧录

```bash
idf.py -p COMx flash        # Windows
idf.py -p /dev/ttyUSB0 flash # Linux / macOS
```

### 4. 使用

1. 上电后等待蓝牙就绪
2. 手机搜索蓝牙设备连接
3. 连接后自动播放
4. 左右滑动切换页面：**画廊** | **音乐** | **拨号**

## 📁 项目结构

```
esp32-s31-korvo-bt/
├── components/              # 所有依赖组件（自包含）
│   ├── lvgl/                # LVGL v9.2.2
│   ├── esp_lvgl_port/       # LVGL LCD 驱动
│   ├── esp_jpeg/            # JPEG 解码器
│   ├── esp_bt_audio/        # 蓝牙音频栈
│   ├── gmf_*/               # ESP-GMF 多媒体框架
│   ├── esp_audio_codec/     # 音频编解码驱动
│   ├── esp_codec_dev/       # 编解码设备抽象
│   ├── esp_board_manager/   # 开发板管理
│   └── gen_bmgr_codes/      # 板级代码生成
├── main/
│   ├── bt_ui/               # LVGL UI 界面（核心）
│   │   ├── bt_ui.c          # 音乐/画廊/拨号器
│   │   ├── bt_ui.h
│   │   ├── fonts/           # 中文字体
│   │   └── images/          # 图标资源
│   └── main.c               # 应用入口
├── partitions_esp32s31.csv  # 分区表
├── sdkconfig.defaults.esp32s31  # 默认 Kconfig
├── CMakeLists.txt
└── README.md
```

## ⚙️ 关键配置

> 需在 `sdkconfig` 中确保以下配置：

```ini
# 中文字体必须
CONFIG_LV_FONT_FMT_TXT_LARGE=y
CONFIG_LV_USE_FONT_PLACEHOLDER=n

# 使用的字体大小
CONFIG_LV_FONT_MONTSERRAT_14=y
CONFIG_LV_FONT_MONTSERRAT_28=y
CONFIG_LV_FONT_MONTSERRAT_32=y
```

## 🎨 自定义

### 添加 GIF 表情包

```bash
# 1. 将 GIF 放入 main/bt_ui/gifs/
# 2. 运行转换脚本
python convert_gif_frames.py
# 3. 重新编译
idf.py build
```

### 添加自定义字体

```bash
# 1. 将 TTF/OTF 字体放入 main/
# 2. 生成 LVGL 字体
python generate_font.py
# 3. 重新编译
```

## 📸 效果

| 画廊 | 音乐播放 | 拨号器 |
|------|---------|--------|
| GIF 表情浏览 | 封面+歌词+频谱 | 蓝牙通话 |

## 📄 许可证

Apache-2.0

- LVGL: MIT License
- ESP-GMF: Apache-2.0
- Espressif 组件: Apache-2.0

## 🙏 致谢

- [Espressif Systems](https://www.espressif.com/) — ESP32-S31 芯片与 IDF
- [LVGL](https://lvgl.io/) — 嵌入式图形库
