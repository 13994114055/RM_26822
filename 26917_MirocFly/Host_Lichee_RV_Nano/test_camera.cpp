#include <opencv2/opencv.hpp>
#include <iostream>

int main() {
    // 尝试打开摄像头，设备索引通常是 0
    cv::VideoCapture cap(0);
    if (!cap.isOpened()) {
        std::cerr << "错误：无法打开摄像头！" << std::endl;
        return -1;
    }

    // 设置你期望的分辨率
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 2560);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 1440);

    cv::Mat frame;
    cap >> frame;

    if (frame.empty()) {
        std::cerr << "错误：捕获到空帧！" << std::endl;
        return -1;
    }

    // 保存图片，方便传输回电脑查看
    cv::imwrite("test_frame.jpg", frame);
    std::cout << "成功保存一帧到 test_frame.jpg" << std::endl;

    return 0;
}
