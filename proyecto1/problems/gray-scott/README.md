# Gray-Scott

## Requisitos en Linux

Se necesita tener instalados:

- `g++` con soporte para C++17
- `make`
- `ffmpeg` para generar el video

En Ubuntu o Debian se pueden instalar con:

```bash
sudo apt update
sudo apt install g++ make ffmpeg
```

El objetivo `run_seq_video` usa la fuente `DejaVuSans-Bold.ttf`, que normalmente viene instalada en Linux. Si no está disponible, instala también:

```bash
sudo apt install fonts-dejavu
```

## Ejecución

Desde la carpeta del problema, entra en `scripts/`:

```bash
cd proyecto1/problems/gray-scott/scripts
```

### `make` o `make all`

Compila la versión secuencial con `g++`, optimización `-O3` y C++17. Genera el ejecutable:

```text
gray_scott_seq
```

### `make run_seq`

Compila el programa si es necesario y ejecuta la simulación secuencial para una malla de `512` y `16000` pasos. Esta opción no guarda frames en disco y está pensada para medir el rendimiento.

```bash
make run_seq
```

### `make run_seq_video`

Compila el programa, ejecuta la simulación guardando los frames PPM temporales y usa `ffmpeg` para crear el video:

```bash
make run_seq_video
```

El resultado se guarda en:

```text
proyecto1/problems/gray-scott/videos/gray-scott-secuencial.mp4
```

Los frames temporales se almacenan durante la ejecución en `videos/temp_frames/` y se eliminan automáticamente al finalizar.

### `make clean`

Elimina el ejecutable compilado y cualquier carpeta temporal de frames:

```bash
make clean
```

El video generado no se elimina con `make clean`.
