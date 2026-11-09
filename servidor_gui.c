// Servidor: es el ÚNICO que sube videos. Elige un archivo y se lo ofrece al cliente.
#include <winsock2.h>  // Sockets de Windows (siempre antes que windows.h)
#include <windows.h>   // API base de Windows
#include <commdlg.h>   // Diálogo "Abrir archivo"
#include <commctrl.h>  // Barra de progreso
#include <stdio.h>
#include <string.h>
#include "protocolo.h"
#include "reproductor.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "comctl32.lib")

#define PUERTO 8080        // Puerto TCP de escucha
#define BUFFER_SIZE 16384  // Bloques de 16 KB

#define BTN_ENVIAR      101
#define BTN_ABRIR_REP   102

SOCKET client_socket = INVALID_SOCKET;     // Socket del cliente conectado
volatile BOOL clienteConectado = FALSE;    // Hay un cliente conectado
volatile BOOL enviando = FALSE;            // Hay una transferencia en curso
HWND hEstadoConexion, hEstadoAccion, hProgreso, hBtnEnviar;
char ultimoVideo[MAX_PATH] = "";           // Último video enviado (para reproducirlo)
char rutaEnvioPendiente[MAX_PATH] = "";    // Video elegido para enviar

// Sincronización: el hilo de red (que lee del socket) avisa al hilo de envío
// qué contestó el cliente (MSG_ACEPTAR o MSG_RECHAZAR).
HANDLE evRespuesta = NULL;
volatile int respuestaCliente = 0;         // 0 = sin respuesta / cliente desconectado

// Actualiza el texto de conexión y habilita el botón solo si hay cliente y no se está enviando
void ActualizarEstadoConexion(BOOL conectado, const char* texto) {
    clienteConectado = conectado;
    SetWindowTextA(hEstadoConexion, texto);
    EnableWindow(hBtnEnviar, conectado && !enviando);
}

// Termina un envío: libera el botón y muestra un mensaje final
void FinEnvio(const char* mensaje) {
    enviando = FALSE;
    SetWindowTextA(hEstadoAccion, mensaje);
    EnableWindow(hBtnEnviar, clienteConectado);
}

// Hilo de envío: ofrece el video, espera la respuesta del cliente y, si acepta, lo transmite
DWORD WINAPI HiloEnviarArchivo(LPVOID lpParam) {
    FILE* f = fopen(rutaEnvioPendiente, "rb");
    if (!f) { FinEnvio("No se pudo abrir el archivo elegido."); return 1; }

    _fseeki64(f, 0, SEEK_END);
    uint64_t tamano = (uint64_t)_ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);

    const char* nombreSolo = strrchr(rutaEnvioPendiente, '\\');
    nombreSolo = nombreSolo ? nombreSolo + 1 : rutaEnvioPendiente;

    CabeceraPaquete cab;
    memset(&cab, 0, sizeof(cab));
    cab.tipo = MSG_ARCHIVO;
    strncpy(cab.nombre, nombreSolo, sizeof(cab.nombre) - 1);
    cab.tamano = tamano;

    SendMessageA(hProgreso, PBM_SETPOS, 0, 0);
    SetWindowTextA(hEstadoAccion, "Ofreciendo video al cliente... esperando que elija donde guardarlo.");

    // 1) Ofrece el archivo (solo nombre y tamaño) y espera la respuesta
    SOCKET s = client_socket;
    respuestaCliente = 0;
    ResetEvent(evRespuesta);
    if (send(s, (char*)&cab, sizeof(cab), 0) <= 0) { fclose(f); FinEnvio("Error de red al ofrecer el video."); return 1; }

    WaitForSingleObject(evRespuesta, INFINITE);   // El hilo de red lo despierta con la respuesta
    if (respuestaCliente != MSG_ACEPTAR) {
        fclose(f);
        FinEnvio(respuestaCliente == MSG_RECHAZAR ? "El cliente cancelo la descarga." : "El cliente se desconecto.");
        return 0;
    }

    // 2) El cliente aceptó: transmite el video por bloques
    SetWindowTextA(hEstadoAccion, "Enviando video...");
    char buffer[BUFFER_SIZE];
    size_t bytesLeidos;
    uint64_t totalEnviado = 0;
    BOOL completo = TRUE;
    while ((bytesLeidos = fread(buffer, 1, BUFFER_SIZE, f)) > 0) {
        int enviados = send(s, buffer, (int)bytesLeidos, 0);
        if (enviados <= 0) { completo = FALSE; break; }
        totalEnviado += enviados;
        SendMessageA(hProgreso, PBM_SETPOS, (WPARAM)((totalEnviado * 100) / tamano), 0);
    }
    fclose(f);

    if (completo && totalEnviado == tamano) {
        strcpy(ultimoVideo, rutaEnvioPendiente);
        SendMessageA(hProgreso, PBM_SETPOS, 100, 0);
        FinEnvio("Video enviado con exito.");
    } else {
        FinEnvio("El envio se interrumpio (se perdio la conexion).");
    }
    return 0;
}

