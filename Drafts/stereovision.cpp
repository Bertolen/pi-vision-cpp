#include <opencv2/opencv.hpp>
#include <iostream>
#include <string>

/* should create a depth map given 2 calibrated cameras */

void loadCalibration(const std::string& filename,
                     cv::Mat& cameraMatrix1, cv::Mat& distCoeffs1,
                     cv::Mat& cameraMatrix2, cv::Mat& distCoeffs2,
                     cv::Mat& R, cv::Mat& T) {
    cv::FileStorage fs(filename, cv::FileStorage::READ);
    fs["cameraMatrix1"] >> cameraMatrix1;
    fs["distCoeffs1"] >> distCoeffs1;
    fs["cameraMatrix2"] >> cameraMatrix2;
    fs["distCoeffs2"] >> distCoeffs2;
    fs["R"] >> R;
    fs["T"] >> T;
    fs.release();
}

int main() {
    cv::Mat cameraMatrix1, distCoeffs1, cameraMatrix2, distCoeffs2, R, T;
    loadCalibration("stereo_calib.yml", cameraMatrix1, distCoeffs1, cameraMatrix2, distCoeffs2, R, T);

    cv::Mat R1, R2, P1, P2, Q;
    cv::Size imageSize(640, 480);

    cv::stereoRectify(cameraMatrix1, distCoeffs1, cameraMatrix2, distCoeffs2,
                      imageSize, R, T, R1, R2, P1, P2, Q);

    cv::VideoCapture cap1(0), cap2(1);
    if (!cap1.isOpened() || !cap2.isOpened()) {
        std::cerr << "Erreur : impossible d'ouvrir les caméras." << std::endl;
        return -1;
    }

    cv::Mat map1x, map1y, map2x, map2y;
    cv::initUndistortRectifyMap(cameraMatrix1, distCoeffs1, R1, P1, imageSize, CV_32FC1, map1x, map1y);
    cv::initUndistortRectifyMap(cameraMatrix2, distCoeffs2, R2, P2, imageSize, CV_32FC1, map2x, map2y);

    cv::Ptr<cv::StereoBM> stereo = cv::StereoBM::create(16, 15); // Paramètres ajustables

    while (true) {
        cv::Mat frame1, frame2, gray1, gray2, rectified1, rectified2, disparity;
        cap1 >> frame1;
        cap2 >> frame2;

        if (frame1.empty() || frame2.empty()) break;

        cv::cvtColor(frame1, gray1, cv::COLOR_BGR2GRAY);
        cv::cvtColor(frame2, gray2, cv::COLOR_BGR2GRAY);

        cv::remap(gray1, rectified1, map1x, map1y, cv::INTER_LINEAR);
        cv::remap(gray2, rectified2, map2x, map2y, cv::INTER_LINEAR);

        stereo->compute(rectified1, rectified2, disparity);

        cv::normalize(disparity, disparity, 0, 255, cv::NORM_MINMAX, CV_8U);

        cv::imshow("Disparity", disparity);
        if (cv::waitKey(1) == 27) break; // Appuyer sur 'ESC' pour quitter
    }

    return 0;
}
