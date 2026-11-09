#ifndef PROTOCOLO_H
#define PROTOCOLO_H

#include <stdint.h> // Tipos de tamaño fijo (uint8_t, uint64_t)

// Tipos de mensaje del protocolo.
// Flujo de una transferencia (el servidor es el único que sube videos):
//   1) Servidor -> Cliente : MSG_ARCHIVO  (nombre + tamaño, es una "oferta")
//   2) Cliente  -> Servidor: MSG_ACEPTAR  (eligió dónde guardarlo) o MSG_RECHAZAR (canceló)
//   3) Servidor -> Cliente : los bytes del video (solo si el cliente aceptó)
#define MSG_ESTADO   1  // Saludo / confirmación de conexión
#define MSG_ARCHIVO  2  // El servidor ofrece un video (nombre y tamaño)
#define MSG_ACEPTAR  3  // El cliente acepta la descarga
#define MSG_RECHAZAR 4  // El cliente canceló o no pudo guardar el archivo

#pragma pack(push, 1)  // Sin relleno: la estructura mide exactamente 269 bytes
typedef struct {
    uint8_t tipo;          // 1 byte: MSG_ESTADO, MSG_ARCHIVO, MSG_ACEPTAR o MSG_RECHAZAR
    char nombre[260];      // 260 bytes: nombre del archivo o identificador de estado
    uint64_t tamano;       // 8 bytes: tamaño del archivo (soporta videos de más de 4 GB)
} CabeceraPaquete;
#pragma pack(pop)

#endif // PROTOCOLO_H
