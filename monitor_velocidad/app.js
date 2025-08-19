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
