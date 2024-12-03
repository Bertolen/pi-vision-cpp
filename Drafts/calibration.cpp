#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>
#include <string>

/* should calibrate 2 cameras for stereovision */

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

int main() {
    const int boardWidth = 9, boardHeight = 6;
    const float squareSize = 0.025f; // Taille réelle des carrés en mètres
    const int numImages = 20;

    cv::Size boardSize(boardWidth, boardHeight);

    std::vector<std::vector<cv::Point3f>> objectPoints;
    std::vector<std::vector<cv::Point2f>> imagePoints1, imagePoints2;

    std::vector<cv::Point3f> obj;
    for (int i = 0; i < boardHeight; i++) {
        for (int j = 0; j < boardWidth; j++) {
            obj.push_back(cv::Point3f(j * squareSize, i * squareSize, 0));
        }
    }

    cv::VideoCapture cap1(0), cap2(1); // Deux webcams
    if (!cap1.isOpened() || !cap2.isOpened()) {
        std::cerr << "Erreur : impossible d'ouvrir les caméras." << std::endl;
        return -1;
    }

    int captured = 0;
    cv::Mat frame1, frame2, gray1, gray2;
    while (captured < numImages) {
        cap1 >> frame1;
        cap2 >> frame2;

        if (frame1.empty() || frame2.empty()) break;

        cv::imshow("Caméra 1", frame1);
        cv::imshow("Caméra 2", frame2);

        char key = cv::waitKey(1);
        if (key == 'c') { // Appuyer sur 'c' pour capturer
            std::vector<cv::Point2f> corners1, corners2;

            cv::cvtColor(frame1, gray1, cv::COLOR_BGR2GRAY);
            cv::cvtColor(frame2, gray2, cv::COLOR_BGR2GRAY);

            bool found1 = cv::findChessboardCorners(gray1, boardSize, corners1,
                            cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE);
            bool found2 = cv::findChessboardCorners(gray2, boardSize, corners2,
                            cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE);

            if (found1 && found2) {
                cv::cornerSubPix(gray1, corners1, cv::Size(11, 11), cv::Size(-1, -1),
                                 cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.1));
                cv::cornerSubPix(gray2, corners2, cv::Size(11, 11), cv::Size(-1, -1),
                                 cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.1));

                imagePoints1.push_back(corners1);
                imagePoints2.push_back(corners2);
                objectPoints.push_back(obj);

                captured++;
                std::cout << "Images capturées : " << captured << "/" << numImages << std::endl;
            } else {
                std::cout << "Échec de la détection de l'échiquier sur l'une des caméras." << std::endl;
            }
        }

        if (key == 27) break; // Appuyer sur 'ESC' pour quitter
    }

    cv::destroyAllWindows();

    // Calibration des caméras
    cv::Mat cameraMatrix1, distCoeffs1, cameraMatrix2, distCoeffs2;
    cv::Mat R, T, E, F;

    cv::calibrateCamera(objectPoints, imagePoints1, gray1.size(), cameraMatrix1, distCoeffs1, cv::noArray(), cv::noArray());
    cv::calibrateCamera(objectPoints, imagePoints2, gray2.size(), cameraMatrix2, distCoeffs2, cv::noArray(), cv::noArray());
    cv::stereoCalibrate(objectPoints, imagePoints1, imagePoints2,
                        cameraMatrix1, distCoeffs1,
                        cameraMatrix2, distCoeffs2,
                        gray1.size(), R, T, E, F,
                        cv::CALIB_FIX_INTRINSIC,
                        cv::TermCriteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 100, 1e-5));

    // Sauvegarder les paramètres de calibration
    saveCalibration("stereo_calib.yml", cameraMatrix1, distCoeffs1, cameraMatrix2, distCoeffs2, R, T);

    std::cout << "Calibration terminée et sauvegardée dans 'stereo_calib.yml'." << std::endl;
    return 0;
}
