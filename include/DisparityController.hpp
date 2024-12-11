#pragma once

#include <opencv2/opencv.hpp>
#include <civetweb.h>
#include <thread>
#include <iostream>
#include <filesystem>

#include "IndexController.hpp"
#include "commons.hpp"

class DisparityController {
    public:
    DisparityController(struct mg_context* ctx, IndexController* indexCtrl);
    ~DisparityController();

    // Threads de caméra
    void static disparityThread();

    // Handlers
    static int streamHandler(struct mg_connection *conn, void *param);
    static int rootHandler(struct mg_connection *conn, void *param);

    private:

    static void loadCalibration(const std::string& filename,
                     cv::Mat& cameraMatrix1, cv::Mat& distCoeffs1,
                     cv::Mat& cameraMatrix2, cv::Mat& distCoeffs2,
                     cv::Mat& R, cv::Mat& T);

    static bool running;
    static IndexController* indexCtrl;
    static std::mutex disparityMutex;
    static std::thread disThread;
    static cv::Mat disparity;
};