#include <opencv2/opencv.hpp>
#include <civetweb.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <signal.h>
#include <random>
#include <string>
#include <filesystem>
#include "IndexController.hpp"
#include "commons.hpp"

namespace fs = std::filesystem;

std::mutex rectifiedFrameMutexes[NB_WEBCAMS];
std::mutex disparityMutex;
cv::Mat rectifiedFrames[NB_WEBCAMS];
cv::Mat disparity;

bool calibrated = false;
bool running = true;

std::thread disThread;

// Gestion de la page HTML
int rootHandler(struct mg_connection *conn, void *param) {
    FILE *file = fopen("resources/index.html", "r");
    if (!file) {
        mg_printf(conn,
                  "HTTP/1.1 404 Not Found\r\n"
                  "Content-Type: text/plain\r\n\r\n"
                  "File not found!");
        return 404;
    }

    // Lire le contenu du fichier
    fseek(file, 0, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);
    std::vector<char> fileContent(fileSize);
    fread(fileContent.data(), 1, fileSize, file);
    fclose(file);

    // Envoyer la réponse
    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/html\r\n"
              "Content-Length: %zu\r\n\r\n",
              fileSize);
    mg_write(conn, fileContent.data(), fileSize);
    return 200;
}

// Gestion du signal d'arrêt
void handleSignal(int signal) {
    //std::cout << "Signal reçu " << signal << std::endl;
    if (signal == SIGINT || signal == SIGTERM) {
        running = false;
        std::cout << "Arrêt du serveur." << std::endl;
    }
}

// Gestionnaire de la requête, affiche le flux MJPEG
int disparityStreamHandler(struct mg_connection *conn, void *param) {
    // En-têtes pour le flux MJPEG
    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
              "Cache-Control: no-cache\r\n"
              "\r\n");

    while (running) {
        std::vector<uchar> buf;
        {
            std::lock_guard<std::mutex> lock(disparityMutex);
            if (!disparity.empty()) {
                cv::imencode(".jpg", disparity, buf);
            }
        }

        if (!buf.empty()) {
            mg_printf(conn,
                      "--frame\r\n"
                      "Content-Type: image/jpeg\r\n"
                      "Content-Length: %lu\r\n\r\n",
                      buf.size());
            mg_write(conn, buf.data(), buf.size());
            mg_printf(conn, "\r\n");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30 FPS
    }

    return 200; // Réponse HTTP réussie
}

// Chargement des paramétres de calibration
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

// Creation du flux de disparité
void disparityThread(){
    cv::Mat cameraMatrix1, distCoeffs1, cameraMatrix2, distCoeffs2, R, T;
    loadCalibration("./data/calibration/stereo_calib.yml", cameraMatrix1, distCoeffs1, cameraMatrix2, distCoeffs2, R, T);

    cv::Mat R1, R2, P1, P2, Q;
    cv::Size imageSize(640, 480);

    cv::stereoRectify(cameraMatrix1, distCoeffs1, cameraMatrix2, distCoeffs2,
                      imageSize, R, T, R1, R2, P1, P2, Q);

    cv::Mat map1x, map1y, map2x, map2y;
    cv::initUndistortRectifyMap(cameraMatrix1, distCoeffs1, R1, P1, imageSize, CV_32FC1, map1x, map1y);
    cv::initUndistortRectifyMap(cameraMatrix2, distCoeffs2, R2, P2, imageSize, CV_32FC1, map2x, map2y);

    cv::Ptr<cv::StereoBM> stereo = cv::StereoBM::create(16, 15); // Paramètres ajustables

    while (running) {
        cv::Mat frame1, frame2, gray1, gray2, rectified1, rectified2, disparityTemp;

        if (frame1.empty() || frame2.empty()) {
            continue;
        }

        cv::cvtColor(frame1, gray1, cv::COLOR_BGR2GRAY);
        cv::cvtColor(frame2, gray2, cv::COLOR_BGR2GRAY);

        cv::remap(gray1, rectified1, map1x, map1y, cv::INTER_LINEAR);
        cv::remap(gray2, rectified2, map2x, map2y, cv::INTER_LINEAR);

        // stereo->compute(gray1, gray2, disparityTemp);
        stereo->compute(rectified1, rectified2, disparityTemp);

        cv::normalize(disparityTemp, disparityTemp, 0, 255, cv::NORM_MINMAX, CV_8U);

        {
            std::lock_guard<std::mutex> lock(disparityMutex);
            disparity = disparityTemp.clone();
        }

        {
            std::lock_guard<std::mutex> lock(rectifiedFrameMutexes[0]);
            rectifiedFrames[0] = rectified1.clone();
        }
        {
            std::lock_guard<std::mutex> lock(rectifiedFrameMutexes[1]);
            rectifiedFrames[1] = rectified2.clone();
        }
    }
}

// Sauvegarde du fichier de calibration
void saveCalibration(const std::string& filename,
                     const cv::Mat& cameraMatrix1, const cv::Mat& distCoeffs1,
                     const cv::Mat& cameraMatrix2, const cv::Mat& distCoeffs2,
                     const cv::Mat& R, const cv::Mat& T) {
    cv::FileStorage fs(filename, cv::FileStorage::WRITE);
    fs << "cameraMatrix1" << cameraMatrix1;
    fs << "distCoeffs1" << distCoeffs1;
    fs << "cameraMatrix2" << cameraMatrix2;
    fs << "distCoeffs2" << distCoeffs2;
    fs << "R" << R;
    fs << "T" << T;
    fs.release();
}

