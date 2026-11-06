
#ifndef REPRODUCTOR_H
#define REPRODUCTOR_H

#include <windows.h>   // Cabecera principal de la API de Windows
#include <commctrl.h>  // Requerido para controles avanzados como el Trackbar/Slider
#include <vlc/vlc.h>   // Cabecera principal de libVLC
#include <stdio.h>     // Entrada y salida estándar para funciones de texto

#pragma comment(lib, "libvlc.lib")    // Enlaza automáticamente la librería de desarrollo de VLC
#pragma comment(lib, "comctl32.lib")  // Enlaza la librería de controles comunes de Windows

// Identificadores de control únicos para los clics en la interfaz multimedia
#define ID_PLAYPAUSA   501
#define ID_REINICIAR   502
#define ID_RETRO_10S   503
#define ID_AVAN_10S    504
#define ID_VOLUMEN     505
#define ID_PANT_COMP   506

// Variables globales del reproductor (estáticas para limitar su alcance a este archivo)
static libvlc_instance_t* vlcInstRep = NULL;    // Puntero a la instancia base del motor de VLC
static libvlc_media_player_t* vlcPlayRep = NULL; // Puntero al reproductor multimedia de VLC
static HWND hLienzoVideo = NULL;                 // Manejador de la ventana estática donde se dibuja el video
static char rutaVideoLocal[MAX_PATH] = "";       // Buffer para almacenar la ruta en disco del video cargado

