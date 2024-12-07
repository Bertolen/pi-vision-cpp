#include <opencv2/opencv.hpp>
#include <civetweb.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <signal.h>
#include <random>
#include <string>
#include <filesystem>
#include <regex>

namespace fs = std::filesystem;

#define NB_WEBCAMS 2
std::mutex frameMutexes[NB_WEBCAMS];
cv::Mat frames[NB_WEBCAMS];
int cameraDevice[NB_WEBCAMS] = {0, 2};

bool running = true;

int nbImages = 0;

// Thread séparé qui capture le flux des caméras
void captureThread(int camID) {
    cv::VideoCapture cap(cameraDevice[camID]);
    if (!cap.isOpened()) {
        std::cerr << "Erreur : impossible d'ouvrir la caméra. ID = " << camID << std::endl;
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
            std::lock_guard<std::mutex> lock(frameMutexes[camID]);
            frames[camID] = temp_frame.clone();
        }
    }
}

// Gestionnaire de la requête, affiche le flux MJPEG
int streamHandler(struct mg_connection *conn, void *param) {
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
int rootHandler(struct mg_connection *conn, void *param) {
    FILE *file = fopen("index.html", "r");
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

// Enregistrement des images des deux caméras
void saveFrames() {
    for (int i = 0 ; i < NB_WEBCAMS ; i++) {
        std::string fileName = "images/camera" + std::to_string(i) + "-" + std::to_string(nbImages) + ".jpg";
        {
             std::lock_guard<std::mutex> lock(frameMutexes[i]);
             cv::imwrite(fileName, frames[i]);
        }
    }

    nbImages++;
}

// Affiche le nombre d'images prises
std::string printImageCount() {
    return "Image count : " + std::to_string(nbImages);
}

// Gestion de l'affichage du nombre d'images prises
int imageCountHandler(struct mg_connection *conn, void *param) {
    mg_printf(conn,
              "HTTP/1.1 200OK\r\n"
              "Content-Type: text/pain\r\n\r\n"
              "%s",
              printImageCount());
    return 200;
}

// Gestion du bouton
int buttonHandler(struct mg_connection *conn, void *param) {
    saveFrames();

    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/plain\r\n\r\n"
              "Success!");
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

// Lecture du nombre d'images déj'a présentes par caméra
int nbImagesInMemoryByCamID(int camID) {
    int cpt = 0;
    std::string directoryPath = "./images";

    std::regex pattern("^camera" + std::to_string(camID) + ".*\\.jpg$");

    try {
        // Parcourir les fichiers dans le dossier
        for (const auto& entry : fs::directory_iterator(directoryPath)) {
            // Vérifier si l'entrée est un fichier régulier
            if (fs::is_regular_file(entry)) {
                // Extraire le nom du fichier
                std::string filename = entry.path().filename().string();

                // Vérifier si le nom du fichier correspond au modèle
                if (std::regex_match(filename, pattern)) {
                    ++cpt;
                }
            }
        }

        // Afficher le nombre de fichiers correspondants
        return cpt;
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Erreur : " << e.what() << std::endl;
        return -1;
    }
}

// Lecture du nombre de combinaisons d'images déjà présentes
int nbImagesInMemory() {
    if (NB_WEBCAMS < 2) {
        std::cerr << "Erreur : il doit y avoir au moins deux cameras" << std::endl;
        return -1;
    }

    int nbCombinations = nbImagesInMemoryByCamID(0);

    for (int i = 1 ; i < NB_WEBCAMS ; i++) {
        int nbCurrentImages = nbImagesInMemoryByCamID(i);
        if (nbCombinations < nbCurrentImages) {
            nbCombinations = nbCurrentImages;
        }
    }

    std::cout << "Nombre de combinaisons d'images : " << std::to_string(nbCombinations) << std::endl;
    return nbCombinations;
}

int main() {
    // Enregistrement du gestionnaire de signal
    signal(SIGTERM, handleSignal);
    signal(SIGINT, handleSignal);

    // Initialise le nombre d'images
    nbImages = nbImagesInMemory();

    // Initialise les identifiants des caméras
    int cam1ID = 0;
    int cam2ID = 1;

    // Lance le thread de capture vidéo
    std::thread capThread1(captureThread, cam1ID);
    std::thread capThread2(captureThread, cam2ID);

    // Initialise le serveur HTTP
    const char *options[] = {"listening_ports", "8080", nullptr};
    struct mg_callbacks callbacks = {};
    struct mg_context *ctx = mg_start(&callbacks, nullptr, options);

    if (ctx == nullptr) {
        std::cerr << "Erreur : impossible de démarrer le serveur HTTP." << std::endl;
        running = false;
    } else {
        mg_set_request_handler(ctx, "/video1", streamHandler, &cam1ID);
        mg_set_request_handler(ctx, "/video2", streamHandler, &cam2ID);
        mg_set_request_handler(ctx, "/saveFrames", buttonHandler, nullptr);
        mg_set_request_handler(ctx, "/imageCount", imageCountHandler, nullptr);
        mg_set_request_handler(ctx, "/", rootHandler, nullptr);
        std::cout << "Serveur démarré sur http://localhost:8080/" << std::endl;
    }

    // Boucle principale pour maintenir le programme actif
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    capThread1.join();
    //capThread2.join();
    if (ctx) mg_stop(ctx);
    return 0;
}
