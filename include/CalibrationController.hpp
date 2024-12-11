#pragma once

#include <opencv2/opencv.hpp>
#include <civetweb.h>
#include <thread>
#include <iostream>
#include <filesystem>

#include "commons.hpp"
#include "IndexController.hpp"

namespace fs = std::filesystem;

class CalibrationController {

    public:

    CalibrationController(struct mg_context* ctx, IndexController* indexCtrl);
    ~CalibrationController();

    // Handlers
    static int streamHandler(struct mg_connection *conn, void *param);
    static int rootHandler(struct mg_connection *conn, void *param);
    static int buttonHandler(struct mg_connection *conn, void *param);

    int nbImagesInMemory();
    int nbImagesInMemoryByCamID(int camID);

    private:

    static void saveFrames();

    IndexController* indexCtrl;

    static std::mutex chessboardMutexes[NB_WEBCAMS];
    static cv::Mat chessboards[NB_WEBCAMS];
    static bool running;
    static int nbImages;
};
