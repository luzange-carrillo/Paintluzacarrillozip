// =====================================================================
//  PAINT EN C++ Y OPENGL CORE  -  Proyecto: paintluzacarrillo
//  Panel lateral con ImGui. El lienzo se rasteriza a mano (Bresenham y Flood Fill).
// =====================================================================

#include <glad/glad.h>                // carga las funciones de OpenGL
#include <GLFW/glfw3.h>               // ventana, mouse y contexto
#include "imgui.h"                    // nucleo de ImGui
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"    
#include <iostream>                   // mensajes de error en consola
#include <vector>                     // listas dinamicas (vertices, paleta, pila)
#include <cmath>                      // sqrt, cos, sin, lround
#include <cstdlib>                    // abs para enteros
#include <cstring>                    // memcpy para copiar la matriz
#include <algorithm>                  // min y max

// ============================ MEDIDAS ================================
const int ANCHO_VENTANA = 1000;       // ancho total de la ventana en pixeles
const int ALTO_VENTANA = 600;        // alto total de la ventana en pixeles
const int ANCHO_BARRA = 200;        // ancho del panel lateral (zona de ImGui)
const int COLUMNAS = 80;         // cantidad de celdas en horizontal
const int FILAS = 60;         // cantidad de celdas en vertical
const int LADO_CELDA = 10;         // cada celda mide 10 x 10 pixeles
const float PI_APROX = 3.14159265f; // valor de pi para los poligonos
const float PASO_GIRO_GRADOS = 15.0f;  // grados que gira la figura en cada clic de "Rotar"

// ============================ TIPOS ==================================
struct Tinte { float r, g, b; };      // un color con componentes de 0.0 a 1.0

// Lista de utensilios (herramientas) que existen en el panel
enum class Utensilio {
    PINCEL,        // trazo libre
    LINEA,         // segmento recto
    CIRCULO,       // circunferencia
    TRIANGULO,     // triangulo
    CUADRADO,      // cuadrado
    PENTAGONO,     // pentagono regular
    HEXAGONO,      // hexagono regular
    CUBETA,        // relleno por inundacion
    BORRADOR,      // borra celdas
    SELECCIONAR,   // toma una figura del lienzo para poder girarla
    ROTAR,         // accion: gira la figura seleccionada (no es una herramienta)
    LIMPIAR_TODO   // vacia todo el lienzo (es una accion, no una herramienta)
};

struct Casilla { int fila, columna; };  // una posicion dentro de la cuadricula

// ============================ COLORES FIJOS ==========================
const Tinte TINTE_PAPEL = { 1.0f, 1.0f, 1.0f };   // blanco: color de fondo del lienzo

// ============================ ESTADO GLOBAL ==========================
Tinte tramaPixeles[FILAS][COLUMNAS];    // matriz que guarda el color de cada celda
Tinte tramaRespaldo[FILAS][COLUMNAS];   // copia del lienzo para la vista previa al arrastrar

std::vector<Tinte> paletaColores;       // los colores disponibles

Utensilio utensilioActual = Utensilio::PINCEL;   // herramienta seleccionada ahora
int indiceTinteActual = 0;                       // posicion del color elegido en la paleta

bool arrastrando = false;               // true mientras se mantiene presionado dentro del lienzo
int anclaFila = 0, anclaColumna = 0;    // celda donde se inicio el arrastre
int huellaFila = 0, huellaColumna = 0;  // ultima celda procesada (para unir trazos libres)

// ---- Estado de la figura seleccionada ----
bool haySeleccion = false;              // true si hay una figura tomada del lienzo
Tinte tramaFlotante[FILAS][COLUMNAS];   // la figura tomada, tal como estaba (sin girar)
Tinte tramaGirada[FILAS][COLUMNAS];     // la figura ya girada (lo que se ve en pantalla)
float pivoteCol = 0.0f, pivoteFila = 0.0f; // centro de giro (centro de la figura)
float anguloGrados = 0.0f;              // giro acumulado desde que se selecciono
int vistaMinF = 0, vistaMaxF = -1;      // filas ocupadas por la figura girada (para el marco)
int vistaMinC = 0, vistaMaxC = -1;      // columnas ocupadas por la figura girada

// ============================ SHADERS ================================
// Vertex shader: recibe posicion en pixeles y la convierte a coordenadas de OpenGL
const char* fuenteVertices = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec3 aColor;
uniform vec2 uResolucion;
out vec3 vColor;
void main() {
    float x = aPos.x / uResolucion.x * 2.0 - 1.0;
    float y = 1.0 - aPos.y / uResolucion.y * 2.0;
    gl_Position = vec4(x, y, 0.0, 1.0);
    vColor = aColor;
}
)";

// Fragment shader: pinta cada pixel con el color recibido
const char* fuenteFragmentos = R"(
#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(vColor, 1.0);
}
)";

// Compila un shader y avisa por consola si hay errores
unsigned int compilarEtapa(unsigned int tipo, const char* codigo) {
    unsigned int etapa = glCreateShader(tipo);        // crea el objeto shader
    glShaderSource(etapa, 1, &codigo, nullptr);       // le entrega el texto del codigo
    glCompileShader(etapa);                           // lo compila
    int exito; char registro[512];                    // variables para revisar el resultado
    glGetShaderiv(etapa, GL_COMPILE_STATUS, &exito);  // consulta si compilo bien
    if (!exito) {                                     // si fallo...
        glGetShaderInfoLog(etapa, 512, nullptr, registro); // obtiene el mensaje
        std::cout << "Error de shader: " << registro << "\n"; // lo muestra
    }
    return etapa;                                     // devuelve el identificador
}

