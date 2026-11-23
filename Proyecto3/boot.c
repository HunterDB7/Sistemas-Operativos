// Programa de arranque UEFI
// Imprime un mensaje en pantalla y se queda ahi.
// No usamos librerias, solo definimos lo que necesitamos de UEFI.

typedef unsigned long long U64;
typedef unsigned short CHAR16;

// la pantalla (solo usamos ClearScreen y OutputString)
struct Pantalla {
    void *Reset;
    U64 (*OutputString)(struct Pantalla *p, CHAR16 *texto);
    void *TestString;
    void *QueryMode;
    void *SetMode;
    void *SetAttribute;
    U64 (*ClearScreen)(struct Pantalla *p);
};

// tabla del sistema, solo llegamos hasta ConOut
struct Tabla {
    char cabecera[24];
    void *vendor;
    U64 revision;
    void *ConInHandle;
    void *ConIn;
    void *ConOutHandle;
    struct Pantalla *ConOut;
};

// el firmware llama a esta funcion al arrancar
U64 efi_main(void *imagen, struct Tabla *tabla) {
    tabla->ConOut->ClearScreen(tabla->ConOut);
    tabla->ConOut->OutputString(tabla->ConOut, L"Hola! Este es el Proyecto 3 de Sistemas Operativos\r\n");
    tabla->ConOut->OutputString(tabla->ConOut, L"Este mensaje sale de nuestro programa de arranque\r\n");

    // bucle infinito para que no se cierre el mensaje
    while (1) {
    }
    return 0;
}
