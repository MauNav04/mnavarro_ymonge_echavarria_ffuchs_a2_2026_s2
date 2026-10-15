"""Validación del punto de entrada: compatibilidad, errores y archivos preservados."""
import pathlib
import subprocess
import sys
import tempfile

binary = pathlib.Path(sys.argv[1]).resolve()
fine_binary = pathlib.Path(sys.argv[2]).resolve()
coarse_binary = pathlib.Path(sys.argv[3]).resolve()

def run(*args, executable=binary):
    result = subprocess.run([str(executable), *map(str, args)], capture_output=True, text=True, timeout=30)
    if result.returncode:
        raise AssertionError(result.stderr)
    return result.stdout

with tempfile.TemporaryDirectory(prefix="fractal-cli-") as directory:
    work = pathlib.Path(directory)
    image = work / "input.ppm"
    image.write_bytes(b"P6\n16 24\n255\n" + bytes((i * 17 + i // 7) % 256 for i in range(16 * 24 * 3)))
    assert "fractal_seq" in run("--help")
    assert "fractal_fine" in run("--help",executable=fine_binary)
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
    # El modelo nuevo comparte exactamente reglas y reconstrucción con la base.
    import csv
    for mode in ["compress", "compress-gray"]:
        reference = work / "reference.frc"
        run(mode, image, reference, 4, 3)
        for quantum in [1, 3, 100]:
            fine, trace = work / "fine.frc", work / "schedule.csv"
            output = run(mode, image, fine, 4, 3, "--teams", 5,
                         "--threads-per-team", 2, "--quantum-domains", quantum, "--trace-schedule", trace, executable=fine_binary)
            assert "Round-Robin cooperativo" in output
            assert reference.read_bytes() == fine.read_bytes()
            with trace.open() as file:
                events = list(csv.DictReader(file))
            assert events and int(events[0]["worker"]) == 0
            assert all(0 < int(e["domain_end"])-int(e["domain_begin"]) <= quantum for e in events)
            assert set(int(e["channel"]) for e in events) == ({0,1,2} if mode == "compress" else {0})
            run("decompress", fine, work / "fine.png", 80)
    assert "fractal_coarse" in run("--help", executable=coarse_binary)
    for mode in ["compress", "compress-gray"]:
        reference, coarse, trace = work / "reference.frc", work / "coarse.frc", work / "coarse.csv"
        run(mode, image, reference, 4, 3)
        output = run(mode, image, coarse, 4, 3, "--teams", 5, "--threads-per-team", 2,
                     "--trace-schedule", trace, executable=coarse_binary)
        assert "stall simulado" in output
        assert coarse.read_bytes() == reference.read_bytes()
        with trace.open() as file:
            events = list(csv.DictReader(file))
        assert events and all(e["reason"] in ["stall", "domain_complete"] for e in events)
        assert all(int(e["domain_end"]) > int(e["domain_begin"]) for e in events)
        run("decompress", coarse, work / "coarse.png", 80)
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
    for model in ["coarse-grained", "CMP", "SMT"]:
        invalid.append((["compress", image, sentinel, "--model", model, "--teams", 5,
                         "--threads-per-team", 2], "solo admite el modelo"))
    invalid = [(binary,args,message) for args,message in invalid]
    invalid.extend([
        (fine_binary,["compress", image, sentinel, "--quantum-domains", 0], "entero positivo"),
        (binary,["compress", image, sentinel, "--quantum-domains", 1], "requiere fine-grained"),
        (binary,["compress", image, sentinel, "--trace-schedule", work / "bad.csv"], "requiere fine-grained"),
        (fine_binary,["decompress", sentinel, image], "Decodificacion fine-grained"),
        (fine_binary,["compress", image, sentinel, "--trace-schedule", image], "archivos distintos"),
        (fine_binary,["compress", image, sentinel, "--trace-schedule", sentinel], "archivos distintos"),
        (binary,["compress", image, sentinel, "--model", "fine-grained"], "solo admite el modelo"),
        (fine_binary,["compress", image, sentinel, "--model", "sequential"], "solo admite el modelo"),
    ])
    invalid.extend([
        (coarse_binary,["compress", image, sentinel, "--quantum-domains", 1], "requiere fine-grained"),
        (coarse_binary,["decompress", sentinel, image], "Decodificacion coarse-grained"),
        (coarse_binary,["compress", image, sentinel, "--model", "fine-grained"], "solo admite el modelo"),
        (coarse_binary,["compress", image, sentinel, "--trace-schedule", image], "archivos distintos"),
        (coarse_binary,["compress", image, sentinel, "--trace-schedule", sentinel], "archivos distintos"),
        (coarse_binary,["compress", image, sentinel, "--teams", 0], "entero positivo"),
        (coarse_binary,["compress", image, sentinel, "--trace-schedule"], "Falta valor"),
    ])
    original = image.read_bytes()
    for executable,args,message in invalid:
        result = subprocess.run([str(executable), *map(str, args)], capture_output=True, text=True, timeout=30)
        assert result.returncode != 0, args
        assert message in result.stderr, (args, result.stderr)
        assert sentinel.read_bytes() == b"preservar"
        assert image.read_bytes() == original
    print(f"OK CLI: RGB/gris, sintaxis original/opciones, reconstruccion, ayuda y {len(invalid)} rechazos sin alterar archivos.")