// Une vertex y fragment shader en un programa listo para usar
unsigned int armarPrograma() {
    unsigned int ev = compilarEtapa(GL_VERTEX_SHADER, fuenteVertices);      // compila vertices
    unsigned int ef = compilarEtapa(GL_FRAGMENT_SHADER, fuenteFragmentos);  // compila fragmentos
    unsigned int prog = glCreateProgram();   // crea el programa
    glAttachShader(prog, ev);                // adjunta el vertex shader
    glAttachShader(prog, ef);                // adjunta el fragment shader
    glLinkProgram(prog);                     // enlaza ambos
    glDeleteShader(ev);                      // ya no se necesitan por separado
    glDeleteShader(ef);                      // idem
    return prog;                             // devuelve el programa
}

// ============================ DIBUJO DE CAJAS ========================
// El lienzo se arma como triangulos: cada vertice son 5 floats (x, y, r, g, b)
std::vector<float> acopioVertices;

// Agrega un vertice a la lista que se enviara a la GPU
void anadirPunto(float x, float y, Tinte t) {
    acopioVertices.push_back(x);    // posicion horizontal
    acopioVertices.push_back(y);    // posicion vertical
    acopioVertices.push_back(t.r);  // componente roja
    acopioVertices.push_back(t.g);  // componente verde
    acopioVertices.push_back(t.b);  // componente azul
}

// Un rectangulo se forma con dos triangulos (6 vertices)
void anadirCaja(float x, float y, float w, float h, Tinte t) {
    anadirPunto(x, y, t);   // triangulo 1: arriba-izquierda
    anadirPunto(x + w, y, t);   // triangulo 1: arriba-derecha
    anadirPunto(x + w, y + h, t);   // triangulo 1: abajo-derecha
    anadirPunto(x, y, t);   // triangulo 2: arriba-izquierda
    anadirPunto(x + w, y + h, t);   // triangulo 2: abajo-derecha
    anadirPunto(x, y + h, t);   // triangulo 2: abajo-izquierda
}

// ============================ UTILIDADES =============================
// Compara dos colores (los valores vienen de las mismas constantes, por eso es exacto)
bool tintesIguales(const Tinte& a, const Tinte& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;       // true si los tres canales coinciden
}

// Mantiene un entero dentro de un rango
int acotar(int valor, int minimo, int maximo) {
    return std::max(minimo, std::min(valor, maximo));    // lo recorta al intervalo
}

// ============================ LIENZO =================================
// Pone todas las celdas en blanco
void vaciarLienzo() {
    for (int f = 0; f < FILAS; f++)                      // recorre las filas
        for (int c = 0; c < COLUMNAS; c++)               // recorre las columnas
            tramaPixeles[f][c] = TINTE_PAPEL;            // celda en blanco
}

// Pinta una sola celda, ignorando posiciones fuera de la cuadricula
void pintarCelda(int fila, int columna, Tinte t) {
    if (fila < 0 || fila >= FILAS || columna < 0 || columna >= COLUMNAS) return; // fuera: no hace nada
    tramaPixeles[fila][columna] = t;                     // guarda el color en la matriz
}

// Pinta un bloque cuadrado centrado en la celda (radioPunta 0 = una celda, 1 = bloque de 3x3)
void estamparPunta(int fila, int columna, Tinte t, int radioPunta) {
    for (int df = -radioPunta; df <= radioPunta; df++)       // desplazamiento vertical
        for (int dc = -radioPunta; dc <= radioPunta; dc++)   // desplazamiento horizontal
            pintarCelda(fila + df, columna + dc, t);         // pinta cada celda del bloque
}

// ============================ ALGORITMO DE BRESENHAM (LINEA) =========
// Une dos celdas con una recta usando solo aritmetica entera
void trazarSegmento(int c0, int f0, int c1, int f1, Tinte t, int radioPunta) {
    int difX = std::abs(c1 - c0);        // distancia horizontal absoluta
    int difY = -std::abs(f1 - f0);       // distancia vertical, guardada en negativo
    int pasoX = (c0 < c1) ? 1 : -1;      // sentido de avance en columnas
    int pasoY = (f0 < f1) ? 1 : -1;      // sentido de avance en filas
    int acumulado = difX + difY;         // termino de error inicial de Bresenham
    while (true) {                       // se repite hasta llegar al punto final
        estamparPunta(f0, c0, t, radioPunta);          // pinta la celda actual
        if (c0 == c1 && f0 == f1) break;               // si llego al final, termina
        int doble = 2 * acumulado;                     // error duplicado (evita decimales)
        if (doble >= difY) { acumulado += difY; c0 += pasoX; } // avanza en columna
        if (doble <= difX) { acumulado += difX; f0 += pasoY; } // avanza en fila
    }
}

// ============================ BRESENHAM (CIRCULO, PUNTO MEDIO) =======
// Marca las 8 posiciones simetricas de un punto del circulo
void ocho_simetrias(int cc, int cf, int x, int y, Tinte t) {
    pintarCelda(cf + y, cc + x, t);   // octante 1
    pintarCelda(cf + y, cc - x, t);   // octante 2
    pintarCelda(cf - y, cc + x, t);   // octante 3
    pintarCelda(cf - y, cc - x, t);   // octante 4
    pintarCelda(cf + x, cc + y, t);   // octante 5
    pintarCelda(cf + x, cc - y, t);   // octante 6
    pintarCelda(cf - x, cc + y, t);   // octante 7
    pintarCelda(cf - x, cc - y, t);   // octante 8
}

