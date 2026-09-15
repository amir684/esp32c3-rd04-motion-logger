#pragma once
#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="he" dir="rtl">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>יומן חיישן תנועה</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--ink:#1d2330;--mute:#6b7280;--line:#e5e7eb;--acc:#2563eb;--bad:#dc2626}
@media (prefers-color-scheme:dark){:root{--bg:#111418;--card:#1b1f26;--ink:#e8eaed;--mute:#9aa0a6;--line:#2c323b;--acc:#60a5fa;--bad:#f87171}}
*{box-sizing:border-box}
body{margin:0;font-family:system-ui,-apple-system,"Segoe UI",Arial,sans-serif;background:var(--bg);color:var(--ink);padding:16px}
main{max-width:720px;margin:0 auto}
h1{font-size:1.3rem;margin:0 0 12px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin-bottom:12px}
.stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:10px}
.stat b{display:block;font-size:1.5rem;font-variant-numeric:tabular-nums}
.stat span{color:var(--mute);font-size:.85rem}
.row{display:flex;flex-wrap:wrap;gap:8px}
button,a.btn{font:inherit;border:1px solid var(--line);background:var(--card);color:var(--ink);padding:9px 14px;border-radius:9px;cursor:pointer;text-decoration:none}
button.pri{background:var(--acc);border-color:var(--acc);color:#fff}
button.bad{color:var(--bad)}
table{width:100%;border-collapse:collapse;font-variant-numeric:tabular-nums}
td,th{padding:7px 6px;border-bottom:1px solid var(--line);text-align:right}
th{color:var(--mute);font-weight:600;font-size:.85rem}
.day td{background:var(--bg);font-weight:600}
.mute{color:var(--mute);font-size:.85rem}
.warn{color:var(--bad)}
</style>
</head>
<body><main>
<h1>יומן חיישן תנועה RD-04</h1>

<div class="card stats">
  <div class="stat"><b id="cnt">–</b><span>אירועים שמורים</span></div>
  <div class="stat"><b id="today">–</b><span>היום</span></div>
  <div class="stat"><b id="last">–</b><span>אירוע אחרון</span></div>
</div>

<div class="card">
  <div class="mute" id="info">טוען…</div>
  <div class="row" style="margin-top:10px">
    <button class="pri" onclick="load()">רענון</button>
    <a class="btn" href="/api/events?limit=0&dl=1">הורדת CSV</a>
    <button class="bad" onclick="clr()">מחיקת היומן</button>
    <button onclick="slp()">סיום ושינה</button>
  </div>
</div>

<div class="card">
  <table><thead><tr><th>תאריך</th><th>שעה</th><th>#</th></tr></thead><tbody id="tb"></tbody></table>
  <div class="row" style="margin-top:10px"><button id="more" onclick="load(0)" hidden>הצג הכל</button></div>
</div>
</main>
<script>
const $=id=>document.getElementById(id);
const pad=n=>String(n).padStart(2,'0');
const dstr=d=>pad(d.getDate())+'/'+pad(d.getMonth()+1)+'/'+d.getFullYear();
const tstr=d=>pad(d.getHours())+':'+pad(d.getMinutes())+':'+pad(d.getSeconds());
let limit=500;

async function syncTime(){
  await fetch('/api/time?ms='+Date.now(),{method:'POST'});
}
async function status(){
  const s=await (await fetch('/api/status')).json();
  $('cnt').textContent=s.count;
  $('info').innerHTML='מקום ביומן: '+s.capacity+' אירועים (הישנים נמחקים כשמתמלא) · הרשת תיסגר אחרי '+Math.round(s.idleTimeout/60)+' דק׳ ללא פעילות';
  return s;
}
async function load(l){
  if(l!==undefined) limit=l;
  const s=await status();
  const txt=await (await fetch('/api/events?limit='+limit)).text();
  const rows=txt.trim().split('\n').slice(1).filter(Boolean).map(r=>{const [ts,v,seq,boot]=r.split(',');return {ts:+ts,v:v==='1',seq:+seq,boot:+boot}});
  const tb=$('tb'); tb.innerHTML='';
  const todayKey=dstr(new Date()); let today=0, prevDay=null, dayRow=null, dayCnt=0;
  const flush=()=>{ if(dayRow) dayRow.lastChild.textContent=dayCnt+' אירועים'; };
  for(const r of rows){
    let day, time;
    if(r.v){ const d=new Date(r.ts*1000); day=dstr(d); time=tstr(d); if(day===todayKey) today++; }
    else { day='שעה לא ידועה'; time='+'+r.ts+' שנ׳ מהפעלה '+r.boot; }
    if(day!==prevDay){ flush(); dayRow=document.createElement('tr'); dayRow.className='day';
      dayRow.innerHTML='<td colspan="2"></td><td></td>'; dayRow.firstChild.textContent=day; tb.appendChild(dayRow); prevDay=day; dayCnt=0; }
    dayCnt++;
    const tr=document.createElement('tr'); tr.innerHTML='<td></td><td></td><td class="mute"></td>';
    tr.children[1].textContent=time; tr.children[2].textContent=r.seq; tb.appendChild(tr);
  }
  flush();
  $('today').textContent=today;
  $('last').textContent=rows.length&&rows[0].v?tstr(new Date(rows[0].ts*1000)):'–';
  $('more').hidden=!(limit&&s.count>limit);
  if(!rows.length) tb.innerHTML='<tr><td colspan="3" class="mute">אין אירועים</td></tr>';
}
async function clr(){
  if(!confirm('למחוק את כל האירועים?')) return;
  await fetch('/api/clear',{method:'POST'}); load();
}
async function slp(){
  await fetch('/api/sleep',{method:'POST'});
  document.body.innerHTML='<main><h1>החיישן חזר לשינה</h1><p class="mute">אפשר לסגור את הדף.</p></main>';
}
syncTime().then(()=>load());
</script>
</body></html>)HTML";
