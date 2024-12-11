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
#include "CalibrationController.hpp"
#include "DisparityController.hpp"
#include "commons.hpp"

bool running = true;

// Gestion du signal d'arrêt
void handleSignal(int signal) {
    //std::cout << "Signal reçu " << signal << std::endl;
    if (signal == SIGINT || signal == SIGTERM) {
        running = false;
        std::cout << "Arrêt du serveur." << std::endl;
    }
}

int main() {
    // Enregistrement du gestionnaire de signal
    signal(SIGTERM, handleSignal);
    signal(SIGINT, handleSignal);

    // Initialise le serveur HTTP
    const char *options[] = {"listening_ports", "8080", nullptr};
    struct mg_callbacks callbacks = {};
    struct mg_context *ctx = mg_start(&callbacks, nullptr, options);

    if (ctx == nullptr) {
        std::cerr << "Erreur : impossible de démarrer le serveur HTTP." << std::endl;
        running = false;
    } else {
        running = true;
        std::cout << "Serveur démarré sur http://localhost:8080/" << std::endl;
    }

    IndexController* indexController = new IndexController(ctx);
    CalibrationController* calibrationController = new CalibrationController(ctx, indexController);
    DisparityController* disparityController = new DisparityController(ctx, indexController);

    // Boucle principale pour maintenir le programme actif
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    delete calibrationController;
    delete disparityController;
    delete indexController;
    if (ctx) mg_stop(ctx);
    return 0;
}
