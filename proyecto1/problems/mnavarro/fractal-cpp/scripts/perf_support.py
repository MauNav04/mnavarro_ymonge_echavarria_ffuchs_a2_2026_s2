"""Formato Perf y disponibilidad: sin convertir eventos ausentes en ceros medidos."""
import csv
import math
from pathlib import Path

GROUPS = {
    "cpu": ("cycles", "instructions"),
    "cache": ("cache-references", "cache-misses"),
    "l1": ("L1-dcache-loads", "L1-dcache-load-misses"),
    "llc": ("LLC-loads", "LLC-load-misses"),
    "branch": ("branches", "branch-misses"),
    "software": ("task-clock", "context-switches", "cpu-migrations", "page-faults", "duration_time"),
}
EVENTS = tuple(event for events in GROUPS.values() for event in events)
SCOPE = "whole-command-including-startup-io-and-threads"
STRATEGY = "paired-groups-separate-passes-v1"


def canonical(event):
    # El nombre original se conserva también para auditar los modificadores.
    return event.split(":", 1)[0]


def parse_stat(text, requested, min_running=90.0):
    result = {event: {"count": None, "unit": "", "runtime_ns": None, "running_pct": None,
                      "status": "missing", "raw_event": event} for event in requested}
    for fields in csv.reader(text.splitlines(), delimiter=";"):
        fields = [part.strip() for part in fields]
        if len(fields) < 3 or fields[0].startswith("#"):
            continue
        event = canonical(fields[2])
        if event not in result:
            continue
        if result[event]["status"] != "missing":
            raise ValueError("Evento Perf duplicado: " + event)
        row = result[event]
        row.update(unit=fields[1], raw_event=fields[2])
        if fields[0].startswith("<"):
            row["status"] = {"<not counted>": "not_counted", "<not supported>": "not_supported"}.get(fields[0], "unavailable")
            continue
        try:
            value = float(fields[0])
            if not math.isfinite(value) or value < 0:
                raise ValueError("Valor no finito o negativo")
            row["count"] = value
            if event == "task-clock":
                multiplier = {"msec": 1., "ms": 1., "usec": .001, "us": .001, "nsec": .000001, "ns": .000001, "sec": 1000., "s": 1000.}.get(row["unit"])
                if multiplier is None:
                    row["status"] = "unknown_unit"
                    continue
                row.update(count=value * multiplier, unit="ms")
            if event == "duration_time":
                multiplier = {"ns": 1., "nsec": 1., "us": 1000., "usec": 1000., "ms": 1e6, "msec": 1e6, "s": 1e9, "sec": 1e9}.get(row["unit"])
                if multiplier is None:
                    row["status"] = "unknown_unit"
                    continue
                row.update(count=value * multiplier, unit="ns", status="ok")
                continue  # Contador de duración del propio Perf, no una PMU multiplexada.
            if len(fields) < 5 or not fields[3] or not fields[4]:
                row["status"] = "missing_runtime"
                continue
            runtime, running = float(fields[3]), float(fields[4].rstrip("%"))
            if not math.isfinite(runtime) or not math.isfinite(running) or runtime < 0 or not 0 <= running <= 100:
                raise ValueError("Runtime invalido")
            row.update(runtime_ns=runtime, running_pct=running)
            row["status"] = "not_counted" if runtime == 0 or running == 0 else ("low_running" if running < min_running else "ok")
        except ValueError:
            row.update(count=None, status="invalid_value")
    return result


def perf_command(binary, events, command, output):
    # Las parejas PMU deben contar simultáneamente. Los eventos software no usan slots PMU.
    selector = ",".join(events)
    if len(events) > 1 and all(event not in GROUPS["software"] for event in events):
        selector = "{" + selector + "}"
    return [str(binary), "stat", "--no-big-num", "-x", ";", "-o", str(output), "-e", selector, "--", *map(str, command)]


def permission_error(text):
    text = text.lower()
    return any(word in text for word in ("permission", "access denied", "operation not permitted", "perf_event_paranoid"))


def stat_run(perf, events, command, output, timeout, runner, min_running):
    result = runner(perf_command(perf, events, command, output), timeout)
    text = Path(output).read_text() if Path(output).exists() else ""
    parsed = parse_stat(text, events, min_running)
    if result["returncode"] != 0:
        status = "permission_denied" if permission_error(result["stderr"] + text) else "command_error"
        for row in parsed.values():
            row.update(status=status)
    return result, parsed


def discover(perf, command, directory, timeout, runner, min_running):
    """Sondear parejas; si falla una pareja, separar los eventos para localizar el motivo."""
    availability = {}
    for name, events in GROUPS.items():
        result, parsed = stat_run(perf, events, command, directory / f"probe-{name}.stat", timeout, runner, min_running)
        if result["timeout"]:
            raise ValueError("Timeout sondeando eventos Perf")
        if result["returncode"] != 0 or any(row["status"] != "ok" for row in parsed.values()):
            for event in events:
                single, item = stat_run(perf, [event], command, directory / f"probe-{event}.stat", timeout, runner, min_running)
                if single["timeout"]:
                    raise ValueError("Timeout sondeando " + event)
                availability[event] = item[event]
        else:
            availability.update(parsed)
    return availability
