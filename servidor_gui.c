// Guardar obligatoriamente como "servidor_gui.c"
#include <winsock2.h>  // API de Sockets de Windows versión 2 (Debe declararse al inicio)
#include <windows.h>   // API base del sistema operativo Windows
#include <commdlg.h>   // Diálogos comunes de Windows (Explorador de archivos nativo)
#include <commctrl.h>  // Controladores de componentes comunes de Windows (Barras de progreso)
#include <stdio.h>     // Entrada y salida para formateo de cadenas de texto
#include "protocolo.h" // Protocolo de red binario unificado
#include "reproductor.h" // Ventana del reproductor flotante avanzada

// Enlaces estáticos explícitos de librerías de enlace dinámico para el linker
#pragma comment(lib, "ws2_32.lib")   // Librería de sockets de Windows
#pragma comment(lib, "comdlg32.lib") // Librería de explorador de archivos
#pragma comment(lib, "comctl32.lib") // Librería de barras comunes de progreso

#define PUERTO 8080        // Puerto de escucha TCP
#define BUFFER_SIZE 16384  // Tamaño del bloque de lectura/transmisión de datos (16 KB)

// IDs de controles para vincular con las acciones de los botones en consola gráfica
#define BTN_ENVIAR      101
#define BTN_ABRIR_REP   102

SOCKET client_socket = INVALID_SOCKET; // Descriptor del socket asignado al cliente conectado
BOOL clienteConectado = FALSE;          // Estado booleano de sincronización del canal
HWND hEstadoConexion, hEstadoAccion, hProgreso, hBtnEnviar; // Manejadores de los elementos gráficos de la interfaz
char ultimoVideo[MAX_PATH] = "";       // Almacena la ruta del último video manipulado (enviado o recibido)
char rutaEnvioPendiente[MAX_PATH] = ""; // Almacena temporalmente la ruta del video elegido para enviar

// Cambia el estado visual del servidor según la red y activa/desactiva los controles de forma segura
void ActualizarEstadoConexion(BOOL conectado, const char *texto) {
    clienteConectado = conectado; // Sincroniza bandera global
    SetWindowTextA(hEstadoConexion, texto); // Modifica la etiqueta de estado de red
    EnableWindow(hBtnEnviar, conectado); // Habilita el botón de explorar archivos solo si hay conexión estable
}

// Hilo exclusivo encargado de empaquetar y enviar el video por partes sin congelar la ventana
DWORD WINAPI HiloEnviarArchivo(LPVOID lpParam) {
    FILE *f = fopen(rutaEnvioPendiente, "rb"); // Abre el video seleccionado en modo lectura binaria ("rb")
    if (!f) return 1; // Si falla, aborta la ejecución del hilo

    _fseeki64(f, 0, SEEK_END); // Posiciona el puntero al final del archivo para medirlo
    uint64_t tamano = _ftelli64(f); // Obtiene el tamaño de 64 bits en bytes del video
    _fseeki64(f, 0, SEEK_SET); // Regresa el puntero de lectura al inicio absoluto del video

    char *nombreSolo = strrchr(rutaEnvioPendiente, '\\'); // Busca la última diagonal invertida para aislar el nombre
    nombreSolo = nombreSolo ? nombreSolo + 1 : rutaEnvioPendiente; // Si existe, avanza un caracter para omitir la barra

    CabeceraPaquete cab = {0}; // Inicializa la cabecera en ceros
    cab.tipo = MSG_ARCHIVO; // Asigna el código identificador de archivo
    strncpy(cab.nombre, nombreSolo, sizeof(cab.nombre) - 1); // Copia de forma segura el nombre aislado del video
    cab.tamano = tamano; // Copia el tamaño binario en la cabecera

    EnableWindow(hBtnEnviar, FALSE); // Bloquea el botón de envío para evitar transmisiones traslapadas
    SetWindowTextA(hEstadoAccion, "Enviando video..."); // Cambia etiqueta de actividad
    SendMessageA(hProgreso, PBM_SETPOS, 0, 0); // Resetea la aguja de la barra de progreso a cero por ciento

    send(client_socket, (char*)&cab, sizeof(cab), 0); // Transmite la cabecera de control empaquetada de 269 bytes

    char buffer[BUFFER_SIZE]; // Crea el búfer de transmisión de 16 KB en memoria
    size_t bytesLeidos; // Variable para almacenar los bytes cargados desde el disco
    uint64_t totalEnviado = 0; // Acumulador de progreso

    // Lee el archivo por bloques de 16 KB y los envía consecutivamente por el canal TCP
    while ((bytesLeidos = fread(buffer, 1, BUFFER_SIZE, f)) > 0) {
        int enviados = send(client_socket, buffer, (int)bytesLeidos, 0); // Envía los bytes al búfer del socket
        if (enviados <= 0) break; // Si hay error en la red o desconexión, sale del ciclo
        totalEnviado += enviados; // Suma los bytes transmitidos con éxito
        int porcentaje = (int)((totalEnviado * 100) / tamano); // Calcula la regla de tres para el porcentaje
        SendMessageA(hProgreso, PBM_SETPOS, porcentaje, 0); // Actualiza gráficamente la barra de progreso
    }

    fclose(f); // Cierra el descriptor del archivo en el sistema operativo
    strcpy(ultimoVideo, rutaEnvioPendiente); // Almacena la ruta local como el video disponible para reproducir
    SetWindowTextA(hEstadoAccion, "Video enviado con exito."); // Avisa al usuario del fin de la subida
    if (clienteConectado) EnableWindow(hBtnEnviar, TRUE); // Reestablece el botón de subida si el cliente sigue en línea
    return 0; // Finaliza la ejecución exitosa del hilo secundario de transmisión
}

