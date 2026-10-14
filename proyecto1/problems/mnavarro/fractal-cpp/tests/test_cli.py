"""Validación del punto de entrada: compatibilidad, errores y archivos preservados."""
import pathlib
import subprocess
import sys
import tempfile

binary = pathlib.Path(sys.argv[1]).resolve()

def run(*args):
    result = subprocess.run([str(binary), *map(str, args)], capture_output=True, text=True, timeout=30)
    if result.returncode:
        raise AssertionError(result.stderr)
    return result.stdout

with tempfile.TemporaryDirectory(prefix="fractal-cli-") as directory:
    work = pathlib.Path(directory)
    image = work / "input.ppm"
    image.write_bytes(b"P6\n16 24\n255\n" + bytes((i * 17 + i // 7) % 256 for i in range(16 * 24 * 3)))
    assert "--threads-per-team" in run("--help")
    for mode in ["compress", "compress-gray"]:
        legacy, named = work / "legacy.frc", work / "named.frc"
        run(mode, image, legacy, 4, 3)
        output = run(mode, "--model", "secuencial", image, named, "--range", 4,
                     "--stride", 3, "--teams", 1, "--threads-per-team", 1)
        assert "Modelo: sequential" in output
        assert legacy.read_bytes() == named.read_bytes()
        first, second = work / "first.png", work / "second.png"
        run("decompress", legacy, first, 80)
        run("decompress", named, second, "--iterations", 80, "--model", "sequential")
        assert first.read_bytes() == second.read_bytes()
        compare = "compare" if mode == "compress" else "compare-gray"
        assert "MSE global [0,1]: 0" in run(compare, first, second)
    # Rechazar entradas inválidas antes de E/S, sin truncar una salida existente.
    sentinel = work / "sentinel.frc"
    sentinel.write_bytes(b"preservar")
    invalid = [
        (["compress", image, sentinel, "--range", 0], "entero positivo"),
        (["compress", image, sentinel, "--range", -2], "entero positivo"),
        (["compress", image, sentinel, "--range", "8x"], "entero positivo"),
        (["compress", image, sentinel, "--range", 65], "Range fuera"),
        (["compress", image, sentinel, "--stride", 17], "stride fuera"),
        (["compress", image, sentinel, "--teams", 0], "entero positivo"),
        (["compress", image, sentinel, "--teams", 5, "--threads-per-team", 2], "1 equipo de 1 hilo"),
        (["compress", image, sentinel, "--model", "unknown"], "Modelo desconocido"),
        (["compress", image, sentinel, "--model"], "Falta valor"),
        (["compress", image, sentinel, "--teams", "--model", "sequential"], "Falta valor"),
        (["compress", image, sentinel, "--bogus", 1], "Opcion no admitida"),
        (["compress", image, sentinel, "--range", 4, "--range", 4], "Opcion repetida"),
        (["compress", image, sentinel, 4, 3, "--range", 4], "Parametro duplicado"),
        (["compress", image, sentinel, 4, 3, "--stride", 3], "Parametro duplicado"),
        (["compress", image, sentinel, "--iterations", 80], "Opcion no admitida"),
        (["decompress", sentinel, image, "--iterations", 10001], "Iteraciones fuera"),
        (["decompress", sentinel, image, 80, "--iterations", 80], "Parametro duplicado"),
        (["decompress", sentinel, image, "--range", 4], "Opcion no admitida"),
        (["compare", image, image, "--teams", 1], "Opcion no admitida"),
        (["unknown", image, sentinel], "Comando desconocido"),
        (["compress", image, sentinel, 4, 3, 1], "Cantidad de argumentos"),
        (["compress", image, image, 4, 3], "archivos distintos"),
    ]
    for model in ["fine-grained", "coarse-grained", "CMP", "SMT"]:
        invalid.append((["compress", image, sentinel, "--model", model, "--teams", 5,
                         "--threads-per-team", 2], "aun no implementado"))
    original = image.read_bytes()
    for args, message in invalid:
        result = subprocess.run([str(binary), *map(str, args)], capture_output=True, text=True, timeout=30)
        assert result.returncode != 0, args
        assert message in result.stderr, (args, result.stderr)
        assert sentinel.read_bytes() == b"preservar"
        assert image.read_bytes() == original
    print(f"OK CLI: RGB/gris, sintaxis original/opciones, reconstruccion, ayuda y {len(invalid)} rechazos sin alterar archivos.")
