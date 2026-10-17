"""Control de SMT limitado a ON/OFF, con restauración al cerrar el canal."""
import os
import selectors
import signal
import subprocess

from test_models import require
from benchmark import snapshot

# El ayudante privilegiado no ejecuta comandos recibidos: sólo acepta on/off/stop.
# Sigue vivo durante la campaña: la restauración no depende de que sudo siga en caché.
# EOF (también si muere el padre) y señales terminan con restauración a ON.
HELPER = r"""
set -eu
control=/sys/devices/system/cpu/smt/control
active=/sys/devices/system/cpu/smt/active
exec 9>/run/fractal-smt.lock
/usr/bin/flock -n 9 || { echo 'Otra campaña controla SMT.' >&2; exit 1; }
restore() {
    printf 'on\n' > "$control" && test "$(/bin/cat "$active")" = 1
}
trap 'restore || { echo "ERROR: no se pudo restaurar SMT a ON." >&2; exit 1; }' EXIT
trap 'exit 143' TERM HUP
trap 'exit 130' INT
printf 'ready\n'
while IFS= read -r state; do
    case "$state" in
        on|off)
            printf '%s\n' "$state" > "$control"
            expected=0
            if test "$state" = on; then expected=1; fi
            test "$(/bin/cat "$active")" = "$expected"
            printf '%s\n' "$state"
            ;;
        stop) exit 0 ;;
        *) exit 2 ;;
    esac
done
"""


class SmtController:
    def __init__(self):
        self.process = None
        self.handlers = {}
        self.owns_control = False

    def receive(self):
        with selectors.DefaultSelector() as selector:
            selector.register(self.process.stdout, selectors.EVENT_READ)
            require(selector.select(15), "Timeout del ayudante SMT")
        answer = self.process.stdout.readline().strip()
        require(answer, "El ayudante SMT terminó sin confirmar la operación")
        return answer

    def __enter__(self):
        # Sudo pide contraseña aquí una sola vez, nunca durante las mediciones.
        if os.geteuid() != 0:
            subprocess.run(["sudo", "-v"], check=True)
        prefix = [] if os.geteuid() == 0 else ["sudo", "-n"]
        self.process = subprocess.Popen([*prefix, "/bin/sh", "-c", HELPER],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        text=True, start_new_session=True)
        try:
            require(self.receive() == "ready", "Protocolo del ayudante SMT inválido")
            self.owns_control = True
            for signum in (signal.SIGTERM, signal.SIGHUP):
                self.handlers[signum] = signal.getsignal(signum)
                signal.signal(signum, self.interrupted)
            return self
        except BaseException:
            self.__exit__(None, None, None)
            raise

    @staticmethod
    def interrupted(signum, frame):
        raise KeyboardInterrupt(f"Señal {signum}")

    def set_state(self, state):
        require(state in ("on", "off"), "Estado SMT inválido")
        print(f"--> SMT {state.upper()} mediante el kernel", flush=True)
        self.process.stdin.write(state + "\n")
        self.process.stdin.flush()
        require(self.receive() == state, "Cambio SMT sin confirmar")
        current = snapshot()
        require(current["active"] == ("1" if state == "on" else "0") and current["control"] == state,
                "El kernel no confirmó el estado SMT solicitado")

    def __exit__(self, exc_type, exc, traceback):
        # Evitar que un segundo Ctrl+C interrumpa la restauración.
        previous = signal.signal(signal.SIGINT, signal.SIG_IGN)
        try:
            if self.process is not None:
                try:
                    self.process.stdin.close()
                except BrokenPipeError:
                    pass  # Un ayudante terminado ya ejecutó su trap; verificar abajo.
                try:
                    code = self.process.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    self.process.terminate()  # TERM también activa el trap de restauración.
                    code = self.process.wait(timeout=20)
                finally:
                    self.process.stdout.close()
                if self.owns_control:
                    current = snapshot()
                    require(code == 0 and current["active"] == "1" and current["control"] == "on",
                            "No se confirmó la restauración de SMT. Recuperar con: "
                            "echo on | sudo tee /sys/devices/system/cpu/smt/control")
                    print("--> SMT restaurado y verificado: ON", flush=True)
        finally:
            signal.signal(signal.SIGINT, previous)
            for signum, handler in self.handlers.items():
                signal.signal(signum, handler)