// Hilo continuo que escucha conexiones entrantes y recibe videos en background sin bloquear la interfaz
DWORD WINAPI HiloRed(LPVOID lpParam) {
    WSADATA wsa; // Estructura para inicializar la biblioteca Winsock
    SOCKET server_fd; // Descriptor del socket pasivo del servidor
    struct sockaddr_in server, client; // Estructuras de dirección IP/Puerto para servidor y cliente
    int c = sizeof(struct sockaddr_in); // Tamaño de la estructura de direccionamiento

    WSAStartup(MAKEWORD(2, 2), &wsa); // Inicializa Winsock v2.2
    server_fd = socket(AF_INET, SOCK_STREAM, 0); // Inicializa el socket bajo la familia IPv4 y protocolo orientado a flujo TCP

    server.sin_family = AF_INET; // Configura direccionamiento IPv4
    server.sin_addr.s_addr = INADDR_ANY; // Configura aceptación de conexiones dirigidas a cualquier interfaz de red local
    server.sin_port = htons(PUERTO); // Convierte el puerto al orden de bytes de red (Big Endian) mediante htons

    bind(server_fd, (struct sockaddr *)&server, sizeof(server)); // Vincula el puerto e IP asignados al socket del servidor
    listen(server_fd, 1); // Configura el socket en modo escucha pasiva con una cola máxima de 1 cliente pendiente

    while (1) { // Bucle infinito para permitir que el servidor reciba nuevos clientes si el anterior se desconecta
        ActualizarEstadoConexion(FALSE, "CONEXION: DESCONECTADO (Esperando cliente en puerto 8080...)"); // Estado base
        client_socket = accept(server_fd, (struct sockaddr *)&client, &c); // Operación bloqueante que acepta una conexión entrante
        if (client_socket == INVALID_SOCKET) continue; // Si ocurre un error, regresa al inicio de la escucha

        CabeceraPaquete saludo = {MSG_ESTADO, "SERVIDOR_LISTO", 0}; // Instancia paquete de confirmación mutua
        send(client_socket, (char*)&saludo, sizeof(saludo), 0); // Envía saludo de sincronización inicial al cliente conectado
        ActualizarEstadoConexion(TRUE, "CONEXION: CONECTADO AL CLIENTE"); // Informa la conexión en la GUI

        CabeceraPaquete cab; // Almacén para recibir cabeceras entrantes
        char buffer[BUFFER_SIZE]; // Búfer de 16 KB para la descarga

        // Bucle de lectura binaria que espera comandos o descargas enviados por el cliente conectado
        while (recv(client_socket, (char*)&cab, sizeof(cab), MSG_WAITALL) > 0) { // MSG_WAITALL bloquea hasta recibir la estructura completa
            if (cab.tipo == MSG_ESTADO) { // Si el paquete es de confirmación o control recurrente
                ActualizarEstadoConexion(TRUE, "CONEXION: CONECTADO AL CLIENTE (Confirmado)"); // Actualiza leyenda visual
            } 
            else if (cab.tipo == MSG_ARCHIVO) { // Si el paquete anuncia que el cliente va a subirnos un video
                SetWindowTextA(hEstadoAccion, "Descargando video del cliente..."); // Informa acción en la interfaz
                SendMessageA(hProgreso, PBM_SETPOS, 0, 0); // Inicializa aguja de progreso a cero

                char rutaGuardado[MAX_PATH]; // Declara el buffer para ensamblar la ruta final de guardado
                sprintf(rutaGuardado, "recibido_%s", cab.nombre); // Agrega prefijo para no sobrescribir archivos locales
                FILE *f = fopen(rutaGuardado, "wb"); // Crea el archivo destino en modo escritura binaria ("wb")

                uint64_t totalRecibido = 0; // Inicializa acumulador de descarga
                while (totalRecibido < cab.tamano) { // Itera cíclicamente hasta leer la cantidad exacta de bytes prometidos en la cabecera
                    // Protege la lectura para no leer de más en el último fragmento pequeño del video
                    int porLeer = (cab.tamano - totalRecibido < BUFFER_SIZE) ? (int)(cab.tamano - totalRecibido) : BUFFER_SIZE;
                    int leidos = recv(client_socket, buffer, porLeer, 0); // Descarga el bloque binario desde el buffer TCP
                    if (leidos <= 0) break; // Si el cliente se apaga o la red cae, rompe la recepción
                    fwrite(buffer, 1, leidos, f); // Escribe los bytes crudos directamente en el disco duro
                    totalRecibido += leidos; // Actualiza el acumulador de bytes leídos

                    int porcentaje = (int)((totalRecibido * 100) / cab.tamano); // Regla de tres para avance porcentual
                    SendMessageA(hProgreso, PBM_SETPOS, porcentaje, 0); // Refleja el avance en tiempo real en la barra de progreso
                }
                fclose(f); // Cierra el archivo liberando descriptores en disco
                strcpy(ultimoVideo, rutaGuardado); // Registra el video descargado como disponible para reproducir
                SetWindowTextA(hEstadoAccion, "Video recibido. Listo para reproducir."); // Actualiza estado de actividad
            }
        }
        closesocket(client_socket); // Destruye el socket del cliente roto/desconectado
        client_socket = INVALID_SOCKET; // Restablece descriptor global a inválido
    }

    closesocket(server_fd); // Cierra socket pasivo (Línea inalcanzable por bucle infinito, añadida por integridad)
    WSACleanup(); // Apaga la API de Winsock
    return 0; // Finaliza la ejecución del hilo
}

