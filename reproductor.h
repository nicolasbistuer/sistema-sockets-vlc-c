#ifndef REPRODUCTOR_H
#define REPRODUCTOR_H

#include <windows.h>
#include <commctrl.h>
#include <vlc/vlc.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "libvlc.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

// Identificadores de los controles del reproductor
#define ID_PLAYPAUSA   501
#define ID_REINICIAR   502
#define ID_RETRO_10S   503
#define ID_AVAN_10S    504
#define ID_VOLUMEN     505
#define ID_PANT_COMP   506
#define ID_ICONO_VOL   507
#define ID_TIMER_REP   601

#define REP_ALTO_BARRA 56   // Alto (px) de la barra de controles inferior
#define REP_BTN_ANCHO  46   // Ancho de cada botón con icono
#define REP_BTN_ALTO   36   // Alto de cada botón con icono

static libvlc_instance_t*     vlcInstRep = NULL;   // Instancia del motor VLC
static libvlc_media_player_t* vlcPlayRep = NULL;   // Reproductor de VLC
static HWND  hVentanaRep  = NULL;                  // Ventana del reproductor
static HWND  hLienzoVideo = NULL;                  // Área donde VLC dibuja el video
static HWND  hBtnPlay = NULL, hBtnReiniciar = NULL, hBtnRetro = NULL, hBtnAvan = NULL;
static HWND  hIconoVol = NULL, hSliderVol = NULL, hBtnFull = NULL;
static char  rutaVideoLocal[MAX_PATH] = "";
static BOOL  repPantallaCompleta = FALSE;          // TRUE mientras está en pantalla completa
static BOOL  repUltimoReproduciendo = FALSE;       // Último estado dibujado en el botón play/pausa
static WINDOWPLACEMENT repPosPrevia;               // Posición de la ventana antes de pantalla completa
static LONG  repEstiloPrevio = 0;                  // Estilo de la ventana antes de pantalla completa

// ---------------------------------------------------------------------------
// Estado de reproducción: TRUE si el video está sonando (o por empezar)
// ---------------------------------------------------------------------------
static BOOL RepEstaReproduciendo(void) {
    if (!vlcPlayRep) return FALSE;
    libvlc_state_t st = libvlc_media_player_get_state(vlcPlayRep);
    return (st == libvlc_Playing || st == libvlc_Opening || st == libvlc_Buffering);
}

// ---------------------------------------------------------------------------
// Dibujo de los iconos con GDI (no necesita archivos de imagen)
// ---------------------------------------------------------------------------
static void RepTriangulo(HDC hdc, int x, int y, int ancho, int alto, BOOL haciaDerecha) {
    POINT p[3];
    if (haciaDerecha) {
        p[0].x = x;         p[0].y = y;
        p[1].x = x;         p[1].y = y + alto;
        p[2].x = x + ancho; p[2].y = y + alto / 2;
    } else {
        p[0].x = x + ancho; p[0].y = y;
        p[1].x = x + ancho; p[1].y = y + alto;
        p[2].x = x;         p[2].y = y + alto / 2;
    }
    Polygon(hdc, p, 3);
}

static void RepRect(HDC hdc, int x, int y, int ancho, int alto) {
    Rectangle(hdc, x, y, x + ancho, y + alto);
}

