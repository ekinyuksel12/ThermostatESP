#ifndef DASHBOARD_H
#define DASHBOARD_H

// Local web dashboard (served at "/"). Self-contained: no external fonts or scripts, so it
// also works when the internet is down. Polls /api/status every 5 s.

const char dashboard_html[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="theme-color" content="#0b1022">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<title>Thermostat</title>
<style>
:root{
  --bg:#0b1022; --tx:#fff; --tx2:rgba(255,255,255,.7); --tx3:rgba(255,255,255,.48);
  --g1:rgba(255,255,255,.15); --g2:rgba(255,255,255,.05); --gb:rgba(255,255,255,.2);
  --ghi:rgba(255,255,255,.38); --gsh:rgba(0,0,0,.45); --track:rgba(255,255,255,.12);
  --heat:#ff8a3d; --eco:#34c759; --off:#8e8e93; --alert:#ff453a; --cool:#64d2ff;
  --b1:#3d7bff; --b2:#7a5cff; --b3:#26c6da; --blobop:.55;
  --r:28px;
  font-family:-apple-system,BlinkMacSystemFont,"SF Pro Display","SF Pro Text","Segoe UI",Roboto,"Helvetica Neue",Arial,sans-serif;
  -webkit-font-smoothing:antialiased; color-scheme:dark;
}
@media (prefers-color-scheme:light){:root{
  --bg:#eef2fb; --tx:#0b1022; --tx2:rgba(11,16,34,.68); --tx3:rgba(11,16,34,.46);
  --g1:rgba(255,255,255,.66); --g2:rgba(255,255,255,.34); --gb:rgba(255,255,255,.8);
  --ghi:rgba(255,255,255,.95); --gsh:rgba(40,52,110,.18); --track:rgba(11,16,34,.09);
  --blobop:.42; color-scheme:light;
}}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{margin:0;min-height:100%}
body{background:var(--bg);color:var(--tx);overflow-x:hidden;
  padding:calc(env(safe-area-inset-top) + 12px) 16px calc(env(safe-area-inset-bottom) + 112px)}
button,input{font:inherit;color:inherit}
button{cursor:pointer;border:0;background:none;padding:0}
:focus-visible{outline:2px solid var(--cool);outline-offset:3px;border-radius:12px}

/* ---------- living background ---------- */
.bg{position:fixed;inset:-20vmax;z-index:-1;overflow:hidden;pointer-events:none}
.blob{position:absolute;width:70vmax;height:70vmax;border-radius:50%;filter:blur(70px);opacity:var(--blobop);
  transition:background-color 1.6s ease;animation:drift 26s ease-in-out infinite alternate}
.b1{background:var(--b1);top:0;left:-5vmax}
.b2{background:var(--b2);top:30vmax;right:-10vmax;animation-duration:32s}
.b3{background:var(--b3);bottom:-10vmax;left:20vmax;animation-duration:38s}
@keyframes drift{to{transform:translate(8vmax,-6vmax) scale(1.12)}}
body.s-heating{--b1:#ff7a3d;--b2:#ff3d77;--b3:#ffb340}
body.s-eco{--b1:#2fbf71;--b2:#1fa2a6;--b3:#8fd14f}
body.s-off{--b1:#6b7280;--b2:#4b5563;--b3:#94a3b8}
body.s-alert{--b1:#ff453a;--b2:#ff7a3d;--b3:#7a5cff}
@media (prefers-reduced-motion:reduce){.blob{animation:none}*{transition:none!important}}

/* ---------- liquid glass ---------- */
.glass{position:relative;isolation:isolate;background:linear-gradient(160deg,var(--g1),var(--g2));
  -webkit-backdrop-filter:blur(30px) saturate(170%);backdrop-filter:blur(30px) saturate(170%);
  border:1px solid var(--gb);border-radius:var(--r);
  box-shadow:inset 0 1px 0 var(--ghi),inset 0 -1px 0 rgba(255,255,255,.06),0 20px 50px -18px var(--gsh)}
.glass::before{content:"";position:absolute;inset:0;border-radius:inherit;pointer-events:none;z-index:-1;
  background:radial-gradient(130% 70% at 10% -10%,rgba(255,255,255,.22),transparent 55%),
             radial-gradient(80% 50% at 110% 120%,rgba(255,255,255,.08),transparent 60%)}
.card{padding:18px}
.card + .card,.tiles + .card,.card + .tiles,.hero + .card{margin-top:14px}

/* ---------- header ---------- */
.top{display:flex;align-items:center;justify-content:space-between;gap:12px;margin:4px 2px 16px}
.top h1{margin:0;font-size:28px;font-weight:700;letter-spacing:-.02em}
.clock{font-size:13px;color:var(--tx3);margin-top:2px;font-variant-numeric:tabular-nums}
.top-r{display:flex;align-items:center;gap:8px}
.pill{display:inline-flex;align-items:center;gap:7px;padding:7px 12px;border-radius:999px;font-size:13px;font-weight:600;white-space:nowrap}
.dot{width:8px;height:8px;border-radius:50%;background:var(--tx3)}
.s-heating .dot{background:var(--heat);box-shadow:0 0 0 0 rgba(255,138,61,.7);animation:pulse 2s infinite}
.s-eco .dot{background:var(--eco)} .s-alert .dot{background:var(--alert)} .s-idle .dot{background:var(--cool)}
.s-offline .dot{background:var(--alert);animation:none}
@keyframes pulse{70%{box-shadow:0 0 0 7px rgba(255,138,61,0)}100%{box-shadow:0 0 0 0 rgba(255,138,61,0)}}
.lang{display:flex;padding:3px;border-radius:999px}
.lang button{padding:5px 9px;border-radius:999px;font-size:12px;font-weight:700;color:var(--tx3)}
.lang button.on{background:var(--g1);color:var(--tx);box-shadow:inset 0 1px 0 var(--ghi)}

/* ---------- hero / dial ---------- */
.hero{padding:22px 18px 20px;text-align:center}
.dial-wrap{position:relative;width:min(76vw,290px);margin:0 auto}
.dial{display:block;width:100%;height:auto;overflow:visible}
.dial .trk,.dial .prg,.dial .go{fill:none;stroke-width:14;stroke-linecap:round}
.dial .trk{stroke:var(--track)}
.dial .prg,.dial .go{transition:stroke-dasharray .8s cubic-bezier(.2,.8,.2,1)}
/* faded "still to go" segment between the room temperature and the target dot */
.dial .go{opacity:.32}
.s-heating .dial .go{animation:gopulse 2.4s ease-in-out infinite alternate}
@keyframes gopulse{from{opacity:.2}to{opacity:.5}}
.dial .tgt{fill:#fff;filter:drop-shadow(0 2px 6px rgba(0,0,0,.45));transition:cx .6s,cy .6s,opacity .3s}
.s-off .dial .tgt{opacity:.45}
@media (prefers-reduced-motion:reduce){.dial .go{animation:none}}
.dial-c{position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center;padding-top:6px}
.dial-lbl{font-size:13px;color:var(--tx2);font-weight:500}
.dial-big{font-size:clamp(64px,19vw,84px);font-weight:200;letter-spacing:-.05em;line-height:1;font-variant-numeric:tabular-nums;margin:4px 0 2px}
.dial-big .deg{font-size:.45em;vertical-align:top;margin-left:2px;font-weight:300}
.dial-sub{font-size:13px;color:var(--tx3);min-height:18px}
.target{position:relative;display:flex;align-items:center;justify-content:center;gap:22px;margin-top:calc(min(76vw,290px) * -0.14)}
.rbtn{width:58px;height:58px;border-radius:50%;font-size:30px;font-weight:300;line-height:1;display:grid;place-items:center;transition:transform .15s,opacity .2s}
.rbtn:active{transform:scale(.9)}
.rbtn:disabled{opacity:.35;cursor:default}
.target-mid{min-width:110px}
.target-lbl{font-size:12px;text-transform:uppercase;letter-spacing:.08em;color:var(--tx3);font-weight:600}
.target-val{font-size:34px;font-weight:500;letter-spacing:-.02em;font-variant-numeric:tabular-nums}
.target-eco{font-size:12px;color:var(--tx2);min-height:16px}
.ttt{margin:14px 6px 0;font-size:15px;line-height:1.4;color:var(--tx2)}

/* ---------- segmented mode control ---------- */
.seg{position:relative;display:grid;grid-template-columns:repeat(3,1fr);padding:4px;border-radius:20px;background:rgba(127,127,127,.14)}
.seg-thumb{position:absolute;top:4px;bottom:4px;left:4px;width:calc((100% - 8px)/3);border-radius:16px;
  background:linear-gradient(160deg,var(--g1),var(--g2));box-shadow:inset 0 1px 0 var(--ghi),0 6px 18px -6px var(--gsh);
  border:1px solid var(--gb);transition:transform .35s cubic-bezier(.3,1.3,.5,1)}
.seg button{position:relative;z-index:1;display:flex;flex-direction:column;align-items:center;gap:4px;padding:10px 4px;font-size:14px;font-weight:600;color:var(--tx2);border-radius:16px}
.seg button svg{width:22px;height:22px}
.seg button.on{color:var(--tx)}
.seg button[data-mode=HEAT].on svg{color:var(--heat)} .seg button[data-mode=ECO].on svg{color:var(--eco)} .seg button[data-mode=OFF].on svg{color:var(--off)}
.mode-desc{margin:12px 4px 0;font-size:14px;line-height:1.45;color:var(--tx2)}

/* ---------- tiles ---------- */
.tiles{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.tile{padding:15px 16px;border-radius:24px}
.tile .lbl{display:flex;align-items:center;gap:6px;font-size:12px;font-weight:600;color:var(--tx2)}
.tile .lbl svg{width:15px;height:15px;opacity:.8}
.tile .val{font-size:28px;font-weight:400;letter-spacing:-.02em;margin:6px 0 2px;font-variant-numeric:tabular-nums}
.tile .val small{font-size:15px;color:var(--tx3);margin-left:2px}
.tile .cap{font-size:12px;color:var(--tx3);line-height:1.35}

/* ---------- card internals ---------- */
.card-h{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-bottom:10px}
.card-h h2{margin:0;font-size:17px;font-weight:650;letter-spacing:-.01em}
.ibtn{width:30px;height:30px;border-radius:50%;display:grid;place-items:center;color:var(--tx2);background:rgba(127,127,127,.14);flex:none}
.ibtn svg{width:18px;height:18px}
.ibtn[aria-expanded=true]{background:var(--cool);color:#00121d}
.info{margin:0 0 12px;padding:12px 14px;border-radius:16px;background:rgba(127,127,127,.12);font-size:14px;line-height:1.5;color:var(--tx2)}
.reason{margin:0 0 14px;font-size:16px;line-height:1.45}
.meter .top-row{display:flex;justify-content:space-between;font-size:13px;color:var(--tx2);margin-bottom:6px}
.bar{height:10px;border-radius:99px;background:var(--track);overflow:hidden}
.bar i{display:block;height:100%;width:0;border-radius:inherit;background:linear-gradient(90deg,#ffb340,var(--heat),#ff3d77);transition:width .8s cubic-bezier(.2,.8,.2,1)}
.bar.cool i{background:linear-gradient(90deg,var(--cool),#7a5cff)}
.kv{display:flex;justify-content:space-between;align-items:baseline;gap:12px;padding:11px 0;border-top:1px solid rgba(127,127,127,.18);font-size:14px}
.kv:first-child{border-top:0}
.kv span{color:var(--tx2)} .kv b{font-weight:600;text-align:right;font-variant-numeric:tabular-nums;word-break:break-word}
.meter + .kv{margin-top:12px}
.stats{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin:4px 0 16px}
.stats.two{grid-template-columns:1fr 1fr;gap:14px}
.stat span{display:block;font-size:12px;color:var(--tx3);font-weight:600}
.stat b{display:block;font-size:24px;font-weight:450;letter-spacing:-.02em;margin-top:2px;font-variant-numeric:tabular-nums}
.stat b small{font-size:13px;color:var(--tx3);font-weight:500;margin-left:2px}
.stat em{display:block;font-style:normal;font-size:12px;color:var(--tx3);margin-top:2px;line-height:1.3}
.chart{display:grid;grid-template-columns:repeat(7,1fr);gap:8px;align-items:end;height:150px;padding-top:18px}
.col{display:flex;flex-direction:column;align-items:center;justify-content:flex-end;height:100%;gap:6px;min-width:0}
.col .v{font-size:10px;color:var(--tx3);font-variant-numeric:tabular-nums;white-space:nowrap}
.col .b{width:100%;max-width:30px;border-radius:9px 9px 5px 5px;background:linear-gradient(180deg,rgba(255,255,255,.55),rgba(255,255,255,.18));min-height:4px;transition:height .8s cubic-bezier(.2,.8,.2,1)}
.col.today .b{background:linear-gradient(180deg,#ffb340,var(--heat))}
.col .d{font-size:11px;color:var(--tx3)}
.col.today .d{color:var(--tx);font-weight:700}
@media (prefers-color-scheme:light){.col .b{background:linear-gradient(180deg,rgba(11,16,34,.32),rgba(11,16,34,.12))}}
.foot{margin:12px 0 0;font-size:12px;color:var(--tx3)}
.steps{margin:0 0 14px;padding-left:20px;font-size:14px;line-height:1.5;color:var(--tx2)}
.steps li{margin:4px 0}
.status-line{margin:0 0 12px;font-size:14px;font-weight:600}
.field{display:flex;gap:8px}
.field input{flex:1;min-width:0;padding:13px 14px;border-radius:16px;border:1px solid var(--gb);background:rgba(127,127,127,.12);font-size:16px;font-variant-numeric:tabular-nums}
.field input::placeholder{color:var(--tx3)}
.btn{padding:13px 16px;border-radius:16px;font-weight:650;background:var(--cool);color:#00121d;white-space:nowrap;transition:transform .15s}
.btn:active{transform:scale(.96)}
.btn.ghost{background:rgba(127,127,127,.16);color:var(--tx)}
.linkbtn{margin-top:12px;font-size:14px;color:var(--alert);font-weight:600}
.msg{min-height:20px;margin:10px 0 0;font-size:14px;color:var(--tx2)}
.prog{margin-top:6px}
.prog .bar i{background:linear-gradient(90deg,var(--cool),#7a5cff)}
.prog p{margin:6px 0 0;font-size:12px;color:var(--tx3)}
.ok{color:var(--eco)} .bad{color:var(--alert)} .warn{color:#ffd60a}
@media (prefers-color-scheme:light){.ok{color:#1a7f3c} .bad{color:#c4000f} .warn{color:#9a5a00}}
a.btn{display:inline-block;text-decoration:none;margin-top:4px}

/* ---------- banners ---------- */
.banner{padding:16px 16px 14px;margin-bottom:14px;border-radius:24px;
  background:linear-gradient(160deg,rgba(255,69,58,.42),rgba(255,69,58,.16));border-color:rgba(255,120,110,.45)}
.banner h3{display:flex;align-items:center;gap:8px;margin:0 0 6px;font-size:16px}
.banner h3 svg{width:20px;height:20px;flex:none}
.banner p{margin:0;font-size:14px;line-height:1.45;color:var(--tx)}
.banner .btn{margin-top:12px;background:rgba(255,255,255,.9);color:#3a0500;padding:9px 14px;font-size:14px}

/* ---------- tabs ---------- */
.tab{display:none;animation:fade .35s ease}
.tab.on{display:block}
@keyframes fade{from{opacity:0;transform:translateY(6px)}}
.tabbar{position:fixed;left:50%;bottom:calc(env(safe-area-inset-bottom) + 12px);transform:translateX(-50%);
  display:grid;grid-template-columns:repeat(4,1fr);width:min(calc(100% - 28px),440px);padding:6px;border-radius:999px;z-index:10}
.tabbar button{display:flex;flex-direction:column;align-items:center;gap:2px;padding:7px 2px;border-radius:999px;font-size:11px;font-weight:600;color:var(--tx3);transition:background .25s,color .25s}
.tabbar svg{width:22px;height:22px}
.tabbar button.on{color:var(--tx);background:rgba(127,127,127,.2);box-shadow:inset 0 1px 0 var(--ghi)}
.toast{position:fixed;left:50%;bottom:calc(env(safe-area-inset-bottom) + 92px);transform:translate(-50%,20px);opacity:0;
  padding:10px 16px;border-radius:999px;font-size:14px;font-weight:600;pointer-events:none;transition:.3s;z-index:11;white-space:nowrap}
.toast.show{opacity:1;transform:translate(-50%,0)}
.ver{margin:18px 0 0;text-align:center;font-size:12px;color:var(--tx3)}
.s-offline main{opacity:.6}

/* ---------- larger screens ---------- */
@media (min-width:820px){
  body{padding-left:24px;padding-right:24px}
  .top,main{max-width:1000px;margin-left:auto;margin-right:auto}
  .tab.on{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:16px;align-items:start}
  .tab.on > *{margin-top:0!important}
  .span2{grid-column:1/-1}
  #tab-home .hero{grid-row:span 3}
  .tiles{grid-template-columns:1fr 1fr}
}
</style>
</head>
<body class="s-loading">
<div class="bg" aria-hidden="true"><i class="blob b1"></i><i class="blob b2"></i><i class="blob b3"></i></div>

<header class="top">
  <div>
    <h1 data-i18n="appTitle">Thermostat</h1>
    <div class="clock"><span id="clock">--:--</span> · <span id="syncTxt"></span></div>
  </div>
  <div class="top-r">
    <span class="pill glass" id="pill"><i class="dot"></i><span id="pillTxt">…</span></span>
    <div class="lang glass" role="group" data-i18n-aria="langAria">
      <button data-lang="en" type="button">EN</button><button data-lang="tr" type="button">TR</button>
    </div>
  </div>
</header>

<main>
<!-- ================= HOME ================= -->
<section class="tab on" id="tab-home" role="tabpanel">
  <div id="banners" class="span2"></div>

  <article class="glass hero">
    <div class="dial-wrap">
      <svg class="dial" viewBox="0 0 240 240" aria-hidden="true">
        <defs>
          <linearGradient id="gHeat" x1="0" y1="1" x2="1" y2="0"><stop offset="0" stop-color="#ffb340"/><stop offset=".6" stop-color="#ff8a3d"/><stop offset="1" stop-color="#ff3d77"/></linearGradient>
          <linearGradient id="gIdle" x1="0" y1="1" x2="1" y2="0"><stop offset="0" stop-color="#64d2ff"/><stop offset="1" stop-color="#7a5cff"/></linearGradient>
          <linearGradient id="gEco" x1="0" y1="1" x2="1" y2="0"><stop offset="0" stop-color="#8fd14f"/><stop offset="1" stop-color="#34c759"/></linearGradient>
          <linearGradient id="gOff" x1="0" y1="1" x2="1" y2="0"><stop offset="0" stop-color="#8e8e93"/><stop offset="1" stop-color="#b8b8be"/></linearGradient>
        </defs>
        <circle class="trk" cx="120" cy="120" r="100" stroke-dasharray="471.24 628.32" transform="rotate(135 120 120)"/>
        <circle class="go" id="dialGo" cx="120" cy="120" r="100" stroke="url(#gIdle)" stroke-dasharray="0 628.32" transform="rotate(135 120 120)"/>
        <circle class="prg" id="dialPrg" cx="120" cy="120" r="100" stroke="url(#gIdle)" stroke-dasharray="0 628.32" transform="rotate(135 120 120)"/>
        <circle class="tgt" id="dialTgt" cx="49.3" cy="190.7" r="9"/>
      </svg>
      <div class="dial-c">
        <div class="dial-lbl" data-i18n="insideNow">Inside now</div>
        <div class="dial-big"><span id="indoor">--</span><span class="deg">°</span></div>
        <div class="dial-sub" id="trendTxt"></div>
      </div>
    </div>
    <div class="target">
      <button class="rbtn glass" id="minus" type="button" data-i18n-aria="minusAria">−</button>
      <div class="target-mid">
        <div class="target-lbl" data-i18n="target">Target</div>
        <div class="target-val"><span id="target">--</span>°</div>
        <div class="target-eco" id="ecoNote"></div>
      </div>
      <button class="rbtn glass" id="plus" type="button" data-i18n-aria="plusAria">+</button>
    </div>
    <p class="ttt" id="ttt"></p>
  </article>

  <article class="glass card">
    <div class="seg" id="modeSeg" role="radiogroup">
      <span class="seg-thumb" id="segThumb"></span>
      <button type="button" role="radio" data-mode="HEAT"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M12 3c1 3.5 5 5.5 5 10a5 5 0 0 1-10 0c0-2.2 1.2-3.6 2.5-4.6.2 1.6 1 2.6 2 3.1C11 9 11.2 5.8 12 3z"/></svg><span data-i18n="mHeat">Heat</span></button>
      <button type="button" role="radio" data-mode="ECO"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M5 20c0-9 5.5-15 15-15 0 9.5-6 15-14 15"/><path d="M5 20l9-9"/></svg><span data-i18n="mEco">Eco</span></button>
      <button type="button" role="radio" data-mode="OFF"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"><path d="M12 3v8"/><path d="M6.3 7.3a8 8 0 1 0 11.4 0"/></svg><span data-i18n="mOff">Off</span></button>
    </div>
    <p class="mode-desc" id="modeDesc"></p>
  </article>

  <div class="tiles">
    <div class="glass tile">
      <div class="lbl"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 3s6 6.5 6 11a6 6 0 0 1-12 0c0-4.5 6-11 6-11z"/></svg><span data-i18n="humidity">Humidity</span></div>
      <div class="val" id="hum">--</div><div class="cap" id="humCap"></div>
    </div>
    <div class="glass tile">
      <div class="lbl"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M14 14.8V4a2 2 0 0 0-4 0v10.8a4 4 0 1 0 4 0z"/></svg><span data-i18n="feels">Feels like</span></div>
      <div class="val" id="feels">--</div><div class="cap" data-i18n="feelsCap"></div>
    </div>
    <div class="glass tile">
      <div class="lbl"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M7 18a4.5 4.5 0 1 1 1-8.9A6 6 0 0 1 19.5 11 3.5 3.5 0 0 1 18 18z"/></svg><span data-i18n="outdoor">Outdoor</span></div>
      <div class="val" id="out">--</div><div class="cap" id="outCap"></div>
    </div>
    <div class="glass tile">
      <div class="lbl"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 3c1 3.5 5 5.5 5 10a5 5 0 0 1-10 0c0-2.2 1.2-3.6 2.5-4.6.2 1.6 1 2.6 2 3.1C11 9 11.2 5.8 12 3z"/></svg><span data-i18n="gasToday">Gas today</span></div>
      <div class="val" id="gasT">--</div><div class="cap" id="gasTCap"></div>
    </div>
  </div>

  <article class="glass card">
    <div class="card-h"><h2 data-i18n="happening">What's happening</h2><button class="ibtn" type="button" data-info="iHappening" data-i18n-aria="infoAria"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7.5v.01"/></svg></button></div>
    <p class="info" id="iHappening" data-i18n="happeningInfo" hidden></p>
    <p class="reason" id="reason"></p>
    <div class="meter"><div class="top-row"><span data-i18n="burnerShare">Burner share</span><b id="dutyTxt">--</b></div><div class="bar"><i id="dutyBar"></i></div></div>
    <div class="kv"><span data-i18n="baseLoad">Weather base load</span><b id="baseLoad">--</b></div>
  </article>
</section>

<!-- ================= ENERGY ================= -->
<section class="tab" id="tab-energy" role="tabpanel">
  <article class="glass card span2">
    <div class="card-h"><h2 data-i18n="gasTitle">Gas usage</h2><button class="ibtn" type="button" data-info="iGas" data-i18n-aria="infoAria"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7.5v.01"/></svg></button></div>
    <p class="info" id="iGas" hidden></p>
    <div class="stats">
      <div class="stat"><span data-i18n="today">Today</span><b id="gToday">--</b><em id="gTodayMin"></em></div>
      <div class="stat"><span data-i18n="yesterday">Yesterday</span><b id="gYest">--</b></div>
      <div class="stat"><span data-i18n="last7">7 days</span><b id="gWeek">--</b><em id="gWeekMin"></em></div>
    </div>
    <div class="chart" id="gChart"></div>
    <p class="foot" id="gFoot"></p>
  </article>

  <article class="glass card span2">
    <div class="card-h"><h2 data-i18n="calTitle">Calibrate with your gas meter</h2><button class="ibtn" type="button" data-info="iCal" data-i18n-aria="infoAria"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7.5v.01"/></svg></button></div>
    <p class="info" id="iCal" data-i18n="calInfo" hidden></p>
    <p class="status-line" id="calStatus"></p>
    <ol class="steps"><li data-i18n="cal1"></li><li id="cal2"></li><li data-i18n="cal3"></li></ol>
    <div class="field">
      <input id="meterIn" type="text" inputmode="decimal" autocomplete="off" data-i18n-ph="meterPh" data-i18n-aria="calTitle">
      <button class="btn" id="meterSave" type="button" data-i18n="saveReading">Save reading</button>
    </div>
    <p class="msg" id="calMsg" role="status"></p>
    <button class="linkbtn" id="calReset" type="button" data-i18n="resetCal">Reset calibration</button>
  </article>
</section>

<!-- ================= INSIGHTS ================= -->
<section class="tab" id="tab-insights" role="tabpanel">
  <article class="glass card">
    <div class="card-h"><h2 data-i18n="homeTitle">Your home, learned</h2><button class="ibtn" type="button" data-info="iHome" data-i18n-aria="infoAria"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7.5v.01"/></svg></button></div>
    <p class="info" id="iHome" data-i18n="homeInfo" hidden></p>
    <div class="stats two">
      <div class="stat"><span data-i18n="heatUp">Heat-up speed</span><b id="mHeat">--</b><em data-i18n="heatUpCap"></em></div>
      <div class="stat"><span data-i18n="heatLoss">Heat loss</span><b id="mLoss">--</b><em data-i18n="heatLossCap"></em></div>
      <div class="stat"><span data-i18n="coolOff">If heating stopped</span><b id="mCool">--</b><em data-i18n="coolOffCap"></em></div>
      <div class="stat"><span data-i18n="trendLbl">Trend</span><b id="mTrend">--</b><em data-i18n="trendCap"></em></div>
    </div>
    <div class="prog"><div class="bar"><i id="learnBar"></i></div><p id="learnTxt"></p></div>
  </article>

  <article class="glass card">
    <div class="card-h"><h2 data-i18n="wxTitle">Weather-aware heating</h2><button class="ibtn" type="button" data-info="iWx" data-i18n-aria="infoAria"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7.5v.01"/></svg></button></div>
    <p class="info" id="iWx" data-i18n="wxInfo" hidden></p>
    <div class="kv"><span data-i18n="wxNow">Outside now</span><b id="wxNow">--</b></div>
    <div class="kv"><span data-i18n="wx3h">In 3 hours</span><b id="wx3h">--</b></div>
    <div class="kv"><span data-i18n="wxUsed">Used for control</span><b id="wxUsed">--</b></div>
    <div class="kv"><span data-i18n="baseLoad">Weather base load</span><b id="wxBase">--</b></div>
  </article>

  <article class="glass card">
    <div class="card-h"><h2 data-i18n="wdTitle">Boiler watchdog</h2><button class="ibtn" type="button" data-info="iWd" data-i18n-aria="infoAria"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7.5v.01"/></svg></button></div>
    <p class="info" id="iWd" data-i18n="wdInfo" hidden></p>
    <p class="status-line" id="wdStatus"></p>
  </article>

  <article class="glass card">
    <div class="card-h"><h2 data-i18n="ghTitle">Google Home</h2></div>
    <p class="mode-desc" style="margin:0" data-i18n="ghInfo"></p>
  </article>
</section>

<!-- ================= DEVICE ================= -->
<section class="tab" id="tab-device" role="tabpanel">
  <article class="glass card"><div class="card-h"><h2 data-i18n="boilerTitle">Boiler controller</h2></div><div id="boilerKv"></div></article>
  <article class="glass card"><div class="card-h"><h2 data-i18n="sensorTitle">Room sensor</h2></div><div id="sensorKv"></div></article>
  <article class="glass card span2">
    <div class="card-h"><h2 data-i18n="otaTitle">Firmware update</h2></div>
    <p class="mode-desc" style="margin:0 0 12px" data-i18n="otaInfo"></p>
    <a class="btn ghost" href="/update" data-i18n="otaBtn">Open update page</a>
  </article>
  <p class="ver span2" id="ver"></p>
</section>
</main>

<nav class="tabbar glass" role="tablist">
  <button type="button" role="tab" data-tab="home" class="on"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M3 11.5 12 4l9 7.5V20a1 1 0 0 1-1 1h-5v-6h-6v6H4a1 1 0 0 1-1-1z"/></svg><span data-i18n="tabHome">Home</span></button>
  <button type="button" role="tab" data-tab="energy"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M12 3c1 3.5 5 5.5 5 10a5 5 0 0 1-10 0c0-2.2 1.2-3.6 2.5-4.6.2 1.6 1 2.6 2 3.1C11 9 11.2 5.8 12 3z"/></svg><span data-i18n="tabEnergy">Energy</span></button>
  <button type="button" role="tab" data-tab="insights"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M4 20V10M10 20V4M16 20v-7M21 20H3"/></svg><span data-i18n="tabInsights">Insights</span></button>
  <button type="button" role="tab" data-tab="device"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><rect x="6" y="6" width="12" height="12" rx="2"/><path d="M9 2v4M15 2v4M9 18v4M15 18v4M2 9h4M2 15h4M18 9h4M18 15h4"/></svg><span data-i18n="tabDevice">Device</span></button>
</nav>
<div class="toast glass" id="toast" role="status" aria-live="polite"></div>

<script>
const I18N = {
en: {
  appTitle:'Thermostat', synced:'Time synced', notSynced:'Clock not synced yet',
  st_heating:'Heating', st_idle:'Idle', st_eco:'Eco', st_off:'Off', st_alert:'Check', stOffline:'Offline', st_loading:'Connecting…',
  insideNow:'Inside now', target:'Target', minusAria:'Lower target by 0.5 degrees', plusAria:'Raise target by 0.5 degrees',
  ecoNote:'Eco aims for {t}°', trendUp:'Rising {v}°/h', trendDown:'Falling {v}°/h', trendFlat:'Steady',
  tttHeating:'Reaches {t}° in about {d} — around {eta}',
  tttNearly:'Almost at {t}° — easing in gently so it doesn’t overshoot',
  tttAt:'At {t}° — holding the temperature',
  tttUnreachable:'Can’t reach {t}° at today’s outdoor temperature', tttEcoHint:' — Eco limits the burner, try Heat',
  tttOff:'Heating is off', tttUnknown:'Waiting for the room sensor',
  mHeat:'Heat', mEco:'Eco', mOff:'Off',
  dHeat:'Keeps the room with the sensor (your coldest room) at the target, using full boiler power when needed.',
  dEco:'Saves gas: aims {s} °C below the target and runs the burner at most {p} of the time. Only on this page — Google Home shows it as Heat.',
  dOff:'No heating at all. Use it in summer or when you are away for a long time.',
  humidity:'Humidity', humDry:'Dry air', humOk:'Comfortable', humWet:'Humid — air the room',
  feels:'Feels like', feelsCap:'For information only', outdoor:'Outdoor', in3h:'In 3 h: {v}°', noWeather:'No weather data yet',
  gasToday:'Gas today', burnerMin:'Burner {v} min',
  happening:'What’s happening',
  happeningInfo:'Every 20 minutes the thermostat decides what share of the time the burner should run. It starts from what your home needs at the current outdoor temperature (the weather base load), then adds heat when the room is below target and less when it is above. It also counts the heat still stored in the radiators, so it stops a little early instead of overshooting. The burner always runs at least 3 minutes and rests at least 5 minutes to protect the boiler.',
  r_starting:'Starting up…', r_waiting_sensor:'Waiting for the first reading from the room sensor.',
  r_sensor_timeout:'No data from the room sensor for 10 minutes, so heating is paused for safety.',
  r_off:'Heating is switched off.', r_satisfied:'Warm enough — no heat needed right now.',
  r_heating_full:'Heating at full power to reach the target.',
  r_pwm_on:'Burner on for this part of the 20-minute cycle (share: {p}).',
  r_pwm_off:'Burner resting for the rest of this cycle (share: {p}).',
  r_min_off_wait:'Waiting a few minutes before restarting the burner — this protects the boiler.',
  r_min_on_hold:'Letting the burner finish its minimum run — this protects the boiler.',
  burnerShare:'Burner share this cycle', baseLoad:'Weather base load',
  a_boiler_t:'Boiler may not be heating',
  a_boiler_b:'The burner has been on for {m} minutes but the room changed by {a} °C (about {e} °C was expected). Check the boiler display for an error code, the water pressure (usually 1–2 bar) and the gas supply.',
  a_sensor_t:'Room sensor offline',
  a_sensor_b:'No temperature from the sensor{since}. Heating is paused until it is back — check its power and Wi-Fi.', since:' since {v}',
  a_offline_t:'Can’t reach the thermostat', a_offline_b:'Retrying every 5 seconds. Check that this phone or computer is on the same Wi-Fi.',
  dismiss:'Dismiss',
  gasTitle:'Gas usage', today:'Today', yesterday:'Yesterday', last7:'7 days', minShort:'{v} min burner',
  gasInfo:'Estimated from burner time: hours × {r} m³/h (the boiler at full load) × the calibration factor. Days start at midnight Turkey time and are kept across restarts. The boiler often burns below full load, so calibrate with your gas meter below for real numbers.',
  gasFactor:'Calibrated against the gas meter: ×{f}', gasUncal:'Not calibrated yet — numbers assume full load', dayUnknown:'Waiting for the clock to sync before counting calendar days.',
  calTitle:'Calibrate with your gas meter',
  calInfo:'The thermostat only knows when it asks the boiler for heat, not how hard the boiler burns. Comparing two meter readings with the burner time between them gives a correction factor. The meter also counts hot water (and a gas cooker), so calibrated numbers include those.',
  cal1:'Read the number on your gas meter and save it here.', cal2:'Let the heating run for a few days — at least {h} hours of burner time.', cal3:'Save the new reading. The estimate corrects itself; repeat any time to stay accurate.',
  calNone:'Not calibrated — using the full-load rate.', calProgress:'Step 2 of 3 — started at {m} m³ · {h} of {n} burner hours so far.', calDone:'Calibrated: estimates × {f}.',
  meterPh:'e.g. 12345.678', saveReading:'Save', resetCal:'Reset calibration', resetConfirm:'Reset the gas calibration to factor 1.0?',
  cal_started:'Saved. Enter the next reading after a few days of heating.', cal_calibrated:'Calibrated! New factor ×{f}.',
  cal_need_more_burn:'Not enough burner time yet: {h} of {n} hours.', cal_meter_not_higher:'The new reading must be higher than {m} m³.',
  cal_invalid:'Please enter a valid number.', cal_reset:'Calibration reset.',
  homeTitle:'Your home, learned',
  homeInfo:'Every hour the thermostat compares how fast the room warmed or cooled with how much the burner ran and how cold it was outside. From this it learns two numbers about your home: how fast the heating can warm it and how fast it loses heat. Old data fades over about two days, so it follows the seasons. These numbers drive the time-to-target estimate and the weather-aware heating.',
  heatUp:'Heat-up speed', heatUpCap:'at full power, before losses', heatLoss:'Heat loss', heatLossCap:'of the inside–outside gap',
  coolOff:'If heating stopped', coolOffCap:'the room would cool this fast now', trendLbl:'Trend', trendCap:'last 30 minutes',
  learning:'Learning — {n} of {m} hours of usable data', learned:'Learned — based on the last {n} hours of data',
  wxTitle:'Weather-aware heating',
  wxInfo:'A home loses more heat when it is cold outside. Instead of waiting for the room to cool, the thermostat starts from the burner share your home needs at the outdoor temperature. It averages now and 3 hours ahead because a house reacts slowly. The room sensor then fine-tunes it.',
  wxNow:'Outside now', wx3h:'In 3 hours', wxUsed:'Used for control',
  wdTitle:'Boiler watchdog',
  wdInfo:'After the burner has run for an hour, the thermostat checks that the room warmed up as much as the learned model expects. If it did not — or the room got colder — you get an alert here and a push notification in the Sinric Pro app. You are also warned when the room sensor goes silent.',
  wdOk:'All good — no problems detected.', wdAlert:'Active alert — see the Home tab.',
  ghTitle:'Google Home',
  ghInfo:'Google Home sees this thermostat with Heat and Off, the target temperature and the real room temperature and humidity. Eco is only available on this page. “Turn on” brings back the last mode you used.',
  boilerTitle:'Boiler controller', sensorTitle:'Room sensor',
  kIp:'IP address', kWifi:'Wi-Fi', kFw:'Firmware', kUp:'Uptime', kHeap:'Free memory', kReset:'Last restart', kClock:'Clock',
  kSw:'Relay switches', kDrops:'Wi-Fi drops', kWx:'Weather errors', kSamples:'Learning samples', kFaults:'Boiler alerts',
  kStatus:'Status', kRead:'Read errors', kSeen:'Last reading', online:'Online', offline:'Offline',
  otaTitle:'Firmware update', otaInfo:'This board has 1 MB of flash, so updates take two steps: first upload the tiny updater, then the new firmware. The README explains it.', otaBtn:'Open update page',
  tabHome:'Home', tabEnergy:'Energy', tabInsights:'Insights', tabDevice:'Device',
  uH:'h', uMin:'min', uS:'s', uD:'d', ago:'{v} ago', saved:'Saved', saveFailed:'Couldn’t save — try again', infoAria:'More information', langAria:'Language'
},
tr: {
  appTitle:'Termostat', synced:'Saat senkron', notSynced:'Saat henüz senkron değil',
  st_heating:'Isıtıyor', st_idle:'Beklemede', st_eco:'Eko', st_off:'Kapalı', st_alert:'Kontrol et', stOffline:'Çevrimdışı', st_loading:'Bağlanıyor…',
  insideNow:'Şu an içeride', target:'Hedef', minusAria:'Hedefi 0,5 derece düşür', plusAria:'Hedefi 0,5 derece yükselt',
  ecoNote:'Eko hedefi {t}°', trendUp:'Yükseliyor {v}°/sa', trendDown:'Düşüyor {v}°/sa', trendFlat:'Sabit',
  tttHeating:'Yaklaşık {d} içinde {t}° olacak — saat {eta} civarı',
  tttNearly:'{t}° hedefine çok yakın — aşırı ısınmamak için yavaşça yaklaşıyor',
  tttAt:'{t}° hedefinde — sıcaklık korunuyor',
  tttUnreachable:'Bugünkü dış sıcaklıkta {t}° hedefine ulaşılamıyor', tttEcoHint:' — Eko brülörü sınırlıyor, Isıtma modunu deneyin',
  tttOff:'Isıtma kapalı', tttUnknown:'Oda sensörü bekleniyor',
  mHeat:'Isıtma', mEco:'Eko', mOff:'Kapalı',
  dHeat:'Sensörün bulunduğu odayı (en soğuk odanızı) hedef sıcaklıkta tutar; gerektiğinde kombinin tam gücünü kullanır.',
  dEco:'Gaz tasarrufu: hedefin {s} °C altını hedefler ve brülörü zamanın en fazla {p} kadarında çalıştırır. Sadece bu sayfada var — Google Home bunu Isıtma olarak gösterir.',
  dOff:'Hiç ısıtma yapılmaz. Yazın veya uzun süre evde olmadığınızda kullanın.',
  humidity:'Nem', humDry:'Kuru hava', humOk:'Konforlu', humWet:'Nemli — odayı havalandırın',
  feels:'Hissedilen', feelsCap:'Sadece bilgi amaçlı', outdoor:'Dış hava', in3h:'3 saat sonra: {v}°', noWeather:'Henüz hava durumu yok',
  gasToday:'Bugünkü gaz', burnerMin:'Brülör {v} dk',
  happening:'Şu an ne oluyor',
  happeningInfo:'Termostat her 20 dakikada bir brülörün zamanın ne kadarında çalışacağına karar verir. Önce evinizin mevcut dış sıcaklıkta ihtiyaç duyduğu payla başlar (hava durumu taban yükü); oda hedefin altındaysa daha fazla, üstündeyse daha az ısıtır. Radyatörlerde hâlâ depolanan ısıyı da hesaba katar, böylece aşırı ısıtmak yerine biraz erken durur. Kombiyi korumak için brülör en az 3 dakika çalışır ve en az 5 dakika dinlenir.',
  r_starting:'Başlatılıyor…', r_waiting_sensor:'Oda sensöründen ilk ölçüm bekleniyor.',
  r_sensor_timeout:'Oda sensöründen 10 dakikadır veri gelmiyor; güvenlik için ısıtma durduruldu.',
  r_off:'Isıtma kapalı.', r_satisfied:'Yeterince sıcak — şu an ısıtmaya gerek yok.',
  r_heating_full:'Hedefe ulaşmak için tam güçle ısıtıyor.',
  r_pwm_on:'Brülör 20 dakikalık döngünün bu bölümünde çalışıyor (pay: {p}).',
  r_pwm_off:'Brülör döngünün kalanında dinleniyor (pay: {p}).',
  r_min_off_wait:'Brülör yeniden çalışmadan önce birkaç dakika bekleniyor — bu kombiyi korur.',
  r_min_on_hold:'Brülörün minimum çalışma süresi tamamlanıyor — bu kombiyi korur.',
  burnerShare:'Bu döngüde brülör payı', baseLoad:'Hava durumu taban yükü',
  a_boiler_t:'Kombi ısıtmıyor olabilir',
  a_boiler_b:'Brülör {m} dakikadır açık ama oda sıcaklığı {a} °C değişti (yaklaşık {e} °C bekleniyordu). Kombi ekranında hata kodu olup olmadığını, su basıncını (genelde 1–2 bar) ve gaz beslemesini kontrol edin.',
  a_sensor_t:'Oda sensörü çevrimdışı',
  a_sensor_b:'Sensörden sıcaklık gelmiyor{since}. Sensör geri gelene kadar ısıtma durduruldu — gücünü ve Wi-Fi bağlantısını kontrol edin.', since:' (saat {v} itibarıyla)',
  a_offline_t:'Termostata ulaşılamıyor', a_offline_b:'5 saniyede bir yeniden deneniyor. Bu telefonun veya bilgisayarın aynı Wi-Fi ağında olduğundan emin olun.',
  dismiss:'Kapat',
  gasTitle:'Gaz kullanımı', today:'Bugün', yesterday:'Dün', last7:'7 gün', minShort:'brülör {v} dk',
  gasInfo:'Brülör süresinden tahmin edilir: saat × {r} m³/sa (kombinin tam yükü) × kalibrasyon katsayısı. Günler Türkiye saatiyle gece yarısı başlar ve yeniden başlatmalarda korunur. Kombi çoğu zaman tam yükün altında yandığından, gerçek değerler için aşağıdan gaz sayacınızla kalibre edin.',
  gasFactor:'Gaz sayacına göre kalibre edildi: ×{f}', gasUncal:'Henüz kalibre edilmedi — değerler tam yük varsayar', dayUnknown:'Takvim günlerini saymak için saatin senkron olması bekleniyor.',
  calTitle:'Gaz sayacınızla kalibre edin',
  calInfo:'Termostat yalnızca kombiden ne zaman ısı istediğini bilir, kombinin ne kadar güçlü yandığını bilmez. İki sayaç değerini aradaki brülör süresiyle karşılaştırmak bir düzeltme katsayısı verir. Sayaç sıcak suyu (ve varsa gazlı ocağı) da saydığı için kalibre edilmiş değerler bunları da içerir.',
  cal1:'Gaz sayacınızdaki değeri okuyun ve buraya kaydedin.', cal2:'Isıtmayı birkaç gün çalıştırın — en az {h} saat brülör süresi.', cal3:'Yeni değeri kaydedin. Tahmin kendini düzeltir; doğru kalması için istediğiniz zaman tekrarlayın.',
  calNone:'Kalibre edilmedi — tam yük değeri kullanılıyor.', calProgress:'Adım 2/3 — {m} m³ ile başlandı · şimdiye kadar {h}/{n} brülör saati.', calDone:'Kalibre edildi: tahminler × {f}.',
  meterPh:'örn. 12345,678', saveReading:'Kaydet', resetCal:'Kalibrasyonu sıfırla', resetConfirm:'Gaz kalibrasyonu 1,0 katsayısına sıfırlansın mı?',
  cal_started:'Kaydedildi. Birkaç günlük ısıtmadan sonra yeni değeri girin.', cal_calibrated:'Kalibre edildi! Yeni katsayı ×{f}.',
  cal_need_more_burn:'Henüz yeterli brülör süresi yok: {h}/{n} saat.', cal_meter_not_higher:'Yeni değer {m} m³ değerinden büyük olmalı.',
  cal_invalid:'Lütfen geçerli bir sayı girin.', cal_reset:'Kalibrasyon sıfırlandı.',
  homeTitle:'Evinizin öğrenilen profili',
  homeInfo:'Termostat her saat, odanın ne kadar hızlı ısındığını veya soğuduğunu brülörün ne kadar çalıştığı ve dışarının ne kadar soğuk olduğuyla karşılaştırır. Böylece eviniz hakkında iki değer öğrenir: ısıtmanın evi ne kadar hızlı ısıtabildiği ve evin ne kadar hızlı ısı kaybettiği. Eski veriler yaklaşık iki günde etkisini yitirir, böylece mevsimlere uyum sağlar. Bu değerler hedefe ulaşma süresi tahminini ve hava durumuna duyarlı ısıtmayı yönetir.',
  heatUp:'Isınma hızı', heatUpCap:'tam güçte, kayıplar hariç', heatLoss:'Isı kaybı', heatLossCap:'iç–dış sıcaklık farkının',
  coolOff:'Isıtma dursaydı', coolOffCap:'oda şu an bu hızla soğurdu', trendLbl:'Eğilim', trendCap:'son 30 dakika',
  learning:'Öğreniyor — {m} saatlik verinin {n} saati toplandı', learned:'Öğrenildi — son {n} saatlik veriye dayanıyor',
  wxTitle:'Hava durumuna duyarlı ısıtma',
  wxInfo:'Dışarısı soğukken ev daha fazla ısı kaybeder. Termostat odanın soğumasını beklemek yerine, evinizin mevcut dış sıcaklıkta ihtiyaç duyduğu brülör payıyla başlar. Ev yavaş tepki verdiği için şimdiki ve 3 saat sonraki sıcaklığın ortalamasını kullanır. Ardından oda sensörü ince ayar yapar.',
  wxNow:'Şu an dışarısı', wx3h:'3 saat sonra', wxUsed:'Kontrolde kullanılan',
  wdTitle:'Kombi gözcüsü',
  wdInfo:'Brülör bir saat çalıştıktan sonra termostat, odanın öğrenilen modelin beklediği kadar ısınıp ısınmadığını kontrol eder. Isınmadıysa — ya da oda soğuduysa — burada bir uyarı görürsünüz ve Sinric Pro uygulamasına bildirim gelir. Oda sensörü sessiz kaldığında da uyarılırsınız.',
  wdOk:'Her şey yolunda — sorun tespit edilmedi.', wdAlert:'Aktif uyarı var — Ev sekmesine bakın.',
  ghTitle:'Google Home',
  ghInfo:'Google Home bu termostatı Isıtma ve Kapalı modlarıyla, hedef sıcaklıkla ve gerçek oda sıcaklığı ile nemiyle görür. Eko yalnızca bu sayfada kullanılabilir. “Aç” komutu en son kullandığınız modu geri getirir.',
  boilerTitle:'Kombi kontrolcüsü', sensorTitle:'Oda sensörü',
  kIp:'IP adresi', kWifi:'Wi-Fi', kFw:'Yazılım', kUp:'Çalışma süresi', kHeap:'Boş bellek', kReset:'Son yeniden başlatma', kClock:'Saat',
  kSw:'Röle anahtarlama', kDrops:'Wi-Fi kopmaları', kWx:'Hava durumu hataları', kSamples:'Öğrenme örnekleri', kFaults:'Kombi uyarıları',
  kStatus:'Durum', kRead:'Okuma hataları', kSeen:'Son ölçüm', online:'Çevrimiçi', offline:'Çevrimdışı',
  otaTitle:'Yazılım güncelleme', otaInfo:'Bu kartta 1 MB flash bellek olduğu için güncelleme iki adımda yapılır: önce küçük güncelleyici, sonra yeni yazılım yüklenir. Ayrıntılar README dosyasında.', otaBtn:'Güncelleme sayfasını aç',
  tabHome:'Ev', tabEnergy:'Enerji', tabInsights:'Analiz', tabDevice:'Cihaz',
  uH:'sa', uMin:'dk', uS:'sn', uD:'g', ago:'{v} önce', saved:'Kaydedildi', saveFailed:'Kaydedilemedi — tekrar deneyin', infoAria:'Daha fazla bilgi', langAria:'Dil'
}};

const $ = id => document.getElementById(id);
const store = {
  get(k) { try { return localStorage.getItem(k); } catch (e) { return null; } },
  set(k, v) { try { localStorage.setItem(k, v); } catch (e) {} }
};
let L = store.get('lang') || ((navigator.language || '').toLowerCase().startsWith('tr') ? 'tr' : 'en');
let D = null, fails = 0, holdT = null, holdUntil = 0, sendTimer = null, dismissed = '';

function t(k, p) {
  let s = (I18N[L] && I18N[L][k] != null) ? I18N[L][k] : (I18N.en[k] != null ? I18N.en[k] : k);
  if (p) for (const x in p) s = s.split('{' + x + '}').join(p[x]);
  return s;
}
const loc = () => L === 'tr' ? 'tr-TR' : 'en-GB';
const ok = v => v !== undefined && v !== null && !isNaN(v);
function num(v, d = 1) { return ok(v) ? Number(v).toLocaleString(loc(), { minimumFractionDigits: d, maximumFractionDigits: d }) : '--'; }
function signed(v, d = 1) { return ok(v) ? (v > 0 ? '+' : v < 0 ? '−' : '') + num(Math.abs(v), d) : '--'; }
function pct(v, d = 0) { const s = num(v, d); return L === 'tr' ? '%' + s : s + '%'; }
function hm(ms) { return new Date(ms).toLocaleTimeString(loc(), { hour: '2-digit', minute: '2-digit', hour12: false }); }
function dur(m) {
  m = Math.max(1, Math.round(m)); const h = Math.floor(m / 60), r = m % 60;
  return h ? (r ? h + ' ' + t('uH') + ' ' + r + ' ' + t('uMin') : h + ' ' + t('uH')) : r + ' ' + t('uMin');
}
function age(s) {
  if (!ok(s)) return '--';
  if (s < 60) return t('ago', { v: s + ' ' + t('uS') });
  if (s < 3600) return t('ago', { v: Math.round(s / 60) + ' ' + t('uMin') });
  return t('ago', { v: dur(s / 60) });
}
function upt(s) {
  if (!ok(s)) return '--';
  const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.floor(s % 3600 / 60);
  return (d ? d + ' ' + t('uD') + ' ' : '') + h + ' ' + t('uH') + ' ' + m + ' ' + t('uMin');
}
function setText(id, v) { const el = $(id); if (el && el.textContent !== v) el.textContent = v; }
function setHTML(id, v) { const el = $(id); if (el && el.innerHTML !== v) el.innerHTML = v; }
function esc(s) { return String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c])); }

function applyLang() {
  document.documentElement.lang = L;
  document.querySelectorAll('[data-i18n]').forEach(el => el.textContent = t(el.dataset.i18n));
  document.querySelectorAll('[data-i18n-ph]').forEach(el => el.placeholder = t(el.dataset.i18nPh));
  document.querySelectorAll('[data-i18n-aria]').forEach(el => el.setAttribute('aria-label', t(el.dataset.i18nAria)));
  document.querySelectorAll('.lang button').forEach(b => b.classList.toggle('on', b.dataset.lang === L));
  document.title = t('appTitle');
  render();
}

function stateOf() {
  if (!D) return 'loading';
  if ((D.alerts || []).length) return 'alert';
  if (D.system.mode === 'OFF') return 'off';
  if (D.system.relay) return 'heating';
  if (D.system.mode === 'ECO') return 'eco';
  return 'idle';
}

function shownTarget() { return (holdT !== null && Date.now() < holdUntil) ? holdT : (D ? D.env.target : null); }

function render() {
  const st = stateOf(), offline = fails >= 2;
  document.body.className = 's-' + st + (offline ? ' s-offline' : '');
  setText('pillTxt', offline ? t('stOffline') : t('st_' + st));
  if (!D) { renderBanners(offline); return; }
  const s = D.system, e = D.env, lim = D.limits || { min_target: 5, max_target: 30, eco_setback: 1, eco_max_duty_pct: 50 };

  // header clock
  const clk = D.time || {};
  setText('clock', clk.local ? clk.local.slice(11) : '--:--');
  setText('syncTxt', clk.synced ? t('synced') : t('notSynced'));

  // dial: 10..30 °C mapped onto a 270° arc
  const frac = v => Math.min(1, Math.max(0, (v - 10) / 20));
  const tgt = shownTarget();
  setText('indoor', num(e.indoor));
  $('dialPrg').setAttribute('stroke-dasharray', (ok(e.indoor) ? frac(e.indoor) * 471.24 : 0).toFixed(1) + ' 628.32');
  const dialGrad = s.relay ? 'Heat' : s.mode === 'OFF' ? 'Off' : s.mode === 'ECO' ? 'Eco' : 'Idle';
  $('dialPrg').setAttribute('stroke', 'url(#g' + dialGrad + ')');
  // Faded segment from the arc start up to the target dot; the solid arc (room temperature)
  // is drawn on top, so only the part still to go shows faded. Hidden when heating is off.
  $('dialGo').setAttribute('stroke-dasharray', (ok(tgt) && s.mode !== 'OFF' ? frac(tgt) * 471.24 : 0).toFixed(1) + ' 628.32');
  $('dialGo').setAttribute('stroke', 'url(#g' + dialGrad + ')');
  const ang = (135 + frac(ok(tgt) ? tgt : 10) * 270) * Math.PI / 180;
  $('dialTgt').setAttribute('cx', (120 + 100 * Math.cos(ang)).toFixed(1));
  $('dialTgt').setAttribute('cy', (120 + 100 * Math.sin(ang)).toFixed(1));
  const tr = e.trend_c_per_h;
  setText('trendTxt', !ok(tr) ? '' : Math.abs(tr) < 0.1 ? t('trendFlat') : tr > 0 ? '↗ ' + t('trendUp', { v: num(tr) }) : '↘ ' + t('trendDown', { v: num(-tr) }));

  setText('target', num(tgt));
  setText('ecoNote', s.mode === 'ECO' ? t('ecoNote', { t: num(tgt - lim.eco_setback) }) : '');
  $('minus').disabled = ok(tgt) && tgt <= lim.min_target;
  $('plus').disabled = ok(tgt) && tgt >= lim.max_target;

  // time to target
  const tt = D.time_to_target || {}, eff = e.effective_target, cur = ok(e.control_temp) ? e.control_temp : e.indoor;
  let tx = '';
  if (tt.state === 'off') tx = t('tttOff');
  else if (tt.state === 'unknown') tx = t('tttUnknown');
  else if (tt.state === 'unreachable') tx = t('tttUnreachable', { t: num(eff) }) + (s.mode === 'ECO' ? t('tttEcoHint') : '');
  else if (tt.state === 'reached') tx = (cur >= eff - 0.05) ? t('tttAt', { t: num(eff) }) : t('tttNearly', { t: num(eff) });
  else if (tt.state === 'heating') tx = t('tttHeating', { t: num(eff), d: dur(tt.minutes), eta: hm(Date.now() + tt.minutes * 60000) });
  setText('ttt', tx);

  // mode
  const modes = ['HEAT', 'ECO', 'OFF'], mi = Math.max(0, modes.indexOf(s.mode));
  $('segThumb').style.transform = 'translateX(' + (mi * 100) + '%)';
  document.querySelectorAll('#modeSeg button').forEach(b => {
    const on = b.dataset.mode === s.mode; b.classList.toggle('on', on); b.setAttribute('aria-checked', on);
  });
  setText('modeDesc', t({ HEAT: 'dHeat', ECO: 'dEco', OFF: 'dOff' }[s.mode] || 'dHeat', { s: num(lim.eco_setback, 0), p: pct(lim.eco_max_duty_pct) }));

  // tiles
  setHTML('hum', ok(e.humidity) ? esc(num(e.humidity, 0)) + '<small>%</small>' : '--');
  setText('humCap', !ok(e.humidity) ? '' : e.humidity < 30 ? t('humDry') : e.humidity > 60 ? t('humWet') : t('humOk'));
  setHTML('feels', ok(e.feels_like) ? esc(num(e.feels_like)) + '<small>°</small>' : '--');
  setHTML('out', ok(e.outdoor) ? esc(num(e.outdoor)) + '<small>°</small>' : '--');
  setText('outCap', ok(e.outdoor_3h) ? t('in3h', { v: num(e.outdoor_3h) }) : ok(e.outdoor) ? '' : t('noWeather'));
  const g = D.gas || {};
  setHTML('gasT', ok(g.today_m3) ? esc(num(g.today_m3, 2)) + '<small>m³</small>' : '--');
  setText('gasTCap', t('burnerMin', { v: g.today_burn_min || 0 }));

  // what's happening
  setText('reason', t('r_' + s.reason, { p: pct(s.duty_pct) }));
  setText('dutyTxt', pct(s.duty_pct));
  $('dutyBar').style.width = Math.min(100, s.duty_pct || 0) + '%';
  setText('baseLoad', ok(e.outdoor_control) ? pct(s.base_load_pct) : '--');

  renderBanners(offline);
  renderEnergy();
  renderInsights();
  renderDevice();
}

function renderBanners(offline) {
  const list = [];
  if (offline) list.push({ t: t('a_offline_t'), b: t('a_offline_b') });
  (D && D.alerts || []).forEach(a => {
    if (a.code === 'boiler_not_heating') {
      if (dismissed === 'boiler' + a.since) return;
      list.push({ t: t('a_boiler_t'), b: t('a_boiler_b', { m: a.minutes, a: signed(a.actual), e: signed(a.expected) }), dismiss: 'boiler' + a.since });
    } else if (a.code === 'sensor_offline') {
      list.push({ t: t('a_sensor_t'), b: t('a_sensor_b', { since: a.since ? t('since', { v: hm(a.since * 1000) }) : '' }) });
    }
  });
  const icon = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M10.3 3.9 1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0z"/><path d="M12 9v4M12 17v.01"/></svg>';
  setHTML('banners', list.map(x => '<div class="glass banner" role="alert"><h3>' + icon + esc(x.t) + '</h3><p>' + esc(x.b) + '</p>' +
    (x.dismiss ? '<button class="btn" type="button" data-dismiss="' + esc(x.dismiss) + '">' + esc(t('dismiss')) + '</button>' : '') + '</div>').join(''));
}

function renderEnergy() {
  const g = D.gas || {};
  setText('iGas', t('gasInfo', { r: num(g.rate_m3_per_h, 1) }));
  setHTML('gToday', esc(num(g.today_m3, 2)) + '<small>m³</small>');
  setHTML('gYest', esc(num(g.yesterday_m3, 2)) + '<small>m³</small>');
  setHTML('gWeek', esc(num(g.week_m3, 1)) + '<small>m³</small>');
  setText('gTodayMin', t('minShort', { v: g.today_burn_min || 0 }));
  setText('gWeekMin', t('minShort', { v: g.week_burn_min || 0 }));
  const hist = g.history_m3 || [], vals = [];
  for (let i = 5; i >= 0; i--) vals.push(hist[i] || 0);
  vals.push(g.today_m3 || 0);
  const max = Math.max(0.01, ...vals), wd = new Intl.DateTimeFormat(loc(), { weekday: 'short' });
  setHTML('gChart', vals.map((v, i) => {
    const d = new Date(Date.now() - (6 - i) * 86400000);
    return '<div class="col' + (i === 6 ? ' today' : '') + '"><span class="v">' + esc(num(v, 1)) + '</span><i class="b" style="height:' + Math.max(3, v / max * 100).toFixed(1) + '%"></i><span class="d">' + esc(wd.format(d)) + '</span></div>';
  }).join(''));
  setText('gFoot', !g.day_known ? t('dayUnknown') : (g.factor && Math.abs(g.factor - 1) > 0.001) ? t('gasFactor', { f: num(g.factor, 2) }) : t('gasUncal'));
  let cs = g.calibrating ? t('calProgress', { m: num(g.calib_start_meter, 3), h: num(g.calib_burn_h, 1), n: num(g.calib_need_h, 0) }) : t('calNone');
  if (!g.calibrating && g.factor && Math.abs(g.factor - 1) > 0.001) cs = t('calDone', { f: num(g.factor, 2) });
  else if (g.calibrating && g.factor && Math.abs(g.factor - 1) > 0.001) cs += ' ' + t('calDone', { f: num(g.factor, 2) });
  setText('calStatus', cs);
  setText('cal2', t('cal2', { h: num(g.calib_need_h || 5, 0) }));
}

function renderInsights() {
  const m = D.model || {}, e = D.env, s = D.system;
  setHTML('mHeat', ok(m.heat_rate_c_per_h) ? '+' + esc(num(m.heat_rate_c_per_h)) + '<small>°/' + esc(t('uH')) + '</small>' : '--');
  setHTML('mLoss', ok(m.loss_per_h) ? esc(pct(m.loss_per_h * 100, 1)) + '<small>/' + esc(t('uH')) + '</small>' : '--');
  const gap = ok(e.indoor) && ok(e.outdoor) ? e.indoor - e.outdoor : null;
  setHTML('mCool', ok(m.loss_per_h) && ok(gap) && gap > 0 ? esc(signed(-m.loss_per_h * gap)) + '<small>°/' + esc(t('uH')) + '</small>' : '--');
  setHTML('mTrend', ok(e.trend_c_per_h) ? esc(signed(e.trend_c_per_h)) + '<small>°/' + esc(t('uH')) + '</small>' : '--');
  const need = m.hours_needed || 12, have = m.hours_learned || 0;
  $('learnBar').style.width = Math.min(100, have / need * 100) + '%';
  setText('learnTxt', m.learned ? t('learned', { n: num(have, 0) }) : t('learning', { n: num(have, 0), m: num(need, 0) }));
  setText('wxNow', ok(e.outdoor) ? num(e.outdoor) + '°' : t('noWeather'));
  setText('wx3h', ok(e.outdoor_3h) ? num(e.outdoor_3h) + '°' : '--');
  setText('wxUsed', ok(e.outdoor_control) ? num(e.outdoor_control) + '°' : '--');
  setText('wxBase', ok(e.outdoor_control) && s.mode !== 'OFF' ? pct(s.base_load_pct) : '--');
  const bad = (D.alerts || []).length > 0;
  $('wdStatus').className = 'status-line ' + (bad ? 'bad' : 'ok');
  setText('wdStatus', bad ? t('wdAlert') : t('wdOk'));
}

function kvRows(rows) {
  return rows.map(r => '<div class="kv"><span>' + esc(r[0]) + '</span><b' + (r[2] ? ' class="' + r[2] + '"' : '') + '>' + esc(r[1]) + '</b></div>').join('');
}
function renderDevice() {
  const n = D.network || {}, h = D.hardware || {}, d = D.diagnostics || {}, s = D.system, c = D.time || {}, x = D.sensor || {};
  setHTML('boilerKv', kvRows([
    [t('kIp'), n.ip || '--'],
    [t('kWifi'), (n.ssid || '--') + ' · ' + (ok(n.rssi_dbm) ? n.rssi_dbm + ' dBm' : '--')],
    [t('kFw'), s.firmware ? 'v' + s.firmware : '--'],
    [t('kUp'), upt(s.uptime_s)],
    [t('kClock'), c.local ? c.local + ' · ' + t('synced') : t('notSynced'), c.synced ? 'ok' : 'warn'],
    [t('kHeap'), ok(h.free_heap_bytes) ? num(h.free_heap_bytes / 1024, 1) + ' KB' : '--'],
    [t('kReset'), h.reset_reason || '--'],
    [t('kSw'), String(d.relay_switch_count ?? '--')],
    [t('kDrops'), String(d.wifi_disconnect_count ?? '--')],
    [t('kWx'), (d.weather_fetch_fails ?? '--') + ' / ' + (d.weather_fetch_attempts ?? '--')],
    [t('kSamples'), String(d.model_samples ?? '--')],
    [t('kFaults'), String(d.boiler_faults ?? '--')]
  ]));
  const on = !!x.connected, fr = x.dht_fail_rate_pct;
  setHTML('sensorKv', kvRows([
    [t('kStatus'), on ? t('online') : t('offline'), on ? 'ok' : 'bad'],
    [t('kIp'), x.ip || '--'],
    [t('kFw'), x.firmware_version && x.firmware_version !== '--' ? 'v' + x.firmware_version : '--'],
    [t('kWifi'), ok(x.rssi_dbm) && x.ip ? x.rssi_dbm + ' dBm' : '--'],
    [t('kUp'), x.ip ? upt(x.uptime_seconds) : '--'],
    [t('kHeap'), ok(x.free_heap_bytes) && x.ip ? num(x.free_heap_bytes / 1024, 1) + ' KB' : '--'],
    [t('kRead'), ok(fr) ? pct(fr, 1) : '--', !ok(fr) ? '' : fr > 10 ? 'bad' : fr > 2 ? 'warn' : 'ok'],
    [t('kSeen'), x.ip ? age(x.last_seen_seconds_ago) : '--']
  ]));
  setText('ver', 'ThermostatESP · v' + (s.firmware || '?'));
}

async function poll() {
  try {
    const r = await fetch('/api/status', { cache: 'no-store' });
    if (!r.ok) throw new Error(r.status);
    D = await r.json(); fails = 0;
  } catch (err) { fails++; }
  render();
}

let toastTimer = null;
function toast(msg) {
  const el = $('toast'); el.textContent = msg; el.classList.add('show');
  clearTimeout(toastTimer); toastTimer = setTimeout(() => el.classList.remove('show'), 1800);
}
async function post(url) {
  const r = await fetch(url, { method: 'POST' });
  let j = {}; try { j = await r.json(); } catch (e) {}
  return { ok: r.ok, j };
}

function bump(delta) {
  if (!D) return;
  const lim = D.limits || { min_target: 5, max_target: 30 };
  let v = Math.round(((shownTarget() || 21) + delta) * 2) / 2;
  v = Math.min(lim.max_target, Math.max(lim.min_target, v));
  holdT = v; holdUntil = Date.now() + 10000; render();
  clearTimeout(sendTimer);
  sendTimer = setTimeout(async () => {
    try {
      const r = await post('/api/set?target=' + v);
      if (!r.ok) throw 0;
      toast(t('saved')); holdUntil = Date.now() + 1500;
    } catch (e) { toast(t('saveFailed')); holdT = null; }
    poll();
  }, 650);
}

async function setMode(m) {
  if (!D || D.system.mode === m) return;
  const prev = D.system.mode; D.system.mode = m; render();
  try { const r = await post('/api/set?mode=' + m); if (!r.ok) throw 0; toast(t('saved')); }
  catch (e) { D.system.mode = prev; render(); toast(t('saveFailed')); }
  poll();
}

async function saveMeter() {
  const raw = $('meterIn').value.trim().replace(',', '.');
  if (!raw || isNaN(Number(raw)) || Number(raw) <= 0) { setText('calMsg', t('cal_invalid')); return; }
  try {
    const r = await post('/api/gas?meter=' + encodeURIComponent(raw));
    const j = r.j || {};
    setText('calMsg', t('cal_' + (j.result || 'invalid'), {
      f: num(j.factor, 2), h: num(j.burn_h, 1), n: num(j.need_h, 0), m: num(j.start_meter, 3)
    }));
    if (r.ok) $('meterIn').value = '';
  } catch (e) { setText('calMsg', t('saveFailed')); }
  poll();
}

function showTab(name) {
  document.querySelectorAll('.tab').forEach(s => s.classList.toggle('on', s.id === 'tab-' + name));
  document.querySelectorAll('.tabbar button').forEach(b => {
    const on = b.dataset.tab === name; b.classList.toggle('on', on); b.setAttribute('aria-selected', on);
  });
  store.set('tab', name);
  window.scrollTo({ top: 0, behavior: 'smooth' });
}

// ---- wiring ----
$('minus').addEventListener('click', () => bump(-0.5));
$('plus').addEventListener('click', () => bump(0.5));
document.querySelectorAll('#modeSeg button').forEach(b => b.addEventListener('click', () => setMode(b.dataset.mode)));
document.querySelectorAll('.lang button').forEach(b => b.addEventListener('click', () => { L = b.dataset.lang; store.set('lang', L); applyLang(); }));
document.querySelectorAll('.tabbar button').forEach(b => b.addEventListener('click', () => showTab(b.dataset.tab)));
document.querySelectorAll('.ibtn').forEach(b => b.addEventListener('click', () => {
  const p = $(b.dataset.info), open = p.hidden; p.hidden = !open; b.setAttribute('aria-expanded', open);
}));
$('meterSave').addEventListener('click', saveMeter);
$('meterIn').addEventListener('keydown', ev => { if (ev.key === 'Enter') saveMeter(); });
$('calReset').addEventListener('click', async () => {
  if (!confirm(t('resetConfirm'))) return;
  try { const r = await post('/api/gas?reset=1'); setText('calMsg', r.ok ? t('cal_reset') : t('saveFailed')); }
  catch (e) { setText('calMsg', t('saveFailed')); }
  poll();
});
$('banners').addEventListener('click', async ev => {
  const b = ev.target.closest('[data-dismiss]'); if (!b) return;
  dismissed = b.dataset.dismiss; render();
  try { await post('/api/alerts/clear'); } catch (e) {}
  poll();
});
document.addEventListener('visibilitychange', () => { if (!document.hidden) poll(); });

const savedTab = store.get('tab');
if (savedTab && $('tab-' + savedTab)) showTab(savedTab);
applyLang();
poll();
setInterval(() => { if (!document.hidden) poll(); }, 5000);
</script>
</body>
</html>
)=====";

#endif