// Circunferencia con centro (cc, cf) y radio entero, solo con enteros
void trazarCircunferencia(int cc, int cf, int radio, Tinte t) {
    int x = 0;                        // empieza arriba del circulo, en x = 0
    int y = radio;                    // con y igual al radio
    int decision = 1 - radio;         // parametro de decision inicial del punto medio
    while (x <= y) {                  // solo se calcula un octante
        ocho_simetrias(cc, cf, x, y, t);               // se replica a los otros siete
        if (decision < 0) {                            // el punto medio queda dentro del circulo
            decision += 2 * x + 3;                     // se mantiene y
        }
        else {                                       // el punto medio queda fuera
            decision += 2 * (x - y) + 5;               // se baja un paso en y
            y--;                                       // reduce y
        }
        x++;                                           // siempre avanza en x
    }
}

// ============================ FIGURAS CON BRESENHAM ==================
// Poligono regular: vertices sobre una circunferencia y lados unidos con Bresenham
void trazarPoligonoRegular(int cc, int cf, int radio, int lados, float anguloInicial, Tinte t) {
    std::vector<int> vertCol(lados), vertFila(lados);  // coordenadas de cada vertice
    for (int i = 0; i < lados; i++) {                  // calcula cada vertice
        float ang = (anguloInicial + 360.0f * i / lados) * PI_APROX / 180.0f; // angulo en radianes
        vertCol[i] = cc + (int)std::lround(radio * std::cos(ang)); // columna del vertice
        vertFila[i] = cf + (int)std::lround(radio * std::sin(ang)); // fila del vertice
    }
    for (int i = 0; i < lados; i++) {                  // une cada vertice con el siguiente
        int sig = (i + 1) % lados;                     // el ultimo se une con el primero
        trazarSegmento(vertCol[i], vertFila[i], vertCol[sig], vertFila[sig], t, 0); // lado
    }
}

// Cuadrado: el lado es la mayor distancia del arrastre; crece hacia donde se arrastra
void trazarCuadrado(int c0, int f0, int c1, int f1, Tinte t) {
    int dc = c1 - c0;                                  // desplazamiento horizontal
    int df = f1 - f0;                                  // desplazamiento vertical
    int lado = std::max(std::abs(dc), std::abs(df));   // lado = el mayor de los dos
    int sentidoC = (dc >= 0) ? 1 : -1;                 // hacia donde crece en columnas
    int sentidoF = (df >= 0) ? 1 : -1;                 // hacia donde crece en filas
    int cEsq = c0 + sentidoC * lado;                   // columna de la esquina opuesta
    int fEsq = f0 + sentidoF * lado;                   // fila de la esquina opuesta
    trazarSegmento(c0, f0, cEsq, f0, t, 0);      // lado superior
    trazarSegmento(cEsq, f0, cEsq, fEsq, t, 0);      // lado derecho
    trazarSegmento(cEsq, fEsq, c0, fEsq, t, 0);      // lado inferior
    trazarSegmento(c0, fEsq, c0, f0, t, 0);      // lado izquierdo
}

// Triangulo isosceles dentro del rectangulo formado por el arrastre
void trazarTriangulo(int c0, int f0, int c1, int f1, Tinte t) {
    int izq = std::min(c0, c1);                        // borde izquierdo del rectangulo
    int der = std::max(c0, c1);                        // borde derecho
    int arriba = std::min(f0, f1);                     // borde superior
    int abajo = std::max(f0, f1);                      // borde inferior
    int medio = (izq + der) / 2;                       // columna de la punta
    trazarSegmento(medio, arriba, izq, abajo, t, 0);   // lado izquierdo
    trazarSegmento(izq, abajo, der, abajo, t, 0);      // base
    trazarSegmento(der, abajo, medio, arriba, t, 0);   // lado derecho
}

// Elige que figura dibujar segun la herramienta activa
void trazarFigura(Utensilio u, int c0, int f0, int c1, int f1, Tinte t) {
    // Distancia entre el inicio y el cursor: sirve de radio para circulo y poligonos
    double dc = c1 - c0, df = f1 - f0;                          // diferencias en columnas y filas
    int radio = (int)std::lround(std::sqrt(dc * dc + df * df)); // radio redondeado
    switch (u) {                                                // segun la herramienta...
    case Utensilio::LINEA:     trazarSegmento(c0, f0, c1, f1, t, 0); break;               // recta
    case Utensilio::CIRCULO:   trazarCircunferencia(c0, f0, radio, t); break;             // circulo
    case Utensilio::TRIANGULO: trazarTriangulo(c0, f0, c1, f1, t); break;                 // triangulo
    case Utensilio::CUADRADO:  trazarCuadrado(c0, f0, c1, f1, t); break;                  // cuadrado
    case Utensilio::PENTAGONO: trazarPoligonoRegular(c0, f0, radio, 5, -90.0f, t); break; // 5 lados, punta arriba
    case Utensilio::HEXAGONO:  trazarPoligonoRegular(c0, f0, radio, 6, 0.0f, t); break;   // 6 lados
    default: break;                                                                        // otras: nada
    }
}

