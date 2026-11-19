//========================================================================
// Laboratorio 07 - IHC 2026-B
// Reto: Panel grafico simplificado -> juego de la serpiente (Snake)
//
// Solo primitivas OpenGL 3.3 Core (GLFW + GLAD):
//   - Area principal : tablero 13x13 (relleno GL_TRIANGLES + borde GL_LINE_LOOP)
//   - Serpiente      : cuadrados (2 triangulos c/u) + punto blanco en la cabeza (GL_POINTS)
//   - Comida         : triangulo
//   - Opcion 1       : boton "cambiar color" (rojo -> verde -> azul)
//   - Opcion 2       : boton "reiniciar"
//   - Indicador      : cuadrado que muestra el color actual de la serpiente
//
// Teclas : flechas = mover, R = reiniciar, ESC = cerrar
// Mouse  : clic izquierdo (al SOLTAR dentro del boton) ejecuta la accion
// La barra de titulo funciona como barra de estado.
//========================================================================

#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <iostream>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>

// ------------------------------------------------------------------------
// Configuracion
// ------------------------------------------------------------------------
const unsigned int SCR_WIDTH  = 800;
const unsigned int SCR_HEIGHT = 600;

// Tablero: 13x13 celdas de 40x40 px (en NDC: 0.1 de ancho x 0.1333 de alto)
const int   COLS = 13, FILAS = 13;
const float CW = 0.1f, CH = 0.13333f;
const float BX0 = -0.95f;                 // esquina izquierda del tablero
const float BY0 = -0.5f * FILAS * CH;     // esquina inferior del tablero

const double PASO = 0.15;                 // segundos entre movimientos

inline float minf(float a, float b) { return a < b ? a : b; }

// ------------------------------------------------------------------------
// Shaders: un solo programa; uColor, uOffset (traslacion) y uEscala (x, y)
// ------------------------------------------------------------------------
const char *vertexShaderSource = "#version 330 core\n"
    "layout (location = 0) in vec3 aPos;\n"
    "uniform vec2 uOffset;\n"
    "uniform vec2 uEscala;\n"
    "void main()\n"
    "{\n"
    "   gl_Position = vec4(aPos.xy * uEscala + uOffset, aPos.z, 1.0);\n"
    "}\0";

const char *fragmentShaderSource = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "uniform vec4 uColor;\n"
    "void main()\n"
    "{\n"
    "   FragColor = uColor;\n"
    "}\n\0";

// ------------------------------------------------------------------------
// Estado del juego e interaccion
// ------------------------------------------------------------------------
struct Celda { int x, y; };
std::deque<Celda> serpiente;
Celda comida;
Celda dir    = { 1, 0 };     // direccion actual
Celda dirSig = { 1, 0 };     // direccion pedida con las flechas
int   puntos = 0;
bool  gameOver = false;

const float COLORES[3][3] = {
    { 0.95f, 0.15f, 0.15f },   // rojo
    { 0.10f, 0.85f, 0.20f },   // verde
    { 0.20f, 0.45f, 1.00f }    // azul
};
const char *NOMBRES[3] = { "Rojo", "Verde", "Azul" };
int colorActual = 0;

// Botones del panel derecho (centro y tamano en NDC)
struct Boton { float cx, cy, sx, sy; };
const Boton BOTONES[2] = {
    { 0.70f,  0.55f, 0.30f, 0.40f },   // 0: cambiar color
    { 0.70f,  0.00f, 0.30f, 0.40f }    // 1: reiniciar
};
const float IND_CX = 0.70f, IND_CY = -0.60f, IND_SX = 0.30f, IND_SY = 0.40f;

enum EstadoBoton { NORMAL, HOVER, PRESIONADO };
bool estabaPresionado = false;
int  botonInicio = -1;       // boton donde empezo la presion (-1 = ninguno)

// ------------------------------------------------------------------------
// Prototipos
// ------------------------------------------------------------------------
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
int  botonBajoCursor(GLFWwindow* window);
void accionBoton(GLFWwindow* window, int i);
void reiniciar(GLFWwindow* window);
void colocarComida();
void actualizarTitulo(GLFWwindow* window);
unsigned int crearPrograma();

