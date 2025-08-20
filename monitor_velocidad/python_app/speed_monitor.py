"""Speed monitoring application for corrugator machine.

This script reads speed values from a network connected board and
plots them in real time using matplotlib. Connection parameters are
loaded from ``config.json`` in the same directory. The board is
expected to expose an HTTP endpoint returning JSON data in the
format::

    {"speed": <value>}

If the board is unreachable or returns invalid data, the graph will
hold the last valid value. The configuration file also includes the
polling interval in seconds.
"""
from __future__ import annotations

import json
import pathlib
import time
from collections import deque
from typing import Deque, Tuple

import matplotlib.animation as animation
import matplotlib.pyplot as plt
import requests

CONFIG_PATH = pathlib.Path(__file__).with_name("config.json")


def load_config() -> Tuple[str, str, float]:
    """Load configuration from ``config.json``.

    Returns
    -------
    tuple
        A tuple ``(board_ip, endpoint, poll_interval)``.
    """
    with CONFIG_PATH.open("r", encoding="utf-8") as fh:
        data = json.load(fh)
    return data["board_ip"], data["endpoint"], float(data.get("poll_interval", 1.0))


def fetch_speed(base_url: str) -> float | None:
    """Fetch the current speed from the board.

    Parameters
    ----------
    base_url: str
        Base URL for the board, e.g. ``"http://192.168.0.100/speed"``.

    Returns
    -------
    float | None
        The speed value if request succeeds, otherwise ``None``.
    """
    try:
        response = requests.get(base_url, timeout=2)
        response.raise_for_status()
        payload = response.json()
        return float(payload["speed"])
    except Exception:
        return None


def main() -> None:
    board_ip, endpoint, poll_interval = load_config()
    base_url = f"http://{board_ip}{endpoint}"

    window: Deque[float] = deque(maxlen=100)
    times: Deque[float] = deque(maxlen=100)

    fig, ax = plt.subplots()
    line, = ax.plot([], [], label="Velocidad")
    ax.set_xlabel("Tiempo (s)")
    ax.set_ylabel("Velocidad")
    ax.set_title("Monitor de velocidad")
    ax.legend(loc="upper right")

    start = time.time()

    def update(_: int) -> Tuple[list[float], list[float]]:
        value = fetch_speed(base_url)
        current = time.time() - start
        if value is not None:
            window.append(value)
            times.append(current)
        line.set_data(list(times), list(window))
        if times:
            ax.set_xlim(max(0, times[0]), max(times))
            ax.set_ylim(min(window) * 0.9, max(window) * 1.1)
        return line,

    ani = animation.FuncAnimation(fig, update, interval=poll_interval * 1000)
    plt.show()


if __name__ == "__main__":
    main()
