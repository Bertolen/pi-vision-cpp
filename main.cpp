#include <opencv2/opencv.hpp>
#include <civetweb.h>
#include <iostream>
#include <thread>
#include <mutex>

std::mutex frame_mutex;
cv::Mat frame;
bool running = true;

void captureThread() {
    cv::VideoCapture cap(0); // 0 pour la webcam par défaut
    if (!cap.isOpened()) {
        std::cerr << "Erreur : impossible d'ouvrir la caméra." << std::endl;
        running = false;
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
            std::lock_guard<std::mutex> lock(frame_mutex);
            frame = temp_frame.clone();
        }
    }
}

int streamHandler(struct mg_connection *conn, void *cbdata) {
    (void)cbdata; // Pas utilisé

    // En-têtes pour le flux MJPEG
    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
              "Cache-Control: no-cache\r\n"
              "\r\n");

    while (running) {
        std::vector<uchar> buf;
        {
            std::lock_guard<std::mutex> lock(frame_mutex);
            if (!frame.empty()) {
                cv::imencode(".jpg", frame, buf);
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

int main() {
    std::thread capThread(captureThread);

    const char *options[] = {"listening_ports", "8080", nullptr};
    struct mg_callbacks callbacks = {};
    struct mg_context *ctx = mg_start(&callbacks, nullptr, options);

    if (ctx == nullptr) {
        std::cerr << "Erreur : impossible de démarrer le serveur HTTP." << std::endl;
        running = false;
    } else {
        mg_set_request_handler(ctx, "/stream", streamHandler, nullptr);
        std::cout << "Serveur démarré sur http://localhost:8080/stream" << std::endl;
        std::cin.get(); // Attente d'une entrée pour arrêter le serveur
    }

    running = false;
    capThread.join();
    if (ctx) mg_stop(ctx);
    return 0;
}

