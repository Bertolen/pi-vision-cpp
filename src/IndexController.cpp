#include "IndexController.hpp"

// Définition des variables statiques
std::mutex IndexController::frameMutexes[NB_WEBCAMS];
cv::Mat IndexController::frames[NB_WEBCAMS];
int IndexController::cameraDevice[NB_WEBCAMS];
bool IndexController::running;
int IndexController::cameraID[NB_WEBCAMS];

// Constructeur de la classe
IndexController::IndexController(struct mg_context* ctx) {

    // Initialise les identifiants des caméras
    cameraID[0] = 0;
    cameraID[1] = 1;
    cameraDevice[0] = 0;
    cameraDevice[1] = 2;

    // Lance les threads de capture vidéo
    capThread1 = std::thread(IndexController::captureThread, cameraID[0]);
    capThread2 = std::thread(IndexController::captureThread, cameraID[1]);

    // Configure les handlers
    if (ctx == nullptr) {
        std::cerr << "Erreur : impossible de démarrer le serveur HTTP." << std::endl;
        running = false;
    } else {
        mg_set_request_handler(ctx, "/video1", streamHandler, &cameraID[0]);
        mg_set_request_handler(ctx, "/video2", streamHandler, &cameraID[1]);
        mg_set_request_handler(ctx, "/", rootHandler, nullptr);
        running = true;
    }
}

// Destructeur
IndexController::~IndexController() {
    running = false;
    capThread1.join();
    capThread2.join();
}

// Thread séparé qui capture le flux des caméras
void IndexController::captureThread(int camID) {
    cv::VideoCapture cap(cameraDevice[camID]);
    if (!cap.isOpened()) {
        std::cerr << "Erreur : impossible d'ouvrir la caméra. ID = " << camID << std::endl;
        return;
    }

    // Configurer la taille de l'image et la fréquence d'images (facultatif)
    cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    cap.set(cv::CAP_PROP_FPS, 30);

    while (running) {
        cv::Mat temp_frame;
        cap >> temp_frame; // Capture une nouvelle image
        if (temp_frame.empty()) continue;

        {
            std::lock_guard<std::mutex> lock(frameMutexes[camID]);
            frames[camID] = temp_frame.clone();
        }
    }
    cap.release();
}

// Gestionnaire de la requête, affiche le flux MJPEG
int IndexController::streamHandler(struct mg_connection *conn, void *param) {
    int *cameraID = (int *)(param);

    // En-têtes pour le flux MJPEG
    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
              "Cache-Control: no-cache\r\n"
              "\r\n");

    while (running) {
        std::vector<uchar> buf;

        {
            std::lock_guard<std::mutex> lock(frameMutexes[*cameraID]);
            if (!frames[*cameraID].empty()) {
                cv::imencode(".jpg", frames[*cameraID], buf);
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

// Gestion de la page HTML
int IndexController::rootHandler(struct mg_connection *conn, void *param) {
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

cv::Mat IndexController::getFrameById(int id) {
    cv::Mat frame;

    {
        std::lock_guard<std::mutex> lock(frameMutexes[id]);
        if (!frames[id].empty()) {
            frame = frames[id].clone();
        }
    }

    return frame;
}