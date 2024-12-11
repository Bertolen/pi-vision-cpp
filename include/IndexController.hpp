#pragma once

#include <opencv2/opencv.hpp>
#include <civetweb.h>
#include <thread>
#include <iostream>
#include <regex>
#include <filesystem>

#include "commons.hpp"

namespace fs = std::filesystem;

class IndexController {

    public:
    IndexController(struct mg_context* ctx);
    ~IndexController();

    // Threads de caméra
    void static captureThread(int camID);

    // Handlers
    static int streamHandler(struct mg_connection *conn, void *param);
    static int rootHandler(struct mg_connection *conn, void *param);

    // Getter
    cv::Mat getFrameById(int id);

    private:

    std::thread capThread1;
    std::thread capThread2;

    static std::mutex frameMutexes[NB_WEBCAMS];
    static cv::Mat frames[NB_WEBCAMS];
    static int cameraDevice[NB_WEBCAMS];
    static int cameraID[NB_WEBCAMS];
    static bool running;
};
