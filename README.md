# paintluzacarrillo
Proyecto de paint Luz Angelica Carrillo
Paint en C++ y OpenGL

Este proyecto consiste en una aplicación de dibujo 2D similar a Paint, desarrollada en C++ utilizando OpenGL. Cuenta con diferentes herramientas para dibujar, crear figuras, cambiar colores, rellenar espacios y rotar dibujos.

Tecnologías utilizadas

- C++
- OpenGL 3.3 Core
- GLFW
- GLAD
- Dear ImGui

Arquitectura del código:

El proyecto está organizado en un solo archivo de main:

- Variables y datos: contiene los colores, las herramientas y las matrices que guardan la información del dibujo.
- Interfaz: crea los botones y la paleta de colores.
- Dibujo: contiene las funciones para crear líneas y figuras.
- Algoritmos: incluye Bresenham, el algoritmo del punto medio y Flood Fill.
- Selección y rotación: permite tomar una figura del lienzo y girarla.
- Manejo del mouse: detecta los clics y movimientos para utilizar las herramientas.
- Renderizado: utiliza OpenGL para mostrar los dibujos en pantalla.

Funcionamiento

El lienzo está formado por una cuadrícula de 80 columnas y 60 filas. Cada celda guarda su color en una matriz llamada `tramaPixeles`.

Cuando el usuario utiliza una herramienta, el programa modifica esa matriz. Después, convierte las celdas dibujadas en figuras geométricas que OpenGL puede mostrar en pantalla.

Desarrollo

Para crear las herramientas se utilizaron diferentes métodos:

- Bresenham: para dibujar líneas.
- Punto medio: para crear circunferencias.
- Seno y coseno: para calcular los vértices de los polígonos y realizar las rotaciones.
- Flood Fill: para rellenar regiones de un mismo color.

Se implementó una herramienta de selección que permite separar una figura del lienzo y rotarla en intervalos de 15 grados.

La interfaz se realizó con Dear ImGui, mientras que GLFW y GLAD se utilizaron para trabajar con la ventana y OpenGL.

Resultado: Se obtuvo una aplicación de dibujo que reúne diferentes herramientas y permite aplicar los algoritmos y conceptos de programación gráfica trabajados durante el proyecto.

