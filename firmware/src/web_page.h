#pragma once
#include <pgmspace.h>

// Single-page phone UI served at "/". Pictures are resized/converted to a
// 240x320 JPEG in the browser before upload so any image format the phone can
// open (JPEG, PNG, HEIC on iPhone, WebP, ...) works and uploads stay small.
static const char WEB_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Picture Frame</title>
<style>
:root{--bg:#0f1318;--card:#1a2230;--text:#eef2f6;--dim:#93a0ae;--acc:#3b82f6;--danger:#ef4444}
*{box-sizing:border-box}body{margin:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--text)}
header{padding:14px 16px 8px;display:flex;align-items:center;justify-content:space-between}
header h1{font-size:18px;margin:0}#clock{color:var(--dim);font-variant-numeric:tabular-nums}
nav{display:flex;border-bottom:1px solid #263142}nav button{flex:1;background:none;border:0;color:var(--dim);padding:12px;font-size:15px}
nav button.on{color:var(--text);border-bottom:2px solid var(--acc)}
section{display:none;padding:14px}section.on{display:block}
.card{background:var(--card);border-radius:12px;padding:14px;margin-bottom:14px}
.card h2{font-size:15px;margin:0 0 10px;color:var(--dim);font-weight:600}
button.btn,label.btn{display:inline-block;background:var(--acc);color:#fff;border:0;border-radius:10px;padding:11px 16px;font-size:15px;cursor:pointer}
button.ghost{background:#263142}button.danger{background:var(--danger)}button.sm{padding:7px 10px;font-size:13px;border-radius:8px}
input[type=file]{display:none}
.row{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin:8px 0}
.row label{color:var(--dim);font-size:14px}
select,input[type=range]{background:#263142;color:var(--text);border:0;border-radius:8px;padding:8px;font-size:15px}
input[type=range]{flex:1;padding:0}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(96px,1fr));gap:10px}
.thumb{background:#000;border-radius:10px;overflow:hidden;position:relative;aspect-ratio:3/4}
.thumb img{width:100%;height:100%;object-fit:cover;display:block}
.thumb.cur{outline:3px solid var(--acc)}
.thumb .acts{position:absolute;left:0;right:0;bottom:0;display:flex;justify-content:space-between;padding:4px;background:linear-gradient(transparent,rgba(0,0,0,.75))}
.thumb .acts button{background:rgba(255,255,255,.15);color:#fff;border:0;border-radius:6px;padding:5px 8px;font-size:12px}
ul.list{list-style:none;margin:0;padding:0}ul.list li{display:flex;align-items:center;gap:10px;padding:10px 0;border-bottom:1px solid #263142}
ul.list li span{flex:1;word-break:break-all}ul.list li.cur span{color:var(--acc)}
.prog{height:8px;background:#263142;border-radius:4px;overflow:hidden;margin-top:8px;display:none}.prog i{display:block;height:100%;background:var(--acc);width:0}
.muted{color:var(--dim);font-size:13px}.toast{position:fixed;left:50%;bottom:24px;transform:translateX(-50%);background:#263142;padding:10px 16px;border-radius:10px;opacity:0;transition:.3s;pointer-events:none}
.toast.on{opacity:1}.ctl{display:flex;gap:8px;justify-content:center}.ctl button{flex:1;font-size:20px;padding:10px}
.sw{display:flex;justify-content:space-between;align-items:center;padding:8px 0}
</style></head><body>
<header><h1>Picture Frame</h1><div id="clock">--:--</div></header>
<nav><button class="on" data-t="pics">Pictures</button><button data-t="music">Music</button><button data-t="set">Settings</button></nav>

<section id="pics" class="on">
 <div class="card"><h2>Add pictures</h2>
  <div class="row"><label class="btn">Choose pictures<input id="picFiles" type="file" accept="image/*" multiple></label>
   <select id="fitMode"><option value="fit">Fit (whole picture, bars)</option><option value="fill">Fill screen (crop)</option></select></div>
  <div class="muted">Pictures are resized to the frame's 240 x 320 screen on your phone before upload.</div>
  <div class="prog" id="picProg"><i></i></div><div class="muted" id="picStatus"></div>
 </div>
 <div class="card"><h2>On the frame <span id="picCount"></span></h2>
  <div class="row"><button class="btn ghost sm" onclick="ss('prev')">&#9664; Prev</button><button class="btn ghost sm" onclick="ss('next')">Next &#9654;</button><button class="btn ghost sm" onclick="loadPics()">Refresh</button></div>
  <div class="grid" id="picGrid"></div></div>
</section>

<section id="music">
 <div class="card"><h2>Now playing</h2>
  <div id="nowPlaying" class="muted">Nothing playing</div>
  <div class="ctl" style="margin-top:10px"><button class="btn ghost" onclick="mu('prev')">&#9198;</button><button class="btn" id="ppBtn" onclick="mu('pause')">&#9654;</button><button class="btn ghost" onclick="mu('stop')">&#9632;</button><button class="btn ghost" onclick="mu('next')">&#9197;</button></div>
  <div class="row"><label>Volume</label><input type="range" id="vol" min="0" max="21" onchange="mu('vol',{value:this.value})"></div>
 </div>
 <div class="card"><h2>Add music</h2>
  <label class="btn">Choose MP3 files<input id="musFiles" type="file" accept="audio/*,.mp3,.wav,.flac,.aac,.m4a" multiple></label>
  <div class="prog" id="musProg"><i></i></div><div class="muted" id="musStatus"></div></div>
 <div class="card"><h2>Songs <span id="musCount"></span></h2><ul class="list" id="musList"></ul></div>
</section>

<section id="set">
 <div class="card"><h2>Slideshow</h2>
  <div class="row"><label>Change picture every</label><select id="interval">
   <option value="5">5 s</option><option value="10">10 s</option><option value="15">15 s</option><option value="30">30 s</option><option value="60">1 min</option><option value="120">2 min</option><option value="300">5 min</option><option value="600">10 min</option><option value="1800">30 min</option><option value="3600">1 hour</option></select></div>
  <div class="sw"><span>Shuffle</span><input type="checkbox" id="shuffle"></div>
  <div class="sw"><span>Show clock over pictures</span><input type="checkbox" id="showClock"></div>
 </div>
 <div class="card"><h2>Clock</h2>
  <div class="row"><label>Time zone</label><select id="tz"></select></div>
  <div class="sw"><span>24-hour clock</span><input type="checkbox" id="clock24"></div>
  <div class="row"><button class="btn ghost sm" onclick="syncTime()">Set frame time from this phone</button></div>
  <div class="muted">The frame syncs time from the internet automatically when it is online.</div>
 </div>
 <div class="card"><h2>Music</h2><div class="sw"><span>Repeat all</span><input type="checkbox" id="repeat"></div></div>
 <div class="card"><h2>Device</h2><div class="muted" id="info"></div></div>
</section>
<div class="toast" id="toast"></div>
<script>
const $=s=>document.querySelector(s);const api=(p,o)=>fetch(p,o).then(r=>r.json());
let status={};
function toast(m){const t=$('#toast');t.textContent=m;t.classList.add('on');clearTimeout(t._h);t._h=setTimeout(()=>t.classList.remove('on'),2500)}
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{document.querySelectorAll('nav button,section').forEach(e=>e.classList.remove('on'));b.classList.add('on');$('#'+b.dataset.t).classList.add('on')});
const q=o=>Object.entries(o).map(([k,v])=>k+'='+encodeURIComponent(v)).join('&');
const post=(p,o={})=>api(p+'?'+q(o),{method:'POST'});

async function refreshStatus(){try{status=await api('/api/status');
 $('#clock').textContent=status.time;$('#nowPlaying').textContent=status.nowPlaying||'Nothing playing';
 $('#ppBtn').innerHTML=(status.playing&&!status.paused)?'&#10074;&#10074;':'&#9654;';
 if(document.activeElement!==$('#vol'))$('#vol').value=status.volume;
 $('#interval').value=status.interval;$('#shuffle').checked=status.shuffle;$('#showClock').checked=status.showClock;$('#clock24').checked=status.clock24;$('#repeat').checked=status.repeat;$('#tz').value=status.tz;
 $('#info').innerHTML=`Firmware ${status.version}<br>Address: http://${status.ip} &middot; http://${status.host}.local<br>Wi-Fi: ${status.ssid} (${status.rssi} dBm)<br>SD card: ${status.sdUsed} / ${status.sdTotal} MB used<br>Free RAM: ${status.heap} KB, PSRAM: ${status.psram} KB<br>Uptime: ${Math.floor(status.uptime/60)} min`;
 document.querySelectorAll('#picGrid .thumb').forEach(t=>t.classList.toggle('cur',t.dataset.n===status.currentPicture));
 document.querySelectorAll('#musList li').forEach(t=>t.classList.toggle('cur',t.dataset.n===status.nowPlaying));
}catch(e){}}

async function loadPics(){const l=await api('/api/list?dir=picture');$('#picCount').textContent='('+l.length+')';
 $('#picGrid').innerHTML=l.map(f=>`<div class="thumb" data-n="${f.name}"><img loading="lazy" src="/file?dir=picture&name=${encodeURIComponent(f.name)}"><div class="acts"><button onclick="showPic('${f.name}')">Show</button><button onclick="del('picture','${f.name}')">&#128465;</button></div></div>`).join('');refreshStatus()}
async function loadMusic(){const l=await api('/api/list?dir=music');$('#musCount').textContent='('+l.length+')';
 $('#musList').innerHTML=l.map(f=>`<li data-n="${f.name}"><span>${f.name}</span><button class="btn sm" onclick="mu('play',{name:'${f.name}'})">&#9654;</button><button class="btn ghost sm" onclick="del('music','${f.name}')">&#128465;</button></li>`).join('')||'<li class="muted">No music yet</li>';refreshStatus()}
async function del(dir,name){if(!confirm('Delete '+name+'?'))return;await post('/api/delete',{dir,name});dir==='picture'?loadPics():loadMusic()}
async function showPic(name){await post('/api/show',{name});toast('Showing '+name);refreshStatus()}
async function ss(cmd){await post('/api/slideshow',{cmd});setTimeout(refreshStatus,400)}
async function mu(cmd,extra={}){const r=await post('/api/music',Object.assign({cmd},extra));if(r.error)toast(r.error);setTimeout(refreshStatus,300)}
async function syncTime(){await post('/api/time',{epoch:Math.floor(Date.now()/1000)});toast('Time set');refreshStatus()}

async function loadImage(file){
 try{return await createImageBitmap(file,{imageOrientation:'from-image'})}catch(e){}
 return new Promise((res,rej)=>{const i=new Image();i.onload=()=>res(i);i.onerror=rej;i.src=URL.createObjectURL(file)})}
async function toFrameJpeg(file,mode){const img=await loadImage(file);const W=240,H=320;const iw=img.width||img.naturalWidth,ih=img.height||img.naturalHeight;
 const c=document.createElement('canvas');c.width=W;c.height=H;const g=c.getContext('2d');g.fillStyle='#000';g.fillRect(0,0,W,H);
 const s=mode==='fill'?Math.max(W/iw,H/ih):Math.min(W/iw,H/ih);const w=Math.round(iw*s),h=Math.round(ih*s);
 g.imageSmoothingEnabled=true;g.imageSmoothingQuality='high';g.drawImage(img,(W-w)/2,(H-h)/2,w,h);
 return new Promise(r=>c.toBlob(r,'image/jpeg',0.9))}

function upload(dir,name,blob,onProg){return new Promise((res,rej)=>{const x=new XMLHttpRequest();x.open('POST','/upload?dir='+dir+'&name='+encodeURIComponent(name));
 x.upload.onprogress=e=>onProg&&onProg(e.loaded/e.total);x.onload=()=>x.status===200?res(JSON.parse(x.responseText)):rej(new Error(x.responseText||x.status));x.onerror=()=>rej(new Error('network'));
 const fd=new FormData();fd.append('file',blob,name);x.send(fd)})}

async function batch(input,dir,prep,prog,stat,done){const files=[...input.files];if(!files.length)return;input.value='';
 const bar=$(prog),st=$(stat);bar.style.display='block';let ok=0;
 for(let i=0;i<files.length;i++){const f=files[i];st.textContent=`Preparing ${i+1}/${files.length}: ${f.name}`;
  try{const {name,blob}=await prep(f);st.textContent=`Uploading ${i+1}/${files.length}: ${name}`;
   await upload(dir,name,blob,p=>bar.firstChild.style.width=((i+p)/files.length*100)+'%');ok++}
  catch(e){toast('Failed: '+f.name+' ('+e.message+')')}}
 bar.style.display='none';bar.firstChild.style.width='0';st.textContent=`${ok} of ${files.length} uploaded`;done()}

$('#picFiles').onchange=e=>batch(e.target,'picture',async f=>({name:f.name.replace(/\.[^.]+$/,'')+'.jpg',blob:await toFrameJpeg(f,$('#fitMode').value)}),'#picProg','#picStatus',loadPics);
$('#musFiles').onchange=e=>batch(e.target,'music',async f=>({name:f.name,blob:f}),'#musProg','#musStatus',loadMusic);

for(const id of['interval','shuffle','showClock','clock24','tz','repeat']){$('#'+id).onchange=async()=>{await post('/api/settings',{interval:$('#interval').value,shuffle:+$('#shuffle').checked,showClock:+$('#showClock').checked,clock24:+$('#clock24').checked,tz:$('#tz').value,repeat:+$('#repeat').checked});toast('Saved');refreshStatus()}}

(async()=>{const tz=await api('/api/timezones');$('#tz').innerHTML=tz.map((t,i)=>`<option value="${i}">${t}</option>`).join('');await refreshStatus();loadPics();loadMusic();setInterval(refreshStatus,5000)})();
</script></body></html>)HTML";
