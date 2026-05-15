#include "camera_utils.hpp"
#include <iostream>

Camera::Camera(int device_id)
    : device_id_(device_id),
      opened_(false)
{
    cv::VideoCapture cap(device_id_);
    if (!cap.isOpened()) {
        std::cerr << "[Camera] 无法打开摄像头 device_id = "
                  << device_id_ << std::endl;
        opened_ = false;
        return;
    }

    // 把 VideoCapture 放进成员变量
    cap_ = std::move(cap);
    opened_ = true;
}

Camera::~Camera()
{
    if (cap_.isOpened()) {
        cap_.release();
    }
}

bool Camera::is_open() const
{
    return opened_;
}

bool Camera::capture(const std::string& save_path) const
{
    if (!opened_) {
        std::cerr << "[Camera] 摄像头未打开，无法截图" << std::endl;
        return false;
    }

    cv::Mat frame;
    cap_ >> frame;

    if (frame.empty()) {
        std::cerr << "[Camera] 读取图像帧失败" << std::endl;
        return false;
    }

    bool ok = cv::imwrite(save_path, frame);
    if (!ok) {
        std::cerr << "[Camera] 图片保存失败: " << save_path << std::endl;
        return false;
    }

    std::cout << "[Camera] 图片已保存: " << save_path << std::endl;
    return true;
}