cmake_minimum_required(VERSION 3.10)

# Déclaration du projet
project(WebcamStreamer)

# Ajouter les fichiers source
add_executable(WebcamStreamer main.cpp)

# Ajoute le chemin vers les en-têtes de CivetWeb
target_include_directories(WebcamStreamer PRIVATE ${CMAKE_SOURCE_DIR}/civetweb/include)
target_link_libraries(WebcamStreamer ${CMAKE_SOURCE_DIR}/civetweb/libcivetweb.so)

# Lier les bibliothèques nécessaires
find_package(OpenCV REQUIRED)
target_link_libraries(WebcamStreamer ${OpenCV_LIBS} pthread)

