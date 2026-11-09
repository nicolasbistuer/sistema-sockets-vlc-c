// Cliente: recibe los videos que sube el servidor y pregunta dónde guardarlos ("Guardar como").
#include <winsock2.h>  // Sockets de Windows (siempre antes que windows.h)
#include <windows.h>
#include <commdlg.h>   // Diálogo "Guardar como"
#include <commctrl.h>  // Barra de progreso
#include <stdio.h>
#include <string.h>
#include "protocolo.h"
#include "reproductor.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "comctl32.lib")

#define IP_SERVIDOR "127.0.0.1" // Cambiar por la IP de la PC servidor si se prueba en red
#define PUERTO 8080
#define BUFFER_SIZE 16384

#define BTN_ABRIR_REP   102
#define WM_APP_PEDIR_RUTA (WM_APP + 1) // Mensaje interno: el hilo de red pide al hilo de la ventana el "Guardar como"

SOCKET sock = INVALID_SOCKET;
volatile BOOL servidorConectado = FALSE;
HWND hVentana, hEstadoConexion, hEstadoAccion, hProgreso, hBtnReproducir;
char ultimoVideo[MAX_PATH] = ""; // Ruta del último video descargado

// Datos que viajan del hilo de red al diálogo "Guardar como"
typedef struct {
    char nombre[MAX_PATH]; // Nombre sugerido (ya limpio)
    char ruta[MAX_PATH];   // Ruta elegida por el usuario (salida)
} PeticionGuardado;

void ActualizarEstadoConexion(BOOL conectado, const char* texto) {
    servidorConectado = conectado;
    SetWindowTextA(hEstadoConexion, texto);
}

// Deja solo el nombre del archivo (sin carpetas) y reemplaza caracteres no válidos en Windows.
// Evita que el servidor pueda mandar nombres como "..\\..\\algo".
void LimpiarNombre(const char* original, char* destino, size_t max) {
    const char* base = original;
    const char* p;
    for (p = original; *p; p++) if (*p == '\\' || *p == '/') base = p + 1;
    strncpy(destino, base, max - 1);
    destino[max - 1] = '\0';
    for (p = destino; *p; p++) {
        if (strchr("<>:\"|?*", *p)) *(char*)p = '_';
    }
    if (destino[0] == '\0') strncpy(destino, "video_recibido", max - 1);
}

// Hilo de red: se conecta (con reconexión automática) y recibe las ofertas del servidor
DWORD WINAPI HiloRed(LPVOID lpParam) {
    WSADATA wsa;
    struct sockaddr_in server;

    WSAStartup(MAKEWORD(2, 2), &wsa);
    server.sin_addr.s_addr = inet_addr(IP_SERVIDOR);
    server.sin_family = AF_INET;
    server.sin_port = htons(PUERTO);

    while (1) {
        ActualizarEstadoConexion(FALSE, "CONEXION: DESCONECTADO (Buscando servidor...)");
        sock = socket(AF_INET, SOCK_STREAM, 0);

        if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
            closesocket(sock);
            Sleep(3000); // Reintenta cada 3 segundos
            continue;
        }

        CabeceraPaquete saludo;
        memset(&saludo, 0, sizeof(saludo));
        saludo.tipo = MSG_ESTADO;
        strcpy(saludo.nombre, "CLIENTE_LISTO");
        send(sock, (char*)&saludo, sizeof(saludo), 0);
        ActualizarEstadoConexion(TRUE, "CONEXION: CONECTADO AL SERVIDOR");

        CabeceraPaquete cab;
        char buffer[BUFFER_SIZE];

        while (recv(sock, (char*)&cab, sizeof(cab), MSG_WAITALL) == (int)sizeof(cab)) {
            if (cab.tipo == MSG_ESTADO) {
                ActualizarEstadoConexion(TRUE, "CONEXION: CONECTADO AL SERVIDOR (Confirmado)");
            }
            else if (cab.tipo == MSG_ARCHIVO) {
                cab.nombre[sizeof(cab.nombre) - 1] = '\0';

                // 1) Pregunta dónde guardarlo (el diálogo se abre en el hilo de la ventana)
                PeticionGuardado pet;
                memset(&pet, 0, sizeof(pet));
                LimpiarNombre(cab.nombre, pet.nombre, sizeof(pet.nombre));
                strcpy(pet.ruta, pet.nombre);
                SetWindowTextA(hEstadoAccion, "El servidor quiere enviarte un video. Elegi donde guardarlo...");
                BOOL acepta = (BOOL)SendMessageA(hVentana, WM_APP_PEDIR_RUTA, 0, (LPARAM)&pet);

                FILE* f = acepta ? fopen(pet.ruta, "wb") : NULL;
                CabeceraPaquete resp;
                memset(&resp, 0, sizeof(resp));
                if (!f) {   // Canceló el diálogo o no se pudo crear el archivo
                    resp.tipo = MSG_RECHAZAR;
                    send(sock, (char*)&resp, sizeof(resp), 0);
                    SetWindowTextA(hEstadoAccion, acepta ? "No se pudo crear el archivo en esa ubicacion." : "Descarga cancelada.");
                    continue;
                }
                resp.tipo = MSG_ACEPTAR;
                send(sock, (char*)&resp, sizeof(resp), 0);

                // 2) Recibe el video por bloques y lo escribe en la ruta elegida
                SetWindowTextA(hEstadoAccion, "Descargando video del servidor...");
                SendMessageA(hProgreso, PBM_SETPOS, 0, 0);
                uint64_t totalRecibido = 0;
                while (totalRecibido < cab.tamano) {
                    int porLeer = (cab.tamano - totalRecibido < BUFFER_SIZE) ? (int)(cab.tamano - totalRecibido) : BUFFER_SIZE;
                    int leidos = recv(sock, buffer, porLeer, 0);
                    if (leidos <= 0) break;
                    fwrite(buffer, 1, leidos, f);
                    totalRecibido += leidos;
                    SendMessageA(hProgreso, PBM_SETPOS, (WPARAM)((totalRecibido * 100) / cab.tamano), 0);
                }
                fclose(f);

                if (totalRecibido == cab.tamano) {
                    strcpy(ultimoVideo, pet.ruta);
                    SendMessageA(hProgreso, PBM_SETPOS, 100, 0);
                    char msg[MAX_PATH + 40];
                    sprintf(msg, "Video guardado en: %s", pet.ruta);
                    SetWindowTextA(hEstadoAccion, msg);
                } else {
                    DeleteFileA(pet.ruta); // Descarga incompleta: no deja un archivo roto
                    SetWindowTextA(hEstadoAccion, "La descarga se interrumpio. Se elimino el archivo incompleto.");
                }
            }
        }

        closesocket(sock);
        sock = INVALID_SOCKET;
    }

    WSACleanup();
    return 0;
}

