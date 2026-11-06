// Guardar obligatoriamente como "cliente_gui.c"
#include <winsock2.h>  // API de Sockets de Windows versión 2 (Siempre al principio)
#include <windows.h>   // API nativa del ecosistema Windows
#include <commdlg.h>   // Explorador de archivos nativo de Windows (OpenFileName)
#include <commctrl.h>  // Barras comunes de interfaz (ProgressBar)
#include <stdio.h>     // Formateo de texto en memoria
#include "protocolo.h" // Estructura binaria de control de red unificada
#include "reproductor.h" // Ventana independiente de control multimedia con libVLC

// Enlaces estáticos explícitos requeridos por el compilador para el Linker
#pragma comment(lib, "ws2_32.lib")   // Librería de red sockets
#pragma comment(lib, "comdlg32.lib") // Librería de exploración de archivos
#pragma comment(lib, "comctl32.lib") // Librería de barras gráficas comunes

#define IP_SERVIDOR "127.0.0.1" // ¡IMPORTANTE!: Cambiar por la IP local de la otra PC para pruebas en red
#define PUERTO 8080            // Puerto TCP idéntico al del servidor
#define BUFFER_SIZE 16384      // Tamaño del buffer binario para transferencias (16 KB)

// Identificadores de botón únicos de la interfaz gráfica de usuario (GUI)
#define BTN_ENVIAR      101
#define BTN_ABRIR_REP   102

SOCKET sock = INVALID_SOCKET; // Descriptor del socket global para conectar con el servidor
BOOL servidorConectado = FALSE; // Estado lógico actual de la comunicación
HWND hEstadoConexion, hEstadoAccion, hProgreso, hBtnEnviar; // Elementos visuales de control
char ultimoVideo[MAX_PATH] = "";       // Ruta del último video guardado o enviado listo para reproducirse
char rutaEnvioPendiente[MAX_PATH] = ""; // Ruta seleccionada en disco para subir por red

// Cambia el estado visual del cliente según la red y activa/desactiva los controles de forma segura
void ActualizarEstadoConexion(BOOL conectado, const char* texto) {
    servidorConectado = conectado; // Sincroniza estado lógico de la conexión
    SetWindowTextA(hEstadoConexion, texto); // Altera la leyenda en la ventana gráfica
    EnableWindow(hBtnEnviar, conectado); // Habilita o deshabilita el botón de exploración de archivos
}

// Hilo exclusivo encargado de leer fragmentos desde el disco y transmitirlos por la red TCP
DWORD WINAPI HiloEnviarArchivo(LPVOID lpParam) {
    FILE* f = fopen(rutaEnvioPendiente, "rb"); // Abre el video seleccionado en modo binario crudo
    if (!f) return 1; // Si no existe o está bloqueado, termina la ejecución del hilo

    _fseeki64(f, 0, SEEK_END); // Envía el cursor al final del archivo de video
    uint64_t tamano = _ftelli64(f); // Mide los bytes exactos usando precisión de 64 bits
    _fseeki64(f, 0, SEEK_SET); // Regresa el cursor de lectura al inicio absoluto del video

    char* nombreSolo = strrchr(rutaEnvioPendiente, '\\'); // Descarte de la ruta absoluta de directorios
    nombreSolo = nombreSolo ? nombreSolo + 1 : rutaEnvioPendiente; // Si encuentra barras, toma el texto posterior

    CabeceraPaquete cab = { 0 }; // Declara estructura base de control
    cab.tipo = MSG_ARCHIVO; // Dicta comando de envío de datos multimedia
    strncpy(cab.nombre, nombreSolo, sizeof(cab.nombre) - 1); // Copia el nombre del archivo de forma segura
    cab.tamano = tamano; // Fija el peso exacto en bytes

    EnableWindow(hBtnEnviar, FALSE); // Desactiva el botón de envío para proteger el socket de colisiones
    SetWindowTextA(hEstadoAccion, "Subiendo video al servidor..."); // Altera texto explicativo de actividad
    SendMessageA(hProgreso, PBM_SETPOS, 0, 0); // Resetea barra de progreso a cero por ciento

    send(sock, (char*)&cab, sizeof(cab), 0); // Envía los metadatos de control iniciales al servidor

    char buffer[BUFFER_SIZE]; // Espacio en memoria RAM de 16 KB para bloques intermedios
    size_t bytesLeidos; // Almacén dinámico de lectura en disco
    uint64_t totalEnviado = 0; // Acumulador aritmético de progreso de subida

    // Lee el archivo de forma iterativa y lo empuja al canal de comunicación socket TCP
    while ((bytesLeidos = fread(buffer, 1, BUFFER_SIZE, f)) > 0) {
        int enviados = send(sock, buffer, (int)bytesLeidos, 0); // Despacha los bytes binarios por red
        if (envenidos <= 0) break; // Si el servidor cayó o la conexión expiró, aborta el envío
        totalEnviado += enviados; // Suma los bytes despachados con éxito
        int porcentaje = (int)((totalEnviado * 100) / tamano); // Calcula el porcentaje de avance
        SendMessageA(hProgreso, PBM_SETPOS, porcentaje, 0); // Modifica la longitud de la barra en la interfaz
    }

    fclose(f); // Libera el video en disco cerrando el descriptor
    strcpy(ultimoVideo, rutaEnvioPendiente); // Fija este archivo como disponible localmente para el reproductor flotante
    SetWindowTextA(hEstadoAccion, "Video subido con exito."); // Notifica al operador el éxito de la operación
    if (servidorConectado) EnableWindow(hBtnEnviar, TRUE); // Devuelve el control al botón si el canal sigue abierto
    return 0; // Termina el hilo de envío de forma correcta
}

