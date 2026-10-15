#!/usr/bin/env bash
set -euo pipefail


# Compila el programa y guarda cada prueba en un archivo diferente.
cd "$(dirname "$0")"
mkdir -p evidencias
gcc -std=gnu11 -Wall -Wextra -pthread MrMeeseeks.c -lm -o meeseeks


# Ejecuta una solicitud y comprueba que aparezca el resultado esperado.
prueba() {
    local nombre="$1" entrada="$2" esperado="$3"
    local archivo="evidencias/${nombre}.txt"
    printf '%s' "$entrada" | ./meeseeks --max-procesos 30 > "$archivo" 2>&1
    if grep -Fq "$esperado" "$archivo"; then
        printf 'OK: %s -> %s\n' "$nombre" "$archivo"
    else
        printf 'ERROR: %s (revisar %s)\n' "$nombre" "$archivo" >&2
        exit 1
    fi
}


# A: solicitudes textuales y creacion de Meeseeks.
prueba A1_facil $'1\nQuiero un cafe frances\n1\n95\n0.5\n0\n' 'Tarea 1: COMPLETADA'
prueba A2_intermedia $'1\nQuiero aprender aleman\n1\n60\n0.5\n0\n' 'convocar a 2 Meeseeks'
prueba A3_dificil $'1\nQuiero ser feliz\n1\n20\n0.5\n0\n' 'convocar a 5 Meeseeks'
prueba A4_imposible $'1\nQuiero pasar SO\n1\n0\n0.5\n0\n' 'Tarea 1: NO COMPLETADA'
prueba A5_aleatoria $'1\nQuiero viajar a Marte\n2\n0.5\n0\n' 'Facilidad aleatoria (uniforme):'


# B: operaciones aritmeticas, logicas y un error de calculo.
prueba B1_suma $'2\n12+8\n0\n' 'Resultado del calculo: 20'
prueba B2_potencia $'2\n5^2\n0\n' 'Resultado del calculo: 25'
prueba B3_xor $'2\n5xor2\n0\n' 'Resultado del calculo: 7'
prueba B4_division_cero $'2\n8/0\n0\n' 'Resultado del calculo: Division por cero'


# C: programas externos y sus codigos de salida.
prueba C1_correcto $'3\n/bin/true\n0\n' 'Resultado del programa: Codigo de salida: 0'
prueba C2_error $'3\n/bin/false\n0\n' 'Resultado del programa: Codigo de salida: 1'
prueba C3_argumentos $'3\n/bin/echo Hola Meeseeks\n0\n' 'Hola Meeseeks'
prueba C4_tiempo $'3\n/bin/sleep 1\n0\n' 'Tarea 1: COMPLETADA'


printf '\nLas salidas se guardaron en la carpeta evidencias/.\n'



