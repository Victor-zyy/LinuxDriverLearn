from PIL import Image

def generate_header(image_path, output_header):
    # 打开并缩放图片到 640x480
    img = Image.open(image_path).convert('RGB').resize((640, 480))
    pixels = img.load()
    width, height = img.size

    with open(output_header, "w") as f:
        f.write("#ifndef IMAGE_DATA_H\n#define IMAGE_DATA_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"static const uint32_t image_data[{width * height}] = {{\n")
        
        for y in range(height):
            line = "    "
            for x in range(width):
                r, g, b = pixels[x, y]
                # 构造 xRGB8888 格式 (0x00RRGGBB)
                hex_val = f"0x00{r:02x}{g:02x}{b:02x}"
                line += hex_val + ", "
            f.write(line + "\n")
            
        f.write("};\n\n#endif")

# 使用方法：将 'your_image.jpg' 替换为你的图片路径
generate_header('image.png', 'image_data.h')
print("脚本已就绪，运行即可生成 image_data.h")