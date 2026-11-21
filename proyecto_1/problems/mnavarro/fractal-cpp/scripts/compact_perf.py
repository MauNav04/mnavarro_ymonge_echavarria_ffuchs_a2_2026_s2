"""Convertir salidas .stat existentes a un JSONL verificado, sin repetir mediciones."""
import argparse
import hashlib
import json
from pathlib import Path


def digest(data):
    return hashlib.sha256(data).hexdigest()


def compact(directory, remove_originals=False):
    directory = Path(directory).resolve()
    meta = json.loads((directory / "metadata.json").read_text())
    if meta.get("experiment") != "perf" or not meta.get("success"):
        raise ValueError("Sólo compactar una captura Perf terminada correctamente")
    paths = sorted(directory.rglob("*.stat"))
    if not paths:
        raise ValueError("No hay archivos .stat para compactar")
    if any(path.is_symlink() or directory not in path.resolve().parents for path in paths):
        raise ValueError("Los .stat deben ser archivos propios dentro de la captura")
    output = directory / "perf-stat.jsonl"
    manifest = directory / "perf-compaction.json"
    if output.exists() or manifest.exists():
        raise ValueError("Ya existe el registro o manifiesto; se conservan sin sobrescribir")
    hashes = {}
    # La copia se escribe primero; hasta verificarla no se retira ningún original.
    with output.open("x", encoding="utf-8", newline="\n") as stream:
        for path in paths:
            data = path.read_bytes()
            relative = path.relative_to(directory).as_posix()
            hashes[relative] = digest(data)
            stream.write(json.dumps({"path": str(path), "text": data.decode("utf-8")}) + "\n")
        stream.flush()
    seen = set()
    with output.open(encoding="utf-8") as stream:
        for line in stream:
            record = json.loads(line)
            path = Path(record["path"])
            relative = path.relative_to(directory).as_posix()
            if relative in seen or digest(record["text"].encode("utf-8")) != hashes[relative]:
                raise ValueError("El JSONL no conserva exactamente los originales")
            seen.add(relative)
    if seen != set(hashes):
        raise ValueError("Faltan salidas en el JSONL")
    report = dict(schema=1, campaign_id=meta["campaign_id"], verified=True, complete=False,
                  source_count=len(paths), source_sha256=hashes, raw_log="perf-stat.jsonl",
                  raw_log_sha256=digest(output.read_bytes()), remove_originals=remove_originals, removed_count=0)
    def save():
        manifest.write_text(json.dumps(report, indent=2) + "\n")
    save()
    try:
        if remove_originals:
            for path in paths:
                if digest(path.read_bytes()) != hashes[path.relative_to(directory).as_posix()]:
                    raise ValueError("Un .stat cambió durante la compactación; conservar originales restantes")
                path.unlink()
                report["removed_count"] += 1
        report["complete"] = True
    finally:
        # Si falla la retirada, el JSONL verificado sigue conservando todas las salidas.
        save()
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir", type=Path, required=True)
    parser.add_argument("--remove-originals", action="store_true",
                        help="retirar únicamente los .stat después de verificar su copia por SHA-256")
    args = parser.parse_args()
    try:
        result = compact(args.input_dir, args.remove_originals)
    except (OSError, ValueError, KeyError, UnicodeError) as error:
        parser.exit(1, f"Error: {error}\n")
    print(f"JSONL verificado: {args.input_dir}/perf-stat.jsonl; {result['source_count']} salidas, {result['removed_count']} .stat retirados.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
