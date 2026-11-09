/**
 * @file    web_page.h
 * @brief   Native web dashboard served at "/" (HTML/CSS/JS stored in flash).
 *
 * @details A single responsive page with tabs, rendered in the browser from
 *          /api/state.json (refreshed every 20 s), /history.csv and
 *          /api/now.json. Chart.js and Leaflet are loaded by the *browser*
 *          from public CDNs; the radar and flight maps fetch OpenStreetMap and
 *          RainViewer tiles directly, so the device only serves JSON.
 *
 *          Tabs: Now, Forecast, Radar, Flights, Air & Grid, Sky & Alerts,
 *          Home, Lab, History, Mirror (live screenshots of the display),
 *          System. The URL hash selects the tab (e.g. /#flights).
 */
#pragma once

static const char WEB_PAGE[] = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Weather Station</title>
<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/leaflet/1.9.4/leaflet.min.css">
<style>
  :root { --bg:#0d1117; --card:#161b22; --card2:#1f2733; --line:#30363d; --text:#e6edf3; --muted:#8b949e; --dim:#6e7681;
          --accent:#58a6ff; --green:#3fb950; --yellow:#e3b341; --orange:#f0883e; --red:#f85149; --cyan:#56d4dd; --purple:#bc8cff; }
  * { box-sizing:border-box; }
  body { margin:0; background:var(--bg); color:var(--text); font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif; }
  header { display:flex; align-items:center; gap:12px; flex-wrap:wrap; padding:12px 18px; border-bottom:1px solid var(--line); }
  header h1 { font-size:18px; margin:0; }
  header .clock { color:var(--muted); }
  header a.cfg { margin-left:auto; color:var(--accent); text-decoration:none; }
  nav { display:flex; gap:4px; overflow-x:auto; padding:8px 12px; border-bottom:1px solid var(--line); position:sticky; top:0; background:var(--bg); z-index:1000; }
  nav a { color:var(--muted); text-decoration:none; padding:7px 12px; border-radius:8px; white-space:nowrap; position:relative; }
  nav a.on { background:var(--card2); color:var(--accent); }
  nav a .dot { position:absolute; top:4px; right:4px; width:7px; height:7px; border-radius:50%; background:var(--orange); }
  main { max-width:1400px; margin:0 auto; padding:16px; }
  section { display:none; } section.on { display:block; }
  .grid { display:grid; gap:14px; grid-template-columns:repeat(auto-fill,minmax(320px,1fr)); }
  .wide { grid-column:1/-1; }
  .card { background:var(--card); border:1px solid var(--line); border-radius:12px; padding:14px 16px; }
  .card h3 { margin:0 0 10px; font-size:13px; color:var(--muted); font-weight:600; text-transform:uppercase; letter-spacing:.05em; display:flex; justify-content:space-between; }
  .card h3 small { text-transform:none; letter-spacing:0; font-weight:400; }
  .big { font-size:64px; font-weight:300; line-height:1; }
  .mid { font-size:28px; font-weight:500; }
  .muted { color:var(--muted); } .dim { color:var(--dim); } .small { font-size:13px; }
  .row { display:flex; justify-content:space-between; gap:10px; padding:5px 0; border-bottom:1px solid #21262d; }
  .row:last-child { border-bottom:0; }
  .chips { display:flex; flex-wrap:wrap; gap:6px; }
  .chip { padding:3px 10px; border-radius:999px; font-size:13px; background:var(--card2); }
  .bar { height:10px; background:var(--card2); border-radius:6px; overflow:hidden; } .bar > i { display:block; height:100%; border-radius:6px; }
  .hours { display:grid; grid-template-columns:repeat(auto-fill,minmax(64px,1fr)); gap:6px; text-align:center; }
  .hours div { background:var(--card2); border-radius:8px; padding:6px 2px; }
  .em { font-size:26px; line-height:1.2; }
  .days { display:grid; grid-template-columns:repeat(auto-fill,minmax(130px,1fr)); gap:10px; }
  .days .card { text-align:center; }
  .chart { position:relative; height:260px; } .chart.sm { height:120px; }
  .map { height:65vh; min-height:380px; border-radius:12px; border:1px solid var(--line); }
  .flex { display:flex; gap:14px; flex-wrap:wrap; } .flex > * { flex:1 1 320px; }
  table { width:100%; border-collapse:collapse; font-size:14px; }
  th, td { text-align:left; padding:6px 8px; border-bottom:1px solid #21262d; } th { color:var(--muted); font-weight:600; }
  tr.sel td { background:#1c2a3f; } tbody tr { cursor:pointer; }
  .photo { width:100%; aspect-ratio:3/2; object-fit:cover; border-radius:8px; background:var(--card2); }
  button, .btn { background:var(--card2); color:var(--text); border:1px solid var(--line); border-radius:6px; padding:5px 12px; font:inherit; font-size:13px; cursor:pointer; text-decoration:none; }
  button.on { border-color:var(--accent); color:var(--accent); }
  .screens { display:grid; grid-template-columns:repeat(auto-fill,minmax(300px,1fr)); gap:14px; }
  .scr img { width:100%; image-rendering:pixelated; border-radius:6px; background:#000; display:block; aspect-ratio:var(--ar,800/480); }
  .warn { border-left:6px solid var(--yellow); }
  .plane { transition:transform .3s; }
  .leaflet-container { background:#1a1f27; }
  .leaflet-tooltip { background:#161b22; color:#e6edf3; border:1px solid #30363d; }
  footer { color:var(--dim); font-size:12px; text-align:center; padding:20px; }
  @media (max-width:520px) { main { padding:10px; } .big { font-size:52px; } }
</style>
</head>
<body>
<header>
  <h1 id="title">Weather Station</h1>
  <span class="clock" id="clock"></span>
  <span class="chips" id="pills"></span>
  <a class="cfg" href="/settings">&#9881; Settings</a>
</header>
<nav id="nav"></nav>
<main>
  <section id="now"></section>
  <section id="forecast"></section>
  <section id="radar"><div class="flex"><div><div id="radarMap" class="map"></div>
    <div style="margin-top:8px;display:flex;gap:8px;align-items:center"><button id="rPlay">&#10073;&#10073;</button><input id="rSlider" type="range" min="0" max="0" value="0" style="flex:1"><span id="rTime" class="muted"></span></div></div>
    <div style="flex:0 1 340px" id="radarSide"></div></div></section>
  <section id="flights"><div class="flex"><div><div id="flightMap" class="map"></div></div><div style="flex:0 1 380px" id="flightSide"></div></div>
    <div class="card" style="margin-top:14px"><h3>Aircraft in range</h3><table id="flightTable"></table></div></section>
  <section id="airgrid"></section>
  <section id="skyalerts"></section>
  <section id="home"></section>
  <section id="lab"></section>
  <section id="history"><div style="display:flex;gap:8px;flex-wrap:wrap;margin-bottom:12px" id="histBtns"></div><div class="grid" id="histCharts"></div></section>
  <section id="mirror"><p class="muted small">Live screenshots of the device display. Click one to enlarge.</p><div class="screens" id="screens"></div></section>
  <section id="system"></section>
</main>
<footer>Data: WeatherFlow Tempest (local) &middot; Open-Meteo &middot; Met Office &middot; AuroraWatch UK &middot; RainViewer &middot; &copy; OpenStreetMap contributors &middot; adsb.lol (ODbL) &middot; adsbdb &middot; planespotters.net &middot; carbonintensity.org.uk</footer>

<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js"></script>
<script src="https://cdnjs.cloudflare.com/ajax/libs/leaflet/1.9.4/leaflet.min.js"></script>
<script>
'use strict';
const TABS = [['now','Now'],['forecast','Forecast'],['radar','Radar'],['flights','Flights'],['airgrid','Air & Grid'],
  ['skyalerts','Sky & Alerts'],['home','Home'],['lab','Lab'],['history','History'],['mirror','Mirror'],['system','System']];
let S = null, tab = 'now', charts = {}, selHex = null;
const $ = id => document.getElementById(id);
const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const num = (v, d = 1) => v === null || v === undefined || isNaN(v) ? '–' : Number(v).toFixed(d);
const hm = t => new Date(t * 1000).toLocaleTimeString([], {hour:'2-digit', minute:'2-digit', hour12:false});
const day = t => new Date(t * 1000).toLocaleDateString([], {weekday:'short', day:'numeric', month:'short'});
const DAYS = ['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];
const COMPASS = ['N','NE','E','SE','S','SW','W','NW'];
const compass = d => COMPASS[Math.round(((d % 360) + 360) % 360 / 45) % 8];
const ago = s => s === undefined ? 'never' : s < 90 ? `${s}s ago` : s < 5400 ? `${Math.round(s/60)} min ago` : `${Math.round(s/3600)} h ago`;
function wx(code, dayTime = true) {
  if (code === 0) return [dayTime ? '☀️' : '🌙', 'Clear'];
  if (code <= 2) return [dayTime ? '🌤️' : '☁️', code === 1 ? 'Mainly clear' : 'Partly cloudy'];
  if (code === 3) return ['☁️', 'Overcast'];
  if (code === 45 || code === 48) return ['🌫️', 'Fog'];
  if (code >= 51 && code <= 57) return ['🌦️', 'Drizzle'];
  if (code >= 61 && code <= 67) return ['🌧️', 'Rain'];
  if (code >= 71 && code <= 77) return ['🌨️', 'Snow'];
  if (code >= 80 && code <= 82) return ['🌦️', 'Showers'];
  if (code === 85 || code === 86) return ['🌨️', 'Snow showers'];
  if (code >= 95) return ['⛈️', 'Thunder'];
  return ['☁️', ''];
}
const LEVEL_COL = {Green:'var(--green)', Yellow:'var(--yellow)', Amber:'var(--orange)', Red:'var(--red)'};
const AQI_COL = ['#50dcc8','#3fb950','#e3b341','#f0883e','#f85149','#bc8cff'];
const CI_COL = ['#6e7681','#28aa5a','#3fb950','#e3b341','#f0883e','#f85149'];
const card = (title, body, cls = '', extra = '') => `<div class="card ${cls}"><h3>${title}${extra ? `<small>${extra}</small>` : ''}</h3>${body}</div>`;
const row = (k, v, col = '') => `<div class="row"><span class="muted">${k}</span><span style="${col ? 'color:' + col : ''}">${v}</span></div>`;
const bar = (pct, col) => `<div class="bar"><i style="width:${Math.max(2, Math.min(100, pct))}%;background:${col}"></i></div>`;
function chart(id, cfg) {
  if (charts[id]) charts[id].destroy();
  const el = $(id); if (!el || !window.Chart) return;
  Chart.defaults.color = '#8b949e'; Chart.defaults.borderColor = '#30363d';
  cfg.options = Object.assign({responsive:true, maintainAspectRatio:false, animation:false, interaction:{mode:'index', intersect:false},
    plugins:{legend:{display:false}}}, cfg.options || {});
  charts[id] = new Chart(el, cfg);
}

// ---------------------------------------------------------------- tabs
function buildNav() {
  $('nav').innerHTML = TABS.map(([id, name]) => `<a href="#${id}" data-tab="${id}">${name}</a>`).join('');
}
function show(t) {
  tab = TABS.some(x => x[0] === t) ? t : 'now';
  document.querySelectorAll('section').forEach(s => s.classList.toggle('on', s.id === tab));
  document.querySelectorAll('nav a').forEach(a => a.classList.toggle('on', a.dataset.tab === tab));
  render();
  if (tab === 'radar') initRadar();
  if (tab === 'flights') initFlights();
  if (tab === 'history') loadHistory();
  if (tab === 'mirror') initMirror();
}
window.addEventListener('hashchange', () => show(location.hash.slice(1)));

// ---------------------------------------------------------------- data
async function refresh() {
  try { S = await (await fetch('/api/state.json')).json(); } catch (e) { return; }
  if (tab === 'flights' && !fMap) initFlights();  // First data arrived while already on the Flights tab.
  render();
}
function render() {
  if (!S) return;
  const sys = S.system, n = S.now;
  $('title').textContent = 'Weather Station · ' + sys.board;
  $('clock').textContent = new Date(sys.now * 1000).toLocaleString([], {weekday:'short', hour:'2-digit', minute:'2-digit', hour12:false});
  const alert = (S.alerts.warnings || []).length || ['Amber','Red'].includes(S.alerts.aurora?.level) || ['Moderate','High'].includes(S.alerts.frost?.level);
  const pills = [];
  if (n.temp !== undefined) pills.push(`<span class="chip">${num(n.temp)}°C · ${esc(n.source)}</span>`);
  if (S.forecast.current) pills.push(`<span class="chip">${wx(S.forecast.current.code, S.forecast.current.isDay)[0]} ${esc(S.forecast.current.desc)}</span>`);
  if (alert) pills.push(`<span class="chip" style="background:#5a3a12">⚠ Alerts</span>`);
  $('pills').innerHTML = pills.join('');
  document.querySelectorAll('nav a').forEach(a => {
    const dot = a.dataset.tab === 'skyalerts' && alert;
    a.innerHTML = TABS.find(t => t[0] === a.dataset.tab)[1] + (dot ? '<span class="dot"></span>' : '');
  });
  ({now:renderNow, forecast:renderForecast, radar:renderRadarSide, flights:renderFlights, airgrid:renderAirGrid,
    skyalerts:renderSkyAlerts, home:renderHome, lab:renderLab, system:renderSystem}[tab] || (() => {}))();
}

// ---------------------------------------------------------------- now
function compassSvg(dir, live, liveDir) {
  const ticks = Array.from({length:24}, (_, i) => { const a = i * 15 * Math.PI / 180, l = i % 6 ? 6 : 12;
    return `<line x1="${Math.sin(a)*(90-l)}" y1="${-Math.cos(a)*(90-l)}" x2="${Math.sin(a)*90}" y2="${-Math.cos(a)*90}" stroke="#8b949e"/>`; }).join('');
  const lbl = ['N','E','S','W'].map((t, i) => { const a = i * Math.PI / 2;
    return `<text x="${Math.sin(a)*70}" y="${-Math.cos(a)*70+6}" text-anchor="middle" fill="${i ? '#e6edf3' : '#f85149'}" font-size="16">${t}</text>`; }).join('');
  const marker = dir === undefined ? '' : `<g transform="rotate(${dir})"><path d="M0,-92 L10,-70 L-10,-70Z" fill="#58a6ff"/></g>`;
  const needle = liveDir === undefined || !(live > 0.4) ? '' : `<g transform="rotate(${liveDir + 180})"><line x1="0" y1="55" x2="0" y2="-55" stroke="#e3b341" stroke-width="4"/><path d="M0,-62 L8,-48 L-8,-48Z" fill="#e3b341"/></g>`;
  return `<svg viewBox="-100 -100 200 200" style="width:200px;height:200px"><circle r="92" fill="#1f2733" stroke="#30363d"/>${ticks}${lbl}${marker}${needle}<circle r="30" fill="#161b22"/></svg>`;
}
function renderNow() {
  const n = S.now, f = S.forecast, a = S.alerts;
  if (n.temp === undefined) { $('now').innerHTML = '<p class="muted">Waiting for data…</p>'; return; }
  const trendTxt = n.trend === null || n.trend === undefined ? '' : ` <span style="color:${Math.abs(n.trend) < 0.5 ? 'var(--muted)' : n.trend > 0 ? 'var(--green)' : 'var(--orange)'}">${n.trend > 0 ? '▲' : n.trend < 0 ? '▼' : '▶'} ${num(n.trend)} / ${Math.round(n.trendMin / 60) || 1}h</span>`;
  const outside = card('Outside', `
      <div style="display:flex;align-items:flex-end;gap:16px;flex-wrap:wrap"><div class="big">${num(n.temp)}°</div>
      <div><div>Feels ${num(n.feels)}°</div>${f.daily ? `<div><span style="color:var(--orange)">H ${num(f.daily[0].max, 0)}°</span> <span style="color:var(--cyan)">L ${num(f.daily[0].min, 0)}°</span></div>` : ''}</div></div>
      <div style="margin-top:10px">${row('Humidity', num(n.humidity, 0) + '%')}${n.dew !== undefined ? row('Dew point', num(n.dew) + '°') : ''}
      ${row('Pressure', num(n.pressure) + ' hPa' + trendTxt)}
      ${n.rainToday !== undefined ? row('Rain today', `${num(n.rainToday)} mm${n.rainRate > 0.05 ? ` · ${num(n.rainRate)} mm/h (${n.intensity})` : ''}`, 'var(--accent)') : ''}
      ${n.uv !== undefined ? row('UV / solar', `${num(n.uv)} · ${num(n.solar, 0)} W/m²`, 'var(--yellow)') : ''}
      ${n.strikesToday !== undefined ? row('Lightning', `${n.strikesToday} today, ${n.strikesHour} last hour${n.lastStrikeTime ? ` · last ${n.lastStrikeKm} km at ${hm(n.lastStrikeTime)}` : ''}`) : ''}</div>`,
      '', `${esc(n.source)}${n.agoS !== undefined ? ' · ' + ago(n.agoS) : ''}`);
  const calm = !(n.windAvg > 0.5) && !(n.liveMph > 0.5);
  const wind = card('Wind', `<div style="display:flex;gap:16px;align-items:center;flex-wrap:wrap">${compassSvg(calm ? undefined : n.windDir, n.liveMph, n.liveDir)}
      <div style="flex:1;min-width:150px">${row('Average', num(n.windAvg) + ' mph')}${row('Gust', num(n.windGust) + ' mph', 'var(--orange)')}
      ${n.windLull !== undefined ? row('Lull', num(n.windLull) + ' mph') : ''}${row('From', calm ? 'calm' : `${compass(n.windDir)} ${n.windDir}°`, 'var(--accent)')}
      ${n.liveMph !== undefined ? row('Live (3 s)', num(n.liveMph) + ' mph', 'var(--yellow)') : ''}</div></div>`);
  // Outlook: live rain > radar arrival > Open-Meteo nowcast.
  let rain = 'Rain outlook unavailable', rc = 'var(--muted)';
  const eta = S.radar.eta, nc = f.nowcast || {};
  if (n.rainRate > 0.05) { rain = `Raining now: ${n.intensity}, ${num(n.rainRate)} mm/h`; rc = 'var(--accent)'; }
  else if (eta.valid && eta.approaching && !eta.rainingHere && eta.minutes <= 120) { rain = `Radar: rain in ~${eta.minutes} min from the ${compass(eta.fromDeg)} (${eta.speedKmh} km/h)`; rc = 'var(--accent)'; }
  else if (nc.state === 'dry') { rain = 'Dry for the next 2 hours'; rc = 'var(--green)'; }
  else if (nc.state === 'starting') { rain = `Rain starting in ~${nc.minutes} min`; rc = 'var(--accent)'; }
  else if (nc.state === 'raining') { rain = nc.minutes >= 0 ? `Raining, easing in ~${nc.minutes} min` : 'Raining for 2 h+'; rc = 'var(--accent)'; }
  const chips = [];
  if (S.air.eaqi !== undefined) chips.push(`<span class="chip" style="background:${AQI_COL[S.air.bandIndex]};color:#0d1117">AQI ${S.air.eaqi} ${S.air.band}</span>`);
  if (S.grid.now !== undefined) chips.push(`<span class="chip">Grid ${S.grid.now} g/kWh ${S.grid.index}</span>`);
  if (a.frost && a.frost.level !== 'None') chips.push(`<span class="chip" style="background:#1e3a4a;color:var(--cyan)">❄ ${esc(a.frost.headline)}</span>`);
  (a.warnings || []).forEach(w => chips.push(`<span class="chip" style="background:${LEVEL_COL[w.level]};color:#0d1117">⚠ ${w.level} ${esc(w.hazard)}</span>`));
  if (f.sunrise) chips.push(`<span class="chip">☀ ${f.sunrise} – ${f.sunset}</span>`);
  const c = f.current ? wx(f.current.code, f.current.isDay) : ['', ''];
  const hours = (f.hourly || []).slice(1, 13).map(h => `<div><div class="small muted">${String(h.h).padStart(2, '0')}</div><div class="em">${wx(h.code, h.day)[0]}</div><div>${num(h.t, 0)}°</div>${h.rain >= 20 ? `<div class="small" style="color:var(--accent)">${h.rain}%</div>` : ''}</div>`).join('');
  const outlook = card('Outlook', `<div style="display:flex;gap:14px;align-items:center"><div style="font-size:56px">${c[0]}</div>
      <div><div class="mid">${esc(f.current?.desc || '')}</div><div style="color:${rc}">${rain}</div></div></div>
      <div class="chips" style="margin:10px 0">${chips.join('')}</div><div class="hours">${hours}</div>`, 'wide');
  const trends = n.trendTemp && n.trendTemp.length > 3 ? card('Last 3 hours (Tempest)', `<div class="flex"><div><div class="small muted">Temperature</div><div class="chart sm"><canvas id="cT"></canvas></div></div><div><div class="small muted">Pressure</div><div class="chart sm"><canvas id="cP"></canvas></div></div><div><div class="small muted">Wind</div><div class="chart sm"><canvas id="cW"></canvas></div></div></div>`, 'wide') : '';
  $('now').innerHTML = `<div class="grid">${outside}${wind}${outlook}${trends}</div>`;
  if (trends) {
    const lab = n.trendTemp.map((_, i) => i);
    const spark = (id, data, col) => chart(id, {type:'line', data:{labels:lab, datasets:[{data, borderColor:col, pointRadius:0, borderWidth:2, tension:.3}]},
      options:{scales:{x:{display:false}, y:{ticks:{maxTicksLimit:3}}}}});
    spark('cT', n.trendTemp, '#f0883e'); spark('cP', n.trendPress, '#3fb950'); spark('cW', n.trendWind, '#56d4dd');
  }
}

// ---------------------------------------------------------------- forecast
function renderForecast() {
  const f = S.forecast;
  if (!f.hourly) { $('forecast').innerHTML = '<p class="muted">Waiting for forecast…</p>'; return; }
  const days = (f.daily || []).map((d, i) => `<div class="card"><div class="muted">${i ? DAYS[d.wd] : 'Today'}</div><div style="font-size:40px">${wx(d.code)[0]}</div>
      <div class="small">${wx(d.code)[1]}</div><div class="mid"><span style="color:var(--orange)">${num(d.max, 0)}°</span> <span style="color:var(--cyan);font-size:20px">${num(d.min, 0)}°</span></div>
      <div class="small" style="color:var(--accent)">💧 ${d.rain}%</div><div class="small muted">UV ${d.uv}</div></div>`).join('');
  $('forecast').innerHTML = `<div class="grid">${card('Next 24 hours', '<div class="chart"><canvas id="cHours"></canvas></div>', 'wide')}
      <div class="wide"><div class="days">${days}</div></div>
      ${card('Sun', `${row('Sunrise', f.sunrise)}${row('Sunset', f.sunset)}${row('Daylight', `${Math.floor(f.daylightS / 3600)}h ${String(Math.round(f.daylightS % 3600 / 60)).padStart(2, '0')}m`)}${row('Forecast sunshine', num(f.sunshineS / 3600) + ' h')}`)}</div>`;
  const h = f.hourly.slice(1, 25);
  chart('cHours', {data:{labels:h.map(x => String(x.h).padStart(2, '0') + ':00'), datasets:[
      {type:'line', label:'°C', data:h.map(x => x.t), borderColor:'#f0883e', yAxisID:'y', pointRadius:2, tension:.3},
      {type:'bar', label:'Rain %', data:h.map(x => x.rain), backgroundColor:'rgba(88,166,255,.35)', yAxisID:'y1'},
      {type:'line', label:'Wind mph', data:h.map(x => x.wind), borderColor:'#56d4dd', borderDash:[4, 3], yAxisID:'y', pointRadius:0}]},
    options:{plugins:{legend:{display:true}}, scales:{y1:{position:'right', min:0, max:100, grid:{drawOnChartArea:false}}}}});
}

// ---------------------------------------------------------------- radar
let rMap = null, rLayers = [], rIdx = 0, rTimer = null, rPlaying = true, rFrames = [];
async function initRadar() {
  if (!window.L) return;
  if (!rMap) {
    rMap = L.map('radarMap', {zoomControl:true}).setView([S?.system.lat || 53.8, S?.system.lon || -2.45], 7);
    L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {maxZoom:12, attribution:'&copy; OpenStreetMap contributors'}).addTo(rMap);
    L.circleMarker([S?.system.lat || 53.8, S?.system.lon || -2.45], {radius:6, color:'#fff', weight:2, fillColor:'#58a6ff', fillOpacity:1}).addTo(rMap);
    $('rPlay').onclick = () => { rPlaying = !rPlaying; $('rPlay').innerHTML = rPlaying ? '&#10073;&#10073;' : '&#9654;'; };
    $('rSlider').oninput = e => { rPlaying = false; $('rPlay').innerHTML = '&#9654;'; showFrame(+e.target.value); };
    await loadRadarFrames();
    setInterval(loadRadarFrames, 5 * 60 * 1000);
    rTimer = setInterval(() => { if (rPlaying && rLayers.length && tab === 'radar') showFrame((rIdx + 1) % (rLayers.length + 3)); }, 600);
  }
  setTimeout(() => rMap.invalidateSize(), 50);
}
async function loadRadarFrames() {
  try {
    const d = await (await fetch('https://api.rainviewer.com/public/weather-maps.json')).json();
    rLayers.forEach(l => rMap.removeLayer(l));
    rFrames = d.radar.past;
    rLayers = rFrames.map(fr => L.tileLayer(`${d.host}${fr.path}/256/{z}/{x}/{y}/2/1_1.png`, {opacity:0, maxNativeZoom:7, maxZoom:12, attribution:'RainViewer'}).addTo(rMap));
    $('rSlider').max = rLayers.length - 1;
    showFrame(rLayers.length - 1);
  } catch (e) { $('rTime').textContent = 'Radar unavailable'; }
}
function showFrame(i) {
  const n = Math.min(i, rLayers.length - 1);
  rLayers.forEach((l, k) => l.setOpacity(k === n ? 0.75 : 0));
  rIdx = i; $('rSlider').value = n;
  if (rFrames[n]) $('rTime').textContent = hm(rFrames[n].time) + (n === rFrames.length - 1 ? ' (latest)' : '');
}
function renderRadarSide() {
  const f = S.forecast, eta = S.radar.eta, nc = f.nowcast || {mm:[]};
  const max = Math.max(1, ...nc.mm);
  const bars = nc.mm.map((v, i) => `<div style="flex:1;display:flex;flex-direction:column;justify-content:flex-end;height:110px"><div style="height:${Math.max(v > 0.01 ? 4 : 0, Math.sqrt(v / 2) * 110)}px;background:${v >= 1 ? 'var(--yellow)' : v >= .25 ? 'var(--accent)' : '#96c8ff'};border-radius:3px"></div></div>`).join('');
  let track = 'No rain on the map', tc = 'var(--muted)';
  if (eta.valid) {
    if (eta.rainingHere) { track = 'Rain overhead now'; tc = 'var(--accent)'; }
    else if (eta.approaching) { track = `Rain in ~${eta.minutes} min (${eta.distanceKm} km away)`; tc = 'var(--accent)'; }
    else { track = 'None heading this way'; tc = 'var(--green)'; }
  } else if (eta.coverage > 1) track = 'Too little rain to track';
  $('radarSide').innerHTML = card('Rain, next 2 hours', `<div class="mid" style="color:${nc.state === 'dry' ? 'var(--green)' : 'var(--accent)'}">${nc.state === 'dry' ? 'Dry' : nc.state === 'starting' ? `In ${nc.minutes} min` : nc.state === 'raining' ? 'Raining' : '–'}</div>
      <div style="display:flex;gap:4px;margin:12px 0">${bars}</div><div class="small muted" style="display:flex;justify-content:space-between"><span>now</span><span>+1 h</span><span>+2 h</span></div>`)
    + '<div style="height:14px"></div>' + card('Radar tracking (device)', `<div style="color:${tc}">${track}</div>${eta.valid && eta.speedKmh >= 3 ? `<div class="small muted">Showers moving from the ${compass(eta.fromDeg)} at ${eta.speedKmh} km/h</div>` : ''}<div class="small dim">${eta.coverage}% of the 375 km map has rain</div>`);
}

// ---------------------------------------------------------------- flights
let fMap = null, fLayer = null, homeLL = null;
function dest(lat, lon, distNm, brg) {
  const R = 3440.065, d = distNm / R, b = brg * Math.PI / 180, p1 = lat * Math.PI / 180, l1 = lon * Math.PI / 180;
  const p2 = Math.asin(Math.sin(p1) * Math.cos(d) + Math.cos(p1) * Math.sin(d) * Math.cos(b));
  const l2 = l1 + Math.atan2(Math.sin(b) * Math.sin(d) * Math.cos(p1), Math.cos(d) - Math.sin(p1) * Math.sin(p2));
  return [p2 * 180 / Math.PI, l2 * 180 / Math.PI];
}
const altCol = a => a.ground ? '#6e7681' : a.alt < 5000 ? '#3fb950' : a.alt < 20000 ? '#56d4dd' : '#e6edf3';
function initFlights() {
  if (!window.L || !S) return;
  if (!fMap) {
    homeLL = [S.system.lat, S.system.lon];
    fMap = L.map('flightMap').setView(homeLL, 9);
    L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {maxZoom:13, attribution:'&copy; OpenStreetMap contributors'}).addTo(fMap);
    L.circle(homeLL, {radius:S.flights.radiusNm * 1852, color:'#30363d', fill:false, weight:1}).addTo(fMap);
    L.circle(homeLL, {radius:S.flights.radiusNm * 926, color:'#30363d', fill:false, weight:1, dashArray:'4 4'}).addTo(fMap);
    L.circleMarker(homeLL, {radius:5, color:'#fff', fillColor:'#58a6ff', fillOpacity:1, weight:2}).addTo(fMap);
    fLayer = L.layerGroup().addTo(fMap);
  }
  setTimeout(() => fMap.invalidateSize(), 50);
  renderFlights();
}
function renderFlights() {
  const fl = S.flights, list = fl.aircraft || [];
  if (!selHex || !list.some(a => a.hex === selHex)) selHex = fl.nearestHex || null;
  if (fMap) {
    fLayer.clearLayers();
    list.forEach(a => {
      const pos = [a.lat, a.lon], sel = a.hex === selHex, col = sel ? '#e3b341' : altCol(a);
      if (a.trail && a.trail.length) L.polyline([...a.trail.map(p => dest(homeLL[0], homeLL[1], p[0], p[1])), pos], {color:col, weight:sel ? 3 : 1.5, opacity:.6}).addTo(fLayer);
      const svg = `<svg viewBox="-12 -12 24 24" width="${sel ? 30 : 22}" height="${sel ? 30 : 22}" style="transform:rotate(${a.trk}deg)"><path d="M0,-11 L2,-6 L2,-2 L11,3 L11,5 L2,2 L2,7 L5,10 L-5,10 L-2,7 L-2,2 L-11,5 L-11,3 L-2,-2 L-2,-6Z" fill="${col}" stroke="#0d1117" stroke-width=".8"/></svg>`;
      L.marker(pos, {icon:L.divIcon({html:svg, className:'plane', iconSize:[sel ? 30 : 22, sel ? 30 : 22]})}).addTo(fLayer)
        .bindTooltip(`${esc(a.cs || a.reg)} · ${a.ground ? 'ground' : a.alt + ' ft'}`, {direction:'top'}).on('click', () => select(a.hex));
    });
  }
  const a = list.find(x => x.hex === selHex);
  let side = card('Aircraft', '<p class="muted">No aircraft in range</p>');
  if (a) {
    if (!a.route && a.cs) fetch(`/api/lookup?cs=${encodeURIComponent(a.cs)}&hex=${a.hex}`);
    const r = a.route;
    const routeHtml = r && r.known ? `<div class="mid">${esc(r.from)} → ${esc(r.to)}</div><div class="muted">${esc(r.fromCity)} to ${esc(r.toCity)}</div><div style="color:var(--accent)">${esc(r.airline)}</div>`
      : `<div class="muted">${r ? 'Route unknown' : a.cs ? 'Looking up route…' : 'No callsign'}</div>`;
    side = card(a.hex === S.flights.nearestHex ? 'Nearest aircraft' : 'Selected aircraft', `
      <img class="photo" id="planePhoto" alt="" src="/api/photo?hex=${a.hex}" onerror="this.style.visibility='hidden'">
      <div class="mid" style="color:var(--yellow);margin-top:8px">${esc(a.cs || a.reg || a.hex)}</div>${routeHtml}
      <div style="margin-top:8px">${row('Aircraft', `${esc(a.type)} ${esc(a.reg)}`)}${a.desc ? row('Type', esc(a.desc)) : ''}
      ${row('Altitude', a.ground ? 'On ground' : a.alt.toLocaleString() + ' ft', altCol(a))}${row('Vertical', a.vr > 300 ? `▲ ${a.vr} fpm` : a.vr < -300 ? `▼ ${-a.vr} fpm` : 'Level')}
      ${row('Speed / track', `${a.gs} kt / ${a.trk}°`)}${row('Distance', `${a.dist} nm ${compass(a.brg)}`, 'var(--cyan)')}${a.sq ? row('Squawk', a.sq) : ''}</div>
      <div class="small" style="margin-top:8px"><a href="https://globe.adsb.lol/?icao=${a.hex}" target="_blank" style="color:var(--accent)">Track on adsb.lol ↗</a></div>`);
  }
  $('flightSide').innerHTML = side;
  if (a && a.photo !== 1) setTimeout(() => { const img = $('planePhoto'); if (img && img.src.includes(a.hex)) img.src = `/api/photo?hex=${a.hex}&t=${Date.now()}`; }, 4000);
  $('flightTable').innerHTML = `<thead><tr><th>Callsign</th><th>Route</th><th>Type</th><th>Altitude</th><th>Speed</th><th>Distance</th></tr></thead><tbody>${
    list.map(x => `<tr class="${x.hex === selHex ? 'sel' : ''}" onclick="select('${x.hex}')"><td>${esc(x.cs || x.reg)}</td><td>${x.route && x.route.known ? esc(x.route.from + ' → ' + x.route.to) : '<span class="dim">–</span>'}</td><td>${esc(x.type)}</td><td style="color:${altCol(x)}">${x.ground ? 'ground' : x.alt.toLocaleString() + ' ft'}</td><td>${x.gs} kt</td><td>${x.dist} nm ${compass(x.brg)}</td></tr>`).join('')}</tbody>`;
}
function select(hex) { selHex = hex; renderFlights(); }

// ---------------------------------------------------------------- air & grid
function renderAirGrid() {
  const a = S.air, g = S.grid;
  const pol = (name, v, guide) => `<div style="margin:8px 0"><div style="display:flex;justify-content:space-between"><span>${name}</span><span>${num(v, 0)} µg/m³</span></div>${bar(v / guide * 50, v / guide < .5 ? 'var(--green)' : v / guide < 1 ? 'var(--yellow)' : v / guide < 2 ? 'var(--orange)' : 'var(--red)')}</div>`;
  const air = a.eaqi === undefined ? card('Air quality', '<p class="muted">Waiting…</p>') : card('Air quality', `
      <div style="display:flex;gap:16px;align-items:center"><div class="big" style="color:${AQI_COL[a.bandIndex]}">${a.eaqi}</div><div><div class="mid" style="color:${AQI_COL[a.bandIndex]}">${a.band}</div><div class="muted small">European AQI</div></div></div>
      <div style="display:flex;gap:2px;margin:12px 0">${AQI_COL.map(c => `<div style="flex:1;height:8px;background:${c}"></div>`).join('')}</div>
      ${pol('PM2.5', a.pm25, 15)}${pol('PM10', a.pm10, 45)}${pol('NO₂', a.no2, 25)}${pol('Ozone', a.o3, 100)}<div class="small dim">Bar midpoint = WHO 24-hour guideline</div>`);
  const uv = a.eaqi === undefined ? '' : card('UV & pollen', `${row('UV now', num(S.now.uv ?? a.uv), 'var(--yellow)')}${row('UV max today', num(a.uvMax, 0))}${row('Pollen', `${a.pollen}${a.pollenType ? ' · ' + a.pollenType : ''}`, 'var(--green)')}${row('Grass / birch / alder', `${a.grass} / ${a.birch} / ${a.alder} grains/m³`)}`);
  let grid = card('Electricity grid', '<p class="muted">Waiting…</p>');
  if (g.now !== undefined) {
    const mix = Object.entries(g.mix).filter(e => e[1] > 0.4).sort((x, y) => y[1] - x[1]);
    const FUEL_COL = {Wind:'#78c8ff', Solar:'#e3b341', Nuclear:'#bc8cff', Hydro:'#58a6ff', Biomass:'#8cbe5a', Imports:'#8b949e', Gas:'#f0883e', Coal:'#785a46', Other:'#6e7681'};
    const low = ['Wind','Solar','Nuclear','Hydro','Biomass'].reduce((s, k) => s + (g.mix[k] || 0), 0);
    grid = card('Electricity grid · North West England', `<div style="display:flex;gap:16px;align-items:center;flex-wrap:wrap"><div class="big">${g.now}</div><div><div class="muted">g CO₂/kWh</div><div class="mid">${g.index}</div><div class="small">${low.toFixed(0)}% low-carbon</div></div>
        ${g.greenest ? `<div class="card" style="background:#12361f;border-color:#1f6f3a"><div class="small muted">Greenest 3 hours (next day)</div><div class="mid" style="color:var(--green)">${hm(g.greenest.from)} – ${hm(g.greenest.to)}</div><div class="small">~${g.greenest.avg} g/kWh</div></div>` : ''}</div>
        <div style="margin-top:12px">${mix.map(([k, v]) => `<div style="display:grid;grid-template-columns:80px 1fr 50px;gap:8px;align-items:center;margin:4px 0"><span class="small">${k}</span>${bar(v, FUEL_COL[k])}<span class="small muted">${v}%</span></div>`).join('')}</div>
        <div class="chart" style="margin-top:12px"><canvas id="cGrid"></canvas></div>`, 'wide');
  }
  $('airgrid').innerHTML = `<div class="grid">${air}${uv}${grid}</div>`;
  if (g.forecast) chart('cGrid', {type:'bar', data:{labels:g.forecast.map(s => hm(s[0])), datasets:[{data:g.forecast.map(s => s[1]), backgroundColor:g.forecast.map(s => CI_COL[s[2]])}]},
    options:{scales:{x:{ticks:{maxTicksLimit:12}}, y:{beginAtZero:true, title:{display:true, text:'g/kWh'}}}}});
}

// ---------------------------------------------------------------- sky & alerts
function moonSvg(p) {
  const r = 60, c = Math.cos(2 * Math.PI * p), rx = Math.abs(c) * r, waxing = p < 0.5;
  const outer = waxing ? 1 : 0, inner = c > 0 ? (waxing ? 0 : 1) : (waxing ? 1 : 0);
  return `<svg viewBox="-70 -70 140 140" style="width:160px;height:160px"><circle r="${r}" fill="#2a2f38"/><path d="M0,${-r} A${r},${r} 0 0,${outer} 0,${r} A${rx},${r} 0 0,${inner} 0,${-r}Z" fill="#f2efe6"/><circle r="${r}" fill="none" stroke="#6e7681"/></svg>`;
}
function sunSvg(rise, set, nowT) {
  const toMin = s => { const [h, m] = (s || '0:0').split(':').map(Number); return h * 60 + m; };
  const d = new Date(nowT * 1000), now = d.getHours() * 60 + d.getMinutes(), a = toMin(rise), b = toMin(set);
  let sun = '';
  if (now >= a && now <= b) { const t = Math.PI * (1 - (now - a) / (b - a)); sun = `<circle cx="${Math.cos(t) * 100}" cy="${-Math.sin(t) * 100}" r="11" fill="#e3b341"/>`; }
  return `<svg viewBox="-115 -110 230 125" style="width:100%;max-width:340px"><path d="M-100,0 A100,100 0 0,1 100,0" fill="none" stroke="#30363d" stroke-dasharray="3 5" stroke-width="2"/><line x1="-110" x2="110" y1="0" y2="0" stroke="#6e7681"/>${sun}
    <text x="-100" y="14" fill="#8b949e" font-size="11" text-anchor="middle">${rise}</text><text x="100" y="14" fill="#8b949e" font-size="11" text-anchor="middle">${set}</text></svg>`;
}
function renderSkyAlerts() {
  const al = S.alerts, m = S.sky.moon, f = S.forecast, n = S.now;
  const warn = (al.warnings || []).length ? al.warnings.map(w => `<div class="card warn" style="border-left-color:${LEVEL_COL[w.level]};margin-bottom:8px"><div class="mid" style="color:${LEVEL_COL[w.level]}">⚠ ${w.level} warning · ${esc(w.hazard)}</div><div class="muted">${esc(w.validity)}</div></div>`).join('')
    : `<div class="mid" style="color:var(--green)">✓ No warnings in force</div>`;
  const au = al.aurora;
  const frostCol = {None:'var(--green)', Low:'#96c8ff', Moderate:'var(--cyan)', High:'var(--purple)'}[al.frost.level];
  $('skyalerts').innerHTML = `<div class="grid">
    ${card('Met Office warnings · ' + esc(al.area), warn, '', al.total > (al.warnings || []).length ? `+${al.total - al.warnings.length} more` : '')}
    ${card('Frost & ice', `<div class="mid" style="color:${frostCol}">❄ ${esc(al.frost.headline)}</div><div class="muted">${esc(al.frost.detail)}</div>`, '', 'risk: ' + al.frost.level)}
    ${au ? card('Aurora · AuroraWatch UK', `<div class="mid" style="color:${LEVEL_COL[au.level]}">${au.level} · ${au.nT} nT</div><div class="muted">${esc(au.meaning)}</div><div class="chart sm" style="margin-top:8px"><canvas id="cAur"></canvas></div>`) : ''}
    ${n.strikesToday !== undefined ? card('Lightning · Tempest', `<div class="mid">${n.strikesToday} today</div>${row('Last hour', n.strikesHour)}${n.lastStrikeTime ? row('Last strike', `${n.lastStrikeKm} km at ${hm(n.lastStrikeTime)}`) : ''}`) : ''}
    ${card('Sun', `${f.sunrise ? sunSvg(f.sunrise, f.sunset, S.system.now) : ''}${f.daylightS ? row('Daylight', `${Math.floor(f.daylightS / 3600)}h ${String(Math.round(f.daylightS % 3600 / 60)).padStart(2, '0')}m`) : ''}${f.sunshineS !== undefined ? row('Sunshine forecast', num(f.sunshineS / 3600) + ' h') : ''}${n.solar !== undefined ? row('Solar now', num(n.solar, 0) + ' W/m²') : ''}`)}
    ${card('Moon', `<div style="display:flex;gap:16px;align-items:center;flex-wrap:wrap">${moonSvg(m.phase)}<div><div class="mid">${m.name}</div><div class="muted">${m.illum}% lit · ${m.age} days</div>${row('Full moon', day(m.nextFull))}${row('New moon', day(m.nextNew))}</div></div>`)}
  </div>`;
  if (au) chart('cAur', {type:'bar', data:{labels:au.hours.map((_, i) => i - au.hours.length + 1 + 'h'), datasets:[{data:au.hours.map(h => h[0]), backgroundColor:au.hours.map(h => ({Green:'#3fb950', Yellow:'#e3b341', Amber:'#f0883e', Red:'#f85149'}[h[1]] || '#6e7681'))}]},
    options:{scales:{x:{display:false}, y:{beginAtZero:true, suggestedMax:120}}}});
}

// ---------------------------------------------------------------- home assistant
function renderHome() {
  const h = S.home;
  if (!h.configured || (!h.ok && h.status === 401)) {
    $('home').innerHTML = card('Home Assistant', `<p>${h.status === 401 ? 'Home Assistant rejected the saved token (401).' : 'Home Assistant is not connected yet.'}</p>
      <ol class="muted"><li>In Home Assistant open your profile → Security.</li><li>Create a <b>long-lived access token</b>.</li><li>Paste it on the <a href="/settings" style="color:var(--accent)">settings page</a> and save. It is stored write-only.</li></ol>`);
    return;
  }
  if (!h.ok) { $('home').innerHTML = '<p class="muted">Contacting Home Assistant…</p>'; return; }
  const list = (rows, dec, colFn) => rows.map(r => row(esc(r[0]), `${num(r[1], dec)} ${esc(r[2] === '%' ? '%' : r[2])}`, colFn ? colFn(r[1]) : '')).join('') || '<p class="muted">None</p>';
  const maxP = Math.max(1, ...(h.power || []).map(p => p[1]));
  $('home').innerHTML = `<div class="grid">
    ${card('People', `<div class="chips">${(h.people || []).map(p => `<span class="chip" style="${p[1] === 'home' ? 'background:#12361f;color:var(--green)' : ''}">${esc(p[0])}: ${esc(p[1] === 'not_home' ? 'away' : p[1])}</span>`).join('') || '<span class="muted">No people</span>'}</div>
      <div style="margin-top:10px">${row('Lights on', `${h.lightsOn} / ${h.lightsTotal}`, 'var(--yellow)')}${row('Open doors & windows', (h.open || []).length ? esc(h.open.join(', ')) : 'none', (h.open || []).length ? 'var(--orange)' : 'var(--green)')}</div>`)}
    ${card('Heating', (h.climate || []).map(c => `<div class="row"><span>${esc(c.name)}</span><span><b>${num(c.current)}°</b> <span class="muted">→ ${num(c.target)}°</span> <span style="color:${c.action === 'heating' ? 'var(--orange)' : 'var(--dim)'}">${esc(c.action)}</span></span></div>`).join('') || '<p class="muted">No climate entities</p>')}
    ${card('Temperatures', list(h.temps || [], 1, v => v < 12 ? 'var(--cyan)' : v > 24 ? 'var(--orange)' : ''))}
    ${card('Humidity', list(h.humidity || [], 0, v => v > 70 ? 'var(--accent)' : ''))}
    ${card('Power', (h.power || []).map(p => `<div style="display:grid;grid-template-columns:1fr 2fr 70px;gap:8px;align-items:center;margin:5px 0"><span class="small">${esc(p[0])}</span>${bar(p[1] / maxP * 100, 'var(--yellow)')}<span class="small" style="text-align:right">${num(p[1], 0)} ${esc(p[2])}</span></div>`).join('') || '<p class="muted">No power sensors</p>')}
  </div>`;
}

// ---------------------------------------------------------------- lab
function renderLab() {
  const l = S.lab;
  if (!l.nas && !l.speed && !l.services) { $('lab').innerHTML = '<p class="muted">Contacting home-lab services…</p>'; return; }
  const g = (name, pct, txt, warn = 75, crit = 90) => `<div style="margin:8px 0"><div style="display:flex;justify-content:space-between"><span class="muted">${name}</span><span>${txt}</span></div>${bar(pct, pct >= crit ? 'var(--red)' : pct >= warn ? 'var(--orange)' : 'var(--green)')}</div>`;
  const n = l.nas;
  const nas = n ? card('NAS · ' + esc(n.host), `${g('CPU', n.cpu, n.cpu + ' %')}${g('Memory', n.mem, n.mem + ' %', 85, 95)}${g('Disk', n.disk, `${n.disk}% of ${n.diskTb} TB`, 80, 90)}${n.temp !== null ? g('CPU temperature', n.temp, n.temp + ' °C', 70, 80) : ''}
      ${row('Load', n.load + ' % of ' + esc(n.cpuName))}${row('Containers', `${n.healthy} / ${n.containers} healthy`, n.healthy < n.containers ? 'var(--orange)' : 'var(--green)')}${n.unhealthy ? row('Unhealthy', esc(n.unhealthy), 'var(--orange)') : ''}`, '', 'up ' + esc(n.uptime)) : '';
  const sp = l.speed && l.speed.failed ? card('Internet', '<div class="mid" style="color:var(--orange)">Last speed test failed</div><div class="muted small">Speedtest Tracker reported a failed run</div>', '', l.speed.at ? 'at ' + hm(l.speed.at) : '')
    : l.speed ? card('Internet', `<div style="display:flex;gap:24px"><div><div class="big" style="color:var(--green);font-size:48px">${num(l.speed.down, 0)}</div><div class="muted small">Mbps down</div></div><div><div class="big" style="color:var(--cyan);font-size:48px">${num(l.speed.up, 0)}</div><div class="muted small">Mbps up</div></div><div><div class="big" style="color:var(--yellow);font-size:48px">${num(l.speed.ping, 0)}</div><div class="muted small">ms ping</div></div></div><div class="small dim" style="margin-top:8px">${esc(l.speed.server)}</div>`, '', l.speed.at ? 'tested ' + hm(l.speed.at) : '') : '';
  const sv = l.services ? card('Services · Uptime Kuma', `<div class="mid" style="color:${l.services.up < l.services.total ? 'var(--orange)' : 'var(--green)'}">${l.services.up} / ${l.services.total} up</div>${l.services.down ? `<div class="chips" style="margin-top:8px">${l.services.down.split(', ').map(s => `<span class="chip" style="background:#4a1d1d">${esc(s)}</span>`).join('')}</div>` : '<div class="muted">All healthy</div>'}`) : '';
  const so = (l.sonos || []).length ? card('Sonos', l.sonos.map(s => `<div class="row"><span>${s.state === 'PLAYING' ? '▶' : '■'} ${esc(s.room)} <span class="dim small">${esc(s.model)}</span></span><span class="${s.state === 'PLAYING' ? '' : 'muted'}">${s.state === 'PLAYING' && s.title ? esc(s.title) + (s.artist ? ' – ' + esc(s.artist) : '') : s.state === 'PAUSED_PLAYBACK' ? 'Paused' : 'Idle'}</span></div>`).join('')) : '';
  $('lab').innerHTML = `<div class="grid">${nas}${sp}${sv}${so}</div>`;
}

// ---------------------------------------------------------------- system
function renderSystem() {
  const s = S.system, t = s.tempest;
  const up = x => x >= 86400 ? `${Math.floor(x / 86400)}d ${Math.floor(x % 86400 / 3600)}h` : `${Math.floor(x / 3600)}h ${Math.floor(x % 3600 / 60)}m`;
  $('system').innerHTML = `<div class="grid">
    ${card('Device', `${row('Board', esc(s.board))}${row('Address', `<a style="color:var(--accent)" href="http://${s.host}.local/">${s.host}.local</a> · ${s.ip}`)}${row('Wi-Fi', `${esc(s.ssid)} · ${s.rssi} dBm`)}${row('Uptime', up(s.uptimeS))}${row('Free RAM / PSRAM', `${s.heapKb} KB / ${s.psramKb} KB`)}${row('History', `${s.historyRecords} records (${s.historyDays} days kept)`)}`)}
    ${t ? card('Tempest station', `${row('Hub', `${esc(t.hub)} · fw ${t.hubFw} · ${t.hubIp}`)}${row('Hub Wi-Fi', `${t.hubRssi} dBm · up ${up(t.hubUptimeS)}`)}${row('Sensor', `${esc(t.sensor)} · fw ${t.sensorFw}`)}${row('Sensor battery', `${t.battery} V`, t.battery > 2.39 ? 'var(--green)' : 'var(--orange)')}${row('Sensor radio', `${t.sensorRssi} dBm · up ${up(t.sensorUptimeS)}`)}${row('Status', t.status ? 'FAULT ' + t.status : 'OK', t.status ? 'var(--red)' : 'var(--green)')}${row('Packets', t.packets)}`) : ''}
    ${card('Data feeds', s.feeds.map(f => row(f.name, ago(f.agoS), f.ok ? '' : 'var(--orange)')).join(''))}
    ${card('API', ['/api/state.json', '/api/now.json', '/history.csv?days=7', '/api/settings'].map(u => `<div class="row"><a style="color:var(--accent)" href="${u}">${u}</a></div>`).join(''))}
  </div>`;
}

// ---------------------------------------------------------------- history
let histDays = 7;
const HIST = [
  {title:'Temperature (°C)', series:[['t_temp_c', 'Tempest', '#ffa657'], ['temp_c', 'Open-Meteo', '#8b949e', [4, 3]]]},
  {title:'Rain (mm per 15 min)', type:'bar', series:[['t_rain_mm', 'Tempest', '#58a6ff'], ['rain_mm', 'Open-Meteo', '#30363d']]},
  {title:'Peak rain rate (mm/h)', type:'bar', series:[['t_rain_rate_mmh', 'Tempest', '#79c0ff']]},
  {title:'Wind (mph)', series:[['t_wind_mph', 'Average', '#a5d6ff'], ['t_gust_mph', 'Gust', '#d2a8ff', [4, 3]], ['wind_mph', 'Open-Meteo', '#8b949e', [2, 3]]]},
  {title:'Pressure (hPa)', series:[['t_pressure_hpa', 'Tempest', '#7ee787'], ['pressure_hpa', 'Open-Meteo', '#8b949e', [4, 3]]]},
  {title:'Humidity (%)', series:[['t_humidity_pct', 'Tempest', '#56d4dd'], ['humidity_pct', 'Open-Meteo', '#8b949e', [4, 3]]]},
  {title:'UV & solar', series:[['t_uv', 'UV', '#e3b341'], ['t_solar_wm2', 'Solar W/m²', '#f0883e', [4, 3], 'y1']]},
  {title:'Air quality', series:[['eaqi', 'EAQI', '#e3b341'], ['pm25', 'PM2.5', '#ff7b72', null, 'y1']]},
  {title:'Grid carbon (g/kWh)', series:[['carbon_g', 'North West', '#7ee787']]},
  {title:'Aurora activity (nT)', type:'bar', series:[['aurora_nt', 'Activity', '#3fb950']]},
  {title:'Lightning strikes', type:'bar', series:[['t_strikes', 'Strikes', '#d2a8ff']]},
  {title:'Aircraft in range', series:[['aircraft', 'Aircraft', '#d2a8ff']]},
  {title:'Wi-Fi signal (dBm)', series:[['rssi_dbm', 'RSSI', '#58a6ff']]},
];
async function loadHistory() {
  $('histBtns').innerHTML = [1, 3, 7, 30, 90].map(d => `<button class="${d === histDays ? 'on' : ''}" onclick="histDays=${d};loadHistory()">${d === 1 ? '24 h' : d + ' days'}</button>`).join('')
    + `<a class="btn" href="/history.csv?days=${histDays}" download>Download CSV</a>`;
  let text;
  try { text = await (await fetch(`/history.csv?days=${histDays}`)).text(); } catch (e) { return; }
  const lines = text.trim().split('\n'), head = lines.shift().split(',');
  const cols = Object.fromEntries(head.map(h => [h, []]));
  for (const l of lines) { const v = l.split(','); head.forEach((h, i) => cols[h].push(v[i] === '' || v[i] === undefined ? null : h === 'local_time' ? v[i] : Number(v[i]))); }
  const box = $('histCharts');
  Object.keys(charts).filter(k => k.startsWith('h')).forEach(k => { charts[k].destroy(); delete charts[k]; });
  if (!cols.epoch || !cols.epoch.length) { box.innerHTML = '<p class="muted">No history yet.</p>'; return; }
  const labels = cols.epoch.map(e => { const d = new Date(e * 1000); const t = d.toLocaleTimeString([], {hour:'2-digit', minute:'2-digit', hour12:false}); return histDays <= 1 ? t : d.toLocaleDateString([], {weekday:'short', day:'numeric'}) + ' ' + t; });
  box.innerHTML = '';
  HIST.forEach((def, i) => {
    if (!def.series.some(([k]) => (cols[k] || []).some(v => v !== null))) return;
    const el = document.createElement('div');
    el.className = 'card';
    el.innerHTML = `<h3>${def.title}</h3><div class="chart" style="height:220px"><canvas id="h${i}"></canvas></div>`;
    box.appendChild(el);
    const hasY1 = def.series.some(s => s[4] === 'y1');
    chart('h' + i, {type:def.type || 'line', data:{labels, datasets:def.series.map(([k, name, col, dash, axis]) => ({label:name, data:cols[k] || [], borderColor:col, backgroundColor:col, borderDash:dash || [], yAxisID:axis || 'y', borderWidth:1.5, pointRadius:0, spanGaps:true}))},
      options:{plugins:{legend:{display:def.series.length > 1}}, scales:{x:{ticks:{maxTicksLimit:8, maxRotation:0}}, y:{beginAtZero:def.type === 'bar'}, ...(hasY1 ? {y1:{position:'right', grid:{drawOnChartArea:false}}} : {})}}});
  });
}

// ---------------------------------------------------------------- mirror
let mirrorOn = false, mirrorPages = [];
async function initMirror() {
  if (mirrorOn) return;
  mirrorOn = true;
  try { const d = await (await fetch('/api/now.json')).json(); mirrorPages = d.pages || []; if (d.screenW) document.documentElement.style.setProperty('--ar', `${d.screenW}/${d.screenH}`); } catch (e) {}
  $('screens').innerHTML = mirrorPages.map((p, i) => `<div class="card scr"><h3>${i + 1}. ${esc(p)}</h3><img id="scr${i}" alt="${esc(p)}"></div>`).join('');
  (async function loop() {
    for (let i = 0; ; i = (i + 1) % Math.max(1, mirrorPages.length)) {
      if (tab !== 'mirror') { await new Promise(r => setTimeout(r, 1000)); continue; }
      await new Promise(res => { const im = new Image(); im.onload = () => { const el = $('scr' + i); if (el) el.src = im.src; res(); }; im.onerror = () => setTimeout(res, 2000); im.src = `/screen.bmp?p=${i}&t=${Date.now()}`; });
    }
  })();
}

buildNav();
show(location.hash.slice(1) || 'now');
refresh();
setInterval(refresh, 20000);
</script>
</body>
</html>
)HTML";
