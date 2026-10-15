# MR. MEESEEKS


Instituto Tecnológico de Costa Rica - Sede: Campus Tecnológico Central, Cartago
Escuela de Ingeniería en Computación  
Curso: IC-6600 Principios de Sistemas Operativos  
Profesor: M.Sc. Esteban Arias Méndez  
Estudiante: Beatriz Rebeca Díaz Gómez - Carné: 2024090972  
Entrega: 15 de octubre de 2026


## Descripción


Programa en C para Linux que simula la Box de Mr. Meeseeks. Recibe solicitudes, crea procesos con `fork()` y presenta los resultados y tiempos de ejecución al terminar. Se pueden realizar solicitudes textuales, cálculos o ejecutar programas externos.


En las solicitudes textuales, cada Meeseeks intenta cumplir la tarea. Cuando necesita ayuda, crea otros Meeseeks y les comunica la solicitud por pipes. Para controlar los contadores de procesos se usan memoria compartida y semáforos.


## Compilación y ejecución


Desde la carpeta del proyecto:


```bash
gcc -std=gnu11 -Wall -Wextra -pthread MrMeeseeks.c -lm -o meeseeks
./meeseeks
```


También se pueden indicar límites al ejecutar:


```bash
./meeseeks --max-procesos 30
./meeseeks --timeout 360 --max-procesos 100
./meeseeks --sin-limite --max-procesos 30
```


- `--max-procesos`: máximo total de Meeseeks por solicitud (entre 5 y 500). Por defecto es 120.
- `--timeout`: tiempo máximo de una solicitud, en segundos. El mínimo es 300 y el valor por defecto también es 300.
- `--sin-limite`: desactiva el límite de tiempo para experimentos. **No** desactiva el límite de procesos.


## Opciones del menú


1. **Solicitud textual:** se escribe la tarea y se elige una facilidad de 0 a 100 o un valor aleatorio. También se indica el tiempo de simulación; escribir `0` permite generar uno aleatorio entre 0.5 y 5 segundos.
2. **Cálculo:** se ingresa una operación aritmética, lógica, de comparación o de bits. Acepta expresiones con o sin espacios. En este programa `^` y `**` indican potencia, mientras que `xor` indica XOR de bits.
3. **Programa externo:** se escribe un comando como `/bin/true`, `/bin/false` o `/bin/echo hola`. El programa revisa su código de salida; el valor 0 se considera correcto.
0. **Adiós:** termina la Box y muestra el informe de solicitudes.


Los programas externos se ejecutan con `execvp()`. Se admiten argumentos simples separados por espacios, pero no expresiones propias de una terminal, como tuberías o redirecciones.


## Dificultad de las tareas textuales


El valor representa la **facilidad**, no el nivel de dificultad: 100 es muy fácil y 0 es imposible.


|       Facilidad           |             Comportamiento                                        |
|            0              | No puede resolverse; se aplica el límite de procesos o de tiempo. |
| Mayor que 0 y hasta 45    | Si no logra resolverla, intenta convocar 5 hijos.                 |
| Mayor que 45 y hasta 85   | Si no logra resolverla, intenta convocar 2 hijos.                 |
| Mayor que 85 y hasta 100  | La resuelve un solo Meeseeks.                                     |


Cuando se crean hijos para una tarea realizable, el valor de facilidad aumenta progresivamente. La cantidad final de procesos puede variar y la creación de hijos está sujeta al límite configurado.


## Pruebas


Ejecutar desde la carpeta que contiene `MrMeeseeks.c`:


```bash
bash pruebas.sh
```


El script vuelve a compilar el programa, ejecuta pruebas de los tres tipos de solicitud y guarda las salidas en `evidencias/`. Incluye tareas fáciles, intermedias, difíciles e imposibles; operaciones válidas e inválidas; y programas externos que terminan bien o con error.


Los archivos `.txt` de `evidencias/` se pueden usar para revisar el desarrollo de cada ejecución y seleccionar capturas para el reporte. Los resultados aleatorios de las tareas textuales pueden variar entre corridas.


**Pruebas largas:** `pruebas.sh` está pensado para pruebas cortas. Los experimentos de cinco minutos deben ejecutarse aparte porque requieren esperar el tiempo real de la simulación.


### Pruebas del límite de cinco minutos


Para comprobar qué ocurre al llegar al máximo de 300 segundos:


```bash
mkdir -p evidencias
./meeseeks --timeout 300 --max-procesos 120 | tee evidencias/limite_5_minutos.txt
```


Ingresar estos datos, uno por uno, cuando aparezcan las preguntas:


```text
1
Quiero aprender aleman
1
20
151
```


El primer Meeseeks trabaja durante 151 segundos antes de pedir ayuda. Sus hijos requieren otra espera de 151 segundos, por lo que, si la solicitud sigue activa a los 300 segundos, debe aparecer el mensaje `CAOS PLANETARIO` y finalizar la familia de procesos.


Para observar cuánto tarda la misma solicitud sin el temporizador:


```bash
./meeseeks --sin-limite --max-procesos 120 | tee evidencias/sin_limite_5_minutos.txt
```


Ingresar los mismos datos. Esta vez no habrá un límite de tiempo, **pero se mantiene el máximo de 120 Meeseeks**. La ejecución puede superar cinco minutos y no se garantiza que termine aproximadamente a los 302 segundos: depende de las probabilidades y de si se alcanza antes el límite de procesos. Si tarda demasiado, se puede interrumpir con `Ctrl+C`. Si termina una solicitud y regresa al menú, escribir `0` para mostrar el informe final.


Comparar en los dos archivos el estado final, los Meeseeks creados, el pico simultáneo y el tiempo real. Si el experimento sin límite logra completarse, calcular el tiempo adicional restando **300 segundos** a su tiempo total. Si termina sin completar la tarea por el límite de procesos, se debe registrar ese resultado en lugar de afirmar que la tarea se resolvió. Como las decisiones son probabilísticas, las dos corridas pueden producir árboles distintos y conviene repetir el experimento para sacar conclusiones.


Una tarea imposible (facilidad 0) también puede terminar antes de los cinco minutos por alcanzar el límite de procesos.


## Archivos


- `MrMeeseeks.c`: código fuente del programa.
- `pruebas.sh`: ejecución automática de casos de prueba.
- `.gitignore`: evita subir el ejecutable y las salidas generadas.


El reporte PDF de la tarea se entrega por separado, según las indicaciones del curso.



