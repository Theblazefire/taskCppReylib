# Usiamo Ubuntu come base per avere compilatori e librerie raylib aggiornate
FROM ubuntu:24.04

# Evita richieste interattive durante l'installazione dei pacchetti
ENV DEBIAN_FRONTEND=noninteractive

# Installa compilatori C++, CMake, Git e dipendenze grafiche/audio di raylib
RUN apt-get update && apt-get install -y \
    build-essential \
    git \
    cmake \
    libasound2-dev \
    libx11-dev \
    libxrandr-dev \
    libxi-dev \
    libgl1-mesa-dev \
    libglu1-mesa-dev \
    libxcursor-dev \
    libxinerama-dev \
    libwayland-dev \
    libxkbcommon-dev \
    mingw-w64 \
    && rm -rf /var/lib/apt/lists/*

# Scarica e compila raylib dal sorgente ufficiale per Linux
WORKDIR /opt
RUN git clone --depth 1 --branch 5.5 https://github.com/raysan5/raylib.git && \
    cd raylib/src && \
    make PLATFORM=PLATFORM_DESKTOP && \
    make install

# Imposta la cartella di lavoro dell'app
WORKDIR /app

# Copia il codice sorgente nel container
COPY task.cpp .

# Compila la versione Linux nativa
RUN g++ task.cpp -o Tasks2_linux -std=c++17 -O2 -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

# Comando di avvio predefinito
CMD ["./Tasks2_linux"]