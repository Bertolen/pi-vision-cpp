#include "CalibrationController.hpp"

// Définition des variables statiques
std::mutex CalibrationController::chessboardMutexes[NB_WEBCAMS];
cv::Mat CalibrationController::chessboards[NB_WEBCAMS];
bool CalibrationController::running;
int CalibrationController::nbImages;
IndexController* CalibrationController::indexCtrl;
cv::Size CalibrationController::boardSize;
int CalibrationController::cameraID[NB_WEBCAMS];

CalibrationController::CalibrationController(struct mg_context* ctx, IndexController* indexCtrl) {

    this->indexCtrl = indexCtrl;

     // C'est le nombre de COINS INTERNES et pas de cases (donc pour 8*6 cases il faut 7*5 coins)
    boardWidth = 7;
    boardHeight = 5;
    squareSize = 0.019f; // Taille réelle des carrés en mètres
    boardSize = cv::Size(boardWidth, boardHeight);

    // Initialise le nombre d'images
    nbImages = nbImagesInMemory();

    // Initialise les identifiants des caméras
    cameraID[0] = 0;
    cameraID[1] = 1;

    // Lance les threads de capture vidéo
    calibThread1 = std::thread(CalibrationController::calibThread, cameraID[0]);
    calibThread2 = std::thread(CalibrationController::calibThread, cameraID[1]);

    // Configure les handlers
    if(ctx == nullptr) {
        std::cerr << "Erreur : impossible de démarrer le serveur HTTP." << std::endl;
        running = false;
    } else {
        mg_set_request_handler(ctx, "/chessboard1", streamHandler, &cameraID[0]);
        mg_set_request_handler(ctx, "/chessboard2", streamHandler, &cameraID[1]);
        mg_set_request_handler(ctx, "/calibrate", buttonHandler, nullptr);
        mg_set_request_handler(ctx, "/calibration", rootHandler, nullptr);
        running = true;
    }
}

CalibrationController::~CalibrationController() {
    running = false;
}

// Thread qui reprends l'image et tente de trouver l'échiquier
void CalibrationController::calibThread(int camID) {
    while(running){
        cv::Mat frame = indexCtrl->getFrameById(camID);
        if(frame.empty()) continue;

        // Conversion de l'image en nuaces de gris
        cv::Mat gray;
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

        // Recherche de l'échiquier
        std::vector<cv::Point2f> corners;
        bool found = cv::findChessboardCorners(gray, boardSize, corners,
                        cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE);

        if(found) {            
            // Affichage de l'échiquier
            cv::cornerSubPix(gray, corners, cv::Size(11, 11), cv::Size(-1, -1),
                             cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.1));
            cv::drawChessboardCorners(gray, boardSize, corners, found);
        }

        {
            std::lock_guard<std::mutex> lock(chessboardMutexes[camID]);
            chessboards[camID] = gray.clone();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30 FPS
    }
}

// Lecture du nombre d'images déj'a présentes par caméra
int CalibrationController::nbImagesInMemoryByCamID(int camID) {
    int cpt = 0;
    std::string directoryPath = "./data/images";

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
int CalibrationController::nbImagesInMemory() {
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

// Gestionnaire de la requête, affiche le flux MJPEG
int CalibrationController::streamHandler(struct mg_connection *conn, void *param) {
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
            std::lock_guard<std::mutex> lock(chessboardMutexes[*cameraID]);
            if (!chessboards[*cameraID].empty()) {
                cv::imencode(".jpg", chessboards[*cameraID], buf);
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

// Enregistrement des images des deux caméras
void CalibrationController::saveFrames() {
    for (int i = 0 ; i < NB_WEBCAMS ; i++) {
        std::string fileName = "data/images/camera" + std::to_string(i) + "-" + std::to_string(nbImages) + ".jpg";
        {
             std::lock_guard<std::mutex> lock(chessboardMutexes[i]);
             cv::imwrite(fileName, chessboards[i]);
        }
    }

    nbImages++;
}

// Gestion du bouton
int CalibrationController::buttonHandler(struct mg_connection *conn, void *param) {
    saveFrames();

    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/plain\r\n\r\n"
              "Success!");
    return 200;
}

// Gestion de la page HTML
int CalibrationController::rootHandler(struct mg_connection *conn, void *param) {
    FILE *file = fopen("resources/calibration.html", "r");
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
