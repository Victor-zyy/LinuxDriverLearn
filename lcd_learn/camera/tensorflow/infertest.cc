#include <iostream>
#include <fstream>
#include <vector>
#include <memory>
#include <cstring>

#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/kernels/register.h"
#include "tensorflow/lite/model.h"

/**
 * 
 * Load labels file to vector
 * 
 */ 
static std::vector<std::string> LoadLabels(const std::string& filename) {
    std::vector<std::string> labels;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Can't open : " << filename << std::endl;
        return labels;
    }
    std::string line;
    while (std::getline(file, line)) {
        labels.push_back(line);
    }
    file.close();
    return labels;
}

/**
 *  Get Name in labels by index
 */
std::string GetLabelName(const std::vector<std::string>& labels, int index) {
    if (index >= 0 && index < (int)labels.size()) {
        return labels[index];
    }
    return "unknown index";
}

bool LoadImageToBuffer(const char* filename, std::vector<uint8_t>& out) {
    std::ifstream f(filename, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

int main() {

    std::vector<std::string> labels = LoadLabels("labels.txt");

    // 1. 加载模型
    auto model = tflite::FlatBufferModel::BuildFromFile("mobilenet_v2.tflite");
    if (!model) {
        std::cerr << "Failed to load model\n";
        return -1;
    }

    // 2. 创建解释器
    tflite::ops::builtin::BuiltinOpResolver resolver;
    std::unique_ptr<tflite::Interpreter> interpreter;

    tflite::InterpreterBuilder(*model, resolver)(&interpreter);
    if (!interpreter) {
        std::cerr << "Failed to create interpreter\n";
        return -1;
    }

    // 3. 分配 tensor 内存
    interpreter->AllocateTensors();

    // 4. 获取输入 tensor 信息
    int input = interpreter->inputs()[0];
    TfLiteTensor* input_tensor = interpreter->tensor(input);

    int height = input_tensor->dims->data[1];
    int width  = input_tensor->dims->data[2];
    int channels = input_tensor->dims->data[3];

    std::cout << "Model input: " << height << "x" << width << "x" << channels << "\n";

    // 5. 加载图片（必须是 224x224 RGB）
    std::vector<uint8_t> image;
    if (!LoadImageToBuffer("input.rgb", image)) {
        std::cerr << "Failed to load image\n";
        return -1;
    }

    if (image.size() != height * width * channels) {
        std::cerr << "Image size mismatch\n";
        return -1;
    }

    // 6. 拷贝到输入 tensor
    memcpy(input_tensor->data.uint8, image.data(), image.size());

    // 7. 执行推理
    if (interpreter->Invoke() != kTfLiteOk) {
        std::cerr << "Invoke failed\n";
        return -1;
    }

    // 8. 读取输出
    int output = interpreter->outputs()[0];
    TfLiteTensor* output_tensor = interpreter->tensor(output);

    int output_size = output_tensor->dims->data[1];
    uint8_t* scores = output_tensor->data.uint8;

    // 9. 找到最大概率
    int best = 0;
    for (int i = 1; i < output_size; i++) {
        if (scores[i] > scores[best]) best = i;
    }

    std::cout << "Predicted class = " << best
              << "  score = " << (int)scores[best] << "\n";

    std::string obj_class = GetLabelName(labels, best);

    std::cout << "ClassName:  " << obj_class << std::endl;

    return 0;
}
