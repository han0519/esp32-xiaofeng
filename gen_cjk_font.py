#!/usr/bin/env python3
"""Generate LVGL v9 1bpp CJK font (20992 chars + ASCII + extras)."""
import freetype, os, sys
sys.stdout.reconfigure(encoding='utf-8')

FONT_PATH = r"C:\Windows\Fonts\msyh.ttc"
FONT_INDEX = 0  # TTC face index
OUTPUT_C  = r"c:\Users\han\Desktop\audio\esp32-s31-korvo-bt\main\bt_ui\fonts\lv_font_notosanssc_regular_28.c"
FONT_SIZE = 28
FONT_NAME = "lv_font_notosanssc_regular_28"
BPP = 1

ASCII_START, ASCII_END = 0x20, 0x7F
ASCII_COUNT = ASCII_END - ASCII_START

CJK_START, CJK_END = 0x4E00, 0xA000
CJK_COUNT = CJK_END - CJK_START

EXTRA = (
    [0x3000,0x3001,0x3002,0x3003,0x3005,0x3006,0x3007,0x3008,0x3009,0x300A,0x300B,0x300C,0x300D,0x300E,0x300F,
     0x3010,0x3011,0x3012,0x3013,0x3014,0x3015,0x3016,0x3017,0x301D,0x301E,0x301F,
     0x3021,0x3022,0x3023,0x3024,0x3025,0x3026,0x3027,0x3028,0x3029,
     0x3030,0x3031,0x3032,0x3033,0x3034,0x3035,0x3036,0x3037,0x3038,0x3039,0x303A,0x303B,0x303C,0x303D,0x303E,
     0xFF01,0xFF02,0xFF03,0xFF04,0xFF05,0xFF06,0xFF07,0xFF08,0xFF09,0xFF0A,0xFF0B,0xFF0C,0xFF0D,0xFF0E,0xFF0F,
     0xFF10,0xFF11,0xFF12,0xFF13,0xFF14,0xFF15,0xFF16,0xFF17,0xFF18,0xFF19,0xFF1A,0xFF1B,0xFF1C,0xFF1D,0xFF1E,0xFF1F,
     0xFF20,0xFF21,0xFF22,0xFF23,0xFF24,0xFF25,0xFF26,0xFF27,0xFF28,0xFF29,0xFF2A,0xFF2B,0xFF2C,0xFF2D,0xFF2E,0xFF2F,
     0xFF30,0xFF31,0xFF32,0xFF33,0xFF34,0xFF35,0xFF36,0xFF37,0xFF38,0xFF39,0xFF3A,0xFF3B,0xFF3C,0xFF3D,0xFF3E,0xFF3F,
     0xFF40,0xFF41,0xFF42,0xFF43,0xFF44,0xFF45,0xFF46,0xFF47,0xFF48,0xFF49,0xFF4A,0xFF4B,0xFF4C,0xFF4D,0xFF4E,0xFF4F,
     0xFF50,0xFF51,0xFF52,0xFF53,0xFF54,0xFF55,0xFF56,0xFF57,0xFF58,0xFF59,0xFF5A,0xFF5B,0xFF5C,0xFF5D,0xFF5E,
     0xFFE0,0xFFE1,0xFFE5,
     0x00C0,0x00C1,0x00C2,0x00C3,0x00C4,0x00C5,0x00C6,0x00C7,0x00C8,0x00C9,0x00CA,0x00CB,0x00CC,0x00CD,0x00CE,0x00CF,
     0x00D0,0x00D1,0x00D2,0x00D3,0x00D4,0x00D5,0x00D6,0x00D8,0x00D9,0x00DA,0x00DB,0x00DC,0x00DD,0x00DE,
     0x00E0,0x00E1,0x00E2,0x00E3,0x00E4,0x00E5,0x00E6,0x00E7,0x00E8,0x00E9,0x00EA,0x00EB,0x00EC,0x00ED,0x00EE,0x00EF,
     0x00F0,0x00F1,0x00F2,0x00F3,0x00F4,0x00F5,0x00F6,0x00F8,0x00F9,0x00FA,0x00FB,0x00FC,0x00FD,0x00FE,0x00FF,
     0x2013,0x2014,0x2018,0x2019,0x201C,0x201D,0x2022,0x2026,
     0x00B7,0x221E,0x25CF,0x25B2,0x25BC,0x2605,0x266A,0x266B]
)

