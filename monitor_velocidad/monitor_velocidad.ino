#include <SPI.h>
#include <Ethernet.h>
#include <EEPROM.h>
#include <avr/pgmspace.h>
#include "EncoderInterrupt.h"

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Monitor de Velocidad</title>
  <link rel="stylesheet" href="/styles.css" />
</head>
<body>
  <h1>Monitor de Velocidad</h1>
  <div class="metrics">
    <p>Velocidad: <span id="vel">0</span></p>
    <p>Posición: <span id="pos">0</span></p>
    <p>Tiempo: <span id="time">0</span>s</p>
    <button id="reset">Reiniciar</button>
  </div>
  <hr />
  <h2>Configuración de Red</h2>
  <form id="netForm">
    <label><input type="checkbox" id="dhcp"> Usar DHCP</label>
    <div id="static">
      <label>IP: <input id="ip" placeholder="192.168.1.177" /></label>
      <label>Máscara: <input id="sm" placeholder="255.255.255.0" /></label>
      <label>Gateway: <input id="gw" placeholder="192.168.1.1" /></label>
      <label>DNS: <input id="dns" placeholder="8.8.8.8" /></label>
    </div>
    <button type="submit">Guardar</button>
  </form>
  <script src="/app.js"></script>
</body>
</html>
)rawliteral";

const char APP_JS[] PROGMEM = R"rawliteral(
const vel = document.getElementById('vel');
const pos = document.getElementById('pos');
const timeEl = document.getElementById('time');
const resetBtn = document.getElementById('reset');
const dhcp = document.getElementById('dhcp');
const ip = document.getElementById('ip');
const gw = document.getElementById('gw');
const sm = document.getElementById('sm');
const dns = document.getElementById('dns');
const staticDiv = document.getElementById('static');

resetBtn.addEventListener('click', ()=>fetch('/reset'));

dhcp.addEventListener('change', ()=>{
  staticDiv.style.display = dhcp.checked ? 'none':'block';
});

async function loadData(){
  const r = await fetch('/data');
  const j = await r.json();
  vel.textContent = j.speed.toFixed(2);
  pos.textContent = j.position;
  timeEl.textContent = j.time;
}
setInterval(loadData, 500); loadData();

async function loadConfig(){
  const r = await fetch('/config');
  const j = await r.json();
  dhcp.checked = j.dhcp;
  ip.value = j.ip;
  gw.value = j.gateway;
  sm.value = j.subnet;
  dns.value = j.dns;
  staticDiv.style.display = dhcp.checked ? 'none':'block';
}
loadConfig();

const form = document.getElementById('netForm');
form.addEventListener('submit', async (e)=>{
  e.preventDefault();
  const p = new URLSearchParams();
  p.append('dhcp', dhcp.checked ? '1':'0');
  p.append('ip', ip.value);
  p.append('gw', gw.value);
  p.append('sm', sm.value);
  p.append('dns', dns.value);
  await fetch('/config?'+p.toString());
  alert('Configuración guardada. Reinicie el dispositivo para aplicar.');
});
)rawliteral";

const char STYLES_CSS[] PROGMEM = R"rawliteral(
body{font-family:sans-serif;text-align:center;margin:20px;}
.metrics p{margin:4px 0;}
#static label{display:block;margin:4px 0;}
button{margin-top:10px;}
)rawliteral";

struct NetConfig {
  uint8_t magic;
  uint8_t dhcp;
  byte ip[4];
  byte gw[4];
  byte sm[4];
  byte dns[4];
};

NetConfig netCfg;
const uint8_t CFG_MAGIC = 0x42;

byte mac[] = {0xDE,0xAD,0xBE,0xEF,0xFE,0xED};
EthernetServer server(80);

EncoderInterrupt encoder(2,3);
long lastPosition = 0;
unsigned long lastTime = 0;
unsigned long startTime = 0;

