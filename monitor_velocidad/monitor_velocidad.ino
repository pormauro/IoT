#include <SPI.h>
#include <Ethernet.h>
#include <Encoder.h>
#include <avr/pgmspace.h>

// Configuración de red: ajustar a la red local
byte mac[] = {0xDE,0xAD,0xBE,0xEF,0xFE,0xED};
IPAddress ip(192,168,1,177);
EthernetServer server(80);

// Contenido web incrustado
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Monitor de Velocidad</title>
  <link rel="stylesheet" href="styles.css" />
</head>
<body>
  <header class="hdr">
    <h1>Monitor de Velocidad</h1>
    <button id="toggle-theme" class="theme-toggle" aria-pressed="false" title="Cambiar tema">
      <span class="ico">🌙</span><span class="txt">Dark</span>
    </button>
  </header>
  <main class="content">
    <section class="metric">
      <h2>Velocidad</h2>
      <p id="velocidad" class="value">0</p>
    </section>
    <section class="metric">
      <h2>Posición</h2>
      <p id="posicion" class="value">0</p>
    </section>
    <section class="metric">
      <h2>Tiempo</h2>
      <p id="tiempo" class="value">0</p>
    </section>
    <button id="reset" class="btn primary">Reiniciar</button>
  </main>
  <script src="app.js"></script>
</body>
</html>
)rawliteral";

const char APP_JS[] PROGMEM = R"rawliteral(
(function(){
  const $ = (s, r=document)=> r.querySelector(s);
  const root=document.documentElement;
  const toggleBtn=$('#toggle-theme');
  const themeKey='monitor_velocidad_theme';
  const saved=localStorage.getItem(themeKey);
  if(saved==='dark') root.classList.add('dark');
  function updateToggle(){
    const dark=root.classList.contains('dark');
    if(toggleBtn) toggleBtn.setAttribute('aria-pressed', dark?'true':'false');
    const ico=toggleBtn?toggleBtn.querySelector('.ico'):null;
    const txt=toggleBtn?toggleBtn.querySelector('.txt'):null;
    if(ico) ico.textContent=dark?'☀️':'🌙';
    if(txt) txt.textContent=dark?'Light':'Dark';
  }
  if(toggleBtn){
    toggleBtn.addEventListener('click', ()=>{
      root.classList.toggle('dark');
      localStorage.setItem(themeKey, root.classList.contains('dark')?'dark':'light');
      updateToggle();
    });
  }
  updateToggle();

  async function update(){
    try{
      const res = await fetch('/data',{cache:'no-store'});
      if(!res.ok) throw new Error('net');
      const j = await res.json();
      $('#velocidad').textContent = j.speed.toFixed(2);
      $('#posicion').textContent = j.position;
      $('#tiempo').textContent = j.time;
    }catch(e){
      console.warn('update failed',e);
    }
  }
  setInterval(update,500);
  update();

  const resetBtn=$('#reset');
  if(resetBtn){
    resetBtn.addEventListener('click', ()=>{ fetch('/reset',{cache:'no-store'}); });
  }
})();
)rawliteral";

const char STYLES_CSS[] PROGMEM = R"rawliteral(
:root{
  --bg:#f5f7fa;
  --text:#1f2a37;
  --primary:#2563eb;
}
html.dark{
  --bg:#0b1220;
  --text:#e6edf6;
}
body{
  margin:0;
  font-family:sans-serif;
  background:var(--bg);
  color:var(--text);
  display:flex;
  flex-direction:column;
  align-items:center;
}
.hdr{
  width:100%;
  padding:1rem 2rem;
  display:flex;
  justify-content:space-between;
  align-items:center;
}
.content{
  text-align:center;
  padding:2rem;
}
.metric{
  margin:2rem 0;
}
.metric h2{
  font-size:2rem;
  margin-bottom:1rem;
}
.value{
  font-size:6rem;
}
.btn{
  font-size:2rem;
  padding:1rem 2rem;
  background:var(--primary);
  color:#fff;
  border:none;
  border-radius:8px;
  cursor:pointer;
}
.theme-toggle{
  font-size:1.5rem;
  background:none;
  border:none;
  cursor:pointer;
  color:var(--text);
}
)rawliteral";

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

void sendProgmem(EthernetClient &client, const char *data){
  for(size_t i=0;;i++){
    char c = pgm_read_byte_near(data + i);
    if(c==0) break;
    client.write(c);
  }
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
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: application/javascript");
      client.println("Connection: close");
      client.println();
      sendProgmem(client, APP_JS);
      return;
    }

    if(req.startsWith("GET /styles.css")){
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: text/css");
      client.println("Connection: close");
      client.println();
      sendProgmem(client, STYLES_CSS);
      return;
    }

    // Página principal
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("Connection: close");
    client.println();
    sendProgmem(client, INDEX_HTML);
  }
}