// ============================ FLOOD FILL (CUBETA) ====================
// Rellena la region conectada (4 vecinos) del color de la celda tocada
void rellenarInundacion(int fila, int columna, Tinte nuevo) {
    if (fila < 0 || fila >= FILAS || columna < 0 || columna >= COLUMNAS) return; // fuera: nada
    Tinte original = tramaPixeles[fila][columna];       // color que se va a reemplazar
    if (tintesIguales(original, nuevo)) return;         // si ya es igual, no hay que hacer nada
    std::vector<Casilla> pila;                          // pila de celdas pendientes (evita recursion)
    pila.push_back({ fila, columna });                    // empieza en la celda tocada
    while (!pila.empty()) {                             // mientras queden celdas por revisar
        Casilla actual = pila.back();                   // toma la ultima de la pila
        pila.pop_back();                                // y la saca
        int f = actual.fila, c = actual.columna;        // atajos para legibilidad
        if (f < 0 || f >= FILAS || c < 0 || c >= COLUMNAS) continue;  // fuera de la cuadricula
        if (!tintesIguales(tramaPixeles[f][c], original)) continue;   // es borde u otro color
        tramaPixeles[f][c] = nuevo;                     // pinta la celda con el color nuevo
        pila.push_back({ f + 1, c });                     // vecino de abajo
        pila.push_back({ f - 1, c });                     // vecino de arriba
        pila.push_back({ f, c + 1 });                     // vecino de la derecha
        pila.push_back({ f, c - 1 });                     // vecino de la izquierda
    }
}

// ============================ SELECCION Y GIRO =======================
// Pone todas las celdas de una matriz en blanco
void despejarMatriz(Tinte(&matriz)[FILAS][COLUMNAS]) {
    for (int f = 0; f < FILAS; f++)                      // recorre las filas
        for (int c = 0; c < COLUMNAS; c++)               // recorre las columnas
            matriz[f][c] = TINTE_PAPEL;                  // celda en blanco
}

// Indica si una posicion esta dentro de la cuadricula
bool celdaValida(int fila, int columna) {
    return fila >= 0 && fila < FILAS && columna >= 0 && columna < COLUMNAS; // dentro de los limites
}

// Calcula la figura girada a partir de la original y del angulo acumulado.
// Siempre parte de la original, asi los giros repetidos no la deforman.
void recalcularGiro() {
    despejarMatriz(tramaGirada);                         // empieza con el resultado vacio
    float rad = anguloGrados * PI_APROX / 180.0f;        // angulo en radianes
    float cs = std::cos(rad);                            // coseno del angulo
    float sn = std::sin(rad);                            // seno del angulo

    // Paso 1 (giro inverso): cada celda destino busca de que celda original proviene
    for (int f = 0; f < FILAS; f++) {                    // recorre filas destino
        for (int c = 0; c < COLUMNAS; c++) {             // recorre columnas destino
            float dx = c - pivoteCol;                    // distancia horizontal al centro de giro
            float dy = f - pivoteFila;                   // distancia vertical al centro de giro
            int origC = (int)std::lround(pivoteCol + dx * cs + dy * sn);   // columna de origen
            int origF = (int)std::lround(pivoteFila - dx * sn + dy * cs);  // fila de origen
            if (!celdaValida(origF, origC)) continue;                      // origen fuera: nada
            if (tintesIguales(tramaFlotante[origF][origC], TINTE_PAPEL)) continue; // origen vacio: nada
            tramaGirada[f][c] = tramaFlotante[origF][origC];               // copia el color
        }
    }

    // Paso 2 (giro directo): rellena huecos que el paso 1 haya dejado en los trazos finos
    for (int f = 0; f < FILAS; f++) {                    // recorre filas origen
        for (int c = 0; c < COLUMNAS; c++) {             // recorre columnas origen
            if (tintesIguales(tramaFlotante[f][c], TINTE_PAPEL)) continue; // celda vacia: se salta
            float dx = c - pivoteCol;                    // distancia horizontal al centro
            float dy = f - pivoteFila;                   // distancia vertical al centro
            int destC = (int)std::lround(pivoteCol + dx * cs - dy * sn);   // columna destino
            int destF = (int)std::lround(pivoteFila + dx * sn + dy * cs);  // fila destino
            if (!celdaValida(destF, destC)) continue;                      // destino fuera: nada
            if (tintesIguales(tramaGirada[destF][destC], TINTE_PAPEL))     // solo si esta vacio
                tramaGirada[destF][destC] = tramaFlotante[f][c];           // coloca el color
        }
    }

    // Calcula el rectangulo que ocupa la figura girada (sirve para dibujar el marco)
    vistaMinF = FILAS; vistaMaxF = -1;                   // valores iniciales invertidos
    vistaMinC = COLUMNAS; vistaMaxC = -1;                // idem para columnas
    for (int f = 0; f < FILAS; f++) {                    // recorre filas
        for (int c = 0; c < COLUMNAS; c++) {             // recorre columnas
            if (tintesIguales(tramaGirada[f][c], TINTE_PAPEL)) continue; // vacia: se salta
            vistaMinF = std::min(vistaMinF, f); vistaMaxF = std::max(vistaMaxF, f); // limites de filas
            vistaMinC = std::min(vistaMinC, c); vistaMaxC = std::max(vistaMaxC, c); // limites de columnas
        }
    }
}

// Deja la figura (ya girada) fija sobre el lienzo y termina la seleccion
void confirmarSeleccion() {
    if (!haySeleccion) return;                           // si no hay nada seleccionado, no hace nada
    for (int f = 0; f < FILAS; f++)                      // recorre filas
        for (int c = 0; c < COLUMNAS; c++)               // recorre columnas
            if (!tintesIguales(tramaGirada[f][c], TINTE_PAPEL))  // solo las celdas con color
                tramaPixeles[f][c] = tramaGirada[f][c];          // las estampa en el lienzo
    haySeleccion = false;                                // ya no hay figura flotante
}