def main():
    face = freetype.Face(FONT_PATH, index=FONT_INDEX)
    face.set_char_size(FONT_SIZE * 64)

    ascender = face.size.ascender >> 6
    height = face.size.height >> 6
    base_line = ascender
    line_height = height

    print(f"Ascender={ascender} Height={height} Base={base_line}")
    print(f"ASCII:{ASCII_COUNT} CJK:{CJK_COUNT} Extra:{len(EXTRA)} Total:{ASCII_COUNT+CJK_COUNT+len(EXTRA)}")

    glyphs = []
    bitmap_data = bytearray(b'\x00')

    def proc(ch):
        idx = face.get_char_index(ch)
        if idx == 0:
            glyphs.append({'cp':ord(ch),'aw':FONT_SIZE*16,'bw':0,'bh':0,'ox':0,'oy':0,'bo':len(bitmap_data)})
            return
        face.load_char(ch, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
        g = face.glyph
        bm = g.bitmap
        bw, bh = bm.width, bm.rows
        stride = (bw + 7) // 8
        packed = []
        for y in range(bh):
            for xb in range(stride):
                b = 0
                for bit in range(8):
                    x = xb * 8 + bit
                    if x < bw:
                        si = y * bm.pitch + (x // 8)
                        sb = 7 - (x % 8)
                        if si < len(bm.buffer) and (bm.buffer[si] & (1 << sb)):
                            b |= (1 << (7 - bit))
                packed.append(b)
        bo = len(bitmap_data)
        bitmap_data.extend(packed)
        glyphs.append({'cp':ord(ch),'aw':(g.advance.x>>6)*16,'bw':bw,'bh':bh,'ox':g.bitmap_left,'oy':g.bitmap_top,'bo':bo})

    # ASCII
    print("[1/3] ASCII...")
    for c in range(ASCII_START, ASCII_END):
        proc(chr(c))
    print(f"  {len(glyphs)} glyphs")

    # CJK
    print(f"[2/3] CJK {CJK_COUNT} chars...")
    missing = 0
    for c in range(CJK_START, CJK_END):
        if face.get_char_index(chr(c)) == 0:
            missing += 1
        proc(chr(c))
        d = c - CJK_START + 1
        if d % 1000 == 0:
            print(f"  {d}/{CJK_COUNT} ({d*100//CJK_COUNT}%) miss={missing}")
    cjk_glyph_start = ASCII_COUNT
    print(f"  Done, index {cjk_glyph_start}-{len(glyphs)-1}")

    assert len(glyphs) == ASCII_COUNT + CJK_COUNT, f"Count mismatch: {len(glyphs)} vs {ASCII_COUNT+CJK_COUNT}"

    # Extras
    print(f"[3/3] Extras {len(EXTRA)} chars...")
    ext_start = len(glyphs)
    ext_sorted = sorted(set(EXTRA))
    for cp in ext_sorted:
        proc(chr(cp))
    ext_count = len(glyphs) - ext_start
    print(f"  {ext_count} glyphs, index {ext_start}-{len(glyphs)-1}")

    total = len(glyphs)
    bmp_sz = len(bitmap_data)
    print(f"\nTotal: {total} glyphs, bitmap: {bmp_sz} bytes ({bmp_sz/1024:.0f}KB)")

    # ── Write C file ──
    with open(OUTPUT_C, 'w', encoding='utf-8') as f:
        f.write(f'/* LVGL v9 28px CJK font, {total} glyphs, {bmp_sz}B bitmap */\n')
        f.write('#include "lvgl.h"\n\n')
        f.write(f'#ifndef {FONT_NAME.upper()}\n#define {FONT_NAME.upper()} 1\n#endif\n')
        f.write(f'#if {FONT_NAME.upper()}\n\n')

        # Bitmap
        f.write(f'static LV_ATTRIBUTE_LARGE_CONST const uint8_t {FONT_NAME}_bitmap[] = {{\n')
        for i in range(0, bmp_sz, 16):
            chunk = bitmap_data[i:i+16]
            line = ','.join(f'0x{b:02X}' for b in chunk)
            if i + 16 < bmp_sz: line += ','
            f.write(f'    {line}\n')
        f.write('};\n\n')

        # Glyph descriptors
        f.write(f'static const lv_font_fmt_txt_glyph_dsc_t {FONT_NAME}_dsc[] = {{\n')
        for g in glyphs:
            f.write(f'    {{.bitmap_index={g["bo"]},.adv_w={g["aw"]},.box_w={g["bw"]},.box_h={g["bh"]},.ofs_x={g["ox"]},.ofs_y={g["oy"]}}},\n')
        f.write('};\n\n')

        # ASCII cmap: FORMAT0_FULL (range <= 256, contiguous)
        f.write(f'/* ASCII cmap: FORMAT0_FULL U+{ASCII_START:04X}-U+{ASCII_END-1:04X} */\n')
        f.write(f'static const uint8_t {FONT_NAME}_ascii_ofs[] = {{')
        f.write(','.join(str(i) for i in range(ASCII_COUNT)))
        f.write('};\n')
        f.write(f'static const lv_font_fmt_txt_cmap_t {FONT_NAME}_cmap0 = {{\n')
        f.write(f'    .range_start=0x{ASCII_START:04X},.range_length={ASCII_COUNT},.glyph_id_start=0,\n')
        f.write(f'    .unicode_list=NULL,.glyph_id_ofs_list={FONT_NAME}_ascii_ofs,.list_length={ASCII_COUNT},\n')
        f.write(f'    .type=LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL}};\n\n')

        # CJK cmap: SPARSE_TINY (range >> 256, binary search on unicode_list)
        f.write(f'/* CJK cmap: SPARSE_TINY U+{CJK_START:04X}-U+{CJK_END-1:04X} */\n')
        f.write(f'static const uint16_t {FONT_NAME}_cjk_unicode[] = {{\n')
        for i in range(0, CJK_COUNT, 16):
            chunk = list(range(i, min(i+16, CJK_COUNT)))
            line = ','.join(f'0x{v:04X}' for v in chunk)
            if i + 16 < CJK_COUNT: line += ','
            f.write(f'    {line}\n')
        f.write('};\n')
        f.write(f'static const lv_font_fmt_txt_cmap_t {FONT_NAME}_cmap1 = {{\n')
        f.write(f'    .range_start=0x{CJK_START:04X},.range_length={CJK_COUNT},.glyph_id_start={cjk_glyph_start},\n')
        f.write(f'    .unicode_list={FONT_NAME}_cjk_unicode,.glyph_id_ofs_list=NULL,.list_length={CJK_COUNT},\n')
        f.write(f'    .type=LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}};\n\n')

        # Extras cmap: SPARSE_TINY
        f.write(f'/* Extras cmap: SPARSE_TINY ({ext_count} chars) */\n')
        f.write(f'static const uint16_t {FONT_NAME}_ext_unicode[] = {{\n')
        for i in range(0, len(ext_sorted), 8):
            chunk = ext_sorted[i:i+8]
            line = ','.join(f'0x{v:04X}' for v in chunk)
            if i + 8 < len(ext_sorted): line += ','
            f.write(f'    {line}\n')
        f.write('};\n')
        f.write(f'static const lv_font_fmt_txt_cmap_t {FONT_NAME}_cmap2 = {{\n')
        f.write(f'    .range_start=0,.range_length={ext_count},.glyph_id_start={ext_start},\n')
        f.write(f'    .unicode_list={FONT_NAME}_ext_unicode,.glyph_id_ofs_list=NULL,.list_length={ext_count},\n')
        f.write(f'    .type=LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}};\n\n')

        # cmap array
        f.write(f'static const lv_font_fmt_txt_cmap_t {FONT_NAME}_cmaps[] = {{\n')
        f.write(f'    {FONT_NAME}_cmap0,\n')
        f.write(f'    {FONT_NAME}_cmap1,\n')
        f.write(f'    {FONT_NAME}_cmap2}};\n\n')

        # Font descriptor
        f.write(f'static const lv_font_fmt_txt_dsc_t {FONT_NAME}_fdsc = {{\n')
        f.write(f'    .glyph_bitmap={FONT_NAME}_bitmap,.glyph_dsc={FONT_NAME}_dsc,.cmaps={FONT_NAME}_cmaps,\n')
        f.write(f'    .kern_dsc=NULL,.kern_scale=0,.cmap_num=3,.bpp={BPP},.kern_classes=0,.bitmap_format=0}};\n\n')

        # Font object
        f.write(f'#if LVGL_VERSION_MAJOR >= 9\n')
        f.write(f'const lv_font_t {FONT_NAME} = {{\n')
        f.write(f'    .get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,\n')
        f.write(f'    .line_height={line_height},.base_line={base_line},.subpx=LV_FONT_SUBPX_NONE,\n')
        f.write(f'    .underline_position=-{max(1,FONT_SIZE//14)},.underline_thickness={max(1,FONT_SIZE//24)},\n')
        f.write(f'    .dsc=&{FONT_NAME}_fdsc,.fallback=NULL}};\n')
        f.write(f'#else\n')
        f.write(f'lv_font_t {FONT_NAME} = {{\n')
        f.write(f'    .get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,\n')
        f.write(f'    .line_height={line_height},.base_line={base_line},.subpx=LV_FONT_SUBPX_NONE,\n')
        f.write(f'    .underline_position=-{max(1,FONT_SIZE//14)},.underline_thickness={max(1,FONT_SIZE//24)},\n')
        f.write(f'    .dsc=&{FONT_NAME}_fdsc}};\n')
        f.write(f'#endif\n\n')
        f.write(f'#endif /*{FONT_NAME.upper()}*/\n')

    sz = os.path.getsize(OUTPUT_C)
    print(f'Written: {OUTPUT_C} ({sz/1024/1024:.1f}MB)')

    # Verify a few CJK chars
    with open(OUTPUT_C, 'r', encoding='utf-8') as vf:
        vtxt = vf.read()
    ok = True
    for ch in '\u4e00\u6b4c\u66f2\u64ad\u653e\u4e2d\u6587':
        found = f'0x{ord(ch):04X}' in vtxt
        print(f'  Verify {ch} U+{ord(ch):04X}: {"OK" if found else "MISSING"}')
        if not found: ok = False
    print(f'{"ALL GOOD" if ok else "MISSING CHARS!"}')

if __name__ == '__main__':
    main()
