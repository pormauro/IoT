#include <SPI.h>
#include <Ethernet.h>
#include <Encoder.h>

// Configuración de red: ajustar a la red local
byte mac[] = {0xDE,0xAD,0xBE,0xEF,0xFE,0xED};
IPAddress ip(192,168,1,177);
EthernetServer server(80);

// Pines del encoder (usar interrupciones para precisión)
Encoder encoder(2,3);
long lastPosition = 0;
unsigned long lastTime = 0;
unsigned long startTime = 0;

void setup(){
  Ethernet.begin(mac, ip);
  server.begin();
  encoder.write(0);
  lastPosition = 0;
  lastTime = millis();
  startTime = millis();
}

void loop(){
  EthernetClient client = server.available();
  if(client){
    String req = client.readStringUntil('\r');
    client.flush();

    if(req.startsWith("GET /reset")){
      encoder.write(0);
      lastPosition = 0;
      lastTime = millis();
      startTime = millis();
      client.println("HTTP/1.1 200 OK");
      client.println("Connection: close");
      client.println();
      return;
    }

    if(req.startsWith("GET /data")){
      long pos = encoder.read();
      unsigned long now = millis();
      float dt = (now - lastTime) / 1000.0;
      float speed = dt>0 ? (pos - lastPosition) / dt : 0.0;
      lastPosition = pos;
      lastTime = now;
      unsigned long elapsed = (now - startTime) / 1000;

      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: application/json");
      client.println("Connection: close");
      client.println();
      client.print('{');
      client.print("\"position\":"); client.print(pos); client.print(',');
      client.print("\"speed\":"); client.print(speed,2); client.print(',');
      client.print("\"time\":"); client.print(elapsed);
      client.println('}');
      return;
    }

    // Página principal
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("Connection: close");
    client.println();
    client.println(F(
"<!DOCTYPE html><html lang='es'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
"<title>Monitor de Velocidad</title></head><body style='font-family:sans-serif;text-align:center;'>"
"<h1>Monitor de Velocidad</h1>"
"<p>Velocidad: <span id='sp'>0</span></p>"
"<p>Posición: <span id='ps'>0</span></p>"
"<p>Tiempo: <span id='tm'>0</span>s</p>"
"<button onclick=\"fetch('/reset')\">Reiniciar</button>"
"<script>async function u(){const r=await fetch('/data');const j=await r.json();sp.textContent=j.speed.toFixed(2);ps.textContent=j.position;tm.textContent=j.time;}setInterval(u,500);u();</script>"
"</body></html>"));
  }
}
