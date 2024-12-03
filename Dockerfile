# Étape 1 : Construire l'application
FROM ubuntu:22.04 AS build

# Installer les dépendances nécessaires
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential \
    cmake \
    libopencv-dev \
    wget \
    unzip \
    && rm -rf /var/lib/apt/lists/*

# Télécharger CivetWeb
RUN ln -fs /usr/share/zoneinfo/Europe/Paris /etc/localtime \
    && dpkg-reconfigure -f noninteractive tzdata \
    && wget https://github.com/civetweb/civetweb/archive/refs/heads/master.zip -O civetweb.zip \
    && unzip civetweb.zip \
    && mkdir -p /app \
    && mv civetweb-master /app/civetweb

# Construire CivetWeb
WORKDIR /app/civetweb
RUN make lib WITH_CPP=1 WITH_WEBSOCKET=1 \
    && make clean slib WITH_CPP=1 WITH_WEBSOCKET=1

# Ajouter le code source
WORKDIR /app
COPY . /app

# Construire le projet
WORKDIR /app
RUN cmake . && make

# Étape 2 : Image finale
FROM ubuntu:22.04

# Installer les bibliothèques nécessaires à l'exécution
RUN apt-get update && apt-get install -y \
    libopencv-core-dev \
    libopencv-imgcodecs-dev \
    libopencv-videoio-dev \
    && mkdir -p /app/civetweb \
    && rm -rf /var/lib/apt/lists/* \
    && export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/app/civetweb/

# Copier les binaires construits
WORKDIR /app
COPY --from=build /app/WebcamStreamer /app
COPY --from=build /app/civetweb/libcivetweb.so.1 /app/civetweb
COPY --from=build /app/index.html /app

# Exposer le port HTTP
EXPOSE 8080

# Lancer l'application
ENTRYPOINT ["./WebcamStreamer"]

