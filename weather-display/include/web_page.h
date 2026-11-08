/**
 * @file    web_page.h
 * @brief   Dashboard HTML/CSS/JS served at "/" (stored in flash).
 *
 * @details Single self-contained page. Chart.js is loaded from the jsDelivr
 *          CDN by the *browser*, so the board itself never fetches it.
 *          - Screens: every display page mirrored from /screen.bmp?p=N (any board size),
 *            refreshed round-robin; click one to enlarge it with fast refresh
 *            (shows the radar animation).
 *          - History: charts from /history.csv for 24 h / 3 d / 7 d, plus a
 *            CSV download link.
 */
#pragma once

static const char WEB_PAGE[] = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Weather Display</title>
<style>
  :root { --bg:#0d1117; --card:#161b22; --line:#30363d; --text:#e6edf3; --muted:#8b949e; --accent:#58a6ff; }
  * { box-sizing:border-box; }
  body { margin:0; background:var(--bg); color:var(--text); font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif; }
  header { padding:16px 20px; border-bottom:1px solid var(--line); display:flex; flex-wrap:wrap; gap:12px; align-items:center; }
  h1 { font-size:20px; margin:0 12px 0 0; }
  h2 { font-size:16px; margin:0 0 12px; color:var(--muted); font-weight:600; text-transform:uppercase; letter-spacing:.06em; }
  .pill { background:var(--card); border:1px solid var(--line); border-radius:999px; padding:4px 12px; font-size:13px; white-space:nowrap; }
  .pill b { font-weight:600; }
  main { padding:20px; max-width:1500px; margin:0 auto; }
  section { margin-bottom:32px; }
  .screens { display:grid; grid-template-columns:repeat(auto-fill,minmax(250px,1fr)); gap:14px; }
  .scr { background:var(--card); border:1px solid var(--line); border-radius:10px; padding:8px; cursor:zoom-in; }
  .scr img { width:100%; aspect-ratio:var(--ar,240/135); image-rendering:pixelated; display:block; border-radius:4px; background:#000; }
  .scr div { font-size:13px; color:var(--muted); margin:6px 2px 0; }
  .bar { display:flex; gap:8px; align-items:center; margin-bottom:12px; flex-wrap:wrap; }
  button, a.btn { background:var(--card); color:var(--text); border:1px solid var(--line); border-radius:6px; padding:5px 12px; font:inherit; font-size:13px; cursor:pointer; text-decoration:none; }
  button.on { border-color:var(--accent); color:var(--accent); }
  .charts { display:grid; grid-template-columns:repeat(auto-fill,minmax(420px,1fr)); gap:14px; }
  .chart { background:var(--card); border:1px solid var(--line); border-radius:10px; padding:10px 12px; height:240px; display:flex; flex-direction:column; }
  .chart h3 { margin:0 0 6px; font-size:13px; color:var(--muted); font-weight:600; }
  .chart .c { position:relative; flex:1; min-height:0; }
  #empty { color:var(--muted); display:none; }
  #modal { position:fixed; inset:0; background:rgba(0,0,0,.85); display:none; align-items:center; justify-content:center; cursor:zoom-out; z-index:10; flex-direction:column; gap:10px; }
  #modal img { width:min(96vw,1200px); aspect-ratio:var(--ar,240/135); image-rendering:pixelated; border-radius:6px; }
  #modal div { color:var(--muted); }
  footer { color:var(--muted); font-size:12px; padding:0 20px 24px; text-align:center; }
  @media (max-width:500px) { .charts { grid-template-columns:1fr; } main { padding:12px; } }
</style>
</head>
<body>
<header>
  <h1>Weather Display</h1>
  <span id="pills"></span>
</header>
<main>
  <section>
    <h2>Screens</h2>
    <div class="screens" id="screens"></div>
  </section>
  <section>
    <h2>History</h2>
    <div class="bar">
      <button data-days="1">24 h</button>
      <button data-days="3">3 days</button>
      <button data-days="7" class="on">7 days</button>
      <a class="btn" id="csv" href="/history.csv?days=7" download>Download CSV</a>
      <span id="count" style="color:var(--muted);font-size:13px"></span>
    </div>
    <p id="empty">No history logged yet. The first record is written at the next quarter hour.</p>
    <div class="charts" id="charts"></div>
  </section>
</main>
<div id="modal"><img id="big" alt=""><div id="bigName"></div></div>
<footer>Data: WeatherFlow Tempest (local) · Open-Meteo · Met Office · AuroraWatch UK (Lancaster University) · RainViewer · © OpenStreetMap contributors · adsb.lol (ODbL) · adsbdb</footer>

<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js"></script>
<script>
let pages = [], imgs = [], next = 0, focus = -1, days = 7, charts = [];

function pill(label, value) { return value === undefined ? '' : `<span class="pill">${label} <b>${value}</b></span> `; }

async function loadNow() {
  try {
    const d = await (await fetch('/api/now.json')).json();
    pages = d.pages || pages;
    if (d.screenW) document.documentElement.style.setProperty('--ar', `${d.screenW}/${d.screenH}`);
    if (d.board) document.querySelector('h1').textContent = 'Weather Display · ' + d.board;
    document.getElementById('pills').innerHTML =
      pill(d.source || '', d.temp !== undefined ? `${d.temp.toFixed(1)}°C ${d.condition || ''}` : undefined) +
      pill('Rain today', d.rainToday !== undefined ? d.rainToday.toFixed(1) + ' mm' : undefined) +
      pill('Wind', d.wind !== undefined ? `${Math.round(d.wind)} mph ${d.windDir}` : undefined) +
      pill('AQI', d.aqi !== undefined ? `${d.aqi} ${d.aqiBand}` : undefined) +
      pill('Warnings', d.warnings) + pill('Aurora', d.aurora) +
      pill('Aircraft', d.aircraft) + (d.battery !== undefined ? pill('Battery', d.battery + '%') : '') +
      pill('Wi-Fi', d.rssi + ' dBm') + pill('Up', Math.floor(d.uptimeMin / 60) + 'h' + (d.uptimeMin % 60) + 'm');
    document.getElementById('count').textContent = `${d.historyCount} records, every ${d.historyIntervalMin} min`;
    return d;
  } catch (e) { return null; }
}

function buildScreens() {
  const box = document.getElementById('screens');
  box.innerHTML = '';
  imgs = pages.map((name, i) => {
    const card = document.createElement('div');
    card.className = 'scr';
    card.innerHTML = `<img alt="${name}"><div>${i + 1}. ${name}</div>`;
    card.onclick = () => openModal(i);
    box.appendChild(card);
    return card.querySelector('img');
  });
}

function load(i, img) {
  return new Promise(res => {
    const t = new Image();
    t.onload = () => { img.src = t.src; res(true); };
    t.onerror = () => res(false);
    t.src = `/screen.bmp?p=${i}&t=${Date.now()}`;
  });
}

// Refresh one screen at a time so the board is never asked for two at once.
async function screenLoop() {
  for (;;) {
    if (focus >= 0) {
      await load(focus, document.getElementById('big'));
      await new Promise(r => setTimeout(r, 300));
    } else if (imgs.length) {
      const ok = await load(next, imgs[next]);
      next = (next + 1) % imgs.length;
      await new Promise(r => setTimeout(r, ok ? 350 : 3000));
    } else {
      await new Promise(r => setTimeout(r, 1000));
    }
  }
}

function openModal(i) {
  focus = i;
  document.getElementById('big').src = imgs[i].src;
  document.getElementById('bigName').textContent = pages[i] + ' — live (click to close)';
  document.getElementById('modal').style.display = 'flex';
}
document.getElementById('modal').onclick = () => { focus = -1; document.getElementById('modal').style.display = 'none'; };

// ---- history charts ------------------------------------------------------
const AURORA_COLORS = ['#888', '#3fb950', '#e3b341', '#f0883e', '#f85149'];
const CHARTS = [
  { title: 'Temperature (°C)', series: [['t_temp_c', 'Tempest', '#ffa657'], ['temp_c', 'Open-Meteo', '#8b949e', [4, 3]]] },
  { title: 'Rain (mm per 15 min)', type: 'bar', series: [['t_rain_mm', 'Tempest', '#58a6ff'], ['rain_mm', 'Open-Meteo', '#30363d']] },
  { title: 'Wind (mph)', series: [['t_wind_mph', 'Tempest avg', '#a5d6ff'], ['t_gust_mph', 'Tempest gust', '#d2a8ff', [4, 3]], ['wind_mph', 'Open-Meteo', '#8b949e', [2, 3]]] },
  { title: 'Pressure (hPa)', series: [['t_pressure_hpa', 'Tempest', '#7ee787'], ['pressure_hpa', 'Open-Meteo', '#8b949e', [4, 3]]] },
  { title: 'Humidity (%)', series: [['t_humidity_pct', 'Tempest', '#56d4dd'], ['humidity_pct', 'Open-Meteo', '#8b949e', [4, 3]]] },
  { title: 'UV & solar', series: [['t_uv', 'UV index', '#e3b341'], ['t_solar_wm2', 'Solar W/m²', '#f0883e', [4, 3], 'y1']] },
  { title: 'Lightning strikes', type: 'bar', series: [['t_strikes', 'Strikes', '#d2a8ff']] },
  { title: 'Air quality', series: [['eaqi', 'European AQI', '#e3b341'], ['pm25', 'PM2.5 µg/m³', '#ff7b72', null, 'y1']] },
  { title: 'Aurora activity (nT)', type: 'bar', series: [['aurora_nt', 'Activity', null]], aurora: true },
  { title: 'Aircraft within range', series: [['aircraft', 'Aircraft', '#d2a8ff']], step: true },
  { title: 'Battery (V)', series: [['t_battery_v', 'Tempest sensor', '#3fb950'], ['battery_v', 'Display', '#8b949e', [4, 3]]] },
  { title: 'Wi-Fi signal (dBm)', series: [['rssi_dbm', 'RSSI', '#58a6ff']] },
];

function parseCsv(text) {
  const lines = text.trim().split('\n');
  const head = lines.shift().split(',');
  const cols = Object.fromEntries(head.map(h => [h, []]));
  for (const l of lines) {
    const v = l.split(',');
    head.forEach((h, i) => cols[h].push(v[i] === '' || v[i] === undefined ? null : (h === 'local_time' ? v[i] : Number(v[i]))));
  }
  return cols;
}

function label(epoch) {
  const d = new Date(epoch * 1000);
  const hm = d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', hour12: false });
  return days <= 1 ? hm : d.toLocaleDateString([], { weekday: 'short' }) + ' ' + hm;
}

async function loadHistory() {
  document.getElementById('csv').href = `/history.csv?days=${days}`;
  let cols;
  try { cols = parseCsv(await (await fetch(`/history.csv?days=${days}`)).text()); } catch (e) { return; }
  const n = cols.epoch ? cols.epoch.length : 0;
  document.getElementById('empty').style.display = n ? 'none' : 'block';
  charts.forEach(c => c.destroy());
  charts = [];
  const box = document.getElementById('charts');
  box.innerHTML = '';
  if (!n || !window.Chart) return;
  const labels = cols.epoch.map(label);

  Chart.defaults.color = '#8b949e';
  Chart.defaults.borderColor = '#30363d';
  for (const def of CHARTS) {
    // Skip charts with no data at all (e.g. no Tempest, or no battery on this board).
    if (!def.series.some(([k]) => (cols[k] || []).some(v => v !== null))) continue;
    const card = document.createElement('div');
    card.className = 'chart';
    card.innerHTML = `<h3>${def.title}</h3><div class="c"><canvas></canvas></div>`;
    box.appendChild(card);
    const datasets = def.series.map(([key, name, color, dash, axis]) => ({
      label: name, data: cols[key] || [], yAxisID: axis || 'y',
      borderColor: color, backgroundColor: def.aurora ? (cols.aurora_level || []).map(l => AURORA_COLORS[l] || '#888') : color,
      borderDash: dash || [], borderWidth: 1.6, pointRadius: 0, tension: 0.25, spanGaps: true, stepped: !!def.step,
    }));
    const hasY1 = def.series.some(s => s[4] === 'y1');
    charts.push(new Chart(card.querySelector('canvas'), {
      type: def.type || 'line',
      data: { labels, datasets },
      options: {
        responsive: true, maintainAspectRatio: false, animation: false,
        interaction: { mode: 'index', intersect: false },
        plugins: { legend: { display: datasets.length > 1, labels: { boxWidth: 12 } } },
        scales: {
          x: { ticks: { maxTicksLimit: 8, maxRotation: 0 } },
          y: { beginAtZero: def.type === 'bar' },
          ...(hasY1 ? { y1: { position: 'right', grid: { drawOnChartArea: false } } } : {}),
        },
      },
    }));
  }
}

document.querySelectorAll('button[data-days]').forEach(b => b.onclick = () => {
  document.querySelectorAll('button[data-days]').forEach(x => x.classList.remove('on'));
  b.classList.add('on');
  days = Number(b.dataset.days);
  loadHistory();
});

(async () => {
  const d = await loadNow();
  if (!pages.length) pages = ['Now', 'Hourly', '5-Day', 'Radar', 'Air', 'Alerts', 'Sky', 'Flights', 'System'];
  buildScreens();
  screenLoop();
  loadHistory();
  setInterval(loadNow, 30000);
  setInterval(loadHistory, 15 * 60 * 1000);
})();
</script>
</body>
</html>
)HTML";