// Muestra el diálogo "Guardar como" (se ejecuta en el hilo de la ventana)
BOOL PedirRutaGuardado(HWND hwnd, PeticionGuardado* pet) {
    static char extension[16];
    const char* punto = strrchr(pet->nombre, '.');
    strncpy(extension, (punto && punto[1]) ? punto + 1 : "mp4", sizeof(extension) - 1);
    extension[sizeof(extension) - 1] = '\0';

    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE); // Si estaba minimizado, lo muestra

    OPENFILENAMEA ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = pet->ruta;              // Entrada: nombre sugerido. Salida: ruta completa elegida
    ofn.nMaxFile = sizeof(pet->ruta);
    ofn.lpstrFilter = "Videos\0*.mp4;*.mkv;*.avi;*.mov;*.wmv\0Todos los archivos\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = extension;
    ofn.lpstrTitle = "Guardar video como";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetSaveFileNameA(&ofn) ? TRUE : FALSE;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
        hVentana = hwnd;
        hBtnReproducir = CreateWindowA("BUTTON", "Reproducir ultimo video descargado", WS_VISIBLE | WS_CHILD, 20, 15, 260, 35, hwnd, (HMENU)BTN_ABRIR_REP, NULL, NULL);
        hEstadoConexion = CreateWindowA("STATIC", "CONEXION: INICIANDO...", WS_VISIBLE | WS_CHILD | SS_ENDELLIPSIS, 20, 60, 560, 20, hwnd, NULL, NULL, NULL);
        hEstadoAccion = CreateWindowA("STATIC", "Estado: Esperando videos del servidor...", WS_VISIBLE | WS_CHILD | SS_ENDELLIPSIS, 20, 85, 560, 20, hwnd, NULL, NULL, NULL);
        hProgreso = CreateWindowExA(0, PROGRESS_CLASSA, NULL, WS_VISIBLE | WS_CHILD | PBS_SMOOTH, 20, 110, 560, 20, hwnd, NULL, NULL, NULL);
        SendMessageA(hProgreso, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        {
            HANDLE h = CreateThread(NULL, 0, HiloRed, NULL, 0, NULL);
            if (h) CloseHandle(h);
        }
        break;

    case WM_APP_PEDIR_RUTA:   // Lo manda el hilo de red; el hilo de red espera hasta que esto termine
        return PedirRutaGuardado(hwnd, (PeticionGuardado*)lParam);

    case WM_COMMAND:
        if (LOWORD(wParam) == BTN_ABRIR_REP) {
            if (strlen(ultimoVideo) > 0) AbrirReproductorInterno(hwnd, ultimoVideo);
            else MessageBoxA(hwnd, "Aun no se ha descargado ningun video.", "Reproductor", MB_ICONWARNING);
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icex);

    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "ClienteVLCClass";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowA("ClienteVLCClass", "Cliente de Video (recibe videos)", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX | WS_VISIBLE,
        690, 50, 615, 200, NULL, NULL, hInstance, NULL);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}