// Hilo de red: acepta clientes y lee sus mensajes (saludo, aceptar, rechazar)
DWORD WINAPI HiloRed(LPVOID lpParam) {
    WSADATA wsa;
    SOCKET server_fd;
    struct sockaddr_in server, client;
    int c = sizeof(struct sockaddr_in);

    WSAStartup(MAKEWORD(2, 2), &wsa);
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PUERTO);

    bind(server_fd, (struct sockaddr*)&server, sizeof(server));
    listen(server_fd, 1);

    while (1) {
        ActualizarEstadoConexion(FALSE, "CONEXION: DESCONECTADO (Esperando cliente en puerto 8080...)");
        client_socket = accept(server_fd, (struct sockaddr*)&client, &c);
        if (client_socket == INVALID_SOCKET) continue;

        CabeceraPaquete saludo;
        memset(&saludo, 0, sizeof(saludo));
        saludo.tipo = MSG_ESTADO;
        strcpy(saludo.nombre, "SERVIDOR_LISTO");
        send(client_socket, (char*)&saludo, sizeof(saludo), 0);
        ActualizarEstadoConexion(TRUE, "CONEXION: CONECTADO AL CLIENTE");

        CabeceraPaquete cab;
        // El servidor solo lee mensajes de control; el cliente no le sube videos
        while (recv(client_socket, (char*)&cab, sizeof(cab), MSG_WAITALL) == (int)sizeof(cab)) {
            if (cab.tipo == MSG_ESTADO) {
                ActualizarEstadoConexion(TRUE, "CONEXION: CONECTADO AL CLIENTE (Confirmado)");
            } else if (cab.tipo == MSG_ACEPTAR || cab.tipo == MSG_RECHAZAR) {
                respuestaCliente = cab.tipo;   // Despierta al hilo de envío
                SetEvent(evRespuesta);
            }
        }

        // El cliente se fue: si había un envío esperando respuesta, lo destraba
        respuestaCliente = 0;
        SetEvent(evRespuesta);
        closesocket(client_socket);
        client_socket = INVALID_SOCKET;
    }

    closesocket(server_fd);
    WSACleanup();
    return 0;
}

// Elige el video a enviar y lanza el hilo de envío
void SeleccionarVideo(HWND hwnd) {
    if (!clienteConectado || enviando) return;

    OPENFILENAMEA ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = rutaEnvioPendiente;
    ofn.nMaxFile = sizeof(rutaEnvioPendiente);
    ofn.lpstrFilter = "Videos\0*.mp4;*.mkv;*.avi;*.mov;*.wmv\0Todos\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) {
        enviando = TRUE;                       // Bloquea nuevos envíos hasta terminar
        EnableWindow(hBtnEnviar, FALSE);
        HANDLE h = CreateThread(NULL, 0, HiloEnviarArchivo, NULL, 0, NULL);
        if (h) CloseHandle(h);
        else FinEnvio("No se pudo iniciar el envio.");
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
        hBtnEnviar = CreateWindowA("BUTTON", "Enviar video al cliente", WS_VISIBLE | WS_CHILD | WS_DISABLED, 20, 15, 190, 35, hwnd, (HMENU)BTN_ENVIAR, NULL, NULL);
        CreateWindowA("BUTTON", "Reproducir ultimo video", WS_VISIBLE | WS_CHILD, 225, 15, 190, 35, hwnd, (HMENU)BTN_ABRIR_REP, NULL, NULL);
        hEstadoConexion = CreateWindowA("STATIC", "CONEXION: INICIANDO...", WS_VISIBLE | WS_CHILD | SS_ENDELLIPSIS, 20, 60, 560, 20, hwnd, NULL, NULL, NULL);
        hEstadoAccion = CreateWindowA("STATIC", "Estado: Esperando actividad...", WS_VISIBLE | WS_CHILD | SS_ENDELLIPSIS, 20, 85, 560, 20, hwnd, NULL, NULL, NULL);
        hProgreso = CreateWindowExA(0, PROGRESS_CLASSA, NULL, WS_VISIBLE | WS_CHILD | PBS_SMOOTH, 20, 110, 560, 20, hwnd, NULL, NULL, NULL);
        SendMessageA(hProgreso, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

        evRespuesta = CreateEventA(NULL, FALSE, FALSE, NULL);   // Evento de respuesta (auto-reset)
        {
            HANDLE h = CreateThread(NULL, 0, HiloRed, NULL, 0, NULL);
            if (h) CloseHandle(h);
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == BTN_ENVIAR) SeleccionarVideo(hwnd);
        if (LOWORD(wParam) == BTN_ABRIR_REP) {
            if (strlen(ultimoVideo) > 0) AbrirReproductorInterno(hwnd, ultimoVideo);
            else MessageBoxA(hwnd, "Aun no se ha enviado ningun video.", "Reproductor", MB_ICONWARNING);
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
    wc.lpszClassName = "ServidorVLCClass";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowA("ServidorVLCClass", "Servidor de Video (sube videos)", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX | WS_VISIBLE,
        50, 50, 615, 200, NULL, NULL, hInstance, NULL);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}
