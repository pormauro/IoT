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