void loadConfig(){
  EEPROM.get(0, netCfg);
  if(netCfg.magic != CFG_MAGIC){
    netCfg.magic = CFG_MAGIC;
    netCfg.dhcp = 1;
    IPAddress defIp(192,168,1,177);
    IPAddress defGw(192,168,1,1);
    IPAddress defSm(255,255,255,0);
    IPAddress defDns(8,8,8,8);
    for(int i=0;i<4;i++){
      netCfg.ip[i]=defIp[i];
      netCfg.gw[i]=defGw[i];
      netCfg.sm[i]=defSm[i];
      netCfg.dns[i]=defDns[i];
    }
    EEPROM.put(0, netCfg);
  }
}

void saveConfig(){
  EEPROM.put(0, netCfg);
}

void applyNetwork(){
  if(netCfg.dhcp){
    Ethernet.begin(mac);
  }else{
    IPAddress ip(netCfg.ip);
    IPAddress gw(netCfg.gw);
    IPAddress sm(netCfg.sm);
    IPAddress dns(netCfg.dns);
    Ethernet.begin(mac, ip, dns, gw, sm);
  }
}

void setup(){
  loadConfig();
  applyNetwork();
  server.begin();
  encoder.begin();
  encoder.write(0);
  lastPosition = 0;
  lastTime = millis();
  startTime = millis();
}

void sendContent(EthernetClient &client, const char* content, const char* type){
  client.println("HTTP/1.1 200 OK");
  client.print("Content-Type: "); client.println(type);
  client.println("Connection: close");
  client.println();
  for(size_t i=0; pgm_read_byte(&content[i]); i++){
    client.write(pgm_read_byte(&content[i]));
  }
}

void sendConfig(EthernetClient &client){
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.print('{');
  client.print("\"dhcp\":"); client.print(netCfg.dhcp?"true":"false"); client.print(',');
  client.print("\"ip\":\""); client.print(IPAddress(netCfg.ip)); client.print("\",");
  client.print("\"gateway\":\""); client.print(IPAddress(netCfg.gw)); client.print("\",");
  client.print("\"subnet\":\""); client.print(IPAddress(netCfg.sm)); client.print("\",");
  client.print("\"dns\":\""); client.print(IPAddress(netCfg.dns)); client.print("\"");
  client.println('}');
}

void updateConfig(const String &qs){
  int last=0;
  while(last < qs.length()){
    int eq = qs.indexOf('=', last);
    if(eq==-1) break;
    String key = qs.substring(last, eq);
    int amp = qs.indexOf('&', eq);
    if(amp==-1) amp=qs.length();
    String val = qs.substring(eq+1, amp);
    if(key=="dhcp") netCfg.dhcp = val=="1";
    else if(key=="ip"){ IPAddress t; t.fromString(val); for(int i=0;i<4;i++) netCfg.ip[i]=t[i]; }
    else if(key=="gw"){ IPAddress t; t.fromString(val); for(int i=0;i<4;i++) netCfg.gw[i]=t[i]; }
    else if(key=="sm"){ IPAddress t; t.fromString(val); for(int i=0;i<4;i++) netCfg.sm[i]=t[i]; }
    else if(key=="dns"){ IPAddress t; t.fromString(val); for(int i=0;i<4;i++) netCfg.dns[i]=t[i]; }
    last = amp+1;
  }
  saveConfig();
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

    if(req.startsWith("GET /app.js")){
      sendContent(client, APP_JS, "application/javascript");
      return;
    }
    if(req.startsWith("GET /styles.css")){
      sendContent(client, STYLES_CSS, "text/css");
      return;
    }

    if(req.startsWith("GET /config")){
      int q = req.indexOf('?');
      if(q > -1){
        int sp = req.indexOf(' ', q);
        String qs = req.substring(q+1, sp);
        updateConfig(qs);
        client.println("HTTP/1.1 200 OK");
        client.println("Connection: close");
        client.println();
      }else{
        sendConfig(client);
      }
      return;
    }

    sendContent(client, INDEX_HTML, "text/html");
  }
}
