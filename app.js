(function(){
  const $ = (s, r=document)=> r.querySelector(s);
  const $$ = (s, r=document)=> Array.from(r.querySelectorAll(s));

  // ---------- NAV dinámico (nav.json con fallback) ----------
  const NAV_TREE_FALLBACK = [
    { type:'group', label:'General', icon:'📋', children:[
      { type:'link', route:'/inicio', label:'Inicio', icon:'🏠' },
      { type:'link', route:'/dispositivo', label:'Dispositivo', icon:'🛠️' },
    ]},
    { type:'group', label:'Red', icon:'🌐', children:[
      { type:'link', route:'/wifi', label:'Wi-Fi', icon:'📶' },
      { type:'link', route:'/ipv4', label:'IPv4/Ethernet', icon:'🪪' },
      { type:'link', route:'/bluetooth', label:'Bluetooth', icon:'🔵' },
      { type:'group', label:'Perfiles', icon:'🗂️', children:[
        { type:'link', route:'/perfiles', label:'Perfiles de Red', icon:'📑' }
      ]}
    ]},
    { type:'group', label:'Modbus', icon:'🧯', children:[
      { type:'link', route:'/modbus-tcp', label:'Modbus TCP/IP', icon:'🟩' },
      { type:'link', route:'/modbus-rtu', label:'Modbus RTU', icon:'🟧' },
    ]},
    { type:'group', label:'Gateway', icon:'🔗', children:[
      { type:'link', route:'/gateway', label:'Mapping', icon:'🔀' },
      { type:'link', route:'/diagnostico', label:'Diagnóstico', icon:'🩺' },
    ]},
    { type:'group', label:'Sistema', icon:'🖥️', children:[
      { type:'link', route:'/firmware', label:'Actualización', icon:'⬆️' },
      { type:'link', route:'/seguridad', label:'Seguridad', icon:'🔒' },
    ]},
  ];

  async function loadNavTree(){
    try{
      const res = await fetch('nav.json',{cache:'no-store'});
      if(!res.ok) throw new Error('HTTP '+res.status);
      const json = await res.json();
      if(!Array.isArray(json)) throw new Error('Formato inválido');
      return json;
    }catch(e){
      console.warn('[nav] fallback por error:', e.message);
      return NAV_TREE_FALLBACK;
    }
  }

  const sideNav = $('#side-nav');
  function renderTree(nodes, container, level=0){
    nodes.forEach(node=>{
      if(node.type==='group'){
        const g=document.createElement('div');
        g.className='side-group'; g.dataset.level=level;
        const title=document.createElement('div');
        title.className='side-group-title';
        title.innerHTML=`<span class="icon">${node.icon||''}</span><span class="label">${node.label}</span>`;
        g.appendChild(title);
        const items=document.createElement('div');
        items.className='group-items'; items.dataset.level=level;
        g.appendChild(items);
        container.appendChild(g);
        if(node.children && node.children.length) renderTree(node.children,items,level+1);
      }else if(node.type==='link'){
        const a=document.createElement('a');
        a.href=`#${node.route}`; a.className='nav-item'; a.dataset.route=node.route; a.dataset.level=level;
        a.innerHTML=`<span class="icon">${node.icon||''}</span><span class="label">${node.label}</span>`;
        container.appendChild(a);
      }
    });
  }
  function collectRoutes(nodes,out=[]){ nodes.forEach(n=>{ if(n.type==='link') out.push(n.route); if(n.children) collectRoutes(n.children,out); }); return out; }

  // ---------- Tema ----------
  const root=document.documentElement;
  const themeKey='depros_theme';
  const prefersDark=window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches;
  const saved=localStorage.getItem(themeKey);
  if(saved==='dark' || (!saved && prefersDark)) root.classList.add('dark');
  const toggleBtn=$('#toggle-theme');
  function updateToggle(){
    const dark=root.classList.contains('dark');
    if(toggleBtn) toggleBtn.setAttribute('aria-pressed', dark?'true':'false');
    const ico=toggleBtn ? toggleBtn.querySelector('.ico') : null;
    const txt=toggleBtn ? toggleBtn.querySelector('.txt') : null;
    if(ico) ico.textContent = dark ? '☀️' : '🌙';
    if(txt) txt.textContent = dark ? 'Light' : 'Dark';
  }
  if(toggleBtn){
    toggleBtn.addEventListener('click', ()=>{
      root.classList.toggle('dark');
      localStorage.setItem(themeKey, root.classList.contains('dark')?'dark':'light');
      updateToggle();
    });
  }
  updateToggle();

  // ---------- Drawer móvil ----------
  const body = document.body;
  const menuBtn = $('#toggle-sidebar');
  const backdrop = $('#backdrop');
  const sideDrawer = $('#side-drawer');

  function openSidebar(){
    body.classList.add('sidebar-open');
    if(menuBtn) menuBtn.setAttribute('aria-expanded','true');
    if(backdrop) backdrop.removeAttribute('hidden');
    // primera opción de foco dentro del menú
    const firstLink = sideDrawer ? sideDrawer.querySelector('a.nav-item') : null;
    if(firstLink && firstLink.focus) firstLink.focus();
  }
  function closeSidebar(){
    body.classList.remove('sidebar-open');
    if(menuBtn) menuBtn.setAttribute('aria-expanded','false');
    if(backdrop) backdrop.setAttribute('hidden','');
  }
  function toggleSidebar(){
    if(body.classList.contains('sidebar-open')) closeSidebar(); else openSidebar();
  }

  if(menuBtn) menuBtn.addEventListener('click', toggleSidebar);
  if(backdrop) backdrop.addEventListener('click', closeSidebar);
  window.addEventListener('keydown', (e)=>{ if(e.key==='Escape') closeSidebar(); });

  // ---------- Router ----------
  let ROUTES=[]; let sideLinks=[];
  function pageElFromRoute(route){ const id='page-'+route.slice(1).split('/').join('-'); return $('#'+id); }
  function setActive(route){
    $$('.page').forEach(p=>p.classList.remove('active'));
    var current = pageElFromRoute(route) || $('#page-inicio');
    if(current) current.classList.add('active');
    sideLinks.forEach(a=> a.classList.toggle('active', a.dataset.route===route));
    // En móvil, al navegar cerramos el drawer
    closeSidebar();
    try{ window.scrollTo({top:0,behavior:'instant'}); }catch(_){}
  }
  function parseRoute(){ const h=location.hash.replace('#',''); return ROUTES.includes(h)?h:'/inicio'; }
  function bindSideLinkClicks(){
    sideLinks.forEach(a=>{
      a.addEventListener('click',(e)=>{
        e.preventDefault();
        const r=a.getAttribute('data-route')||'/inicio';
        if(location.hash!=='#'+r) location.hash=r;
        setActive(r);
      });
    });
  }

  // ---------- Wi-Fi demo ----------
  function initWifiInteractions(){
    const pass=$('#wifi-pass'); const btnShow=$('#btn-show');
    if(btnShow){
      btnShow.addEventListener('click', ()=>{
        const show=pass.type==='password';
        pass.type=show?'text':'password';
        btnShow.textContent=show?'Ocultar':'Mostrar';
      });
    }
    const scanBtn=$('#btn-scan');
    const modal=$('#scan-modal');
    const closeBtn=$('#scan-close');
    const refreshBtn=$('#scan-refresh');
    const ssidInput=$('#wifi-ssid');
    function openModal(){ if(modal) modal.classList.add('open'); }
    function closeModal(){ if(modal) modal.classList.remove('open'); }
    if(scanBtn) scanBtn.addEventListener('click', openModal);
    if(closeBtn) closeBtn.addEventListener('click', closeModal);
    if(modal) modal.addEventListener('click', (e)=>{ if(e.target===modal) closeModal(); });
    if(refreshBtn) refreshBtn.addEventListener('click', ()=>{/* no-op */});
    $$('[data-choose-ssid]').forEach(b=>{
      b.addEventListener('click', ()=>{ const s=b.getAttribute('data-choose-ssid'); if(ssidInput&&s) ssidInput.value=s; closeModal(); });
    });
    const testBtn=$('#btn-test-wifi');
    const okB=$('#wifi-banner-ok');
    const errB=$('#wifi-banner-err');
    if(testBtn){
      testBtn.addEventListener('click', ()=>{
        if(!ssidInput || !ssidInput.value){ if(okB) okB.style.display='none'; if(errB) errB.style.display='block'; return; }
        if(errB) errB.style.display='none';
        if(okB){
          okB.style.display='block';
          setTimeout(()=>{ okB.style.display='none'; },2000);
        }
      });
    }
  }

  // ---------- Boot ----------
  async function boot(){
    const tree = await loadNavTree();
    if(sideNav){
      sideNav.innerHTML=''; renderTree(tree, sideNav, 0);
      sideLinks = $$('#side-nav a.nav-item'); ROUTES = collectRoutes(tree);
      window.addEventListener('hashchange', ()=> setActive(parseRoute()));
      if(!location.hash) location.hash='/inicio';
      setActive(parseRoute());
      bindSideLinkClicks();
    }
    initWifiInteractions();
  }
  boot();
})();