// Función callback (Procedimiento) que procesa los eventos de la ventana del reproductor
LRESULT CALLBACK ReproductorWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: { // Evento que se ejecuta al crear la ventana por primera vez
        INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES }; // Configura la inicialización de barras
        InitCommonControlsEx(&icex); // Registra las clases de controles comunes de Windows (Trackbar)

        // Crea un control estático que servirá como pantalla negra / lienzo para el video
        hLienzoVideo = CreateWindowA("STATIC", "", WS_VISIBLE | WS_CHILD | WS_BORDER,
            10, 10, 560, 320, hwnd, NULL, NULL, NULL);

        // Botón de Play/Pausa alternable
        CreateWindowA("BUTTON", "Play / Pausa", WS_VISIBLE | WS_CHILD, 10, 345, 95, 30, hwnd, (HMENU)ID_PLAYPAUSA, NULL, NULL);
        // Botón para detener y volver al inicio
        CreateWindowA("BUTTON", "Reiniciar", WS_VISIBLE | WS_CHILD, 110, 345, 75, 30, hwnd, (HMENU)ID_REINICIAR, NULL, NULL);
        // Botón para saltar 10 segundos atrás
        CreateWindowA("BUTTON", "-10s", WS_VISIBLE | WS_CHILD, 190, 345, 50, 30, hwnd, (HMENU)ID_RETRO_10S, NULL, NULL);
        // Botón para saltar 10 segundos adelante
        CreateWindowA("BUTTON", "+10s", WS_VISIBLE | WS_CHILD, 245, 345, 50, 30, hwnd, (HMENU)ID_AVAN_10S, NULL, NULL);

        // Texto estático indicador de volumen
        CreateWindowA("STATIC", "Vol:", WS_VISIBLE | WS_CHILD, 305, 352, 30, 20, hwnd, NULL, NULL, NULL);
        // Barra deslizante (Trackbar) para el volumen de audio
        HWND hVolSlider = CreateWindowExA(0, TRACKBAR_CLASSA, "", WS_VISIBLE | WS_CHILD | TBS_AUTOTICKS | TBS_HORZ,
            335, 345, 130, 30, hwnd, (HMENU)ID_VOLUMEN, NULL, NULL);
        SendMessageA(hVolSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100)); // Fija rango del slider de 0 a 100
        SendMessageA(hVolSlider, TBM_SETPOS, TRUE, 80); // Establece la posición inicial del volumen al 80%

        // Botón para pasar a Pantalla Completa
        CreateWindowA("BUTTON", "[ F ]", WS_VISIBLE | WS_CHILD, 475, 345, 95, 30, hwnd, (HMENU)ID_PANT_COMP, NULL, NULL);

        // Arranca el motor libVLC de forma embebida
        vlcInstRep = libvlc_new(0, NULL); // Inicializa una instancia limpia de VLC
        if (vlcInstRep) {
            vlcPlayRep = libvlc_media_player_new(vlcInstRep); // Crea el reproductor de medios asociado
            libvlc_media_t* media = libvlc_media_new_path(vlcInstRep, rutaVideoLocal); // Abre el archivo desde el disco
            libvlc_media_player_set_media(vlcPlayRep, media); // Asigna el medio cargado al reproductor
            libvlc_media_release(media); // Libera el descriptor de medio ya asignado
            libvlc_media_player_set_hwnd(vlcPlayRep, hLienzoVideo); // Redirecciona la salida gráfica de VLC al HWND de Windows
            libvlc_audio_set_volume(vlcPlayRep, 80); // Sincroniza el volumen de VLC al 80%
            libvlc_media_player_play(vlcPlayRep); // Inicia la reproducción automática del video
        }
        break;
    }

    case WM_HSCROLL: { // Evento que se dispara al desplazar barras horizontales (Volumen)
        if (LOWORD(wParam) != TB_THUMBTRACK && LOWORD(wParam) != TB_ENDTRACK) { // Ignora pasos intermedios de renderizado
            HWND hSlider = (HWND)lParam; // Obtiene el manejador del control que disparó el mensaje
            int pos = (int)SendMessageA(hSlider, TBM_GETPOS, 0, 0); // Consulta la posición entera del Slider
            if (vlcPlayRep) libvlc_audio_set_volume(vlcPlayRep, pos); // Ajusta dinámicamente el volumen del decodificador de VLC
        }
        break;
    }

    case WM_COMMAND: // Evento que intercepta los clics en los botones
        if (!vlcPlayRep) break; // Si el reproductor no está inicializado, no procesa comandos
        switch (LOWORD(wParam)) {
        case ID_PLAYPAUSA:
            libvlc_media_player_pause(vlcPlayRep); // Pausa el video si está en reproducción, o viceversa
            break;
        case ID_REINICIAR:
            libvlc_media_player_stop(vlcPlayRep);  // Detiene el flujo binario
            libvlc_media_player_play(vlcPlayRep);  // Vuelve a iniciar desde el milisegundo cero
            break;
        case ID_RETRO_10S: {
            libvlc_time_t t = libvlc_media_player_get_time(vlcPlayRep); // Obtiene el tiempo actual en milisegundos
            libvlc_media_player_set_time(vlcPlayRep, (t - 10000 < 0) ? 0 : t - 10000); // Resta 10s asegurando no ser < 0
            break;
        }
        case ID_AVAN_10S: {
            libvlc_time_t t = libvlc_media_player_get_time(vlcPlayRep); // Obtiene el tiempo actual en milisegundos
            libvlc_media_player_set_time(vlcPlayRep, t + 10000); // Adelanta el puntero 10 segundos (10,000 ms)
            break;
        }
        case ID_PANT_COMP:
            libvlc_toggle_fullscreen(vlcPlayRep); // Conmuta nativamente el modo pantalla completa de libVLC
            break;
        }
        break;

    case WM_CLOSE: // Evento que captura el cierre de la ventana flotante (clic en la X)
        if (vlcPlayRep) { libvlc_media_player_stop(vlcPlayRep); libvlc_media_player_release(vlcPlayRep); vlcPlayRep = NULL; } // Destruye el reproductor
        if (vlcInstRep) { libvlc_release(vlcInstRep); vlcInstRep = NULL; } // Libera la instancia del motor de VLC
        DestroyWindow(hwnd); // Fuerza la destrucción total del objeto ventana en Windows
        break;
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam); // Delega los mensajes no manejados al sistema operativo
}

// Función expuesta que registra la clase de ventana y despliega la UI flotante
void AbrirReproductorInterno(HWND hwndPadre, const char* rutaArchivo) {
    strncpy(rutaVideoLocal, rutaArchivo, MAX_PATH - 1); // Almacena de manera segura la ruta del video en la variable global

    WNDCLASSA wc = { 0 }; // Inicializa estructura para registrar la nueva clase de ventana
    wc.lpfnWndProc = ReproductorWndProc; // Enlaza el procedimiento de eventos del reproductor
    wc.hInstance = GetModuleHandle(NULL); // Obtiene el descriptor del proceso ejecutable actual
    wc.lpszClassName = "VentanaReproductorClass"; // Nombre identificador de la clase
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); // Aplica el color gris estándar del sistema al fondo de la ventana
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); // Carga el cursor de flecha clásico
    RegisterClassA(&wc); // Registra la clase en el sistema operativo Windows

    // Crea la ventana flotante bajo estilo POPUP acoplada pero independiente del panel principal de red
    HWND hRepWnd = CreateWindowExA(WS_EX_TOPMOST, "VentanaReproductorClass", "Reproductor Integrado",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 595, 430, hwndPadre, NULL, wc.hInstance, NULL);
}

#endif // Fin de REPRODUCTOR_H

