#  Copyright 2026 Paul Schulze, All rights reserved.
#
#  For licensing terms, Please find the licensing terms in the closest
#  LICENSE.txt in this repository file going up the directory tree.
#
"""Drives one Artemis stream process over its control socket against a real host, standing in
   for the main window: the states must arrive in order, an unknown command must not end the
   stream, "raise" must keep it running, "server-command" must be sent to the host, and
   "stop" must end it cleanly. A second run points
   the process at a socket nobody listens on, which must be reported and not crash it.
   Given the host's process id, a last run kills the host mid-stream: the stream must report
   failed with the error and exit, since nobody would see a dialog it left open.

   test_stream_control.py <Artemis binary> <settings dir with portable.dat> <host> <app> [host pid]"""
import json
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time

state_timeout_s: float = 90.0
stop_timeout_s: float = 20.0
resolution: str = "1280x720"

def _expect(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)

def _start_stream(binary: str, settings_dir: str, host: str, app: str,
                  socket_path: str) -> subprocess.Popen[bytes]:
    return subprocess.Popen([binary, "stream", host, app, "--resolution", resolution,
                             "--display-mode", "windowed", "--control-socket", socket_path,
                             "--stream-id", "7"],
                            cwd=settings_dir, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def _read_messages(connection: socket.socket, buffer: bytearray) -> list[dict[str, str]]:
    messages = []
    try:
        buffer.extend(connection.recv(4096))
    except socket.timeout:
        pass
    while (newline := buffer.find(b"\n")) >= 0:
        messages.append(json.loads(buffer[:newline]))
        del buffer[:newline + 1]
    return messages

def _wait_for_state(connection: socket.socket, buffer: bytearray, wanted: str,
                    seen: list[str], timeout_s: float) -> None:
    deadline = time.monotonic() + timeout_s
    while wanted not in seen and time.monotonic() < deadline:
        for message in _read_messages(connection, buffer):
            _expect(message["id"] == "7", f"expected stream id 7, got {message['id']!r}")
            seen.append(message["state"])
            print(f"  state {message['state']} {message['detail']}".rstrip())
            _expect(message["state"] != "failed", f"stream failed: {message['detail']}")
    _expect(wanted in seen, f"expected state {wanted!r} within {timeout_s} s, saw {seen}")

def _send(connection: socket.socket, command: str, **fields: int) -> None:
    connection.sendall(json.dumps({"command": command, **fields}).encode() + b"\n")

def test_stop_ends_stream(binary: str, settings_dir: str, host: str, app: str) -> None:
    with tempfile.TemporaryDirectory() as directory:
        socket_path = os.path.join(directory, "control")
        server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        server.bind(socket_path)
        server.listen(1)
        server.settimeout(30)

        process = _start_stream(binary, settings_dir, host, app, socket_path)
        try:
            connection, _ = server.accept()
            connection.settimeout(0.5)
            buffer = bytearray()
            seen: list[str] = []

            _wait_for_state(connection, buffer, "streaming", seen, state_timeout_s)
            _expect(seen[0] == "starting", f"expected the first state to be starting, saw {seen}")
            _expect("connecting" in seen, f"expected connecting before streaming, saw {seen}")

            _send(connection, "bogus")
            _send(connection, "raise")
            time.sleep(3)
            _expect(process.poll() is None, "expected the stream to survive an unknown command and raise")

            # Whether the host runs it is the host's business; the stream must send it
            _send(connection, "server-command", index=0)
            _wait_for_state(connection, buffer, "server-command", seen, stop_timeout_s)

            started = time.monotonic()
            _send(connection, "stop")
            _wait_for_state(connection, buffer, "ended", seen, stop_timeout_s)
            exit_code = process.wait(timeout=stop_timeout_s)
            print(f"  stopped in {time.monotonic() - started:.1f} s, exit code {exit_code}")
            _expect(exit_code == 0, f"expected exit code 0 after stop, got {exit_code}")
        finally:
            if process.poll() is None:
                process.kill()
            server.close()

def test_missing_controller_is_reported(binary: str, settings_dir: str, host: str, app: str) -> None:
    # A host that does not exist keeps the process searching instead of streaming
    with tempfile.TemporaryDirectory() as directory:
        before = set(os.listdir(settings_dir))
        process = _start_stream(binary, settings_dir, "no-such-host.invalid", app,
                                os.path.join(directory, "nobody"))
        try:
            time.sleep(12)
            _expect(process.poll() is None, "expected the stream process to keep running without a controller")
        finally:
            process.kill()
            process.wait()

        logs = [name for name in set(os.listdir(settings_dir)) - before if name.endswith(".log")]
        _expect(len(logs) == 1, f"expected one new log in {settings_dir}, found {logs}")
        with open(os.path.join(settings_dir, logs[0]), encoding="utf-8", errors="replace") as log:
            text = log.read()
        _expect("could not connect" in text,
                f"expected the missing controller to be logged in {logs[0]}")

def test_host_loss_ends_stream(binary: str, settings_dir: str, host: str, app: str,
                               host_pid: int) -> None:
    with tempfile.TemporaryDirectory() as directory:
        socket_path = os.path.join(directory, "control")
        server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        server.bind(socket_path)
        server.listen(1)
        server.settimeout(30)

        process = _start_stream(binary, settings_dir, host, app, socket_path)
        try:
            connection, _ = server.accept()
            connection.settimeout(0.5)
            buffer = bytearray()
            seen: list[str] = []
            _wait_for_state(connection, buffer, "streaming", seen, state_timeout_s)

            os.kill(host_pid, signal.SIGKILL)
            details: list[str] = []
            deadline = time.monotonic() + state_timeout_s
            while process.poll() is None and time.monotonic() < deadline:
                for message in _read_messages(connection, buffer):
                    print(f"  state {message['state']} {message['detail']}".rstrip())
                    if message["state"] == "failed":
                        details.append(message["detail"])
            _expect(process.poll() is not None,
                    f"expected the stream process to exit after losing the host within {state_timeout_s} s")
            _expect(len(details) > 0 and details[-1] != "",
                    f"expected a failed state with the error before the process exited, got {details}")
        finally:
            if process.poll() is None:
                process.kill()
            server.close()

def main(arguments: list[str]) -> int:
    if len(arguments) not in (4, 5):
        raise SystemExit(__doc__)
    binary, settings_dir, host, app = arguments[:4]
    for test in (test_stop_ends_stream, test_missing_controller_is_reported):
        print(test.__name__)
        test(binary, settings_dir, host, app)
        print("  passed")
    if len(arguments) == 5:
        print(test_host_loss_ends_stream.__name__)
        test_host_loss_ends_stream(binary, settings_dir, host, app, int(arguments[4]))
        print("  passed")
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
