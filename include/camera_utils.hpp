#ifndef CAMERA_UTILS_H
#define CAMERA_UTILS_H

#include <string>
#include <opencv2/opencv.hpp>

class Camera
{
public:
    /*
     * 构造函数
     * device_id: 摄像头设备号（0 = /dev/video0）
     */
    explicit Camera(int device_id);

    /*
     * 析构函数
     * 自动释放摄像头资源
     */
    ~Camera();

    /*
     * 判断摄像头是否成功打开
     */
    bool is_open() const;

    /*
     * 截取一帧并保存为图片
     */
    bool capture(const std::string& save_path) const;

private:
    int device_id_;
    bool opened_;
    cv::VideoCapture cap_;
};

#endif // CAMERA_UTILS_H