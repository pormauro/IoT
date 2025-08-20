# Speed Monitor Python Application

Esta aplicación Python consulta una placa de medición de velocidad a través de HTTP y
muestra en pantalla completa la velocidad, distancia y tiempos sin utilizar gráficos.

## Configuración

Modifique el archivo `config.json` para indicar la IP de la placa, el endpoint y el
intervalo de consulta:

```json
{
    "board_ip": "192.168.0.100",
    "endpoint": "/speed",
    "poll_interval": 1.0
}
```

## Ejecución

```bash
python3 speed_monitor.py
```

La aplicación abrirá una ventana de matplotlib con los datos en texto grande.
Si existe un archivo ``logo.png`` en la carpeta se mostrará como logo cuadrado.
