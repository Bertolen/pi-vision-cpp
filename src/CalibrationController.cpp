#include "CalibrationController.hpp"

// Définition des variables statiques
std::mutex CalibrationController::chessboardMutexes[NB_WEBCAMS];
cv::Mat CalibrationController::chessboards[NB_WEBCAMS];
bool CalibrationController::running;
bool CalibrationController::capturing;
int CalibrationController::nbImages;
IndexController* CalibrationController::indexCtrl;
cv::Size CalibrationController::boardSize;
int CalibrationController::cameraID[NB_WEBCAMS];
int CalibrationController::boardHeight;
int CalibrationController::boardWidth;
float CalibrationController::squareSize;
std::thread CalibrationController::calibThread1;
std::thread CalibrationController::calibThread2;

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
        mg_set_request_handler(ctx, "/erase", eraseButtonHandler, nullptr);
        mg_set_request_handler(ctx, "/saveFrames", saveButtonHandler, nullptr);
        mg_set_request_handler(ctx, "/calibrate", calibrateButtonHandler, nullptr);
        mg_set_request_handler(ctx, "/calibration", rootHandler, nullptr);
        running = true;
    }

    capturing = true;
}

CalibrationController::~CalibrationController() {
    running = false;
    calibThread1.join();
    calibThread2.join();
}

// Gestion du bouton
int CalibrationController::saveButtonHandler(struct mg_connection *conn, void *param) {
    saveFrames();

    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/plain\r\n\r\n"
              "Success!");
    return 200;
}

// Gestion du bouton
int CalibrationController::eraseButtonHandler(struct mg_connection *conn, void *param) {
    eraseFrames();

    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/plain\r\n\r\n"
              "Success!");
    return 200;
}

// Gestion du bouton
int CalibrationController::calibrateButtonHandler(struct mg_connection *conn, void *param) {
    calibrateCameras();

    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/plain\r\n\r\n"
              "Success!");
    return 200;
}

// Thread qui reprends l'image et tente de trouver l'échiquier
void CalibrationController::calibThread(int camID) {
    while(running && capturing){
        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30 FPS

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
        std::string fileName = "./data/images/camera" + std::to_string(i) + "-" + std::to_string(nbImages) + ".jpg";
        {
             std::lock_guard<std::mutex> lock(chessboardMutexes[i]);
             cv::imwrite(fileName, chessboards[i]);
        }
    }

    nbImages++;
}

// Effacer toutes les images enregistrées
void CalibrationController::eraseFrames() {
    fs::remove_all("data/images/");
    fs::create_directory("data/images/");
    nbImages = 0;
}

// Sauvegarde du fichier de calibration
void CalibrationController::saveCalibration(const std::string& filename,
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
void CalibrationController::calibrateCameras() {
    std::cout << "Début calibration" << std::endl;

    // Stoppe les threads, on n'en a plus besoin
    capturing = false;
    calibThread1.join();
    calibThread2.join();

    std::vector<std::vector<cv::Point3f>> objectPoints;
    std::vector<std::vector<cv::Point2f>> imagePoints1, imagePoints2;
    cv::Mat gray1, gray2;

    std::vector<cv::Point3f> obj;
    for (int i = 0; i < boardHeight; i++) {
        for (int j = 0; j < boardWidth; j++) {
            obj.push_back(cv::Point3f(j * squareSize, i * squareSize, 0));
        }
    }

    for (int i = 0 ; i < nbImages ; i++) {
        std::cout << "Traitement image " << std::to_string(i) << std::endl;

        // Lecture des images enregistrées
        std::string filename1 = "./data/images/camera0-" + std::to_string(i) + ".jpg";
        std::string filename2 = "./data/images/camera1-" + std::to_string(i) + ".jpg";
        cv::Mat frame1 = cv::imread(filename1, 1);
        cv::Mat frame2 = cv::imread(filename2, 1);

        // Conversion en nuance de gris
        cv::cvtColor(frame1, gray1, cv::COLOR_BGR2GRAY);
        cv::cvtColor(frame2, gray2, cv::COLOR_BGR2GRAY);

        // Recherche de l'échiquier
        std::vector<cv::Point2f> corners1, corners2;
        bool found1 = cv::findChessboardCorners(gray1, boardSize, corners1,
                        cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE);
        bool found2 = cv::findChessboardCorners(gray2, boardSize, corners2,
                        cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE);

        if (found1 && found2){
            // Affinage des coins de l'échiquier
            cv::cornerSubPix(gray1, corners1, cv::Size(11, 11), cv::Size(-1, -1),
                             cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.1));
            cv::cornerSubPix(gray2, corners2, cv::Size(11, 11), cv::Size(-1, -1),
                             cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.1));
                             
            // Dessine les coins de l'échiquier sur les images
            cv::drawChessboardCorners(gray1, boardSize, corners1, found1);
            cv::drawChessboardCorners(gray2, boardSize, corners2, found2);

            // Enregistrement des coordonnées dans les tableaux
            imagePoints1.push_back(corners1);
            imagePoints2.push_back(corners2);
            objectPoints.push_back(obj);
        }
        
        // Affichage à l'écran des images avec l'échiquier
        {
            std::lock_guard<std::mutex> lock(chessboardMutexes[0]);
            chessboards[0] = gray1.clone();
        }
        
        {
            std::lock_guard<std::mutex> lock(chessboardMutexes[1]);
            chessboards[1] = gray2.clone();
        }
    }

    // Calibration des caméras
    std::cout << "Calibration caméras" << std::endl;
    std::cout << "Taille nb objets : " << std::to_string(objectPoints.size()) << std::endl;
    std::cout << "Taille échiquiers gauche : " << std::to_string(imagePoints1.size()) << std::endl;
    std::cout << "Taille échiquiers droite : " << std::to_string(imagePoints2.size()) << std::endl;
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

    std::cout << "Calibration terminée et sauvegardée dans './data/calibration/stereo_calib.yml'." << std::endl;
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
