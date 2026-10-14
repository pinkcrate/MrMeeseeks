/* 
                                  MR. MEESEEKS
Instituto Tecnológico de Costa Rica - Cede: Campus Tecnológico Central, Cartago
Escuela de Ingeniería en Computación
Curso: IC-6600 Principios de Sistemas Operativos
Profesor: M.Sc. Esteban Arias Méndez
Estudiante: Beatriz Rebeca Díaz Gómez - Carné: 2024090972
Entrega: 15 de Octubre, 2026*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>


// Datos que se guardan de cada solicitud.
typedef struct {
    char solicitud[256];
    double segundos;
    int resuelta;
    int tipo;
} Registro;


static double tiempo(void) {
    // Mide el tiempo real transcurrido.
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
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
        op[0] = p[0];
        op[1] = p[1];
        p += 2;
    } else if (*p && strchr("+-*/%^<>&|", *p)) {
        op[0] = *p++;
    } else goto formato;

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



int main(void) {
    // Recibe solicitudes y crea un Meeseeks para atenderlas.
    setvbuf(stdout, NULL, _IONBF, 0);
    Registro historial[100];
    int total = 0;
    char linea[256];


    // Muestra el menu hasta que el usuario se despida.
    puts("=== BOX DE MR. MEESEEKS ===");
    while (total < 100) {
        printf("\n1) Solicitud textual\n2) Calculo\n0) Adios\nOpcion: ");
        if (!fgets(linea, sizeof(linea), stdin) || atoi(linea) == 0) break;
        int tipo = atoi(linea);
        if (tipo < 1 || tipo > 2) { puts("Opcion invalida"); continue; }


        // Lee la solicitud y elimina el salto de linea.
        printf("Solicitud: ");
        if (!fgets(linea, sizeof(linea), stdin)) break;
        linea[strcspn(linea, "\n")] = '\0';
        if (!*linea) continue;


        // Un pipe envia la tarea y otro devuelve la respuesta.
        int solicitud[2], respuesta[2];
        if (pipe(solicitud) == -1 || pipe(respuesta) == -1) {
            perror("pipe"); break;
        }
        double inicio = tiempo();
        // Crea un proceso hijo para atender la solicitud.
        pid_t hijo = fork();
        if (hijo < 0) { perror("fork"); break; }
        // El hijo recibe la tarea y se presenta como Meeseeks.
        if (hijo == 0) {
            close(solicitud[1]);
            close(respuesta[0]);
            char tarea[256] = {0};
            read(solicitud[0], tarea, sizeof(tarea));
            close(solicitud[0]);
            printf("[pid=%ld ppid=%ld] Hi, I'm Mr. Meeseeks! Look at Meeeee. (pid=%ld, ppid=%ld, N=1, i=1)\n",
                   (long)getpid(), (long)getppid(), (long)getpid(), (long)getppid());
            printf("[pid=%ld ppid=%ld] Resolviendo: %s\n",
                   (long)getpid(), (long)getppid(), tarea);
            fflush(stdout);
            int exito = 1;
            // Simula el texto o resuelve el calculo.
            if (tipo == 1) usleep(500000);
            else {
                char salida[128];
                exito = calcular(tarea, salida, sizeof(salida));
                printf("[pid=%ld ppid=%ld] Calculo: %s\n",
                       (long)getpid(), (long)getppid(), salida);
            }
            printf("[pid=%ld ppid=%ld] %s. Desaparezco.\n",
                   (long)getpid(), (long)getppid(),
                   exito ? "Mision completa. Gracias" : "Mision no completada");
            fflush(stdout);
            // Envia al padre si la tarea se completo o no.
            write(respuesta[1], &exito, sizeof(exito));
            close(respuesta[1]);
            _exit(0);
        }
        // El padre envia la tarea y espera el resultado.
        close(solicitud[0]);
        close(respuesta[1]);
        char mensaje[256] = {0};
        snprintf(mensaje, sizeof(mensaje), "%s", linea);
        write(solicitud[1], mensaje, sizeof(mensaje));
        close(solicitud[1]);
        waitpid(hijo, NULL, 0);
        int exito = 0;
        read(respuesta[0], &exito, sizeof(exito));
        close(respuesta[0]);


        // Registra el resultado y el tiempo de esta tarea.
        historial[total] = (Registro){.segundos = tiempo() - inicio, .resuelta = exito, .tipo = tipo};
        snprintf(historial[total].solicitud, sizeof(historial[total].solicitud), "%s", linea);
        total++;
        printf("Tarea %d: %s, %.3f segundos, 1 Meeseeks\n",
               total, exito ? "completada" : "fallida", historial[total - 1].segundos);
    }
    // Al salir, muestra lo que paso con cada solicitud.
    puts("\n=== INFORME FINAL ===");
    for (int i = 0; i < total; i++)
        printf("%d) %s | %s | %.3f segundos | 1 Meeseeks\n",
               i + 1, historial[i].solicitud,
               historial[i].resuelta ? "OK" : "FALLO", historial[i].segundos);
    return 0;
}
