#include "DisparityController.hpp"

std::mutex DisparityController::disparityMutex;
std::thread DisparityController::disThread;
cv::Mat DisparityController::disparity;
bool DisparityController::running;
IndexController* DisparityController::indexCtrl;

DisparityController::DisparityController(struct mg_context* ctx, IndexController* indexCtrl) {
    this->indexCtrl = indexCtrl;
    running = true;

    // Lance les threads de capture vidéo
    disThread = std::thread(DisparityController::disparityThread);

    // Configure les handlers
    if(ctx == nullptr) {
        std::cerr << "Erreur : impossible de démarrer le serveur HTTP." << std::endl;
        running = false;
    } else {
        mg_set_request_handler(ctx, "/disparityStream", streamHandler, nullptr);
        mg_set_request_handler(ctx, "/disparity", rootHandler, nullptr);
        running = true;
    }
}

DisparityController::~DisparityController() {
    running = false;
    disThread.join();
}

// Gestionnaire de la requête, affiche le flux MJPEG
int DisparityController::streamHandler(struct mg_connection *conn, void *param) {

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
void DisparityController::loadCalibration(const std::string& filename,
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
void DisparityController::disparityThread(){
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

        frame1 = indexCtrl->getFrameById(0);
        frame2 = indexCtrl->getFrameById(1);
        if(frame1.empty() || frame2.empty()) continue;

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
    }
}

// Gestion de la page HTML
int DisparityController::rootHandler(struct mg_connection *conn, void *param) {
    FILE *file = fopen("resources/disparity.html", "r");
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
