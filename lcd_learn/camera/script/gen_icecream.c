#include <stdio.h>
#include <stdlib.h>
#include <math.h>

int main() {
    const int W = 224;
    const int H = 224;
    unsigned char *img = (unsigned char *)malloc(W * H * 3);
    if (!img) return -1;

    // 简单参数
    int cx = W / 2;      // 冰淇淋球中心 x
    int cy = H / 3;      // 冰淇淋球中心 y
    int radius = 50;     // 冰淇淋球半径

    // 三角形筒的顶点
    int cone_top_x = cx;
    int cone_top_y = cy + radius;
    int cone_bottom_y = H - 20;
    int cone_half_width = 40;

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            unsigned char r, g, b;

            // 默认背景：淡蓝色
            r = 180;
            g = 220;
            b = 255;

            // 判断是否在冰淇淋球内（圆形）
            int dx = x - cx;
            int dy = y - cy;
            if (dx*dx + dy*dy <= radius*radius) {
                // 粉色冰淇淋球
                r = 255;
                g = 180;
                b = 220;
            }

            // 判断是否在冰淇淋筒（三角形）内
            if (y >= cone_top_y && y <= cone_bottom_y) {
                // 线性缩小的宽度
                float t = (float)(y - cone_top_y) / (cone_bottom_y - cone_top_y);
                int half_w = (int)((1.0f - t) * cone_half_width);
                int left = cone_top_x - half_w;
                int right = cone_top_x + half_w;
                if (x >= left && x <= right) {
                    // 浅棕色筒
                    r = 230;
                    g = 200;
                    b = 150;
                }
            }

            int idx = (y * W + x) * 3;
            img[idx + 0] = r;
            img[idx + 1] = g;
            img[idx + 2] = b;
        }
    }

    FILE *f = fopen("icecream.rgb", "wb");
    if (!f) {
        free(img);
        return -1;
    }
    fwrite(img, 1, W * H * 3, f);
    fclose(f);
    free(img);

    printf("Generated icecream.rgb (%dx%d RGB888)\n", W, H);
    return 0;
}