// Abre el explorador de archivos nativo y delega el proceso al hilo de envío de archivos
void SeleccionarVideo(HWND hwnd) {
    if (!clienteConectado) return; // Si no hay nadie enlazado, ignora el llamado de apertura

    OPENFILENAMEA ofn = { 0 }; // Inicializa estructura de ventana común para abrir archivo
    ofn.lStructSize = sizeof(ofn); // Especifica peso de la estructura en bytes
    ofn.hwndOwner = hwnd; // Establece la ventana actual como la dueña del diálogo emergente
    ofn.lpstrFile = rutaEnvioPendiente; // Asigna el búfer donde se guardará la ruta seleccionada por el usuario
    ofn.nMaxFile = sizeof(rutaEnvioPendiente); // Tamaño límite del buffer de la ruta
    ofn.lpstrFilter = "Videos\0*.mp4;*.mkv;*.avi;*.mov;*.wmv\0Todos\0*.*\0"; // Máscara de extensiones de video admitidas
    ofn.nFilterIndex = 1; // Selecciona el filtro de video por defecto
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST; // Exige que la ruta y el archivo existan físicamente en el disco

    if (GetOpenFileNameA(&ofn)) { // Invoca la ventana del explorador nativo; si el usuario da clic en Aceptar:
        CreateThread(NULL, 0, HiloEnviarArchivo, NULL, 0, NULL); // Crea hilo asíncrono para enviar el archivo sin colapsar la UI
    }
}

