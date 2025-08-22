"""Speed Monitor — pantalla principal (velocidad grande, sin gráfico, dark)

Lee JSON desde http://<board_ip><endpoint> (config.json):
{"position": <int>, "speed": <float cuentas/seg>, "time": <uint seg>, "run": <uint>, "stop": <uint>}

- Muestra velocidad en grande, distancia y tiempos.
- Logo opcional si existe ``logo.png``.
- Fullscreen automático, sin superposiciones.
- Estilo oscuro.
- Oculta el menú/toolbar de Matplotlib.
- Si el encoder no responde o JSON inválido → muestra "SIN CONEXIÓN" sin caer.

Requisitos: requests, matplotlib (ver requirements.txt).
"""

from __future__ import annotations
import json
import pathlib
import time
from typing import Optional, Tuple

import matplotlib as mpl
mpl.rcParams['toolbar'] = 'none'  # Oculta toolbar/menú de Matplotlib

import matplotlib.animation as animation
import matplotlib.pyplot as plt
import requests
from matplotlib.gridspec import GridSpec

CONFIG_PATH = pathlib.Path(__file__).with_name("config.json")
LOGO_PATH = pathlib.Path(__file__).with_name("logo.png")

# ---- Tamaños de fuente (ajustables) ----
FONT_SPEED    = 85  # Velocidad principal
FONT_METERS   = 40   # Distancia
FONT_TIME     = 40   # tiempos
FONT_DATETIME = 30   # fecha/hora
FONT_STATUS   = 20   # "SIN CONEXIÓN"


# ---------------- Config ----------------
def load_config() -> Tuple[str, str, float, float]:
    """Carga board_ip, endpoint, poll_interval y ppm desde config.json."""
    with CONFIG_PATH.open("r", encoding="utf-8") as fh:
        data = json.load(fh)
    board_ip = data["board_ip"]
    endpoint = data["endpoint"]
    poll_interval = float(data.get("poll_interval", 1.0))
    ppm = float(data.get("ppm", 1000.0)) or 1000.0
    return board_ip, endpoint, poll_interval, ppm


# ---------------- IO HTTP ----------------
def fetch_json(url: str) -> Optional[dict]:
    """Obtiene JSON del encoder. Devuelve dict o None si falla."""
    try:
        r = requests.get(url, timeout=2.0)
        if r.status_code != 200:
            return None
        if not r.content:
            return None
        return r.json()
    except Exception:
        return None


def get_float(d: dict, key: str) -> Optional[float]:
    try:
        v = d.get(key, None)
        if v is None:
            return None
        return float(v)
    except Exception:
        return None


def get_int(d: dict, key: str) -> Optional[int]:
    try:
        v = d.get(key, None)
        if v is None:
            return None
        return int(v)
    except Exception:
        return None


# ---------------- Fullscreen helpers ----------------
def set_fullscreen(fig):
    """Intenta fullscreen cross-backend."""
    try:
        fig.canvas.manager.full_screen_toggle()
        return
    except Exception:
        pass
    try:
        # TkAgg en Windows
        mng = plt.get_current_fig_manager()
        mng.window.state("zoomed")
    except Exception:
        pass


