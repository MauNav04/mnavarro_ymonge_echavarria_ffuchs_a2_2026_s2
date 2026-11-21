#include "image_io.hpp"
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <stdexcept>
fractal::MultiImage readImage(const std::string& path,bool gray) {
    const auto mat=cv::imread(path,gray?cv::IMREAD_GRAYSCALE:cv::IMREAD_COLOR);
    if(mat.empty()) throw std::runtime_error("No se pudo leer: "+path);
    fractal::MultiImage a;
    for(int c=0;c<(gray?1:3);++c) a.planes.push_back({mat.cols,mat.rows,std::vector<float>(mat.total())});
    for(int y=0;y<mat.rows;++y) for(int x=0;x<mat.cols;++x) {
        if(gray) a.planes[0].at(x,y)=mat.at<unsigned char>(y,x)/255.0f;
        else {
            auto bgr=mat.at<cv::Vec3b>(y,x);
            // OpenCV entrega BGR; la biblioteca y FRC2 usan RGB.
            for(int c=0;c<3;++c) a.planes[c].at(x,y)=bgr[2-c]/255.0f;
        }
    }
    return a;
}
void writeImage(const std::string& path,const fractal::MultiImage& a) {
    (void)fractal::mse(a,a);
    bool gray=a.planes.size()==1;
    cv::Mat mat(a.planes[0].height,a.planes[0].width,gray?CV_8UC1:CV_8UC3);
    for(int y=0;y<mat.rows;++y) for(int x=0;x<mat.cols;++x) {
        if(gray) mat.at<unsigned char>(y,x)=cv::saturate_cast<unsigned char>(a.planes[0].at(x,y)*255);
        else for(int c=0;c<3;++c)
            mat.at<cv::Vec3b>(y,x)[2-c]=cv::saturate_cast<unsigned char>(a.planes[c].at(x,y)*255);
    }
    if(!cv::imwrite(path,mat)) throw std::runtime_error("No se pudo guardar: "+path);
}
