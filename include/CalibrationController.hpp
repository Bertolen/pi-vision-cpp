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

    // Threads de caméra
    void static calibThread(int camID);

    // Handlers
    static int streamHandler(struct mg_connection *conn, void *param);
    static int rootHandler(struct mg_connection *conn, void *param);
    static int saveButtonHandler(struct mg_connection *conn, void *param);
    static int calibrateButtonHandler(struct mg_connection *conn, void *param);
    static int eraseButtonHandler(struct mg_connection *conn, void *param);

    int nbImagesInMemory();
    int nbImagesInMemoryByCamID(int camID);

    private:

    void eraseFrames();
    void calibrateCameras();
    void saveCalibration(const std::string& filename,
                     const cv::Mat& cameraMatrix1, const cv::Mat& distCoeffs1,
                     const cv::Mat& cameraMatrix2, const cv::Mat& distCoeffs2,
                     const cv::Mat& R, const cv::Mat& T);

    static void saveFrames();

    std::thread calibThread1;
    std::thread calibThread2;
    int boardWidth, boardHeight;
    float squareSize;
    
    static cv::Size boardSize;
    static IndexController* indexCtrl;
    static std::mutex chessboardMutexes[NB_WEBCAMS];
    static cv::Mat chessboards[NB_WEBCAMS];
    static int cameraID[NB_WEBCAMS];
    static bool running;
    static int nbImages;
};