static void RepDibujarIcono(const DRAWITEMSTRUCT* dis) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    BOOL presionado = (dis->itemState & ODS_SELECTED) != 0;
    int id = (int)dis->CtlID;

    // Fondo del botón (los botones tienen aspecto de botón clásico)
    if (id != ID_ICONO_VOL) {
        FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));
        DrawEdge(hdc, &rc, presionado ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
    } else {
        FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));
    }

    HBRUSH relleno = CreateSolidBrush(RGB(30, 30, 30));
    HPEN   lapiz   = CreatePen(PS_SOLID, 1, RGB(30, 30, 30));
    HBRUSH brushViejo = (HBRUSH)SelectObject(hdc, relleno);
    HPEN   penViejo   = (HPEN)SelectObject(hdc, lapiz);

    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    int cx = rc.left + w / 2 + (presionado ? 1 : 0);
    int cy = rc.top + h / 2 + (presionado ? 1 : 0);

    switch (id) {
    case ID_PLAYPAUSA:
        if (RepEstaReproduciendo()) {            // Pausa: dos barras verticales
            RepRect(hdc, cx - 8, cy - 9, 6, 18);
            RepRect(hdc, cx + 3, cy - 9, 6, 18);
        } else {                                 // Play: triángulo hacia la derecha
            RepTriangulo(hdc, cx - 6, cy - 10, 16, 20, TRUE);
        }
        break;
    case ID_REINICIAR:                           // Ir al inicio: barra + triángulo hacia la izquierda
        RepRect(hdc, cx - 10, cy - 9, 3, 18);
        RepTriangulo(hdc, cx - 5, cy - 9, 14, 18, FALSE);
        break;
    case ID_RETRO_10S:                           // Retroceder: dos triángulos hacia la izquierda
        RepTriangulo(hdc, cx - 14, cy - 8, 13, 16, FALSE);
        RepTriangulo(hdc, cx + 0,  cy - 8, 13, 16, FALSE);
        break;
    case ID_AVAN_10S:                            // Avanzar: dos triángulos hacia la derecha
        RepTriangulo(hdc, cx - 13, cy - 8, 13, 16, TRUE);
        RepTriangulo(hdc, cx + 1,  cy - 8, 13, 16, TRUE);
        break;
    case ID_PANT_COMP: {                         // Pantalla completa: cuatro esquinas
        int d = repPantallaCompleta ? 3 : 9;     // Esquinas hacia adentro si ya está en pantalla completa
        int l = 6;                               // Largo de cada brazo de la esquina
        int g = 2;                               // Grosor
        // arriba-izquierda
        RepRect(hdc, cx - d - l, cy - d - l, l, g); RepRect(hdc, cx - d - l, cy - d - l, g, l);
        // arriba-derecha
        RepRect(hdc, cx + d,     cy - d - l, l, g); RepRect(hdc, cx + d + l - g, cy - d - l, g, l);
        // abajo-izquierda
        RepRect(hdc, cx - d - l, cy + d + l - g, l, g); RepRect(hdc, cx - d - l, cy + d, g, l);
        // abajo-derecha
        RepRect(hdc, cx + d,     cy + d + l - g, l, g); RepRect(hdc, cx + d + l - g, cy + d, g, l);
        break;
    }
    case ID_ICONO_VOL: {                         // Parlante con ondas de sonido
        POINT p[4] = { {cx - 9, cy - 3}, {cx - 4, cy - 3}, {cx + 2, cy - 9}, {cx + 2, cy + 9} };
        RepRect(hdc, cx - 9, cy - 3, 5, 7);
        Polygon(hdc, p, 4);
        p[0].x = cx - 4; p[0].y = cy + 4; p[1].x = cx + 2; p[1].y = cy + 9;
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Arc(hdc, cx - 2, cy - 5, cx + 8,  cy + 5,  cx + 3, cy + 5,  cx + 3, cy - 5);
        Arc(hdc, cx - 4, cy - 9, cx + 12, cy + 9,  cx + 5, cy + 9,  cx + 5, cy - 9);
        break;
    }
    }

    SelectObject(hdc, brushViejo);
    SelectObject(hdc, penViejo);
    DeleteObject(relleno);
    DeleteObject(lapiz);

    if ((dis->itemState & ODS_FOCUS) && id != ID_ICONO_VOL) {
        RECT foco = rc;
        InflateRect(&foco, -4, -4);
        DrawFocusRect(hdc, &foco);
    }
}

// ---------------------------------------------------------------------------
// Acomoda el video y los controles según el tamaño actual de la ventana
// ---------------------------------------------------------------------------
static void RepDistribuir(HWND hwnd) {
    if (!hLienzoVideo) return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    int ancho = rc.right;
    int altoVideo = rc.bottom - REP_ALTO_BARRA;
    if (altoVideo < 0) altoVideo = 0;
    int y = altoVideo + (REP_ALTO_BARRA - REP_BTN_ALTO) / 2;
    int x = 10;

    MoveWindow(hLienzoVideo, 0, 0, ancho, altoVideo, TRUE);
    MoveWindow(hBtnPlay,     x, y, REP_BTN_ANCHO + 6, REP_BTN_ALTO, TRUE); x += REP_BTN_ANCHO + 12;
    MoveWindow(hBtnReiniciar,x, y, REP_BTN_ANCHO, REP_BTN_ALTO, TRUE);     x += REP_BTN_ANCHO + 6;
    MoveWindow(hBtnRetro,    x, y, REP_BTN_ANCHO, REP_BTN_ALTO, TRUE);     x += REP_BTN_ANCHO + 6;
    MoveWindow(hBtnAvan,     x, y, REP_BTN_ANCHO, REP_BTN_ALTO, TRUE);     x += REP_BTN_ANCHO + 24;
    MoveWindow(hIconoVol,    x, y, 30, REP_BTN_ALTO, TRUE);                x += 32;
    MoveWindow(hSliderVol,   x, y, 130, REP_BTN_ALTO, TRUE);
    MoveWindow(hBtnFull, ancho - 10 - REP_BTN_ANCHO, y, REP_BTN_ANCHO, REP_BTN_ALTO, TRUE);
}

