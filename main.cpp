#include <opencv2/opencv.hpp>
#include <civetweb.h>
#include <vector>
#include <mutex>
#include <thread>

std::mutex frame_mutex;
cv::Mat current_frame;

// Fonction pour capturer le flux vidéo
void captureVideo() {
    cv::VideoCapture cap(0); // Utilise la caméra par défaut
    if (!cap.isOpened()) {
        std::cerr << "Erreur : Impossible d'ouvrir la webcam." << std::endl;
        return;
    }

    while (true) {
        cv::Mat frame;
        cap >> frame; // Capture une image
        if (frame.empty()) {
            std::cerr << "Erreur: image vide." << std::endl;
            break;
        }

        std::lock_guard<std::mutex> lock(frame_mutex);
        current_frame = frame.clone(); // Sauvegarde l'image actuelle
    }
}

// Gestionnaire HTTP pour servir l'image
int httpHandler(struct mg_connection *conn, void *) {
    std::vector<uchar> buf;
    {
        std::lock_guard<std::mutex> lock(frame_mutex);
        if (!current_frame.empty()) {
            cv::imencode(".jpg", current_frame, buf); // Encode l'image en JPEG
        }
    }

    if (buf.empty()) {
        mg_printf(conn, "HTTP/1.1 503 Service Unavailable\r\n\r\n");
        return 503;
    }

    mg_printf(conn, "HTTP/1.1 200 OK\r\n");
    mg_printf(conn, "Content-Type: image/jpeg\r\n");
    mg_printf(conn, "Content-Length: %zu\r\n\r\n", buf.size());
    mg_write(conn, buf.data(), buf.size()); // Envoie l'image au client
    return 200;
}

int main() {
    const char *options[] = {"listening_ports", "8080", nullptr};
    mg_context *ctx;
    mg_callbacks callbacks = {};

    // Initialisation de CivetWeb
    ctx = mg_start(&callbacks, nullptr, options);
    if (!ctx) {
        std::cerr << "Erreur : Impossible de démarrer le serveur HTTP." << std::endl;
        return 1;
    }

    // Enregistre le gestionnaire pour la route "/"
    mg_set_request_handler(ctx, "/", httpHandler, nullptr);

    // Lancer la capture vidéo dans un thread séparé
    std::thread video_thread(captureVideo);
    video_thread.detach();

    std::cout << "Serveur démarré sur le port 8080." << std::endl;

    // Maintenir le serveur en cours d'exécution
    std::cin.get();
    mg_stop(ctx);
    return 0;
}

