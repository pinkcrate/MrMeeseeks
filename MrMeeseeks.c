/* 
                                  MR. MEESEEKS
Instituto Tecnológico de Costa Rica - Cede: Campus Tecnológico Central, Cartago
Escuela de Ingeniería en Computación
Curso: IC-6600 Principios de Sistemas Operativos
Profesor: M.Sc. Esteban Arias Méndez
Estudiante: Beatriz Rebeca Díaz Gómez - Carné: 2024090972
Entrega: 15 de Octubre, 2026*/

#define _GNU_SOURCE
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_TEXT 256
#define MAX_CHILDREN 5

typedef enum { TEXTUAL = 1, CALCULO = 2, EXTERNO = 3 } Tipo;

typedef struct {
    Tipo tipo;
    char texto[MAX_TEXT];
    double facilidad;
    double pausa;
} Tarea;

typedef struct {
    int completada;
    int cancelada;
    pid_t pid, ppid;
    int nivel, instancia;
    char detalle[128];
} Respuesta;

// Los contadores se comparten entre procesos y se protegen con el semaforo.
typedef struct {
    sem_t sem;
    int creados, activos, max_activos, limite;
    double cpu_total, pausas_total;
} Compartido;

typedef struct {
    pid_t pid;
    int resultado_fd, cancelar_fd;
    int terminado;
} Hijo;

typedef struct {
    Tarea tarea;
    Respuesta respuesta;
    double transcurrido, cpu;
    int creados, pico, caos;
} Registro;

static Compartido *estado;
static volatile sig_atomic_t interrumpido = 0;