// Hilo de fondo con bucle de reconexión automática y receptor permanente de paquetes entrantes
DWORD WINAPI HiloRed(LPVOID lpParam) {
    WSADATA wsa; // Estructura de inicialización de la pila de red
    struct sockaddr_in server; // Estructura de destino IP y puerto del host remoto

    WSAStartup(MAKEWORD(2, 2), &wsa); // Enciende la API Winsock versión 2.2

    server.sin_addr.s_addr = inet_addr(IP_SERVIDOR); // Configura e interpreta la IP destino ingresada
    server.sin_family = AF_INET; // Modo de red IPv4
    server.sin_port = htons(PUERTO); // Formatea el puerto de red mediante Big Endian

    while (1) { // Bucle infinito que garantiza reintentar la conexión si el servidor no se encuentra abierto
        ActualizarEstadoConexion(FALSE, "CONEXION: DESCONECTADO (Buscando servidor...)"); // Cambia el estado visual en la GUI
        sock = socket(AF_INET, SOCK_STREAM, 0); // Instancia un nuevo socket TCP orientado a flujos

        if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) { // Intenta establecer el Handshake TCP
            closesocket(sock); // Destruye el socket que falló al conectar
            Sleep(3000); // Pausa la ejecución del hilo por 3 segundos antes de volver a intentar la conexión
            continue; // Salta al inicio del bucle para volver a iniciar el proceso
        }

        CabeceraPaquete saludo = { MSG_ESTADO, "CLIENTE_LISTO", 0 }; // Crea paquete de confirmación de estado
        send(sock, (char*)&saludo, sizeof(saludo), 0); // Envía saludo inicial informando que el cliente está listo
        ActualizarEstadoConexion(TRUE, "CONEXION: CONECTADO AL SERVIDOR"); // Sincroniza estado de éxito en la GUI

        CabeceraPaquete cab; // Declara variable para recibir metadatos
        char buffer[BUFFER_SIZE]; // Búfer de transferencia binaria para la descarga (16 KB)

        // Escucha permanente en red que procesa descargas de video iniciadas desde el extremo del servidor
        while (recv(sock, (char*)&cab, sizeof(cab), MSG_WAITALL) > 0) { // Bloquea hasta extraer los bytes de cabecera completos
            if (cab.tipo == MSG_ESTADO) { // Si el paquete es de confirmación o control de estado recurrente
                ActualizarEstadoConexion(TRUE, "CONEXION: CONECTADO AL SERVIDOR (Confirmado)"); // Refleja estado estable
            }
            else if (cab.tipo == MSG_ARCHIVO) { // Si el paquete contiene una alerta de transmisión de archivo de video
                SetWindowTextA(hEstadoAccion, "Descargando video del servidor..."); // Modifica etiqueta visual de actividad
                SendMessageA(hProgreso, PBM_SETPOS, 0, 0); // Reinicia aguja del indicador gráfico de progreso a cero

                char rutaGuardado[MAX_PATH]; // Búfer para nombre físico destino
                sprintf(rutaGuardado, "descargado_%s", cab.nombre); // Añade prefijo identificador para videos descargados
                FILE* f = fopen(rutaGuardado, "wb"); // Abre o crea archivo destino bajo modo de escritura binaria ("wb")

                uint64_t totalRecibido = 0; // Inicializa contador de bytes escritos
                while (totalRecibido < cab.tamano) { // Bloquea y extrae cíclicamente hasta leer el peso exacto prometido
                    // Controla lectura fraccionada para no invadir segmentos de datos colindantes en red
                    int porLeer = (cab.tamano - totalRecibido < BUFFER_SIZE) ? (int)(cab.tamano - totalRecibido) : BUFFER_SIZE;
                    int leidos = recv(sock, buffer, porLeer, 0); // Extrae fragmento binario crudo del socket TCP
                    if (leidos <= 0) break; // Si hay corte en red, frena la escritura
                    fwrite(buffer, 1, leidos, f); // Vuelca los bytes binarios de la RAM al disco local
                    totalRecibido += leidos; // Incrementa el acumulador de bytes leídos

                    int porcentaje = (int)((totalRecibido * 100) / cab.tamano); // Obtiene porcentaje matemático
                    SendMessageA(hProgreso, PBM_SETPOS, porcentaje, 0); // Desplaza proporcionalmente la barra gráfica
                }
                fclose(f); // Cierra y asegura el archivo final en el almacenamiento local
                strcpy(ultimoVideo, rutaGuardado); // Registra este video descargado en disco como válido para reproducción
                SetWindowTextA(hEstadoAccion, "Video descargado. Listo para reproducir."); // Notifica al usuario de forma amigable
            }
        }

        closesocket(sock); // Si se rompe el ciclo recv por caída de red, cierra el socket viejo
        sock = INVALID_SOCKET; // Restablece identificador a inválido para reactivar ciclo de reconexión infinita
    }

    WSACleanup(); // Apaga módulo Winsock
    return 0;
}

