#!/bin/bash

# 检查是否输入了参数
if [ -z "$1" ]; then
    echo "使用方法: $0 [数量N]"
    echo "例如: $0 6 (将处理 0 到 5)"
    exit 1
fi

N=$1

# 循环从 0 到 N-1
for ((i=0; i<N; i++))
do
    echo "进度: $i / $((N-1))"
    ffmpeg -f rawvideo -pixel_format bgr0 -video_size 224x224 -i "fileimg$i" "output$i.jpg" -y -loglevel quiet
done

echo "完成！已处理 $N 个文件。"
