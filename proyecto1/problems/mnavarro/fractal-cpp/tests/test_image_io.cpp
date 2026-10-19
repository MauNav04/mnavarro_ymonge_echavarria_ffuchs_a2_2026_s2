#include "image_io.hpp"
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <filesystem>
#include <stdexcept>
#include <iostream>
int main() {
 try {
    auto path=std::filesystem::temp_directory_path()/"fractal_rgb_io_test.png";
    cv::Mat known(16,16,CV_8UC3,cv::Scalar(17,83,211)); // B,G,R distintos.
    if(!cv::imwrite(path.string(),known)) throw std::runtime_error("No se pudo escribir PNG.");
    auto a=readImage(path.string());
    if(a.planes[0].at(0,0)!=211/255.0f || a.planes[2].at(0,0)!=17/255.0f)
        throw std::runtime_error("Error BGR -> RGB.");
    writeImage(path.string(),a);
    auto check=cv::imread(path.string(),cv::IMREAD_COLOR);
    if(check.at<cv::Vec3b>(0,0)!=cv::Vec3b(17,83,211)) throw std::runtime_error("Error RGB -> BGR.");
    std::filesystem::remove(path); std::cout<<"OK OpenCV: orden RGB/BGR y PNG.\n";
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
}
