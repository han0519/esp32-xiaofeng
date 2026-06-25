"""Convert GIF images to LVGL v9 RGB565 C arrays with frame animation support."""
import os
import sys
from PIL import Image, ImageSequence

INPUT_DIR = r"C:\Users\han\Desktop\biaoqbao"
OUTPUT_FILE = r"C:\Users\han\Desktop\audio\esp32-s31-korvo-bt\main\bt_ui\gallery_images.h"
MAX_FRAMES_PER_GIF = 4
MAX_SIZE = 64  # Max width/height — minimize flash footprint

def rgb_to_rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)

def convert_image_to_rgb565(img):
    """Convert PIL image to RGB565 byte array."""
    if img.mode != 'RGB':
        img = img.convert('RGB')
    w, h = img.size
    data = bytearray(w * h * 2)
    pixels = img.load()
    idx = 0
    for y in range(h):
        for x in range(w):
            r, g, b = pixels[x, y]
            val = rgb_to_rgb565(r, g, b)
            data[idx] = val & 0xFF
            data[idx + 1] = (val >> 8) & 0xFF
            idx += 2
    return bytes(data), w, h

def write_frame_array(f, name, data, w, h):
    """Write RGB565 frame data as C array."""
    f.write(f"static const uint8_t {name}_data[] = {{\n")
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_str = ', '.join(f'0x{b:02X}' for b in chunk)
        f.write(f"    {hex_str},\n")
    f.write("};\n\n")

def write_frame_descriptor(f, name, w, h):
    """Write LVGL v9 lv_image_dsc_t descriptor."""
    f.write(f"static const lv_image_dsc_t {name} = {{\n")
    f.write(f"    .header = {{\n")
    f.write(f"        .magic = LV_IMAGE_HEADER_MAGIC,\n")
    f.write(f"        .cf = LV_COLOR_FORMAT_RGB565,\n")
    f.write(f"        .flags = 0,\n")
    f.write(f"        .w = {w},\n")
    f.write(f"        .h = {h},\n")
    f.write(f"        .stride = {w * 2},\n")
    f.write(f"    }},\n")
    f.write(f"    .data_size = {w * h * 2},\n")
    f.write(f"    .data = (const uint8_t*){name}_data,\n")
    f.write("};\n\n")

def main():
    gif_files = sorted(
        [f for f in os.listdir(INPUT_DIR) if f.lower().endswith('.gif')],
        key=lambda x: int(''.join(c for c in os.path.splitext(x)[0] if c.isdigit()) or '999')
    )
    
    if not gif_files:
        print(f"No GIF files found in {INPUT_DIR}")
        return

    print(f"Processing {len(gif_files)} GIF files...")
    
    # Collect all animation data
    all_anims = []  # list of (anim_name, [(frame_name, data, w, h), ...])
    
    for idx, gif_name in enumerate(gif_files):
        gif_path = os.path.join(INPUT_DIR, gif_name)
        base_name = f"gif_{idx:02d}"
        
        try:
            img = Image.open(gif_path)
            frames = []
            frame_idx = 0
            
            for frame in ImageSequence.Iterator(img):
                # Convert to RGB
                if frame.mode == 'P' and 'transparency' in frame.info:
                    frame = frame.convert('RGBA')
                else:
                    frame = frame.convert('RGB')
                
                # Resize if needed
                w, h = frame.size
                if w > MAX_SIZE or h > MAX_SIZE:
                    ratio = min(MAX_SIZE / w, MAX_SIZE / h)
                    new_w = int(w * ratio)
                    new_h = int(h * ratio)
                    frame = frame.resize((new_w, new_h), Image.LANCZOS)
                
                # Get frame duration from GIF info
                # duration = frame.info.get('duration', 100)
                
                rgb565_data, fw, fh = convert_image_to_rgb565(frame)
                frames.append((rgb565_data, fw, fh))
                frame_idx += 1
            
            # Keep only MAX_FRAMES_PER_GIF frames (evenly spaced)
            if len(frames) > MAX_FRAMES_PER_GIF:
                step = len(frames) / MAX_FRAMES_PER_GIF
                sampled = [frames[int(i * step)] for i in range(MAX_FRAMES_PER_GIF)]
                frames = sampled
            
            all_anims.append((base_name, frames))
            img.close()
            print(f"  {gif_name}: {len(frames)} frames extracted (original: {frame_idx})")
            
        except Exception as e:
            print(f"  ERROR processing {gif_name}: {e}")
    
    print(f"\nGenerating {OUTPUT_FILE}...")
    
    with open(OUTPUT_FILE, 'w', encoding='utf-8') as f:
        f.write("/* Auto-generated animated gallery images for LVGL v9 */\n")
        f.write("#ifndef GALLERY_IMAGES_H\n")
        f.write("#define GALLERY_IMAGES_H\n\n")
        f.write('#include "lvgl.h"\n')
        f.write("#include <stdint.h>\n\n")
        
        total_anim_count = len(all_anims)
        
        # Write all frame data and descriptors
        for anim_name, frames in all_anims:
            for fi, (data, w, h) in enumerate(frames):
                frame_name = f"anim_{anim_name}_f{fi}"
                write_frame_array(f, frame_name, data, w, h)
                write_frame_descriptor(f, frame_name, w, h)
        
        # Write per-animation frame pointer arrays
        for anim_name, frames in all_anims:
            f.write(f"static const lv_image_dsc_t *{anim_name}_frames[] = {{\n")
            for fi in range(len(frames)):
                f.write(f"    &anim_{anim_name}_f{fi},\n")
            f.write("};\n\n")
            f.write(f"#define {anim_name.upper()}_FRAME_COUNT {len(frames)}\n\n")
        
        # Write gallery animation metadata array
        f.write(f"#define GALLERY_IMAGE_COUNT {total_anim_count}\n\n")
        
        f.write("typedef struct {\n")
        f.write("    const lv_image_dsc_t **frames;\n")
        f.write("    int frame_count;\n")
        f.write("    int frame_delay_ms;  /* Delay between frames */\n")
        f.write("} gallery_anim_info_t;\n\n")
        
        f.write("static const gallery_anim_info_t gallery_anims[GALLERY_IMAGE_COUNT] = {\n")
        for anim_name, _ in all_anims:
            f.write(f"    {{ .frames = {anim_name}_frames, .frame_count = {anim_name.upper()}_FRAME_COUNT, .frame_delay_ms = 150 }},\n")
        f.write("};\n\n")
        
        # Backward-compatible gallery_images array (first frame of each animation)
        f.write("/* First-frame array for initial display */\n")
        f.write("static const lv_image_dsc_t *gallery_images[GALLERY_IMAGE_COUNT] = {\n")
        for anim_name, _ in all_anims:
            f.write(f"    &anim_{anim_name}_f0,\n")
        f.write("};\n\n")
        
        f.write("#endif /* GALLERY_IMAGES_H */\n")
    
    # Calculate total data size
    total_bytes = 0
    for _, frames in all_anims:
        for data, _, _ in frames:
            total_bytes += len(data)
    
    print(f"\nDone! {total_anim_count} animations, {sum(len(f) for _, f in all_anims)} total frames")
    print(f"Total raw pixel data: {total_bytes / 1024:.1f} KB ({total_bytes / 1024 / 1024:.2f} MB)")

if __name__ == '__main__':
    main()
