"""Exportar métricas completas a un ZIP nuevo; incluye CSV ignorados por Git."""
import argparse
from pathlib import Path
import zipfile


def archive(source, output):
    source, output = Path(source).resolve(), Path(output).resolve()
    if not source.is_dir():
        raise ValueError("No existe el directorio de métricas")
    if output == source or source in output.parents:
        raise ValueError("El ZIP debe estar fuera del directorio de entrada")
    if output.exists():
        raise ValueError("El ZIP ya existe; elegir otro nombre para conservarlo")
    paths = sorted(source.rglob("*"))
    if any(path.is_symlink() for path in paths):
        raise ValueError("La exportación exige archivos propios, sin enlaces simbólicos")
    files = [path for path in paths if path.is_file()]
    if not files:
        raise ValueError("El directorio no contiene archivos")
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "x", compression=zipfile.ZIP_DEFLATED) as stream:
        for path in files:
            stream.write(path, source.name + "/" + path.relative_to(source).as_posix())
    with zipfile.ZipFile(output) as stream:
        if stream.testzip() is not None:
            raise ValueError("Error de integridad en el ZIP; conservar las fuentes")
    return len(files), sum(path.suffix == ".csv" for path in files)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        total, csv_count = archive(args.input_dir, args.output)
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        parser.exit(1, f"Error: {error}\n")
    print(f"ZIP: {args.output} ({total} archivos, {csv_count} CSV). Fuentes conservadas.")
    if not csv_count:
        print("Aviso: no contiene CSV; no permite reproducir el análisis completo.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