// Despachador de mensajes nativo (Callback) de la ventana del servidor
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: // Al instanciarse por primera vez la estructura de la aplicación
        // Crea el botón principal para explorar archivos (Inicialmente deshabilitado por falta de par)
        hBtnEnviar = CreateWindowA("BUTTON", "Subir y Enviar Video", WS_VISIBLE | WS_CHILD | WS_DISABLED, 20, 15, 160, 35, hwnd, (HMENU)BTN_ENVIAR, NULL, NULL);
        // Crea el botón que despliega la ventana secundaria flotante del reproductor multimedia avanzado
        CreateWindowA("BUTTON", "Reproducir Video", WS_VISIBLE | WS_CHILD, 195, 15, 160, 35, hwnd, (HMENU)BTN_ABRIR_REP, NULL, NULL);

        // Crea las etiquetas estáticas de texto descriptivo para monitoreo de estados
        hEstadoConexion = CreateWindowA("STATIC", "CONEXION: INICIANDO...", WS_VISIBLE | WS_CHILD, 20, 60, 560, 20, hwnd, NULL, NULL, NULL);
        hEstadoAccion = CreateWindowA("STATIC", "Estado: Esperando actividad...", WS_VISIBLE | WS_CHILD, 20, 85, 560, 20, hwnd, NULL, NULL, NULL);

        // Crea e inicializa la barra de progreso gráfica nativa de Windows (ProgressBar)
        hProgreso = CreateWindowExA(0, PROGRESS_CLASSA, NULL, WS_VISIBLE | WS_CHILD | PBS_SMOOTH, 20, 110, 560, 20, hwnd, NULL, NULL, NULL);
        SendMessageA(hProgreso, PBM_SETRANGE, 0, MAKELPARAM(0, 100)); // Fija rango interno de la barra del 0 al 100%

        CreateThread(NULL, 0, HiloRed, NULL, 0, NULL); // Despliega el hilo persistente de escucha de red
        break;

    case WM_COMMAND: // Intercepta los comandos de clics sobre los botones creados
        if (LOWORD(wParam) == BTN_ENVIAR) SeleccionarVideo(hwnd); // Llama al explorador si se pide subir un archivo
        if (LOWORD(wParam) == BTN_ABRIR_REP) { // Si se presiona el botón de reproducir
            if (strlen(ultimoVideo) > 0) {
                AbrirReproductorInterno(hwnd, ultimoVideo); // Invoca e inicializa la ventana del reproductor avanzado
            }
            else {
                MessageBoxA(hwnd, "Aun no se ha descargado ni seleccionado ningun video.", "Reproductor", MB_ICONWARNING); // Alerta visual
            }
        }
        break;

    case WM_DESTROY: // Al cerrar la aplicación (Clic en la X de la ventana)
        PostQuitMessage(0); // Envía señal al hilo principal para liquidar el bucle de mensajes de Windows
        return 0;
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam); // Resuelve de forma automática los mensajes por defecto del sistema
}

// Punto de entrada oficial de aplicaciones con ventanas gráficas nativas en Windows (WinMain)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_PROGRESS_CLASS }; // Configura la inicialización de la barra de progreso
    InitCommonControlsEx(&icex); // Registra las clases de la barra común en la interfaz

    WNDCLASSA wc = { 0 }; // Inicializa estructura para dar de alta la clase de ventana
    wc.lpfnWndProc = WindowProc; // Asigna nuestro callback despachador de eventos creado
    wc.hInstance = hInstance; // Enlaza la instancia del módulo actual proporcionada por el sistema
    wc.lpszClassName = "ServidorVLCClass"; // Nombre identificador interno de la clase
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); // Asigna fondo blanco predeterminado a la ventana
    RegisterClassA(&wc); // Da de alta la clase ante el kernel de Windows

    // Crea el objeto ventana físico centrado con dimensiones fijas en pantalla (Estilo deshabilitado para redimensionar)
    HWND hwnd = CreateWindowA("ServidorVLCClass", "Servidor Bidireccional de Video", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX | WS_VISIBLE,
        50, 50, 615, 200, NULL, NULL, hInstance, NULL);

    MSG msg; // Estructura contenedora de mensajes del sistema
    while (GetMessageA(&msg, NULL, 0, 0)) { // Bucle de eventos infinito que extrae mensajes de la cola de Windows
        TranslateMessage(&msg); // Traduce pulsaciones de teclas virtuales en caracteres legibles
        DispatchMessageA(&msg); // Despacha el mensaje enviándolo directamente a nuestro WindowProc
    }
    return 0; // Código de salida exitoso del programa entero
}