static double reloj_monotonico(void) {
    // Mide el tiempo real transcurrido.
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static double reloj_cpu(void) {
    // Mide el uso de CPU sin contar las esperas.
    struct timespec t;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static void mensaje(const char *fmt, ...) {
    // Muestra cada mensaje con el PID y PPID.
    char linea[600], texto[480];
    va_list args;
    va_start(args, fmt);
    vsnprintf(texto, sizeof(texto), fmt, args);
    va_end(args);
    int n = snprintf(linea, sizeof(linea), "[pid=%ld ppid=%ld] %s\n",
                     (long)getpid(), (long)getppid(), texto);
    if (n > 0) write(STDOUT_FILENO, linea, (size_t)n);
}

static int bloquear(void) {
    // Espera el semaforo antes de usar los datos compartidos.
    while (sem_wait(&estado->sem) < 0) {
        if (errno != EINTR) return -1;
    }
    return 0;
}

static int leer_todo(int fd, void *dato, size_t n) {
    // Lee todos los datos enviados por el pipe.
    char *p = dato;
    while (n) {
        ssize_t r = read(fd, p, n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        p += r;
        n -= (size_t)r;
    }
    return 0;
}

static int escribir_todo(int fd, const void *dato, size_t n) {
    // Envia los datos completos por el pipe.
    const char *p = dato;
    while (n) {
        ssize_t r = write(fd, p, n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        p += r;
        n -= (size_t)r;
    }
    return 0;
}

static int esperar_o_cancelar(int fd, double segundos) {
    // Espera el tiempo indicado o una orden de cancelacion.
    struct pollfd p = {.fd = fd, .events = POLLIN | POLLHUP};
    int ms = (int)(segundos * 1000);
    for (;;) {
        int r = poll(&p, 1, ms);
        if (r < 0 && errno == EINTR) continue;
        return r != 0;
    }
}

static int calcular(const char *entrada, char *salida, size_t n) {
    // Lee los numeros y reconoce el operador con o sin espacios.
    const char *p = entrada;
    char *fin, op[4] = {0};
    double a, b, r = 0;
    while (isspace((unsigned char)*p)) p++;

    // Operaciones que necesitan un solo numero.
    if (*p == '!' || *p == '~') {
        char signo = *p++;
        a = strtod(p, &fin);
        if (p == fin || !isfinite(a)) goto formato;
        while (isspace((unsigned char)*fin)) fin++;
        if (*fin) goto formato;
        if (signo == '!') r = (a == 0);
        else {
            if (a != trunc(a) || a < INT_MIN || a > INT_MAX) goto entero;
            r = ~(int)a;
        }
        snprintf(salida, n, "%.10g", r);
        return 1;
    }

    a = strtod(p, &fin);
    if (p == fin || !isfinite(a)) goto formato;
    p = fin;
    while (isspace((unsigned char)*p)) p++;
    if (!strncmp(p, "xor", 3)) {
        strcpy(op, "xor");
        p += 3;
    } else if (!strncmp(p, "**", 2) || !strncmp(p, "&&", 2) ||
               !strncmp(p, "||", 2) || !strncmp(p, "==", 2) ||
               !strncmp(p, "!=", 2) || !strncmp(p, ">=", 2) ||
               !strncmp(p, "<=", 2) || !strncmp(p, "<<", 2) ||
               !strncmp(p, ">>", 2)) {
        op[0] = p[0]; op[1] = p[1]; p += 2;
    } else if (*p && strchr("+-*/%^<>&|", *p)) op[0] = *p++;
    else goto formato;

    while (isspace((unsigned char)*p)) p++;
    b = strtod(p, &fin);
    if (p == fin || !isfinite(b)) goto formato;
    while (isspace((unsigned char)*fin)) fin++;
    if (*fin) goto formato;

    // Calcula operaciones aritmeticas, potencias y comparaciones.
    if (!strcmp(op, "+")) r = a + b;
    else if (!strcmp(op, "-")) r = a - b;
    else if (!strcmp(op, "*")) r = a * b;
    else if (!strcmp(op, "/") || !strcmp(op, "%")) {
        if (b == 0) {
            snprintf(salida, n, "Division por cero");
            return 0;
        }
        r = !strcmp(op, "/") ? a / b : fmod(a, b);
    } else if (!strcmp(op, "^") || !strcmp(op, "**")) r = pow(a, b);
    else if (!strcmp(op, "==")) r = (a == b);
    else if (!strcmp(op, "!=")) r = (a != b);
    else if (!strcmp(op, ">")) r = (a > b);
    else if (!strcmp(op, "<")) r = (a < b);
    else if (!strcmp(op, ">=")) r = (a >= b);
    else if (!strcmp(op, "<=")) r = (a <= b);
    else if (!strcmp(op, "&&")) r = (a != 0 && b != 0);
    else if (!strcmp(op, "||")) r = (a != 0 || b != 0);
    else {
        // Las operaciones de bits solo funcionan con enteros.
        if (a != trunc(a) || b != trunc(b) || a < INT_MIN ||
            a > INT_MAX || b < INT_MIN || b > INT_MAX) goto entero;
        int x = (int)a, y = (int)b;
        if (!strcmp(op, "&")) r = x & y;
        else if (!strcmp(op, "|")) r = x | y;
        else if (!strcmp(op, "xor")) r = x ^ y;
        else if (!strcmp(op, "<<") || !strcmp(op, ">>")) {
            if (x < 0 || y < 0 || y > 30 ||
                (!strcmp(op, "<<") && (long long)x * (1LL << y) > INT_MAX)) {
                snprintf(salida, n, "Desplazamiento no valido");
                return 0;
            }
            r = !strcmp(op, "<<") ? (long long)x * (1LL << y) : x >> y;
        }
    }
    if (!isfinite(r)) {
        snprintf(salida, n, "Resultado no valido");
        return 0;
    }
    snprintf(salida, n, "%.10g", r);
    return 1;

entero:
    snprintf(salida, n, "Se necesitan numeros enteros");
    return 0;
formato:
    snprintf(salida, n, "Formato incorrecto");
    return 0;
}

static int ejecutar(const char *comando, char *salida, size_t n) {
    // Ejecuta un programa y revisa su codigo de salida.
    char copia[MAX_TEXT];
    char *argv[24], *guardar;
    int argc = 0;
    snprintf(copia, sizeof(copia), "%s", comando);
    for (char *p = strtok_r(copia, " \t", &guardar);
         p && argc < 23; p = strtok_r(NULL, " \t", &guardar)) argv[argc++] = p;
    argv[argc] = NULL;
    if (!argc) {
        snprintf(salida, n, "Comando vacio");
        return 0;
    }

    // Lanza el programa externo en otro proceso.
    pid_t pid = fork();
    if (pid < 0) {
        snprintf(salida, n, "No se pudo ejecutar el comando");
        return 0;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }

    // Espera su salida y recoge cuanto CPU utilizo.
    int status;
    struct rusage uso;
    pid_t resultado;
    do { resultado = wait4(pid, &status, 0, &uso); }
    while (resultado < 0 && errno == EINTR);
    if (resultado < 0) {
        snprintf(salida, n, "Error al esperar el programa");
        return 0;
    }

    double cpu = uso.ru_utime.tv_sec + uso.ru_utime.tv_usec / 1e6
               + uso.ru_stime.tv_sec + uso.ru_stime.tv_usec / 1e6;
    if (bloquear() == 0) {
        estado->cpu_total += cpu;
        sem_post(&estado->sem);
    }

    // Solo el codigo de salida cero cuenta como exito.
    if (WIFEXITED(status)) {
        snprintf(salida, n, "Codigo de salida: %d", WEXITSTATUS(status));
        return WEXITSTATUS(status) == 0;
    }
    snprintf(salida, n, "El programa termino por una senal");
    return 0;
}

static void trabajador(int solicitud_fd, int respuesta_fd, int cancelar_fd,
                       int nivel, int instancia);

static int crear_meeseeks(const Tarea *tarea, int nivel, int instancia,
                         Hijo *nuevo, const Hijo *anteriores, int cantidad,
                         int cancelar_heredado, int respuesta_heredada) {
    // Crea un Meeseeks hijo y le pasa su tarea por pipe.
    int datos[2], resultado[2], cancelar[2];
    if (pipe(datos) < 0) { perror("pipe"); return -1; }
    if (pipe(resultado) < 0) {
        perror("pipe"); close(datos[0]); close(datos[1]); return -1;
    }
    if (pipe(cancelar) < 0) {
        perror("pipe"); close(datos[0]); close(datos[1]);
        close(resultado[0]); close(resultado[1]); return -1;
    }

    // Controla el limite de Meeseeks con el semaforo.
    if (bloquear() < 0) {
        close(datos[0]); close(datos[1]);
        close(resultado[0]); close(resultado[1]);
        close(cancelar[0]); close(cancelar[1]);
        return -1;
    }
    if (estado->creados >= estado->limite) {
        sem_post(&estado->sem);
        close(datos[0]); close(datos[1]);
        close(resultado[0]); close(resultado[1]);
        close(cancelar[0]); close(cancelar[1]);
        return -1;
    }
    estado->creados++;
    estado->activos++;
    if (estado->activos > estado->max_activos)
        estado->max_activos = estado->activos;
    sem_post(&estado->sem);

    // Crea el hijo y corrige los contadores si falla.
    pid_t pid = fork();
    if (pid < 0) {
        if (bloquear() == 0) {
            estado->creados--;
            estado->activos--;
            sem_post(&estado->sem);
        }
        close(datos[0]); close(datos[1]);
        close(resultado[0]); close(resultado[1]);
        close(cancelar[0]); close(cancelar[1]);
        return -1;
    }
    // El hijo cierra los pipes que no debe usar.
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        close(datos[1]); close(resultado[0]); close(cancelar[1]);
        if (cancelar_heredado >= 0) close(cancelar_heredado);
        if (respuesta_heredada >= 0) close(respuesta_heredada);
        for (int i = 0; i < cantidad; i++) {
            close(anteriores[i].cancelar_fd);
            if (anteriores[i].resultado_fd >= 0)
                close(anteriores[i].resultado_fd);
        }
        // El grupo permite cerrar toda la familia si se agota el tiempo.
        if (nivel == 1) setpgid(0, 0);
        trabajador(datos[0], resultado[1], cancelar[0], nivel, instancia);
        _exit(0);
    }
    // El padre conserva los pipes para recibir la respuesta y cancelar.
    if (nivel == 1) setpgid(pid, pid);
    close(datos[0]); close(resultado[1]); close(cancelar[0]);
    nuevo->pid = pid;
    nuevo->resultado_fd = resultado[0];
    nuevo->cancelar_fd = cancelar[1];
    nuevo->terminado = 0;
    if (escribir_todo(datos[1], tarea, sizeof(*tarea)) < 0)
        mensaje("No se pudo transmitir la tarea a %ld", (long)pid);
    close(datos[1]);
    return 0;
}

static Respuesta resolver_con_hijos(Tarea *tarea, int nivel, int cancelar_fd,
                                   int respuesta_fd) {
    // Pide ayuda a otros Meeseeks y recoge la primera solucion.
    Respuesta final = {0};
    Hijo hijos[MAX_CHILDREN];
    // Para las tareas mas dificiles llama a cinco; para las otras, a dos.
    int cuantos = tarea->facilidad <= 45 ? 5 : 2;
    int creados = 0;
    Tarea siguiente = *tarea;
    // Aging: los nuevos hijos reciben una tarea mas facil, salvo el caso imposible.
    if (tarea->facilidad > 0)
        siguiente.facilidad += (100.0 - siguiente.facilidad) * 0.28;

    mensaje("Necesito ayuda: intentare convocar a %d Meeseeks (facilidad %.2f -> %.2f)",
            cuantos, tarea->facilidad, siguiente.facilidad);
    // Crea nuevos Meeseeks y les pasa la tarea por pipe.
    for (int i = 0; i < cuantos; i++) {
        if (crear_meeseeks(&siguiente, nivel + 1, i + 1, &hijos[creados],
                           hijos, creados, cancelar_fd, respuesta_fd) == 0)
            creados++;
        else mensaje("Limite de procesos alcanzado; no puedo convocar mas");
    }
    int pendientes = creados;
    // Escucha las respuestas de los hijos o una orden del padre.
    while (pendientes) {
        struct pollfd p[MAX_CHILDREN + 1];
        int indices[MAX_CHILDREN + 1];
        int total = 1;
        p[0] = (struct pollfd){.fd = cancelar_fd, .events = POLLIN | POLLHUP};
        indices[0] = -1;
        for (int i = 0; i < creados; i++) {
            if (hijos[i].terminado) continue;
            p[total] = (struct pollfd){.fd = hijos[i].resultado_fd,
                                      .events = POLLIN | POLLHUP};
            indices[total++] = i;
        }
        int listo = poll(p, (nfds_t)total, -1);
        if (listo < 0 && errno == EINTR) continue;
        if (listo < 0) break;

        if (p[0].revents && !final.cancelada) {
            char aviso;
            read(cancelar_fd, &aviso, 1);
            final.cancelada = 1;
            mensaje("Mi padre aviso que la mision termino. Cancelando mis hijos");
            for (int i = 0; i < creados; i++)
                if (!hijos[i].terminado) escribir_todo(hijos[i].cancelar_fd, "X", 1);
        }
        for (int j = 1; j < total; j++) {
            int i = indices[j];
            if (!(p[j].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            Respuesta recibida = {0};
            int ok = leer_todo(hijos[i].resultado_fd, &recibida, sizeof(recibida)) == 0;
            close(hijos[i].resultado_fd);
            hijos[i].resultado_fd = -1;
            hijos[i].terminado = 1;
            pendientes--;
            if (ok && recibida.completada && !final.completada && !final.cancelada) {
                // Conserva la primera solucion y avisa a los hermanos.
                final = recibida;
                mensaje("El Meeseeks %ld encontro la solucion; aviso a sus hermanos",
                        (long)recibida.pid);
                for (int k = 0; k < creados; k++) {
                    if (k != i && !hijos[k].terminado)
                        escribir_todo(hijos[k].cancelar_fd, "X", 1);
                }
            }
        }
    }
    // Cierra los pipes y espera la salida de los hijos.
    for (int i = 0; i < creados; i++) {
        if (!hijos[i].terminado) escribir_todo(hijos[i].cancelar_fd, "X", 1);
        close(hijos[i].cancelar_fd);
        if (hijos[i].resultado_fd >= 0) close(hijos[i].resultado_fd);
        while (waitpid(hijos[i].pid, NULL, 0) < 0 && errno == EINTR) {}
    }
    return final;
}

static void trabajador(int solicitud_fd, int respuesta_fd, int cancelar_fd,
                       int nivel, int instancia) {
    // Atiende la solicitud y le devuelve la respuesta al padre.
    // Recibe la solicitud y prepara sus datos de proceso.
    Tarea tarea;
    if (leer_todo(solicitud_fd, &tarea, sizeof(tarea)) < 0) _exit(2);
    close(solicitud_fd);
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    double cpu_inicio = reloj_cpu();
    Respuesta r = {.pid = getpid(), .ppid = getppid(),
                   .nivel = nivel, .instancia = instancia};
    mensaje("Hi, I'm Mr. Meeseeks! Look at Meeeee. (pid=%ld, ppid=%ld, N=%d, i=%d)",
            (long)getpid(), (long)getppid(), nivel, instancia);
    mensaje("Solicitud recibida por pipe: %s", tarea.texto);

    // En las tareas textuales espera, intenta resolver y pide ayuda si falla.
    if (tarea.tipo == TEXTUAL) {
        double pausa = tarea.pausa > 0 ? tarea.pausa : 0.5 + (rand() % 4501) / 1000.0;
        mensaje("Intentare resolverla; simulacion de %.3f segundos", pausa);
        double inicio_pausa = reloj_monotonico();
        int cancelado = esperar_o_cancelar(cancelar_fd, pausa);
        double pausa_real = reloj_monotonico() - inicio_pausa;
        if (bloquear() == 0) {
            estado->pausas_total += pausa_real;
            sem_post(&estado->sem);
        }
        if (cancelado) {
            r.cancelada = 1;
        } else if (tarea.facilidad > 85 ||
                   (nivel > 1 && tarea.facilidad > 0 &&
                    (rand() % 10000) / 100.0 < tarea.facilidad)) {
            r.completada = 1;
            mensaje("Cumpli la solicitud: %s. Gracias por dejarme ayudar", tarea.texto);
        } else {
            mensaje("No pude resolverla por mi cuenta (facilidad %.2f)", tarea.facilidad);
            r = resolver_con_hijos(&tarea, nivel, cancelar_fd, respuesta_fd);
        }
    // Un solo Meeseeks atiende los calculos y programas externos.
    } else if (tarea.tipo == CALCULO) {
        r.completada = calcular(tarea.texto, r.detalle, sizeof(r.detalle));
        mensaje("Resultado del calculo: %s", r.detalle);
    } else {
        r.completada = ejecutar(tarea.texto, r.detalle, sizeof(r.detalle));
        mensaje("Resultado del programa: %s", r.detalle);
    }

    if (r.completada) mensaje("Mision resuelta. Gracias! Ahora desaparezco");
    else if (r.cancelada) mensaje("La mision ya fue resuelta. Me despido y desaparezco");
    else mensaje("No fue posible completar la mision. Desaparezco");

    // Tiempo de CPU: no cuenta las esperas artificiales de la simulacion.
    if (bloquear() == 0) {
        estado->cpu_total += reloj_cpu() - cpu_inicio;
        estado->activos--;
        sem_post(&estado->sem);
    }
    // Devuelve la respuesta al padre por pipe.
    escribir_todo(respuesta_fd, &r, sizeof(r));
    close(respuesta_fd);
    close(cancelar_fd);
}

static void senal_box(int s) {
    // Marca la interrupcion para cerrar la Box.
    (void)s;
    interrumpido = 1;
}

static void leer_linea(const char *pregunta, char *destino, size_t tam) {
    // Lee lo escrito por el usuario y quita el salto de linea.
    printf("%s", pregunta);
    fflush(stdout);
    if (!fgets(destino, (int)tam, stdin)) {
        destino[0] = '\0';
        interrumpido = 1;
        return;
    }
    destino[strcspn(destino, "\n")] = '\0';
}

static int leer_numero(const char *pregunta, int defecto) {
    // Lee un entero o usa el valor por defecto.
    char linea[64];
    leer_linea(pregunta, linea, sizeof(linea));
    if (!*linea) return defecto;
    char *fin;
    long valor = strtol(linea, &fin, 10);
    while (isspace((unsigned char)*fin)) fin++;
    if (fin == linea || *fin || valor < INT_MIN || valor > INT_MAX) return -1;
    return (int)valor;
}

static double leer_decimal(const char *pregunta, double defecto) {
    // Lee un decimal o usa el valor por defecto.
    char linea[64];
    leer_linea(pregunta, linea, sizeof(linea));
    if (!*linea) return defecto;
    return strtod(linea, NULL);
}

int main(int argc, char **argv) {
    // Configura la Box, atiende solicitudes y muestra el informe.
    // Lee los limites indicados al iniciar el programa.
    int timeout = 300, max_procesos = 120, sin_limite = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--sin-limite")) sin_limite = 1;
        else if (!strcmp(argv[i], "--timeout") && i + 1 < argc) {
            timeout = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--max-procesos") && i + 1 < argc) {
            max_procesos = atoi(argv[++i]);
        } else {
            fprintf(stderr, "Uso: %s [--timeout SEG>=300] [--max-procesos 5..500] [--sin-limite]\n", argv[0]);
            return 1;
        }
    }
    if (timeout < 300 || max_procesos < 5 || max_procesos > 500) {
        fprintf(stderr, "Timeout minimo: 300 s. Limite de Meeseeks: 5..500\n");
        return 1;
    }
    signal(SIGINT, senal_box);
    signal(SIGTERM, senal_box);
    signal(SIGPIPE, SIG_IGN);
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    // Reserva memoria compartida para los contadores y el semaforo.
    estado = mmap(NULL, sizeof(*estado), PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (estado == MAP_FAILED || sem_init(&estado->sem, 1, 1)) {
        perror("mmap/sem_init");
        return 1;
    }
    estado->limite = max_procesos;
    Registro *historial = NULL;
    int usados = 0, capacidad = 0;

    printf("=== BOX DE MR. MEESEEKS ===\n");
    if (sin_limite) printf("Tiempo maximo: desactivado (experimento)");
    else printf("Tiempo maximo: %d segundos", timeout);
    printf("; limite de procesos: %d\n", max_procesos);
    // Recibe varias solicitudes sin reiniciar la Box.
    while (!interrumpido) {
        printf("\n1) Solicitud textual\n2) Calculo\n3) Programa externo\n0) Adios\n");
        int opcion = leer_numero("Opcion: ", -1);
        if (opcion == 0 || interrumpido) break;
        if (opcion < 1 || opcion > 3) { puts("Opcion invalida"); continue; }

        Tarea t = {.tipo = (Tipo)opcion};
        leer_linea("Escriba su solicitud: ", t.texto, sizeof(t.texto));
        if (!*t.texto || interrumpido) { puts("Solicitud vacia"); continue; }
        // Pide la facilidad y duracion si la solicitud es textual.
        if (t.tipo == TEXTUAL) {
            int modo = leer_numero("Facilidad: 1) Elegir  2) Aleatoria: ", 2);
            t.facilidad = modo == 1 ? leer_decimal("Valor de 0 a 100: ", 50)
                                    : (double)(rand() % 101);
            if (t.facilidad < 0 || t.facilidad > 100) {
                puts("Debe ser entre 0 y 100"); continue;
            }
            if (modo != 1) printf("Facilidad aleatoria (uniforme): %.0f\n", t.facilidad);
            t.pausa = leer_decimal("Segundos de simulacion (0 = aleatorio 0.5..5): ", 0);
            if (t.pausa < 0 || t.pausa > 900) { puts("Pausa invalida"); continue; }
        }

        // Reinicia los contadores para esta nueva solicitud.
        if (bloquear() == 0) {
            estado->creados = estado->activos = estado->max_activos = 0;
            estado->cpu_total = estado->pausas_total = 0;
            sem_post(&estado->sem);
        }
        // Crea el primer Meeseeks y empieza a medir el tiempo.
        Hijo raiz;
        double inicio = reloj_monotonico();
        if (crear_meeseeks(&t, 1, 1, &raiz, NULL, 0, -1, -1) < 0) {
            puts("No fue posible crear al primer Meeseeks"); continue;
        }
        Respuesta resultado = {0};
        int caos = 0, status = 0;
        // Espera al primer Meeseeks y vigila el limite de tiempo.
        for (;;) {
            pid_t fin = waitpid(raiz.pid, &status, WNOHANG);
            if (fin == raiz.pid) break;
            if (fin < 0 && errno != EINTR) break;
            if (interrumpido || (!sin_limite && reloj_monotonico() - inicio >= timeout)) {
                caos = 1;
                puts("CAOS PLANETARIO: se termino el tiempo. Cerrando todos los procesos.");
                kill(-raiz.pid, SIGTERM);
                usleep(200000);
                kill(-raiz.pid, SIGKILL);
                while (waitpid(raiz.pid, &status, 0) < 0 && errno == EINTR) {}
                break;
            }
            usleep(50000);
        }
        if (!caos) leer_todo(raiz.resultado_fd, &resultado, sizeof(resultado));
        close(raiz.resultado_fd);
        close(raiz.cancelar_fd);

        // Agranda el historial si ya no queda espacio.
        if (usados == capacidad) {
            capacidad = capacidad ? capacidad * 2 : 8;
            Registro *nuevo = realloc(historial, (size_t)capacidad * sizeof(*nuevo));
            if (!nuevo) { perror("realloc"); break; }
            historial = nuevo;
        }
        // Guarda tiempos, resultados y cantidad de procesos.
        Registro *r = &historial[usados++];
        *r = (Registro){.tarea = t, .respuesta = resultado,
                        .transcurrido = reloj_monotonico() - inicio, .caos = caos};
        if (bloquear() == 0) {
            r->cpu = estado->cpu_total;
            r->creados = estado->creados;
            r->pico = estado->max_activos;
            sem_post(&estado->sem);
        }
        printf("\nTarea %d: %s\n", usados, resultado.completada ? "COMPLETADA" : "NO COMPLETADA");
        if (resultado.completada)
            printf("Solucion: pid=%ld ppid=%ld N=%d i=%d %s\n",
                   (long)resultado.pid, (long)resultado.ppid,
                   resultado.nivel, resultado.instancia,
                   resultado.detalle);
        printf("Meeseeks: %d | Pico simultaneo: %d | Tiempo real: %.3f s | CPU sin pausas simuladas: %.6f s\n",
               r->creados, r->pico, r->transcurrido, r->cpu);
        if (caos) break;
    }

    // Muestra todas las tareas atendidas y sus estadisticas.
    puts("\n=== INFORME FINAL ===");
    printf("Total de tareas solicitadas: %d\n", usados);
    for (int i = 0; i < usados; i++) {
        Registro *r = &historial[i];
        printf("%d) [%s] %s | %.3f s reales | %.6f s CPU | %d Meeseeks",
               i + 1, r->respuesta.completada ? "OK" : "FALLO",
               r->tarea.texto, r->transcurrido, r->cpu, r->creados);
        if (r->caos) printf(" | CAOS PLANETARIO");
        printf("\n");
    }
    // Libera la memoria y destruye el semaforo.
    free(historial);
    sem_destroy(&estado->sem);
    munmap(estado, sizeof(*estado));
    puts("Hasta luego!");
    return 0;
}