// Invoca el explorador de archivos nativo de Windows y levanta el hilo asíncrono de subida
void SeleccionarVideo(HWND hwnd) {
    if (!servidorConectado) return; // Si la conexión no está activa, ignora eventos del botón
    OPENFILENAMEA ofn = { 0 }; // Inicializa estructura de ventana común para abrir archivo
    ofn.lStructSize = sizeof(ofn); // Especifica dimensiones en memoria de la estructura
    ofn.hwndOwner = hwnd; // Establece la ventana actual del cliente como la propietaria legal del explorador flotante
    ofn.lpstrFile = rutaEnvioPendiente; // Apunta el búfer receptor al arreglo global destinado
    ofn.nMaxFile = sizeof(rutaEnvioPendiente); // Fija el límite máximo de caracteres admitidos
    ofn.lpstrFilter = "Videos\0*.mp4;.mkv;.avi;.mov;.wmv\0Todos\0*.*\0"; // Filtra extensiones comunes de video
    ofn.nFilterIndex = 1; // Selecciona la primera máscara de filtrado
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST; // Exige rutas y archivos verificados físicamente en disco
    if (GetOpenFileNameA(&ofn)) { // Abre el explorador de archivos de Windows de forma bloqueante; si se da clic en Abrir:
        CreateThread(NULL, 0, HiloEnviarArchivo, NULL, 0, NULL); // Inicializa hilo asíncrono de red para no congelar la GUI
    }
}
// Procedimiento despachador de eventos nativos de la ventana del Cliente
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: // Al crearse la estructura interna de la ventana gráfica
        // Instancia el botón para elegir y enviar videos (Inicialmente bloqueado hasta conectar con el servidor)
        hBtnEnviar = CreateWindowA("BUTTON", "Subir y Enviar Video", WS_VISIBLE | WS_CHILD | WS_DISABLED, 20, 15, 160, 35, hwnd, (HMENU)BTN_ENVIAR, NULL, NULL);
        // Instancia el botón que invoca el reproductor flotante modular avanzado
        CreateWindowA("BUTTON", "Reproducir Video", WS_VISIBLE | WS_CHILD, 195, 15, 160, 35, hwnd, (HMENU)BTN_ABRIR_REP, NULL, NULL);
        // Instancia los campos estáticos de texto plano para monitor de eventos de red y actividad
        hEstadoConexion = CreateWindowA("STATIC", "CONEXION: INICIANDO...", WS_VISIBLE | WS_CHILD, 20, 60, 560, 20, hwnd, NULL, NULL, NULL);
        hEstadoAccion = CreateWindowA("STATIC", "Estado: Esperando actividad...", WS_VISIBLE | WS_CHILD, 20, 85, 560, 20, hwnd, NULL, NULL, NULL);
        // Instancia el componente de barra de progreso gráfica nativa de Windows
        hProgreso = CreateWindowExA(0, PROGRESS_CLASSA, NULL, WS_VISIBLE | WS_CHILD | PBS_SMOOTH, 20, 110, 560, 20, hwnd, NULL, NULL, NULL);
        SendMessageA(hProgreso, PBM_SETRANGE, 0, MAKELPARAM(0, 100)); // Rango porcentual estándar interno (0-100%)
        CreateThread(NULL, 0, HiloRed, NULL, 0, NULL); // Lanza el hilo secundario de bucle de red y recepción persistente
        break;
    case WM_COMMAND: // Captura clics de botones mediante identificadores únicos asignados
        if (LOWORD(wParam) == BTN_ENVIAR) SeleccionarVideo(hwnd); // Dispara explorador si se pide enviar un archivo
        if (LOWORD(wParam) == BTN_ABRIR_REP) { // Si se solicita abrir la reproducción multimedia
            if (strlen(ultimoVideo) > 0) {
                AbrirReproductorInterno(hwnd, ultimoVideo); // Instancia de forma aislada la ventana del reproductor flotante
            }
            else {
                MessageBoxA(hwnd, "Aun no se ha descargado ni seleccionado ningun video.", "Reproductor", MB_ICONWARNING); // Alerta
            }
        }
        break;
    case WM_DESTROY: // Al presionar la X superior para salir de la aplicación cliente
        PostQuitMessage(0); // Liquida y destruye el bucle de mensajes de la interfaz gráfica principal
        return 0;
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam); // Delega la gestión de mensajes de fondo al sistema
}
// Punto de arranque de la interfaz Win32 ejecutable (WinMain)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_PROGRESS_CLASS }; // Configuración para cargar la barra de progreso
    InitCommonControlsEx(&icex); // Carga y da de alta los controles comunes gráficos en el hilo principal
    WNDCLASSA wc = { 0 }; // Inicializa descriptor de la clase de ventana
    wc.lpfnWndProc = WindowProc; // Asigna nuestra lógica callback despachadora de eventos
    wc.hInstance = hInstance; // Enlaza el descriptor del proceso en ejecución
    wc.lpszClassName = "ClienteVLCClass"; // Nombre identificador único de la clase
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); // Fija color blanco para el lienzo de fondo del formulario
    RegisterClassA(&wc); // Registra la estructura ante el núcleo del sistema operativo Windows
    // Crea el objeto ventana físico con tamaño y posicionamiento simétrico predeterminado
    HWND hwnd = CreateWindowA("ClienteVLCClass", "Cliente Bidireccional de Video", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX | WS_VISIBLE,
        690, 50, 615, 200, NULL, NULL, hInstance, NULL);
    MSG msg; // Descriptor para empaquetado de mensajes entrantes de interfaz
    while (GetMessageA(&msg, NULL, 0, 0)) { // Extrae permanentemente los clics o movimientos de la cola de Windows
        TranslateMessage(&msg); // Interpreta entradas virtuales de teclado
        DispatchMessageA(&msg); // Despacha el evento directo hacia nuestro WindowProc
    }
    return 0; // Fin del proceso
}