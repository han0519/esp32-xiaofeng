"""Generate a 4bpp LVGL v9 font with NotoSansSC TrueType (non-variable)"""
import subprocess
import sys
import os

# All characters we need
lyrics_chinese = (
    "一上下不世为丽了人今以们伤佛你候再几制前动千变可后否在多夜大天女"
    "子寂寞希年幸开待心忘念情想意感愿我拥换控改放无时是最有望求法爱甘"
    "生用界的知禁福离等缘美能自苦要见让走身辈边还这遍道里难面"
)
extra_chinese = (
    "歌曲名专专辑艺术演唱作词作曲音乐播放暂停停止下首上一"
    "未连接蓝牙正在搜索中已配对设备列表音量周杰伦林俊杰"
    "陈奕迅张学友刘德华邓丽君王菲五月天Beyond"
    "说好不哭晴天告白气球稻香七里香简单爱给我一首歌的时间"
)
all_chinese = ''.join(sorted(set(lyrics_chinese + extra_chinese)))

# Use regular (non-variable) font
font_path = r"C:\Windows\Fonts\Noto Sans SC (TrueType).otf"
if not os.path.exists(font_path):
    # Fallback: simhei (黑体) is a regular TTF
    font_path = r"C:\Windows\Fonts\simhei.ttf"
if not os.path.exists(font_path):
    font_path = r"C:\Windows\Fonts\STKAITI.TTF"

print(f"Using font: {font_path}")
print(f"Chinese glyphs: {len(all_chinese)}")

output_file = os.path.join(
    os.path.dirname(__file__),
    "main", "bt_ui", "fonts", "lv_font_notosanssc_regular_28.c"
)

cmd = [
    r"C:\Users\han\AppData\Roaming\npm\lv_font_conv.cmd",
    "--no-compress",
    "--no-prefilter",
    "--bpp", "4",
    "--size", "28",
    "--font", font_path,
    "-r", "0x20-0x7E",
    "--symbols", all_chinese,
    "--format", "lvgl",
    "--lv-include", "lvgl.h",
    "--lv-font-name", "lv_font_notosanssc_regular_28",
    "-o", output_file,
    "--force-fast-kern-format",
]

print(f"Running lv_font_conv...")
result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)

if result.returncode != 0:
    print(f"STDERR: {result.stderr}")
    print(f"STDOUT: {result.stdout}")
    sys.exit(1)

size = os.path.getsize(output_file)
print(f"Font generated: {output_file}")
print(f"File size: {size:,} bytes ({size/1024:.1f} KB)")
print("DONE")
