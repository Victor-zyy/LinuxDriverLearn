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
 *  Static Variable
 * 
 * 
 */
std::vector<std::string> labels;
std::unique_ptr<tflite::Interpreter> interpreter;
std::unique_ptr<tflite::FlatBufferModel> model;

extern "C"{
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

/**
 *  Load image to buffer
 */
bool LoadImageToBuffer(const char* filename, std::vector<uint8_t>& out) {
    std::ifstream f(filename, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

int model_init(void)
{
    /**
     * 1. Load Model
     */
    model = tflite::FlatBufferModel::BuildFromFile("mobilenet_v2.tflite");
    if (!model) {
        std::cerr << "Failed to load model\n";
        return -1;
    }

    /**
     * 2. Build Interpreter
     */
    tflite::ops::builtin::BuiltinOpResolver resolver;

    tflite::InterpreterBuilder(*model, resolver)(&interpreter);
    if (!interpreter) {
        std::cerr << "Failed to create interpreter\n";
        return -1;
    }

    /** 
     * 
     * 3. Allocate memory
     * 
     */
    if (interpreter->AllocateTensors() != kTfLiteOk) {
        printf("Re-allocation failed!\n");
        return -1;
    }

    /**
     * 4. Get Tensor Information
     */
    int input = interpreter->inputs()[0];
    TfLiteTensor* input_tensor = interpreter->tensor(input);

    int height = input_tensor->dims->data[1];
    int width  = input_tensor->dims->data[2];
    int channels = input_tensor->dims->data[3];

    std::cout << "Model input: " << height << "x" << width << "x" << channels << "\n";
    
    return 0;
}


int label_init(void) {

    labels = LoadLabels("labels.txt");
    return labels.empty() ? -1 : 0;

}


int infer_run(uint8_t *image)
{
    if (!interpreter) return -1;

    int input = interpreter->inputs()[0];
    TfLiteTensor* input_tensor = interpreter->tensor(input);
    uint8_t* dst = input_tensor->data.uint8;
    int height = input_tensor->dims->data[1];
    int width  = input_tensor->dims->data[2];

    for (size_t i = 0; i < width * height; i++) {
        dst[i * 3 + 0] = image[i * 4 + 2]; // R
        dst[i * 3 + 1] = image[i * 4 + 1]; // G
        dst[i * 3 + 2] = image[i * 4 + 0]; // B
    }

    if (interpreter->Invoke() != kTfLiteOk) {
        printf("Invoke failed\n");
        return -1;
    }

    /**
     *  Read Output Result
     */

    int output = interpreter->outputs()[0];
    TfLiteTensor* output_tensor = interpreter->tensor(output);

    int output_size = output_tensor->dims->data[1];
    uint8_t* scores = output_tensor->data.uint8;

    /**
     *  Find The Best Score
     */
    int best = 0;
    for (int i = 1; i < output_size; i++) {
        if (scores[i] > scores[best]) best = i;
    }

    printf("Predicted class = %d  score %d\n", best, (int)scores[best]);

    /**
     * Print the result
     */
    std::string result = GetLabelName(labels, best);
    std::cout << "ClassList: " << best << std::endl;
    std::cout << "ObjectName: " << result << std::endl;

    return 0;
}
}