// Toma la figura tocada: todas las celdas con color conectadas entre si (8 vecinos)
bool seleccionarFigura(int fila, int columna) {
    int iniF = -1, iniC = -1;                            // celda de partida (aun no encontrada)
    if (!tintesIguales(tramaPixeles[fila][columna], TINTE_PAPEL)) {  // la celda tocada tiene color?
        iniF = fila; iniC = columna;                     // entonces se parte de ella
    }
    else {                                             // si esta vacia, se buscan vecinas
        for (int df = -1; df <= 1 && iniF < 0; df++) {   // filas cercanas
            for (int dc = -1; dc <= 1; dc++) {           // columnas cercanas
                int f = fila + df, c = columna + dc;     // celda vecina
                if (celdaValida(f, c) && !tintesIguales(tramaPixeles[f][c], TINTE_PAPEL)) {
                    iniF = f; iniC = c;                  // encontro una con color
                    break;                               // sale del ciclo interno
                }
            }
        }
    }
    if (iniF < 0) return false;                          // no hay figura cerca del clic

    despejarMatriz(tramaFlotante);                       // prepara la copia de la figura
    std::vector<Casilla> pila;                           // pila de celdas por revisar
    pila.push_back({ iniF, iniC });                        // empieza en la celda encontrada
    int minF = FILAS, maxF = -1, minC = COLUMNAS, maxC = -1;  // limites de la figura
    while (!pila.empty()) {                              // mientras queden celdas
        Casilla actual = pila.back();                    // toma la ultima
        pila.pop_back();                                 // la saca de la pila
        int f = actual.fila, c = actual.columna;         // atajos
        if (!celdaValida(f, c)) continue;                // fuera de la cuadricula
        if (tintesIguales(tramaPixeles[f][c], TINTE_PAPEL)) continue; // vacia o ya tomada
        tramaFlotante[f][c] = tramaPixeles[f][c];        // copia la celda a la figura flotante
        tramaPixeles[f][c] = TINTE_PAPEL;                // la quita del lienzo (queda en blanco)
        minF = std::min(minF, f); maxF = std::max(maxF, f);  // actualiza limites de filas
        minC = std::min(minC, c); maxC = std::max(maxC, c);  // actualiza limites de columnas
        for (int df = -1; df <= 1; df++)                 // revisa los 8 vecinos
            for (int dc = -1; dc <= 1; dc++)             // en ambas direcciones
                if (df != 0 || dc != 0)                  // sin repetir la celda actual
                    pila.push_back({ f + df, c + dc });    // los agrega a la pila
    }

    pivoteCol = (minC + maxC) / 2.0f;                   // centro horizontal de la figura
    pivoteFila = (minF + maxF) / 2.0f;                   // centro vertical de la figura
    anguloGrados = 0.0f;                                 // sin giro al inicio
    haySeleccion = true;                                 // ahora hay una figura flotante
    recalcularGiro();                                    // arma la vista inicial
    return true;                                         // seleccion exitosa
}

// ============================ LIENZO EN PANTALLA =====================
// Cuadricula permanente: lineas de 1 pixel hechas con rectangulos delgados
void armarRejilla() {
    Tinte grisTenue = { 0.80f, 0.80f, 0.80f };            // color de las lineas
    for (int c = 0; c <= COLUMNAS; c++)                 // lineas verticales
        anadirCaja(ANCHO_BARRA + c * LADO_CELDA - 0.5f, 0, 1.0f, (float)ALTO_VENTANA, grisTenue);
    for (int f = 0; f <= FILAS; f++)                    // lineas horizontales
        anadirCaja((float)ANCHO_BARRA, f * LADO_CELDA - 0.5f, (float)(COLUMNAS * LADO_CELDA), 1.0f, grisTenue);
}

// Convierte una matriz en rectangulos: uno por cada celda que no sea blanca
void armarCeldasDe(const Tinte(&matriz)[FILAS][COLUMNAS]) {
    for (int f = 0; f < FILAS; f++) {                   // recorre filas
        for (int c = 0; c < COLUMNAS; c++) {            // recorre columnas
            if (tintesIguales(matriz[f][c], TINTE_PAPEL)) continue; // blanco: se omite
            anadirCaja((float)(ANCHO_BARRA + c * LADO_CELDA), (float)(f * LADO_CELDA),
                (float)LADO_CELDA, (float)LADO_CELDA, matriz[f][c]); // celda pintada
        }
    }
}

// Marco azul alrededor de la figura seleccionada
void armarMarcoSeleccion() {
    if (!haySeleccion || vistaMaxF < 0) return;          // sin seleccion visible: no hay marco
    Tinte azulMarco = { 0.15f, 0.40f, 0.80f };             // color del marco
    float x = (float)(ANCHO_BARRA + vistaMinC * LADO_CELDA) - 2.0f;   // borde izquierdo
    float y = (float)(vistaMinF * LADO_CELDA) - 2.0f;                 // borde superior
    float w = (float)((vistaMaxC - vistaMinC + 1) * LADO_CELDA) + 4.0f; // ancho del marco
    float h = (float)((vistaMaxF - vistaMinF + 1) * LADO_CELDA) + 4.0f; // alto del marco
    anadirCaja(x, y, w, 2.0f, azulMarco);                // lado superior
    anadirCaja(x, y + h - 2.0f, w, 2.0f, azulMarco);     // lado inferior
    anadirCaja(x, y, 2.0f, h, azulMarco);                // lado izquierdo
    anadirCaja(x + w - 2.0f, y, 2.0f, h, azulMarco);     // lado derecho
}

