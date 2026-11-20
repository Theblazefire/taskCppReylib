================================================================================
  TASKS 2 (C++ & raylib)
================================================================================

Un'applicazione desktop leggera e veloce per la gestione visiva di task e
sotto-task a colonne, sviluppata in C++17 con la libreria grafica raylib
(zero dipendenze esterne).


--------------------------------------------------------------------------------
  SCOPO DEL PROGETTO E CREDITI
--------------------------------------------------------------------------------

* Scopo didattico:
  Questo progetto e stato realizzato a scopo di studio per imparare e mettere
  in pratica il linguaggio C++ e la libreria grafica raylib, partendo da
  conoscenze di base in linguaggio C.

* Ispirazione e Autore Originale:
  L'interfaccia, il design a colonne e le meccaniche di questa applicazione sono
  stati copiati e reimplementati prendendo spunto dal software gratuito
  "Tasks 2" (sviluppato originariamente in Godot Engine) mostrato in questo
  video YouTube:
  https://youtu.be/Kk-ofP6AuPk?si=UIkTqUR5l8G4G-xR

  Autore originale del progetto: ENDRITDEV
  - Canale YouTube: @EndritsHub (endritDev - Yellow Hat Games)
  - Pagina progetto su itch.io: https://endritdev.itch.io/tasks

* Sviluppo:
  Progetto ricreato in C++ e raylib con Gemini Neon.


--------------------------------------------------------------------------------
  FUNZIONALITA PRINCIPALI
--------------------------------------------------------------------------------

* Layout a colonne orizzontali:
  Ogni MainTask rappresenta una colonna indipendente affiancata alle altre, con
  un pulsante rapido (+) per aggiungere nuove colonne in qualsiasi momento.

* Sistema di stati a 5 colori:
  Cliccando sulla pillola verticale a destra di ogni box (oppure con il tasto
  destro del mouse) puoi cambiare lo stato visivo tra:
  [Grigio/Neutro] -> [Rosso] -> [Arancione] -> [Giallo] -> [Verde]

* Barra di avanzamento dinamica:
  Ogni colonna mostra una barra superiore il cui colore si aggiorna in tempo
  reale in base allo stato della MainTask e delle sue SubTask.

* Sotto-task con Drag & Drop:
  Aggiungi sotto-task tramite il pulsante "+ Subtask" e trascinale con il
  mouse per riordinarle o spostarle da una colonna all'altra.

* Testo multilinea senza limiti:
  I box si ridimensionano automaticamente in altezza in base al contenuto
  (supporta Shift+Invio per andare a capo e Ctrl+V per incollare).

* #Hashtag e Ricerca istantanea:
  Scrivendo "#tag" seguito da Spazio dentro una MainTask, il tag viene estratto
  automaticamente sotto la colonna. Usa la barra di ricerca in alto (o clicca
  su un tag) per filtrare le colonne.

* Importazione ed Esportazione JSON:
  - Salvataggio automatico continuo in "tasks2_project.json".
  - Pulsanti "Esporta JSON" e "Importa JSON" con finestra di dialogo nativa.
  - Supporto Drag & Drop: trascina un file .json direttamente sulla finestra
    per caricare il progetto.


--------------------------------------------------------------------------------
  STRUTTURA DEL PROGETTO
--------------------------------------------------------------------------------

  task.cpp              Codice sorgente completo (UI, layout, parser JSON)
  Dockerfile            Configurazione container Ubuntu 24.04 + raylib 5.5
  docker-compose.yml    Avvio rapido tramite Docker Compose
  tasks2_project.json   File di salvataggio automatico locale (generato all'avvio)
  README.txt            Documentazione del progetto


--------------------------------------------------------------------------------
  COMPILAZIONE ED ESECUZIONE
--------------------------------------------------------------------------------

### 1. Windows (MinGW-w64 / w64devkit)

Compilazione standard:
  g++ task.cpp -o Tasks2_raylib.exe -std=c++17 -O2 -lraylib -lopengl32 -lgdi32 -lwinmm

Compilazione STATICA (consigliata per spostare l'.exe su altri PC Windows
senza dover installare DLL aggiuntive):
  g++ task.cpp -o Tasks2_raylib.exe -std=c++17 -O2 -lraylib -lopengl32 -lgdi32 -lwinmm -static -static-libgcc -static-libstdc++

Per nascondere la finestra del terminale all'avvio, aggiungi il flag:
  -mwindows

Avvio:
  .\Tasks2_raylib.exe


### 2. Linux (Ubuntu / Debian)

Compilazione con raylib 5+ installata:
  g++ task.cpp -o Tasks2_linux -std=c++17 -O2 -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

Avvio:
  ./Tasks2_linux


### 3. Docker & Docker Compose

Costruzione dell'immagine:
  docker build -t tasks2-raylib .

Avvio con Docker Compose:
  docker compose up


--------------------------------------------------------------------------------
  GUIDA AI CONTROLLI
--------------------------------------------------------------------------------

  Click sinistro su un box       Modifica il testo del task / sotto-task
  Invio / ESC                    Conferma ed esce dalla modifica testo
  Shift + Invio                  Inserisce una nuova riga nel box
  Ctrl + V                       Incolla testo dagli appunti
  Click su pillola destra        Cambia colore / stato del box
  Click destro su un box         Cambia colore / stato del box
  Trascina una SubTask           Sposta o riordina tra le colonne
  Rotella del mouse              Scorre la board orizzontalmente
  Shift + Rotella del mouse      Scorre la board verticalmente
  Click centrale + trascina      Pan libero della board
  Trascina file .json            Importa il progetto nella board


--------------------------------------------------------------------------------
  FORMATO FILE JSON
--------------------------------------------------------------------------------

I progetti esportati seguono questa struttura leggibile e modificabile:

  {
    "title": "Nome Progetto",
    "version": "0.2",
    "tasks": [
      {
        "text": "Titolo MainTask",
        "status": 1,
        "tags": ["ui", "v1"],
        "subtasks": [
          { "text": "Prima sotto-task", "status": 4 },
          { "text": "Seconda sotto-task", "status": 2 }
        ]
      }
    ]
  }


--------------------------------------------------------------------------------
  TECNOLOGIE UTILIZZATE
--------------------------------------------------------------------------------

* C++17 (STL: vector, string, fstream, cmath)
* raylib 5.x (rendering 2D, gestione input, finestra ridimensionabile)
* Win32 API (comdlg32 caricata a runtime per i dialoghi Apri/Salva file)
* Docker (containerizzazione ambiente di build ed esecuzione)