// ---------------------------------------------------------------------------
// Pantalla completa: la ventana se convierte en un popup del tamaño del monitor.
// (libvlc_toggle_fullscreen no funciona cuando el video se dibuja dentro de
// una ventana propia, por eso se hace a mano.)
// ---------------------------------------------------------------------------
static void RepAlternarPantallaCompleta(HWND hwnd) {
    if (!repPantallaCompleta) {
        MONITORINFO mi;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        repPosPrevia.length = sizeof(repPosPrevia);
        if (!GetWindowPlacement(hwnd, &repPosPrevia)) return;
        if (!GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) return;
        repEstiloPrevio = GetWindowLongA(hwnd, GWL_STYLE);
        SetWindowLongA(hwnd, GWL_STYLE, repEstiloPrevio & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(hwnd, HWND_TOPMOST,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        repPantallaCompleta = TRUE;
    } else {
        SetWindowLongA(hwnd, GWL_STYLE, repEstiloPrevio);
        SetWindowPlacement(hwnd, &repPosPrevia);
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        repPantallaCompleta = FALSE;
    }
    InvalidateRect(hBtnFull, NULL, TRUE);
    RepDistribuir(hwnd);
}

// ---------------------------------------------------------------------------
// Procedimiento de ventana del reproductor
// ---------------------------------------------------------------------------
static LRESULT CALLBACK ReproductorWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES };
        InitCommonControlsEx(&icex);

        // Zona negra donde VLC dibuja el video
        hLienzoVideo = CreateWindowA("STATIC", "", WS_VISIBLE | WS_CHILD | SS_BLACKRECT | WS_CLIPSIBLINGS,
                                     0, 0, 100, 100, hwnd, NULL, NULL, NULL);
        // Botones con icono dibujado a mano (BS_OWNERDRAW)
        hBtnPlay     = CreateWindowA("BUTTON", "", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | WS_TABSTOP, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_PLAYPAUSA, NULL, NULL);
        hBtnReiniciar= CreateWindowA("BUTTON", "", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | WS_TABSTOP, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_REINICIAR, NULL, NULL);
        hBtnRetro    = CreateWindowA("BUTTON", "", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | WS_TABSTOP, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_RETRO_10S, NULL, NULL);
        hBtnAvan     = CreateWindowA("BUTTON", "", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | WS_TABSTOP, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_AVAN_10S, NULL, NULL);
        hIconoVol    = CreateWindowA("STATIC", "", WS_VISIBLE | WS_CHILD | SS_OWNERDRAW, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_ICONO_VOL, NULL, NULL);
        hSliderVol   = CreateWindowExA(0, TRACKBAR_CLASSA, "", WS_VISIBLE | WS_CHILD | TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_VOLUMEN, NULL, NULL);
        hBtnFull     = CreateWindowA("BUTTON", "", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | WS_TABSTOP, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_PANT_COMP, NULL, NULL);
        SendMessageA(hSliderVol, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessageA(hSliderVol, TBM_SETPOS, TRUE, 80);
        RepDistribuir(hwnd);

        vlcInstRep = libvlc_new(0, NULL);
        if (vlcInstRep) {
            vlcPlayRep = libvlc_media_player_new(vlcInstRep);
            libvlc_media_t* media = libvlc_media_new_path(vlcInstRep, rutaVideoLocal);
            libvlc_media_player_set_media(vlcPlayRep, media);
            libvlc_media_release(media);
            libvlc_media_player_set_hwnd(vlcPlayRep, hLienzoVideo);
            libvlc_video_set_mouse_input(vlcPlayRep, 0); // VLC no captura el mouse ni el teclado
            libvlc_video_set_key_input(vlcPlayRep, 0);
            libvlc_audio_set_volume(vlcPlayRep, 80);
            libvlc_media_player_play(vlcPlayRep);
        }
        SetTimer(hwnd, ID_TIMER_REP, 150, NULL); // Refresca el icono play/pausa y vigila la tecla Esc
        return 0;
    }
    case WM_SIZE:
        RepDistribuir(hwnd);
        return 0;
    case WM_GETMINMAXINFO: {                     // Tamaño mínimo para que los controles entren
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 560;
        mmi->ptMinTrackSize.y = 320;
        return 0;
    }
    case WM_DRAWITEM:
        RepDibujarIcono((const DRAWITEMSTRUCT*)lParam);
        return TRUE;
    case WM_TIMER:
        if (wParam == ID_TIMER_REP) {
            BOOL reproduciendo = RepEstaReproduciendo();
            if (reproduciendo != repUltimoReproduciendo) {   // Cambió play <-> pausa (o terminó el video)
                repUltimoReproduciendo = reproduciendo;
                InvalidateRect(hBtnPlay, NULL, TRUE);
            }
            // Esc sale de pantalla completa
            if (repPantallaCompleta && (GetAsyncKeyState(VK_ESCAPE) & 0x8000) && GetForegroundWindow() == hwnd)
                RepAlternarPantallaCompleta(hwnd);
        }
        return 0;
    case WM_HSCROLL:                             // Mueve el control de volumen
        if ((HWND)lParam == hSliderVol && vlcPlayRep) {
            int pos = (int)SendMessageA(hSliderVol, TBM_GETPOS, 0, 0);
            libvlc_audio_set_volume(vlcPlayRep, pos);
        }
        return 0;
    case WM_COMMAND:
        if (!vlcPlayRep) return 0;
        switch (LOWORD(wParam)) {
        case ID_PLAYPAUSA: {
            libvlc_state_t st = libvlc_media_player_get_state(vlcPlayRep);
            if (st == libvlc_Ended || st == libvlc_Stopped || st == libvlc_Error) {
                libvlc_media_player_stop(vlcPlayRep);   // Si terminó, vuelve a empezar
                libvlc_media_player_play(vlcPlayRep);
            } else if (st == libvlc_Paused) {
                libvlc_media_player_set_pause(vlcPlayRep, 0);
            } else {
                libvlc_media_player_set_pause(vlcPlayRep, 1);
            }
            InvalidateRect(hBtnPlay, NULL, TRUE);
            break;
        }
        case ID_REINICIAR:
            libvlc_media_player_stop(vlcPlayRep);
            libvlc_media_player_play(vlcPlayRep);
            break;
        case ID_RETRO_10S: {
            libvlc_time_t t = libvlc_media_player_get_time(vlcPlayRep);
            libvlc_media_player_set_time(vlcPlayRep, (t - 10000 < 0) ? 0 : t - 10000);
            break;
        }
        case ID_AVAN_10S: {
            libvlc_time_t t = libvlc_media_player_get_time(vlcPlayRep);
            libvlc_media_player_set_time(vlcPlayRep, t + 10000);
            break;
        }
        case ID_PANT_COMP:
            RepAlternarPantallaCompleta(hwnd);
            break;
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, ID_TIMER_REP);
        if (vlcPlayRep) { libvlc_media_player_stop(vlcPlayRep); libvlc_media_player_release(vlcPlayRep); vlcPlayRep = NULL; }
        if (vlcInstRep) { libvlc_release(vlcInstRep); vlcInstRep = NULL; }
        repPantallaCompleta = FALSE;
        hLienzoVideo = NULL;
        hVentanaRep = NULL;
        return 0;
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Abre el reproductor en una ventana propia (si ya había uno abierto, lo reemplaza)
// ---------------------------------------------------------------------------
void AbrirReproductorInterno(HWND hwndPadre, const char* rutaArchivo) {
    static BOOL claseRegistrada = FALSE;
    HINSTANCE hInst = GetModuleHandle(NULL);

    if (hVentanaRep && IsWindow(hVentanaRep)) DestroyWindow(hVentanaRep); // Un solo reproductor a la vez

    strncpy(rutaVideoLocal, rutaArchivo, MAX_PATH - 1);
    rutaVideoLocal[MAX_PATH - 1] = '\0';

    if (!claseRegistrada) {
        WNDCLASSA wc = { 0 };
        wc.lpfnWndProc = ReproductorWndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = "VentanaReproductorClass";
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
        RegisterClassA(&wc);
        claseRegistrada = TRUE;
    }

    // Ventana con área de cliente de 800 x (450 de video + barra de controles)
    RECT r = { 0, 0, 800, 450 + REP_ALTO_BARRA };
    DWORD estilo = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    AdjustWindowRect(&r, estilo, FALSE);
    hVentanaRep = CreateWindowExA(0, "VentanaReproductorClass", "Reproductor Integrado", estilo | WS_VISIBLE,
                                  CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                                  hwndPadre, NULL, hInst, NULL);
}
#endif // REPRODUCTOR_H
