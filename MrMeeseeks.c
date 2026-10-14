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


// Datos que se guardan de cada solicitud.
typedef struct {
    char solicitud[256];
    double segundos;
    int resuelta;
} Registro;


static double tiempo(void) {
    // Mide el tiempo real transcurrido.
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}


int main(void) {
    // Recibe solicitudes, crea un Meeseeks y guarda los resultados.
    setvbuf(stdout, NULL, _IONBF, 0);
    Registro historial[100];
    int total = 0;
    char linea[256];


    // Muestra el menu hasta que el usuario se despida.
    puts("=== BOX DE MR. MEESEEKS ===");
    while (total < 100) {
        printf("\n1) Solicitud textual\n0) Adios\nOpcion: ");
        if (!fgets(linea, sizeof(linea), stdin) || atoi(linea) == 0) break;
        if (atoi(linea) != 1) { puts("Opcion invalida"); continue; }


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
            // Simula medio segundo de trabajo.
            usleep(500000);
            int exito = 1;
            printf("[pid=%ld ppid=%ld] Mision completa. Gracias! Desaparezco.\n",
                   (long)getpid(), (long)getppid());
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
        historial[total] = (Registro){.segundos = tiempo() - inicio, .resuelta = exito};
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






