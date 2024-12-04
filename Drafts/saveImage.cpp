#include <opencv2/opencv.hpp>
#include <iostream>

int main() {
    // Ouvrir la webcam (index 0 par défaut)
    cv::VideoCapture cap(0);

    // Vérifier si la webcam s'est ouverte correctement
    if (!cap.isOpened()) {
        std::cerr << "Erreur : Impossible d'ouvrir la webcam !" << std::endl;
        return -1;
    }

    // Créer une matrice pour stocker l'image capturée
    cv::Mat frame;

    // Lire une image de la webcam
    cap >> frame;

    // Vérifier si l'image a été capturée avec succès
    if (frame.empty()) {
        std::cerr << "Erreur : Impossible de capturer une image !" << std::endl;
        return -1;
    }

    // Afficher l'image capturée dans une fenêtre
    cv::imshow("Image capturée", frame);

    // Enregistrer l'image capturée sur le disque
    std::string filename = "image_captured.jpg";
    if (cv::imwrite(filename, frame)) {
        std::cout << "Image enregistrée avec succès sous le nom : " << filename << std::endl;
    } else {
        std::cerr << "Erreur : Impossible d'enregistrer l'image !" << std::endl;
    }

    // Attendre que l'utilisateur appuie sur une touche pour fermer la fenêtre
    cv::waitKey(0);

    // Libérer la ressource webcam
    cap.release();
    cv::destroyAllWindows();

    return 0;
}