# ---------------- Main ----------------
def main() -> None:
    board_ip, endpoint, poll_interval, ppm = load_config()
    base_url = f"http://{board_ip}{endpoint}"

    # Estilo dark global
    plt.style.use('dark_background')

    # ---- Layout en una fila: info grande + logo ----
    fig = plt.figure(figsize=(14, 8))
    set_fullscreen(fig)
    gs = GridSpec(1, 2, width_ratios=[4, 1], figure=fig)
    info_ax = fig.add_subplot(gs[0, 0])   # Texto
    logo_ax = fig.add_subplot(gs[0, 1])   # Logo

    # Fondo oscuro coherente
    fig.patch.set_facecolor("#0e1117")
    info_ax.set_facecolor('#0e1117')
    logo_ax.set_facecolor('#0e1117')

    # ---- Config ax del HUD (sin ejes, sin marcos) ----
    for _ax in (info_ax, logo_ax):
        _ax.set_xticks([]); _ax.set_yticks([])
        _ax.set_frame_on(False)

    # Texto grande centrado
    speed_text = info_ax.text(
        0.02, 0.85, "Vel: --.- m/min",
        va="center", ha="left", fontsize=FONT_SPEED, fontweight="bold", color='orange'
    )
    meters_text = info_ax.text(
        0.02, 0.70, "Dist: --.- m",
        va="center", ha="left", fontsize=FONT_METERS, fontweight="bold", color='white'
    )
    total_text = info_ax.text(
        0.02, 0.55, "Total: --.- min",
        va="center", ha="left", fontsize=FONT_TIME, fontweight="bold", color='white'
    )
    run_text = info_ax.text(
        0.02, 0.40, "Marcha: --.- min",
        va="center", ha="left", fontsize=FONT_TIME, fontweight="bold", color='green'
    )
    stop_text = info_ax.text(
        0.02, 0.25, "Parada: --.- min",
        va="center", ha="left", fontsize=FONT_TIME, fontweight="bold", color='red'
    )
    datetime_text = info_ax.text(
        0.02, 0.10, "--",
        va="center", ha="left", fontsize=FONT_DATETIME, color='white'
    )
    status_text = info_ax.text(
        0.02, 0.02, "",
        va="center", ha="left", fontsize=FONT_STATUS, fontweight="bold"
    )

    # Logo a la derecha (si existe). Mantener relación cuadrada
    logo_ax.set_axis_off()
    if LOGO_PATH.exists():
        try:
            img = plt.imread(str(LOGO_PATH))
            logo_ax.imshow(img)
            logo_ax.set_box_aspect(1)
        except Exception:
            pass

    # Estado interno
    last_position: Optional[int] = None
    meters_value: Optional[float] = None

    def update(_: int):
        nonlocal last_position, meters_value

        # Fecha/hora SIEMPRE (sin label)
        datetime_text.set_text(time.strftime("%Y-%m-%d %H:%M:%S"))

        # Intentar leer del encoder
        payload = fetch_json(base_url)
        if payload is None:
            status_text.set_text("SIN CONEXIÓN")
            status_text.set_color("tab:red")
        else:
            spd_cps = get_float(payload, "speed")      # cuentas/seg
            pos_counts = get_int(payload, "position")  # cuentas acumuladas
            total_secs = get_int(payload, "time")
            run_secs = get_int(payload, "run")
            stop_secs = get_int(payload, "stop")

            if spd_cps is not None:
                spd_mpm = (spd_cps * 60.0) / (ppm if ppm != 0 else 1.0)
                speed_text.set_text(f"Vel: {spd_mpm:.2f} m/min")
                status_text.set_text("")
            else:
                status_text.set_text("SIN CONEXIÓN")
                status_text.set_color("tab:red")

            if pos_counts is not None:
                last_position = pos_counts
                meters_value = pos_counts / (ppm if ppm != 0 else 1.0)
                meters_text.set_text(f"Fabricado: {meters_value:.2f} m")
            if total_secs is not None:
                total_text.set_text(f"Tiempo Total: {total_secs / 60:.1f} min")
            if run_secs is not None:
                run_text.set_text(f"Tiempo Marcha: {run_secs / 60:.1f} min")
            if stop_secs is not None:
                stop_text.set_text(f"Tiempo Parada: {stop_secs / 60:.1f} min")

        return speed_text, meters_text, total_text, run_text, stop_text, datetime_text, status_text

    ani = animation.FuncAnimation(fig, update, interval=max(100, int(poll_interval * 1000)))

    # Ajuste de márgenes para que HUD no se recorte
    plt.subplots_adjust(top=0.95, left=0.05, right=0.95, bottom=0.05)

    # Como refuerzo, intenta eliminar toolbar si algún backend la deja creada:
    try:
        mgr = plt.get_current_fig_manager()
        tb = getattr(mgr, "toolbar", None)
        if tb is not None:
            try:
                tb.pack_forget()  # TkAgg
            except Exception:
                try:
                    tb.hide()     # Qt5Agg / Wx
                except Exception:
                    pass
    except Exception:
        pass

    plt.show()


if __name__ == "__main__":
    main()