// ============================ PANEL CON IMGUI ========================
// Crea los doce colores de la paleta
void prepararPaleta() {
    paletaColores = {
        {0.00f, 0.00f, 0.00f},   // negro
        {0.85f, 0.10f, 0.10f},   // rojo
        {1.00f, 0.55f, 0.00f},   // naranja
        {1.00f, 0.90f, 0.10f},   // amarillo
        {0.10f, 0.70f, 0.20f},   // verde
        {0.00f, 0.40f, 0.20f},   // verde oscuro
        {0.10f, 0.30f, 0.90f},   // azul
        {0.40f, 0.80f, 1.00f},   // celeste
        {0.55f, 0.20f, 0.75f},   // morado
        {1.00f, 0.50f, 0.75f},   // rosado
        {0.50f, 0.30f, 0.12f},   // cafe
        {0.55f, 0.55f, 0.55f}    // gris
    };
}

// Dibuja un boton de herramienta; devuelve true si se hizo clic en el
bool botonUtensilio(const char* rotulo, Utensilio id) {
    bool elegido = (id == utensilioActual);              // esta herramienta esta activa?
    bool esLimpiar = (id == Utensilio::LIMPIAR_TODO);    // es el boton de borrar todo?
    bool colorear = elegido || esLimpiar;                // solo estos dos llevan color propio
    if (colorear) {                                      // si hay que colorear el boton...
        ImVec4 fondo = esLimpiar ? ImVec4(0.80f, 0.25f, 0.25f, 1.0f)   // rojo para limpiar todo
            : ImVec4(0.15f, 0.40f, 0.80f, 1.0f);  // azul si esta elegido
        ImGui::PushStyleColor(ImGuiCol_Button, fondo);          // color normal
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, fondo);   // color al pasar el mouse
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, fondo);    // color al presionar
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1)); // texto blanco
    }
    bool clic = ImGui::Button(rotulo, ImVec2(-1.0f, 26.0f));    // boton de ancho completo
    if (colorear) ImGui::PopStyleColor(4);                      // devuelve los 4 estilos
    return clic;                                                // informa si fue presionado
}

// Construye toda la barra lateral con ImGui
void construirPanelImGui() {
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));                          // pegada a la esquina
    ImGui::SetNextWindowSize(ImVec2((float)ANCHO_BARRA, (float)ALTO_VENTANA)); // ocupa todo el alto
    ImGui::SetNextWindowBgAlpha(1.0f);                                    // fondo totalmente opaco
    ImGuiWindowFlags banderas = ImGuiWindowFlags_NoDecoration |           // sin titulo ni bordes
        ImGuiWindowFlags_NoMove |                 // no se puede arrastrar
        ImGuiWindowFlags_NoSavedSettings;         // no guarda posicion
    ImGui::Begin("PanelLateral", nullptr, banderas);                      // abre la ventana del panel

    ImGui::Text("HERRAMIENTAS");                                          // titulo de la seccion
    ImGui::Separator();                                                   // linea divisoria

    // Un boton por cada herramienta (nombres en espanol)
    if (botonUtensilio("Pincel", Utensilio::PINCEL))    utensilioActual = Utensilio::PINCEL;    // trazo libre
    if (botonUtensilio("Linea", Utensilio::LINEA))     utensilioActual = Utensilio::LINEA;     // recta
    if (botonUtensilio("Circulo", Utensilio::CIRCULO))   utensilioActual = Utensilio::CIRCULO;   // circulo
    if (botonUtensilio("Triangulo", Utensilio::TRIANGULO)) utensilioActual = Utensilio::TRIANGULO; // triangulo
    if (botonUtensilio("Cuadrado", Utensilio::CUADRADO))  utensilioActual = Utensilio::CUADRADO;  // cuadrado
    if (botonUtensilio("Pentagono", Utensilio::PENTAGONO)) utensilioActual = Utensilio::PENTAGONO; // pentagono
    if (botonUtensilio("Hexagono", Utensilio::HEXAGONO))  utensilioActual = Utensilio::HEXAGONO;  // hexagono
    if (botonUtensilio("Cubeta", Utensilio::CUBETA))    utensilioActual = Utensilio::CUBETA;    // relleno
    if (botonUtensilio("Borrador", Utensilio::BORRADOR))  utensilioActual = Utensilio::BORRADOR;  // borrador
    if (botonUtensilio("Seleccionar", Utensilio::SELECCIONAR)) utensilioActual = Utensilio::SELECCIONAR; // elegir figura
    if (botonUtensilio("Rotar", Utensilio::ROTAR)) {                      // accion: gira la figura elegida
        if (haySeleccion) {                                               // solo si hay una figura seleccionada
            anguloGrados += PASO_GIRO_GRADOS;                             // suma el paso de giro
            recalcularGiro();                                             // recalcula la figura girada
        }
    }
    if (botonUtensilio("Limpiar todo", Utensilio::LIMPIAR_TODO)) {        // accion inmediata
        haySeleccion = false;                                             // descarta la figura flotante
        vaciarLienzo();                                                   // deja el lienzo en blanco
        arrastrando = false;                                              // cancela cualquier arrastre
    }

    // Si se cambia a otra herramienta, la figura seleccionada se fija donde quedo
    static Utensilio utensilioPrevio = Utensilio::PINCEL;                 // herramienta del cuadro anterior
    if (utensilioActual != utensilioPrevio) {                             // hubo un cambio de herramienta
        if (utensilioActual != Utensilio::SELECCIONAR) confirmarSeleccion(); // fija la figura al salir
        utensilioPrevio = utensilioActual;                                // recuerda la herramienta nueva
    }

    ImGui::Spacing();                                                     // pequeno espacio
    ImGui::Text("COLOR");                                                 // titulo de la paleta
    ImGui::Separator();                                                   // linea divisoria

    // Cuadros de color: 4 por fila
    for (int i = 0; i < (int)paletaColores.size(); i++) {                 // uno por cada color
        if (i % 4 != 0) ImGui::SameLine();                                // misma fila salvo cada 4
        ImGui::PushID(i);                                                 // identificador unico del cuadro
        Tinte t = paletaColores[i];                                       // color de este cuadro
        ImVec4 colorVisible(t.r, t.g, t.b, 1.0f);                         // lo convierte al formato de ImGui
        if (ImGui::ColorButton("##tinte", colorVisible, ImGuiColorEditFlags_NoTooltip, ImVec2(36.0f, 36.0f)))
            indiceTinteActual = i;                                        // clic: lo selecciona
        if (i == indiceTinteActual) {                                     // marca el color elegido
            ImVec2 esqMin = ImGui::GetItemRectMin();                      // esquina superior izquierda del cuadro
            ImVec2 esqMax = ImGui::GetItemRectMax();                      // esquina inferior derecha del cuadro
            ImGui::GetWindowDrawList()->AddRect(                          // dibuja un marco azul alrededor
                ImVec2(esqMin.x - 2.0f, esqMin.y - 2.0f),
                ImVec2(esqMax.x + 2.0f, esqMax.y + 2.0f),
                IM_COL32(38, 102, 204, 255), 0.0f, 0, 3.0f);
        }
        ImGui::PopID();                                                   // cierra el identificador
    }

    ImGui::End();                                                         // cierra la ventana del panel
}