int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Snake", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "No se pudo crear la ventana GLFW" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSwapInterval(1);

    if (!gladLoadGL(glfwGetProcAddress))
    {
        std::cout << "No se pudo inicializar GLAD" << std::endl;
        return -1;
    }

    unsigned int programa = crearPrograma();
    int locColor  = glGetUniformLocation(programa, "uColor");
    int locOffset = glGetUniformLocation(programa, "uOffset");
    int locEscala = glGetUniformLocation(programa, "uEscala");

    // --- Datos de geometria -------------------------------------------
    // Cuadrado unitario (indices: 2 triangulos) y triangulo unitario
    float cuadrado[] = {
         0.5f,  0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
        -0.5f, -0.5f, 0.0f,
        -0.5f,  0.5f, 0.0f
    };
    unsigned int indices[] = { 0, 1, 3,  1, 2, 3 };
    float triangulo[] = {
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
         0.0f,  0.5f, 0.0f
    };

    unsigned int VAO[2], VBO[2], EBO;
    glGenVertexArrays(2, VAO);
    glGenBuffers(2, VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO[0]);                                   // cuadrado
    glBindBuffer(GL_ARRAY_BUFFER, VBO[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cuadrado), cuadrado, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(VAO[1]);                                   // triangulo
    glBindBuffer(GL_ARRAY_BUFFER, VBO[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(triangulo), triangulo, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    float rangoLinea[2] = { 1.0f, 1.0f };
    glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, rangoLinea);

    // --- Funciones de dibujo (reutilizan los mismos VAO) -----------------
    auto relleno = [&](float cx, float cy, float sx, float sy, float r, float g, float b) {
        glUniform2f(locOffset, cx, cy);
        glUniform2f(locEscala, sx, sy);
        glUniform4f(locColor, r, g, b, 1.0f);
        glBindVertexArray(VAO[0]);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    };
    auto borde = [&](float cx, float cy, float sx, float sy, float r, float g, float b, float grosor) {
        glUniform2f(locOffset, cx, cy);
        glUniform2f(locEscala, sx, sy);
        glUniform4f(locColor, r, g, b, 1.0f);
        glBindVertexArray(VAO[0]);
        glLineWidth(minf(grosor, rangoLinea[1]));
        glDrawArrays(GL_LINE_LOOP, 0, 4);
    };
    auto puntos4 = [&](float cx, float cy, float sx, float sy, float r, float g, float b, float tam) {
        glUniform2f(locOffset, cx, cy);
        glUniform2f(locEscala, sx, sy);
        glUniform4f(locColor, r, g, b, 1.0f);
        glBindVertexArray(VAO[0]);
        glPointSize(tam);
        glDrawArrays(GL_POINTS, 0, 4);
    };
    auto celdaX = [](int gx) { return BX0 + (gx + 0.5f) * CW; };
    auto celdaY = [](int gy) { return BY0 + (gy + 0.5f) * CH; };

    std::srand((unsigned)std::time(NULL));
    reiniciar(window);
    double ultimoPaso = glfwGetTime();

    // --- Bucle de renderizado -----------------------------------------
    while (!glfwWindowShouldClose(window))
    {
        // ---- Mouse: estados de los botones ---------------------------
        int  dentro     = botonBajoCursor(window);
        bool presionado = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        if (presionado && !estabaPresionado) botonInicio = dentro;
        if (!presionado && estabaPresionado)
        {
            if (botonInicio != -1 && dentro == botonInicio) accionBoton(window, botonInicio);
            botonInicio = -1;
        }
        estabaPresionado = presionado;

        // ---- Logica de la serpiente ----------------------------------
        double ahora = glfwGetTime();
        if (!gameOver && ahora - ultimoPaso >= PASO)
        {
            ultimoPaso = ahora;
            dir = dirSig;
            Celda h = { serpiente.front().x + dir.x, serpiente.front().y + dir.y };
            bool choca = h.x < 0 || h.x >= COLS || h.y < 0 || h.y >= FILAS;
            for (size_t i = 0; i < serpiente.size() && !choca; i++)
                if (serpiente[i].x == h.x && serpiente[i].y == h.y) choca = true;

            if (choca) { gameOver = true; actualizarTitulo(window); }
            else
            {
                serpiente.push_front(h);
                if (h.x == comida.x && h.y == comida.y) { puntos++; colocarComida(); actualizarTitulo(window); }
                else serpiente.pop_back();
            }
        }

        // ---- Dibujo ---------------------------------------------------
        glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(programa);

        // Area principal: tablero
        float bcx = BX0 + 0.5f * COLS * CW, bsx = COLS * CW, bsy = FILAS * CH;
        relleno(bcx, 0.0f, bsx, bsy, 0.15f, 0.18f, 0.22f);
        borde(bcx, 0.0f, bsx, bsy, 0.9f, 0.9f, 0.9f, 3.0f);

        // Comida: triangulo naranja
        glUniform2f(locOffset, celdaX(comida.x), celdaY(comida.y));
        glUniform2f(locEscala, CW * 0.8f, CH * 0.8f);
        glUniform4f(locColor, 1.0f, 0.5f, 0.2f, 1.0f);
        glBindVertexArray(VAO[1]);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // Serpiente: cuadrados del color elegido (oscurecida si hay game over)
        float k = gameOver ? 0.4f : 1.0f;
        for (size_t i = 0; i < serpiente.size(); i++)
            relleno(celdaX(serpiente[i].x), celdaY(serpiente[i].y), CW * 0.9f, CH * 0.9f,
                    COLORES[colorActual][0] * k, COLORES[colorActual][1] * k, COLORES[colorActual][2] * k);

        // Cabeza marcada con un punto blanco (GL_POINTS)
        glUniform2f(locOffset, celdaX(serpiente.front().x), celdaY(serpiente.front().y));
        glUniform2f(locEscala, 0.0f, 0.0f);
        glUniform4f(locColor, 1.0f, 1.0f, 1.0f, 1.0f);
        glBindVertexArray(VAO[0]);
        glPointSize(8.0f);
        glDrawArrays(GL_POINTS, 0, 1);

        // Botones (relleno + borde verde + vertices rojos, 3 estados)
        for (int i = 0; i < 2; i++)
        {
            EstadoBoton est = NORMAL;
            if (presionado && botonInicio == i && dentro == i) est = PRESIONADO;
            else if (dentro == i && !presionado)               est = HOVER;

            float r = (i == 0) ? 1.0f : 0.6f, g = (i == 0) ? 0.5f : 0.65f, b = (i == 0) ? 0.2f : 0.75f;
            float sx = BOTONES[i].sx, sy = BOTONES[i].sy, grosor = 3.0f;
            if (est == HOVER)      { r += 0.15f; g += 0.15f; b += 0.15f; grosor = 6.0f; }
            if (est == PRESIONADO) { r *= 0.7f;  g *= 0.7f;  b *= 0.7f;  grosor = 6.0f; sx *= 0.94f; sy *= 0.94f; }

            relleno(BOTONES[i].cx, BOTONES[i].cy, sx, sy, minf(r, 1.0f), minf(g, 1.0f), minf(b, 1.0f));
            borde(BOTONES[i].cx, BOTONES[i].cy, sx, sy, 0.1f, 0.8f, 0.2f, grosor);
            puntos4(BOTONES[i].cx, BOTONES[i].cy, sx, sy, 0.9f, 0.1f, 0.1f, 12.0f);
        }

        // Indicador de estado: color actual de la serpiente
        relleno(IND_CX, IND_CY, IND_SX, IND_SY, COLORES[colorActual][0], COLORES[colorActual][1], COLORES[colorActual][2]);
        borde(IND_CX, IND_CY, IND_SX, IND_SY, 0.9f, 0.9f, 0.9f, 3.0f);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(2, VAO);
    glDeleteBuffers(2, VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteProgram(programa);
    glfwTerminate();
    return 0;
}

// ------------------------------------------------------------------------
// Devuelve el indice del boton bajo el cursor (-1 si ninguno).
// Pixeles -> NDC (-1..1, y hacia arriba).
// ------------------------------------------------------------------------
int botonBajoCursor(GLFWwindow* window)
{
    double mx, my;
    int w, h;
    glfwGetCursorPos(window, &mx, &my);
    glfwGetWindowSize(window, &w, &h);
    if (w == 0 || h == 0) return -1;

    float xNdc = (float)(2.0 * mx / w - 1.0);
    float yNdc = (float)(1.0 - 2.0 * my / h);

    for (int i = 0; i < 2; i++)
        if (std::fabs(xNdc - BOTONES[i].cx) <= 0.5f * BOTONES[i].sx &&
            std::fabs(yNdc - BOTONES[i].cy) <= 0.5f * BOTONES[i].sy)
            return i;
    return -1;
}

void accionBoton(GLFWwindow* window, int i)
{
    if (i == 0) { colorActual = (colorActual + 1) % 3; actualizarTitulo(window); }
    else        { reiniciar(window); }
}

void reiniciar(GLFWwindow* window)
{
    serpiente.clear();
    serpiente.push_back({ 6, 6 });
    serpiente.push_back({ 5, 6 });
    serpiente.push_back({ 4, 6 });
    dir = dirSig = { 1, 0 };
    puntos = 0;
    gameOver = false;
    colocarComida();
    actualizarTitulo(window);
}

void colocarComida()
{
    bool libre = false;
    while (!libre)
    {
        comida.x = std::rand() % COLS;
        comida.y = std::rand() % FILAS;
        libre = true;
        for (size_t i = 0; i < serpiente.size(); i++)
            if (serpiente[i].x == comida.x && serpiente[i].y == comida.y) libre = false;
    }
}

// ------------------------------------------------------------------------
// Teclado: flechas mueven (sin permitir giro de 180 grados), R reinicia.
// ------------------------------------------------------------------------
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    (void)scancode; (void)mods;
    if (action != GLFW_PRESS) return;

    if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(window, GLFW_TRUE);
    if (key == GLFW_KEY_R)      reiniciar(window);

    Celda n = dirSig;
    if      (key == GLFW_KEY_UP)    n = {  0,  1 };
    else if (key == GLFW_KEY_DOWN)  n = {  0, -1 };
    else if (key == GLFW_KEY_LEFT)  n = { -1,  0 };
    else if (key == GLFW_KEY_RIGHT) n = {  1,  0 };
    else return;

    if (n.x != -dir.x || n.y != -dir.y) dirSig = n;
}

void actualizarTitulo(GLFWwindow* window)
{
    char titulo[200];
    std::snprintf(titulo, sizeof(titulo),
                  "Snake | Color: %s | Puntos: %d | Flechas: mover, R: reiniciar%s",
                  NOMBRES[colorActual], puntos, gameOver ? " | GAME OVER" : "");
    glfwSetWindowTitle(window, titulo);
}

unsigned int crearPrograma()
{
    int  exito;
    char log[512];

    unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertexShaderSource, NULL);
    glCompileShader(vs);
    glGetShaderiv(vs, GL_COMPILE_STATUS, &exito);
    if (!exito) { glGetShaderInfoLog(vs, 512, NULL, log); std::cout << "ERROR VERTEX SHADER\n" << log << std::endl; }

    unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragmentShaderSource, NULL);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &exito);
    if (!exito) { glGetShaderInfoLog(fs, 512, NULL, log); std::cout << "ERROR FRAGMENT SHADER\n" << log << std::endl; }

    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &exito);
    if (!exito) { glGetProgramInfoLog(prog, 512, NULL, log); std::cout << "ERROR LINK\n" << log << std::endl; }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    (void)window;
    glViewport(0, 0, width, height);
}