// Calibration des caméras
void calibrateCameras(int numImages) {
    std::cout << "Début calibration" << std::endl;

    const int boardWidth = 7, boardHeight = 5; // C'est le nombre de COINS INTERNES et pas de cases (donc pour 8*6 cases il faut 7*5 coins)
    const float squareSize = 0.019f; // Taille réelle des carrés en mètres

    cv::Size boardSize(boardWidth, boardHeight);

    std::vector<std::vector<cv::Point3f>> objectPoints;
    std::vector<std::vector<cv::Point2f>> imagePoints1, imagePoints2;
    cv::Mat gray1, gray2;

    std::vector<cv::Point3f> obj;
    for (int i = 0; i < boardHeight; i++) {
        for (int j = 0; j < boardWidth; j++) {
            obj.push_back(cv::Point3f(j * squareSize, i * squareSize, 0));
        }
    }

    for (int i = 0 ; i < numImages ; i++) {
        std::cout << "Traitement image " << std::to_string(i) << std::endl;
        std::string filename1 = "./data/images/camera0-" + std::to_string(i) + ".jpg";
        std::string filename2 = "./data/images/camera1-" + std::to_string(i) + ".jpg";
        cv::Mat frame1 = cv::imread(filename1, 1);
        cv::Mat frame2 = cv::imread(filename2, 1);

        cv::cvtColor(frame1, gray1, cv::COLOR_BGR2GRAY);
        cv::cvtColor(frame2, gray2, cv::COLOR_BGR2GRAY);

        std::vector<cv::Point2f> corners1, corners2;
        bool found1 = cv::findChessboardCorners(gray1, boardSize, corners1,
                        cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE);
        bool found2 = cv::findChessboardCorners(gray2, boardSize, corners2,
                        cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE);

        if (found1 && found2){
            std::cout << "  Echiquier trouvé!" << std::endl;
            cv::cornerSubPix(gray1, corners1, cv::Size(11, 11), cv::Size(-1, -1),
                             cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.1));
            cv::cornerSubPix(gray2, corners2, cv::Size(11, 11), cv::Size(-1, -1),
                             cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.1));

            imagePoints1.push_back(corners1);
            imagePoints2.push_back(corners2);
            objectPoints.push_back(obj);
        }
    }

    // Calibration des caméras
    std::cout << "Calibration caméras" << std::endl;
    cv::Mat cameraMatrix1, distCoeffs1, cameraMatrix2, distCoeffs2;
    cv::Mat R, T, E, F;

    std::cout << "Caméra 1" << std::endl;
    cv::calibrateCamera(objectPoints, imagePoints1, gray1.size(), cameraMatrix1, distCoeffs1, cv::noArray(), cv::noArray());
    std::cout << "Caméra 2" << std::endl;
    cv::calibrateCamera(objectPoints, imagePoints2, gray2.size(), cameraMatrix2, distCoeffs2, cv::noArray(), cv::noArray());
    std::cout << "Stéréovision" << std::endl;
    cv::stereoCalibrate(objectPoints, imagePoints1, imagePoints2,
                        cameraMatrix1, distCoeffs1,
                        cameraMatrix2, distCoeffs2,
                        gray1.size(), R, T, E, F,
                        cv::CALIB_FIX_INTRINSIC,
                        cv::TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 100, 1e-5));

    // Sauvegarder les paramètres de calibration
    saveCalibration("./data/calibration/stereo_calib.yml", cameraMatrix1, distCoeffs1, cameraMatrix2, distCoeffs2, R, T);
    calibrated = true;
    disThread = std::thread(disparityThread);

    std::cout << "Calibration terminée et sauvegardée dans './data/calibration/stereo_calib.yml'." << std::endl;
}

// Gestion du bouton de calibration
int calibrationButtonHandler(struct mg_connection *conn, void *param) {
    calibrateCameras(20); // TODO

    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/plain\r\n\r\n"
              "Success!");
    return 200;
}

int main() {
    // Enregistrement du gestionnaire de signal
    signal(SIGTERM, handleSignal);
    signal(SIGINT, handleSignal);

    calibrated = fs::exists("./data/calibration/stereo_calib.yml");

    if(calibrated) {
        std::cout << "Caméras pré-calibrées. Début du stream de disparité" << std::endl;
        disThread = std::thread(disparityThread);
    }

    // Initialise le serveur HTTP
    const char *options[] = {"listening_ports", "8080", nullptr};
    struct mg_callbacks callbacks = {};
    struct mg_context *ctx = mg_start(&callbacks, nullptr, options);

    if (ctx == nullptr) {
        std::cerr << "Erreur : impossible de démarrer le serveur HTTP." << std::endl;
        running = false;
    } else {
        mg_set_request_handler(ctx, "/disparity", disparityStreamHandler, nullptr);
        mg_set_request_handler(ctx, "/calibrate", calibrationButtonHandler, nullptr);
        running = true;
        std::cout << "Serveur démarré sur http://localhost:8080/" << std::endl;
    }

    IndexController* indexController = new IndexController(ctx);

    // Boucle principale pour maintenir le programme actif
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    if (calibrated) disThread.join();
    delete indexController;
    if (ctx) mg_stop(ctx);
    return 0;
}