// ============================ MOUSE (SOLO LIENZO) ====================
// Lee el mouse en cada cuadro y ejecuta la herramienta activa sobre el lienzo
void atenderRaton(GLFWwindow* ventana) {
    static bool estabaPulsado = false;                       // estado del boton en el cuadro anterior
    double mx, my;                                           // posicion del cursor
    glfwGetCursorPos(ventana, &mx, &my);                     // lee la posicion en pixeles
    bool pulsadoAhora = glfwGetMouseButton(ventana, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS; // esta presionado?
    bool recienPulsado = pulsadoAhora && !estabaPulsado;     // acaba de presionarse
    bool recienSoltado = !pulsadoAhora && estabaPulsado;     // acaba de soltarse
    estabaPulsado = pulsadoAhora;                            // recuerda el estado para el siguiente cuadro

    Tinte tinteElegido = paletaColores[indiceTinteActual];   // color seleccionado en la paleta

    if (recienPulsado) {                                     // ---- inicio del clic ----
        if (ImGui::GetIO().WantCaptureMouse) return;         // el clic fue sobre el panel de ImGui
        if (mx < ANCHO_BARRA || mx >= ANCHO_VENTANA || my < 0 || my >= ALTO_VENTANA) return; // fuera del lienzo
        int col = acotar((int)((mx - ANCHO_BARRA) / LADO_CELDA), 0, COLUMNAS - 1); // columna tocada
        int fil = acotar((int)(my / LADO_CELDA), 0, FILAS - 1);                    // fila tocada
        anclaColumna = col; anclaFila = fil;                 // guarda donde empezo el arrastre
        huellaColumna = col; huellaFila = fil;               // y la ultima celda procesada
        if (utensilioActual == Utensilio::SELECCIONAR) {     // seleccionar: actua una sola vez
            confirmarSeleccion();                            // fija la figura anterior donde quedo
            seleccionarFigura(fil, col);                     // intenta tomar la figura tocada
            arrastrando = false;                             // no hay arrastre
        }
        else if (utensilioActual == Utensilio::CUBETA) {   // cubeta: actua una sola vez
            rellenarInundacion(fil, col, tinteElegido);      // aplica Flood Fill
            arrastrando = false;                             // no hay arrastre
        }
        else if (utensilioActual == Utensilio::PINCEL) {   // pincel libre
            estamparPunta(fil, col, tinteElegido, 0);        // pinta la primera celda
            arrastrando = true;                              // empieza el arrastre
        }
        else if (utensilioActual == Utensilio::BORRADOR) { // borrador
            estamparPunta(fil, col, TINTE_PAPEL, 1);         // borra un bloque de 3x3
            arrastrando = true;                              // empieza el arrastre
        }
        else {                                             // cualquier figura
            std::memcpy(tramaRespaldo, tramaPixeles, sizeof(tramaPixeles)); // guarda el lienzo actual
            arrastrando = true;                              // empieza el arrastre
        }
    }

    if (arrastrando && pulsadoAhora) {                       // ---- mientras se mantiene presionado ----
        int col = acotar((int)((mx - ANCHO_BARRA) / LADO_CELDA), 0, COLUMNAS - 1); // celda actual (limitada)
        int fil = acotar((int)(my / LADO_CELDA), 0, FILAS - 1);                    // fila actual (limitada)
        if (utensilioActual == Utensilio::PINCEL) {          // trazo libre
            trazarSegmento(huellaColumna, huellaFila, col, fil, tinteElegido, 0); // une con Bresenham
            huellaColumna = col; huellaFila = fil;           // actualiza la ultima celda
        }
        else if (utensilioActual == Utensilio::BORRADOR) { // borrador
            trazarSegmento(huellaColumna, huellaFila, col, fil, TINTE_PAPEL, 1);  // borra el recorrido
            huellaColumna = col; huellaFila = fil;           // actualiza la ultima celda
        }
        else {                                             // figuras con vista previa
            std::memcpy(tramaPixeles, tramaRespaldo, sizeof(tramaPixeles)); // restaura el lienzo previo
            trazarFigura(utensilioActual, anclaColumna, anclaFila, col, fil, tinteElegido); // dibuja la figura
        }
    }

    if (recienSoltado) arrastrando = false;                  // ---- al soltar: la figura queda fija ----
}

// ============================ PROGRAMA PRINCIPAL =====================
int main() {
    glfwInit();                                              // inicia GLFW
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);           // pide OpenGL 3.x
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);           // version 3.3
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); // perfil Core
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);              // tamano fijo: 10 px por celda

    GLFWwindow* ventana = glfwCreateWindow(ANCHO_VENTANA, ALTO_VENTANA, "Paint - Luza Carrillo", nullptr, nullptr); // crea la ventana
    if (!ventana) { std::cout << "No se pudo crear la ventana\n"; glfwTerminate(); return -1; } // revisa errores
    glfwMakeContextCurrent(ventana);                         // activa el contexto de OpenGL

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { // carga las funciones de OpenGL
        std::cout << "No se pudo inicializar GLAD\n";        // aviso de error
        return -1;                                           // termina
    }

    // ---- Inicio de ImGui ----
    IMGUI_CHECKVERSION();                                    // verifica que los archivos de ImGui coincidan
    ImGui::CreateContext();                                  // crea el contexto de ImGui
    ImGui::StyleColorsLight();                               // tema claro, combina con el lienzo
    ImGui_ImplGlfw_InitForOpenGL(ventana, true);             // conecta ImGui con GLFW (y sus callbacks)
    ImGui_ImplOpenGL3_Init("#version 330");                  // conecta ImGui con OpenGL 3.3

    unsigned int programa = armarPrograma();                 // compila y enlaza los shaders
    int ubicResolucion = glGetUniformLocation(programa, "uResolucion"); // localiza el uniform una sola vez

    unsigned int vao, vbo;                                   // objetos de geometria
    glGenVertexArrays(1, &vao);                              // crea el VAO
    glGenBuffers(1, &vbo);                                   // crea el VBO
    glBindVertexArray(vao);                                  // activa el VAO
    glBindBuffer(GL_ARRAY_BUFFER, vbo);                      // activa el VBO
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);                   // atributo 0: posicion
    glEnableVertexAttribArray(0);                            // habilita el atributo 0
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(2 * sizeof(float))); // atributo 1: color
    glEnableVertexAttribArray(1);                            // habilita el atributo 1

    vaciarLienzo();                                          // lienzo inicial en blanco
    prepararPaleta();                                        // crea los colores de la paleta

    while (!glfwWindowShouldClose(ventana)) {                // ciclo principal
        // ---- Nuevo cuadro de ImGui ----
        ImGui_ImplOpenGL3_NewFrame();                        // prepara el lado OpenGL
        ImGui_ImplGlfw_NewFrame();                           // prepara el lado GLFW (mouse, tamano)
        ImGui::NewFrame();                                   // abre el cuadro de interfaz

        construirPanelImGui();                               // define los botones y la paleta
        atenderRaton(ventana);                               // procesa el mouse sobre el lienzo

        ImGui::Render();                                     // genera los datos de dibujo de ImGui

        int fbAncho, fbAlto;                                 // tamano real del framebuffer
        glfwGetFramebufferSize(ventana, &fbAncho, &fbAlto);  // lo consulta
        glViewport(0, 0, fbAncho, fbAlto);                   // ajusta el area de dibujo

        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);                // fondo blanco
        glClear(GL_COLOR_BUFFER_BIT);                        // limpia la pantalla

        acopioVertices.clear();                              // vacia la lista de vertices
        anadirCaja((float)ANCHO_BARRA, 0, (float)(COLUMNAS * LADO_CELDA), (float)(FILAS * LADO_CELDA), TINTE_PAPEL); // papel
        armarCeldasDe(tramaPixeles);                         // celdas dibujadas en el lienzo
        if (haySeleccion) armarCeldasDe(tramaGirada);        // figura seleccionada (ya girada)
        armarRejilla();                                      // cuadricula encima de las celdas
        armarMarcoSeleccion();                               // marco azul de la figura seleccionada

        glUseProgram(programa);                              // activa los shaders
        glUniform2f(ubicResolucion, (float)ANCHO_VENTANA, (float)ALTO_VENTANA); // envia la resolucion
        glBindVertexArray(vao);                              // activa el VAO
        glBindBuffer(GL_ARRAY_BUFFER, vbo);                  // activa el VBO
        glBufferData(GL_ARRAY_BUFFER, acopioVertices.size() * sizeof(float), acopioVertices.data(), GL_DYNAMIC_DRAW); // sube los datos
        glDrawArrays(GL_TRIANGLES, 0, (int)(acopioVertices.size() / 5)); // dibuja el lienzo

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); // dibuja el panel de ImGui encima

        glfwSwapBuffers(ventana);                            // muestra el cuadro
        glfwPollEvents();                                    // procesa eventos de la ventana
    }

    // ---- Cierre ordenado ----
    ImGui_ImplOpenGL3_Shutdown();                            // libera el lado OpenGL de ImGui
    ImGui_ImplGlfw_Shutdown();                               // libera el lado GLFW de ImGui
    ImGui::DestroyContext();                                 // destruye el contexto de ImGui
    glDeleteVertexArrays(1, &vao);                           // libera el VAO
    glDeleteBuffers(1, &vbo);                                // libera el VBO
    glDeleteProgram(programa);                               // libera el programa
    glfwTerminate();                                         // cierra GLFW
    return 0;                                                // fin del programa
}
