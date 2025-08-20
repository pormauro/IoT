"""Speed Monitor — pantalla dividida (arriba info grande, abajo gráfico, dark)

Lee JSON desde http://<board_ip><endpoint> (config.json):
{"position": <int>, "speed": <float cuentas/seg>, "time": <uint seg>, "run": <uint>, "stop": <uint>}

- Arriba (HUD): Vel: <m/min>, Dist: <m>, Total/Marcha/Parada (min), "YYYY-MM-DD HH:MM:SS", logo (si logo.png existe).
- Abajo: gráfico de velocidad (m/min).
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
from collections import deque
from typing import Deque, Optional, Tuple

import matplotlib as mpl
mpl.rcParams['toolbar'] = 'none'  # Oculta toolbar/menú de Matplotlib

import matplotlib.animation as animation
import matplotlib.pyplot as plt
import requests
from matplotlib.gridspec import GridSpec

CONFIG_PATH = pathlib.Path(__file__).with_name("config.json")
LOGO_PATH = pathlib.Path(__file__).with_name("logo.png")

# ---- Tamaños de fuente (ajustables) ----
FONT_SPEED    = 35   # Vel:
FONT_METERS   = 35   # Dist:
FONT_TIME     = 35   # tiempos
FONT_DATETIME = 35   # fecha/hora
FONT_STATUS   = 25   # "SIN CONEXIÓN"


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

    # Datos para el gráfico (velocidad m/min)
    times: Deque[float] = deque(maxlen=600)
    speeds: Deque[float] = deque(maxlen=600)

    # ---- Layout en 2 filas: arriba HUD, abajo gráfico ----
    fig = plt.figure(figsize=(14, 8))
    set_fullscreen(fig)
    gs = GridSpec(2, 2, height_ratios=[1, 2], width_ratios=[3, 1], figure=fig)
    info_ax = fig.add_subplot(gs[0, 0])   # HUD texto (izq)
    logo_ax = fig.add_subplot(gs[0, 1])   # logo (der)
    ax = fig.add_subplot(gs[1, :])        # gráfico (fila completa)

    # Fondo oscuro coherente
    fig.patch.set_facecolor("#0e1117")
    ax.set_facecolor('#12151b')
    info_ax.set_facecolor('#0e1117')
    logo_ax.set_facecolor('#0e1117')

    # Config ax del gráfico
    (line,) = ax.plot([], [], label="Velocidad (m/min)", color="orangered", linewidth=2.5)
    ax.set_xlabel("Tiempo (s)")
    ax.set_ylabel("Velocidad (m/min)")
    #ax.set_title(f"Monitor de velocidad — {board_ip}{endpoint}")
    ax.legend(loc="upper right")

    # ---- Config ax del HUD (sin ejes, sin marcos) ----
    for _ax in (info_ax, logo_ax):
        _ax.set_xticks([]); _ax.set_yticks([])
        _ax.set_frame_on(False)

    # Texto grande (columna izquierda del HUD)
    # Coordenadas del info_ax: (0,0) abajo-izq, (1,1) arriba-der
    speed_text = info_ax.text(
        0.01, 1.0, "Vel: --.- m/min",
        va="top", ha="left", fontsize=FONT_SPEED, fontweight="bold", color='white'
    )
    meters_text = info_ax.text(
        0.01, 0.8, "Dist: --.- m",
        va="top", ha="left", fontsize=FONT_METERS, fontweight="bold", color='white'
    )
    total_text = info_ax.text(
        0.01, 0.6, "Total: --.- min",
        va="top", ha="left", fontsize=FONT_TIME, fontweight="bold", color='white'
    )
    run_text = info_ax.text(
        0.01, 0.4, "Marcha: --.- min",
        va="top", ha="left", fontsize=FONT_TIME, fontweight="bold", color='white'
    )
    stop_text = info_ax.text(
        0.01, 0.2, "Parada: --.- min",
        va="top", ha="left", fontsize=FONT_TIME, fontweight="bold", color='white'
    )
    datetime_text = info_ax.text(
        0.01, 0.0, "--",
        va="top", ha="left", fontsize=FONT_DATETIME, color='white'
    )
    status_text = info_ax.text(
        0.99, 0.05, "",  # sin texto al inicio; se usa si no hay conexión
        va="bottom", ha="right", fontsize=FONT_STATUS, fontweight="bold"
    )

    # Logo a la derecha (si existe). Mantener ratio y ocupar área del logo_ax
    logo_ax.set_axis_off()
    if LOGO_PATH.exists():
        try:
            img = plt.imread(str(LOGO_PATH))
            logo_ax.imshow(img)
            logo_ax.set_aspect("equal")
        except Exception:
            pass

    # Estado interno
    start = time.time()
    last_position: Optional[int] = None
    meters_value: Optional[float] = None
    last_valid_speed: Optional[float] = None  # m/min

    def update(_: int):
        nonlocal last_position, meters_value, last_valid_speed

        # Fecha/hora SIEMPRE (sin label)
        datetime_text.set_text(time.strftime("%Y-%m-%d %H:%M:%S"))

        # Intentar leer del encoder
        payload = fetch_json(base_url)
        if payload is None:
            # Sin conexión
            status_text.set_text("SIN CONEXIÓN")
            status_text.set_color("tab:red")
            # (opcional) extender la línea con último válido:
            # now = time.time() - start
            # if last_valid_speed is not None:
            #     times.append(now); speeds.append(last_valid_speed)
        else:
            spd_cps = get_float(payload, "speed")      # cuentas/seg
            pos_counts = get_int(payload, "position")  # cuentas acumuladas
            total_secs = get_int(payload, "time")
            run_secs = get_int(payload, "run")
            stop_secs = get_int(payload, "stop")

            if spd_cps is not None:
                spd_mpm = (spd_cps * 60.0) / (ppm if ppm != 0 else 1.0)
                last_valid_speed = spd_mpm
                now = time.time() - start
                times.append(now)
                speeds.append(spd_mpm)
                speed_text.set_text(f"Vel: {spd_mpm:.2f} m/min")
                status_text.set_text("")  # conectado: sin cartel
            else:
                status_text.set_text("SIN CONEXIÓN")
                status_text.set_color("tab:red")

            if pos_counts is not None:
                last_position = pos_counts
                meters_value = pos_counts / (ppm if ppm != 0 else 1.0)
                meters_text.set_text(f"Dist: {meters_value:.2f} m")
            if total_secs is not None:
                total_text.set_text(f"Total: {total_secs / 60:.1f} min")
            if run_secs is not None:
                run_text.set_text(f"Marcha: {run_secs / 60:.1f} min")
            if stop_secs is not None:
                stop_text.set_text(f"Parada: {stop_secs / 60:.1f} min")

        # Ajustar ejes del gráfico
        if times and speeds:
            xmin = max(0.0, times[-1] - 120.0)  # ventana ~120s
            xmax = max(times[-1], xmin + 10.0)
            ymin = min(speeds)
            ymax = max(speeds)
            if ymin == ymax:
                ymin -= 1.0; ymax += 1.0
            ax.set_xlim(xmin, xmax)
            pad_y = 0.1 * (abs(ymax) + 1.0)
            ax.set_ylim(ymin - pad_y, ymax + pad_y)
        else:
            ax.set_xlim(0, 10)
            ax.set_ylim(0, 10)

        line.set_data(list(times), list(speeds))
        return line, speed_text, meters_text, total_text, run_text, stop_text, datetime_text, status_text

    ani = animation.FuncAnimation(fig, update, interval=max(100, int(poll_interval * 1000)))

    # Ajuste de márgenes para que HUD no se recorte
    plt.subplots_adjust(top=0.98, left=0.03, right=0.98, bottom=0.06, hspace=0.20)

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
