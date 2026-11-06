
#ifndef PROTOCOLO_H
#define PROTOCOLO_H

#include <stdint.h> // Incluye tipos de datos con tamaños fijos en bits (uint8_t, uint64_t)

#define MSG_ESTADO  1  // Constante para indicar paquete de latido o sincronización de conexión
#define MSG_ARCHIVO 2  // Constante para indicar inicio de transferencia de un archivo de video

#pragma pack(push, 1)  // Fuerza al compilador a empaquetar la estructura a 1 byte (sin alineación automática)
typedef struct {
    uint8_t tipo;          // 1 byte: Guarda el tipo de mensaje (MSG_ESTADO o MSG_ARCHIVO)
    char nombre[260];      // 260 bytes: Guarda el nombre del archivo o un identificador de estado (MAX_PATH = 260)
    uint64_t tamano;       // 8 bytes: Guarda el tamaño del archivo (soporta videos superiores a 4 GB)
} CabeceraPaquete;
#pragma pack(pop)      // Restaura la alineación por defecto del compilador

#endif // Fin de PROTOCOLO_H

