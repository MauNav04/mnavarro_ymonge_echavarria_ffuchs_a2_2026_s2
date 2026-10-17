# Ejecución automática en la Ryzen

Ejecutar desde `proyecto1/problems/mnavarro/fractal-cpp/`, en Ubuntu nativo de la
Ryzen de evaluación. La interfaz habitual consta de cuatro comandos:

```bash
make setup    # Una vez: entorno Python y dependencias científicas
make quick    # Diagnóstico completo: captura, gráficas y ZIP
make run      # SMT ON y OFF: benchmark/escalabilidad 200, Perf 20, gráficas y ZIP
make report   # Regenerar el informe usando los datos existentes
```

No hace falta activar `.venv`: Make utiliza su Python automáticamente cuando existe.
Los comandos anteriores de compilación, pruebas y medición siguen disponibles;
`make help_advanced` muestra las opciones.

## Preparación y prueba rápida

El equipo debe tener las dependencias de sistema ya documentadas en
[pruebas](testing.md) y [profiling](profiling.md): compilador, CMake, OpenCV,
Python con venv y Linux Perf compatible con su kernel. `make setup` crea
`.venv` e instala `scripts/requirements.txt`; no instala paquetes del sistema,
cambia permisos ni modifica SMT.

`make quick` compila en Release y comprueba primero topología, afinidad y
disponibilidad de Perf. Después ejecuta los cinco modelos, escalabilidad y
profiling, analiza los CSV y exporta los resultados. Usa 5 repeticiones para el
benchmark, 2 por configuración de escalabilidad y 1 por grupo de Perf.
Estos datos se identifican como **diagnósticos**, no como campaña formal.

Si una comprobación falla, el proceso se detiene, muestra el error y conserva
los registros parciales. No iniciar varios procesos de medición simultáneamente.

## Campaña completa con un comando

Después de que termine correctamente la prueba rápida:

```bash
make run
```

El comando realiza automáticamente:

1. Compilación Release de todos los modelos.
2. Validación del destino y autorización sudo para controlar SMT.
3. Activación y comprobación de SMT ON.
4. Benchmark con 200 repeticiones por modelo, escalabilidad con 200 por configuración
   y Perf con 20 por modelo/grupo disponible.
5. Desactivación de SMT por software y repetición de esas tres campañas con SMT OFF.
6. Restauración y comprobación de SMT ON.
7. Análisis conjunto ON/OFF, tablas, figuras 01–14 y exportación ZIP.

Las repeticiones de Perf son por grupo: el total de ejecuciones del compresor
es de hasta 120 por modelo con seis grupos disponibles, más sondeos y calentamientos.
Perf acepta un mínimo de 15 repeticiones por grupo; benchmark y escalabilidad
conservan 200 por configuración. La duración depende de la imagen y del hardware.
La consola muestra cada fase y las ubicaciones de resultados.

Los valores habituales ya están fijados: `build/`, imagen `assets/color128.png`,
range 8, stride 8, cuantum 1, equipos 4×2 para Fine/Coarse/CMP y 4×4 para SMT.
La entrada predeterminada mantiene compatibilidad con las campañas anteriores.
Si se desea una imagen más pesada, basta una opción:

```bash
make run INPUT=assets/mirrors512.png
```

Utilizar la misma imagen, parámetros y binarios para comparar ON/OFF. No es
necesario especificar `BUILD_DIR`, `SMT_STATE`, número de corridas ni rutas CSV.

## Comparación SMT ON/OFF

Una sola ejecución de `make run` realiza ON → OFF → ON mediante
`/sys/devices/system/cpu/smt/control`. Ubuntu debe permitir CPU hotplug y SMT
debe estar disponible en BIOS/UEFI: un SMT deshabilitado permanentemente no se
puede activar con este comando.

Puede pedir la contraseña de `sudo` una vez al inicio. No ejecutar `sudo make run`:
sólo el ayudante ON/OFF recibe privilegios; compilación, compresores y análisis
siguen como el usuario normal. No modifica permisos de Perf ni configura sudo.
El ayudante permanece vivo durante las mediciones, por lo que la restauración no
depende de la caducidad de las credenciales. Un lock impide dos controladores simultáneos.

Se comprueba la topología real y la afinidad en cada estado. Los eventos Perf no
soportados aparecen como N/D. El informe de esta ejecución usa exclusivamente su
pareja nueva ON/OFF, sin reemplazar capturas por campañas antiguas.

Ante error, Ctrl+C, SIGTERM o cierre del canal del proceso principal se intenta
restaurar ON y se verifica el resultado. Los datos parciales se conservan; si
falla la restauración, el comando termina con error y muestra cómo recuperarlo:

```bash
echo on | sudo tee /sys/devices/system/cpu/smt/control
cat /sys/devices/system/cpu/smt/active   # Debe mostrar 1
```

Un corte de energía o la terminación forzada del propio ayudante con SIGKILL no
permite ejecutar su restauración. `make quick`, `profile_perf` y los demás
objetivos individuales conservan el estado actual y permiten diagnósticos separados.

## Dónde están los resultados

Cada ejecución utiliza un nombre único con fecha UTC e identificador:

```text
metrics/
  automatic/run-pair-<fecha-id>/
    run.json
    run-on-<fecha-id>/
      run.json
      check/
      benchmark/
      scaling/
      perf/
    run-off-<fecha-id>/   # Misma estructura para OFF
  reports/physical-<fecha-id>/
    benchmark/   # Figuras 01–09 y tablas de rendimiento
    perf/        # Figuras 10–14 y tablas de perfilado
    report.json
  latest.json    # Rutas del último informe y ZIP; estado y fuentes utilizadas
results/
  exports/physical-<fecha-id>.zip
```

Las pruebas rápidas usan nombres `quick-...` e informes `diagnostic-...`.
Se conservan los CSV, metadatos y registros originales. Perf guarda las salidas
crudas en `perf-stat.jsonl`, evitando miles de archivos `.stat` independientes.
El ZIP contiene toda la carpeta de métricas, incluidos los CSV y el informe
actual; permite compartir un solo archivo. No borra los originales ni sobrescribe
exportaciones previas.

Las 14 figuras se guardan a 300 DPI. Los colores por modelo son constantes en ambos
analizadores: secuencial `#64748b`, CMP `#1f77b4`, SMT `#059669`, Coarse
`#7c3aed` y Fine `#0891b2`. OFF usa trazos discontinuos o barras rayadas para
distinguirlo de ON sin cambiar el color del modelo.

## Si ya se hicieron las 200 pruebas

```bash
make report
```

No repite mediciones ni requiere compilar. Busca recursivamente las campañas de
benchmark, escalabilidad y Perf en `metrics/`, incluidas las carpetas anteriores
como `final-on`, `scaling-final-on` y `perf-on`. Necesita los CSV y sus
`metadata.json`, no solamente las imágenes o los archivos `.stat`.

Selecciona la familia completa compatible más reciente, dando prioridad a datos
formales sobre diagnósticos. Dentro de esa familia elige las campañas más
recientes por tipo y estado. Comprueba máquina, imagen, parámetros y binarios;
los analizadores conservan sus validaciones adicionales. Debe existir al menos
un estado con los tres tipos de campaña. Los datos sintéticos y diagnósticos se
identifican explícitamente y no se mezclan con mediciones formales.

Genera un informe nuevo y otro ZIP. También puede hacerse en la computadora local
con los resultados transferidos y las dependencias científicas instaladas:
analizar esos datos no constituye una medición física local.

Las recetas con opciones individuales se conservan en
[benchmarking](benchmarking.md) y [profiling](profiling.md).
