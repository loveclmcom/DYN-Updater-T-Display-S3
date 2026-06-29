/*
 * DYN Updater by LOVECLM.COM — ESP32 + ILI9341 (320×240)  v1.1
 * https://www.loveclm.com
 *
 * Required libraries (install via Library Manager):
 *   Adafruit ILI9341   by Adafruit
 *   Adafruit GFX       by Adafruit
 *
 * Built-in ESP32 libraries (no install needed):
 *   WiFi, WebServer, HTTPClient, WiFiClientSecure, Preferences
 *
 * Configured for Guition ESP32-2432S028 (CYD) — ILI9341 on HSPI bus:
 *   CS   → GPIO 15    DC   → GPIO  2
 *   RST  → not used   MOSI → GPIO 13
 *   SCK  → GPIO 14    MISO → GPIO 12
 *   BL   → GPIO 21
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <time.h>

// ─────────────────────────────────────────────────────────────────────
// PIN DEFINITIONS  (Guition ESP32-2432S028 / CYD — HSPI bus)
// ─────────────────────────────────────────────────────────────────────
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1   // RST tied high internally on Guition board
#define TFT_MOSI 13
#define TFT_SCK  14
#define TFT_MISO 12
#define TFT_BL   21

// ─────────────────────────────────────────────────────────────────────
// TOUCH PINS  (Guition CYD XPT2046 — VSPI-like custom bus)
// ─────────────────────────────────────────────────────────────────────
#define TOUCH_CS   33
#define TOUCH_IRQ  36
#define TOUCH_CLK  25
#define TOUCH_MISO 39
#define TOUCH_MOSI 32

// Calibration — if touch is mirrored/off, swap or invert the min/max values
#define TS_MINX  280
#define TS_MAXX 3800
#define TS_MINY  340
#define TS_MAXY 3800

#define APP_VERSION "v1.1"

// ─────────────────────────────────────────────────────────────────────
// COLOR PALETTE (RGB565)
// ─────────────────────────────────────────────────────────────────────
#define C_BG      0x0841   // #081820 deep navy
#define C_CARD    0x1082   // #102042 card
#define C_BORDER  0x2124   // #212442 border
#define C_ACCENT  0x3C1E   // #3060F8 blue
#define C_WHITE   0xFFFF
#define C_GRAY    0x6B4D   // #696969
#define C_GREEN   0x07E0
#define C_RED     0xF800
#define C_CYAN    0x07FF
#define C_YELLOW  0xFFE0
#define C_ORANGE  0xFC60

// ─────────────────────────────────────────────────────────────────────
// LAYOUT
// ─────────────────────────────────────────────────────────────────────
#define SCR_W   320
#define SCR_H   240
#define HDR_H    44

// Card geometry (landscape 320×240)
#define IP_X     6
#define IP_Y    (HDR_H + 4)
#define IP_W    (SCR_W - 12)
#define IP_H     62

#define R2_Y    (IP_Y + IP_H + 4)   // 114
#define R2_H     62
#define R3_Y    (R2_Y + R2_H + 4)   // 180
#define R3_H     48

#define CL_X     6    // left column x
#define CL_W   152    // left column width
#define CR_X   162    // right column x
#define CR_W   152    // right column width

// ─────────────────────────────────────────────────────────────────────
// APP CONSTANTS
// ─────────────────────────────────────────────────────────────────────
#define AP_SSID          "LOVECLM-DYN-Updater"
#define MDNS_NAME        "LOVECLM-DYN-Updater"
#define AP_PASS          "12345678"
#define DYNDNS_SERVER    "members.dyndns.org"
#define IP_CHECK_URL     "http://api.ipify.org"
#define USER_AGENT       "ESP32DynUpdater/1.0 sbhan@esp32"
#define DEFAULT_INTERVAL 300    // seconds
#define MIN_INTERVAL      60

// ─────────────────────────────────────────────────────────────────────
// OBJECTS
// ─────────────────────────────────────────────────────────────────────
SPIClass         hspi(HSPI);
SPIClass         touchSPI(VSPI);
Adafruit_ILI9341 tft(&hspi, TFT_DC, TFT_CS, TFT_RST);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
WebServer        server(80);
Preferences      prefs;

// ─────────────────────────────────────────────────────────────────────
// CONFIG  (saved to NVS flash)
// ─────────────────────────────────────────────────────────────────────
struct Config {
  char wifiSSID[64];
  char wifiPass[64];
  char dynUser[64];
  char dynPass[64];
  char dynHost[128];
  int  intervalSec;
  int  screenRotation;  // 1 = normal landscape, 3 = 180° flipped
} cfg;

// ─────────────────────────────────────────────────────────────────────
// RUNTIME STATE
// ─────────────────────────────────────────────────────────────────────
struct AppState {
  String        publicIP;
  String        lastSentIP;    // last IP confirmed sent to DynDNS
  String        dynIP;         // IP currently registered on DynDNS (DNS lookup)
  String        lastStatus;
  String        lastUpdateTime;
  bool          wifiOK;
  bool          apMode;
  bool          updating;
  unsigned long lastUpdateMs;
} st;

unsigned long lastDispMs = 0;

// ─────────────────────────────────────────────────────────────────────
// HTML PAGE  (stored in flash via PROGMEM)
// ─────────────────────────────────────────────────────────────────────
static const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>DYN Updater by LOVECLM.COM</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{background:#0f172a;color:#e2e8f0;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;min-height:100vh}
.hdr{background:linear-gradient(135deg,#1e3a5f 0%,#0f172a 100%);border-bottom:1px solid #1e40af;padding:14px 20px;display:flex;align-items:center;gap:12px}
.logo{width:38px;height:38px;background:linear-gradient(135deg,#3b82f6,#8b5cf6);border-radius:10px;display:flex;align-items:center;justify-content:center;font-size:20px;flex-shrink:0}
h1{font-size:1.25rem;font-weight:700}
.sub{color:#64748b;font-size:.72rem;margin-top:1px}
nav{margin-left:auto;display:flex;gap:6px;flex-shrink:0}
nav button{padding:7px 14px;border-radius:8px;border:1px solid #1e40af;background:transparent;color:#94a3b8;cursor:pointer;font-size:.83rem;transition:all .2s}
nav button.act,nav button:hover{background:#1e40af;color:#fff}
.con{max-width:860px;margin:20px auto;padding:0 16px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(175px,1fr));gap:12px;margin-bottom:14px}
.card{background:#1e293b;border:1px solid #334155;border-radius:14px;padding:18px}
.lbl{color:#64748b;font-size:.68rem;text-transform:uppercase;letter-spacing:.1em;margin-bottom:7px}
.val{font-size:1.35rem;font-weight:700;color:#f1f5f9;font-family:'Courier New',monospace}
.badge{display:inline-flex;align-items:center;gap:5px;padding:3px 11px;border-radius:20px;font-size:.78rem;font-weight:500;margin-top:2px}
.b-ok{background:rgba(34,197,94,.15);color:#4ade80;border:1px solid rgba(34,197,94,.3)}
.b-err{background:rgba(239,68,68,.15);color:#f87171;border:1px solid rgba(239,68,68,.3)}
.b-warn{background:rgba(245,158,11,.15);color:#fbbf24;border:1px solid rgba(245,158,11,.3)}
.b-inf{background:rgba(59,130,246,.15);color:#60a5fa;border:1px solid rgba(59,130,246,.3)}
.dot{width:7px;height:7px;border-radius:50%;background:currentColor;flex-shrink:0}
.pulse{animation:pulse 2s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.3}}
.btn{padding:9px 18px;border-radius:10px;border:none;cursor:pointer;font-size:.85rem;font-weight:500;transition:all .2s;display:inline-flex;align-items:center;gap:7px}
.btn-p{background:linear-gradient(135deg,#2563eb,#7c3aed);color:#fff}
.btn-p:hover{opacity:.88;transform:translateY(-1px);box-shadow:0 4px 14px rgba(37,99,235,.45)}
.btn-p:disabled{opacity:.5;transform:none;cursor:not-allowed}
.btn-s{background:#1e293b;color:#94a3b8;border:1px solid #334155}
.btn-s:hover{border-color:#475569;color:#e2e8f0}
.sec{font-size:.92rem;font-weight:600;color:#94a3b8;margin:20px 0 10px;display:flex;align-items:center;gap:8px}
.sec::after{content:'';flex:1;height:1px;background:#1e293b}
.fg{margin-bottom:14px}
label{display:block;font-size:.8rem;color:#94a3b8;margin-bottom:5px}
input{width:100%;padding:9px 12px;background:#0f172a;border:1px solid #334155;border-radius:9px;color:#e2e8f0;font-size:.9rem;outline:none;transition:border .2s}
input:focus{border-color:#3b82f6;box-shadow:0 0 0 3px rgba(59,130,246,.15)}
input[type=number]{width:110px}
.fr{display:grid;grid-template-columns:1fr 1fr;gap:14px}
.hero{background:linear-gradient(135deg,#1e3a5f,#1e1b4b);border:1px solid #3b82f6;border-radius:16px;padding:24px 20px;margin-bottom:14px;text-align:center;position:relative;overflow:hidden}
.hero::before{content:'';position:absolute;top:-40px;right:-40px;width:120px;height:120px;background:rgba(59,130,246,.08);border-radius:50%}
.hero .hl{color:#60a5fa;font-size:.7rem;letter-spacing:.15em;text-transform:uppercase;margin-bottom:8px}
.hero .hv{font-size:2rem;font-weight:800;color:#fff;font-family:'Courier New',monospace;letter-spacing:.06em;margin-bottom:8px}
.hero .hs{color:#64748b;font-size:.8rem}
.hero .hs a{color:#60a5fa;text-decoration:none}
.pb-bg{height:5px;background:#0f172a;border-radius:3px;margin-top:12px;overflow:hidden}
.pb{height:100%;background:linear-gradient(90deg,#2563eb,#7c3aed);border-radius:3px;transition:width 1s linear;min-width:4px}
.spin{display:inline-block;width:14px;height:14px;border:2px solid rgba(255,255,255,.25);border-top-color:#fff;border-radius:50%;animation:spin .75s linear infinite}
@keyframes spin{to{transform:rotate(360deg)}}
.note{background:rgba(245,158,11,.08);border:1px solid rgba(245,158,11,.2);border-radius:9px;padding:10px 14px;font-size:.8rem;color:#fbbf24;margin-top:10px;line-height:1.5}
.toast{position:fixed;bottom:22px;right:22px;background:#1e293b;border:1px solid #334155;border-radius:12px;padding:12px 18px;font-size:.85rem;transform:translateY(120px);opacity:0;transition:all .3s;z-index:999;max-width:300px}
.toast.show{transform:translateY(0);opacity:1}
.toast.tok{border-color:#16a34a;color:#4ade80}
.toast.terr{border-color:#dc2626;color:#f87171}
.info-row{display:flex;justify-content:space-between;align-items:center;margin-top:6px;font-size:.75rem;color:#64748b}
.actions{display:flex;gap:10px;margin-top:14px;flex-wrap:wrap}
</style></head><body>
<div class="hdr">
  <div class="logo">&#127760;</div>
  <div><h1>DYN Updater</h1><div class="sub">by LOVECLM.COM &mdash; ESP32 DNS Client</div></div>
  <nav>
    <button class="act" onclick="pg('dash')">Dashboard</button>
    <button onclick="pg('set')">Settings</button>
  </nav>
</div>
<div class="con">
  <!-- DASHBOARD -->
  <div id="pdash">
    <div class="hero">
      <div class="hl">Your Public IP Address</div>
      <div class="hv" id="pip">&#8212;</div>
      <div class="hs">Access this device at&nbsp;<a id="dlink" href="#">loading&hellip;</a></div>
    </div>
    <div class="grid">
      <div class="card">
        <div class="lbl">Hostname</div>
        <div style="font-size:.95rem;font-weight:600;color:#f1f5f9;word-break:break-all" id="phost">&#8212;</div>
      </div>
      <div class="card">
        <div class="lbl">Update Status</div>
        <div id="pbadge">&#8212;</div>
        <div class="info-row"><span id="ptime">Never updated</span></div>
      </div>
      <div class="card">
        <div class="lbl">Next Update</div>
        <div class="val" id="pnext">&#8212;</div>
        <div class="pb-bg"><div class="pb" id="ppb" style="width:0%"></div></div>
      </div>
      <div class="card">
        <div class="lbl">Interval</div>
        <div class="val" id="pival">&#8212;</div>
      </div>
    </div>
    <div class="actions">
      <button class="btn btn-p" onclick="manualUpd()" id="ubtn">&#8635;&nbsp;Update Now</button>
      <button class="btn btn-s" onclick="loadSt()">&#8635;&nbsp;Refresh</button>
    </div>
  </div>
  <!-- SETTINGS -->
  <div id="pset" style="display:none">
    <div class="sec">DynDNS Credentials</div>
    <div class="card">
      <div class="fg"><label>Hostname</label><input id="shost" placeholder="myhost.dyndns.org"></div>
      <div class="fr">
        <div class="fg"><label>Username</label><input id="suser" placeholder="your-dyndns-username"></div>
        <div class="fg"><label>Password</label><div style="position:relative"><input type="password" id="spass" placeholder="&#8226;&#8226;&#8226;&#8226;&#8226;&#8226;&#8226;&#8226;" style="padding-right:38px"><button type="button" onclick="togglePwd('spass',this)" style="position:absolute;right:10px;top:50%;transform:translateY(-50%);background:none;border:none;cursor:pointer;color:#64748b;font-size:1.1rem;line-height:1">&#128065;</button></div></div>
      </div>
    </div>
    <div class="sec">Update Schedule</div>
    <div class="card">
      <div class="fg">
        <label>Interval (minutes)</label>
        <input type="number" id="sival" min="1" max="1440" value="5">
        <div style="color:#64748b;font-size:.75rem;margin-top:6px">Minimum 1 min &mdash; DynDNS recommends &ge;5 min to avoid abuse blocks.</div>
      </div>
    </div>
    <div class="sec">WiFi Network</div>
    <div class="card">
      <div class="fg">
        <label>Network (SSID)</label>
        <div style="display:flex;gap:8px;align-items:center;margin-bottom:8px">
          <button class="btn btn-s" onclick="scanWifi()" id="scan-btn" type="button" style="padding:6px 12px;font-size:.8rem;flex-shrink:0">&#128225; Scan</button>
          <span id="scan-st" style="color:#64748b;font-size:.78rem"></span>
        </div>
        <select id="sssid-sel" style="width:100%;padding:9px 12px;background:#0f172a;border:1px solid #334155;border-radius:9px;color:#e2e8f0;font-size:.9rem;outline:none;margin-bottom:8px" onchange="selSSID()">
          <option value="">-- Tap Scan to search networks --</option>
        </select>
        <label style="margin-top:4px">Or type SSID manually</label>
        <input id="sssid" placeholder="Network name">
      </div>
      <div class="fg"><label>Password</label><div style="position:relative"><input type="password" id="swpass" placeholder="&#8226;&#8226;&#8226;&#8226;&#8226;&#8226;&#8226;&#8226;" style="padding-right:38px"><button type="button" onclick="togglePwd('swpass',this)" style="position:absolute;right:10px;top:50%;transform:translateY(-50%);background:none;border:none;cursor:pointer;color:#64748b;font-size:1.1rem;line-height:1">&#128065;</button></div></div>
      <div class="note">&#9888; Changing the SSID restarts the device. Leave password blank to keep the existing one.</div>
    </div>
    <div class="actions" style="margin-top:18px">
      <button class="btn btn-p" onclick="saveCfg()">&#10003;&nbsp;Save Settings</button>
      <button class="btn btn-s" onclick="loadCfg()">Reset</button>
    </div>
  </div>
</div>
<div class="toast" id="toast"></div>
<script>
var itv=300,cd,curPage='dash';
function pg(p){
  document.getElementById('pdash').style.display=p==='dash'?'':'none';
  document.getElementById('pset').style.display=p==='set'?'':'none';
  document.querySelectorAll('nav button').forEach(function(b,i){b.classList.toggle('act',(p==='dash'&&i===0)||(p==='set'&&i===1));});
  curPage=p;
  if(p==='set')loadCfg();else loadSt();
}
function toast(m,t){
  var e=document.getElementById('toast');
  e.textContent=m;e.className='toast '+(t||'')+' show';
  setTimeout(function(){e.className='toast '+(t||'');},3400);
}
function badge(s){
  if(!s)return '<span class="badge b-inf"><span class="dot"></span>Pending</span>';
  if(s.indexOf('good')===0)return '<span class="badge b-ok"><span class="dot pulse"></span>Updated</span>';
  if(s.indexOf('nochg')===0)return '<span class="badge b-inf"><span class="dot"></span>No Change</span>';
  if(s==='badauth')return '<span class="badge b-err"><span class="dot"></span>Bad Credentials</span>';
  if(s==='nohost')return '<span class="badge b-err"><span class="dot"></span>Host Not Found</span>';
  if(s==='abuse')return '<span class="badge b-warn"><span class="dot"></span>Account Blocked</span>';
  if(s.indexOf('err')===0)return '<span class="badge b-err"><span class="dot"></span>Connection Error</span>';
  return '<span class="badge b-warn"><span class="dot"></span>'+s+'</span>';
}
function fmtS(s){if(s<=0)return'Due';var m=Math.floor(s/60),r=s%60;return m>0?m+'m '+r+'s':r+'s';}
function fmtI(s){var m=Math.floor(s/60);return m+' min'+(m!==1?'s':'');}
function startCd(rem,tot){
  clearInterval(cd);var r=Math.max(0,rem);
  function tick(){
    document.getElementById('pnext').textContent=fmtS(r);
    document.getElementById('ppb').style.width=(tot>0?Math.max(0,(1-r/tot)*100):100)+'%';
    if(r>0)r--;else{clearInterval(cd);if(curPage==='dash')loadSt();}
  }
  tick();cd=setInterval(tick,1000);
}
async function loadSt(){
  try{
    var r=await fetch('/api/status'),d=await r.json();
    document.getElementById('pip').textContent=d.ip||'—';
    document.getElementById('phost').textContent=d.hostname||'Not configured';
    document.getElementById('pbadge').innerHTML=badge(d.lastStatus||'');
    document.getElementById('ptime').textContent=d.lastUpdateTime?'Updated: '+d.lastUpdateTime:'Never updated';
    document.getElementById('pival').textContent=fmtI(d.intervalSec||300);
    itv=d.intervalSec||300;
    startCd(d.nextUpdateIn||0,itv);
    var ip=d.deviceIP||location.hostname;
    var lnk=document.getElementById('dlink');
    lnk.href='http://'+ip;lnk.textContent=ip;
  }catch(e){console.warn('status err',e);}
}
async function loadCfg(){
  try{
    var r=await fetch('/api/config'),d=await r.json();
    document.getElementById('shost').value=d.dynHost||'';
    document.getElementById('suser').value=d.dynUser||'';
    document.getElementById('spass').value='';
    document.getElementById('sival').value=Math.round((d.intervalSec||300)/60);
    document.getElementById('sssid').value=d.wifiSSID||'';
    document.getElementById('swpass').value='';
  }catch(e){}
}
async function saveCfg(){
  var iv=parseInt(document.getElementById('sival').value)*60;
  if(iv<60){toast('Interval must be at least 1 minute','terr');return;}
  var b={dynHost:document.getElementById('shost').value,dynUser:document.getElementById('suser').value,dynPass:document.getElementById('spass').value,intervalSec:iv,wifiSSID:document.getElementById('sssid').value,wifiPass:document.getElementById('swpass').value};
  try{
    var r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)}),d=await r.json();
    if(d.ok)toast('Settings saved!','tok');else toast('Error: '+(d.error||'unknown'),'terr');
  }catch(e){toast('Save failed — device may be restarting','terr');}
}
async function manualUpd(){
  var btn=document.getElementById('ubtn');
  btn.innerHTML='<span class="spin"></span> Updating&hellip;';btn.disabled=true;
  try{
    var r=await fetch('/api/update',{method:'POST'}),d=await r.json();
    toast(d.result||'Update triggered',d.ok?'tok':'terr');
    setTimeout(loadSt,2500);
  }catch(e){toast('Update failed','terr');}
  setTimeout(function(){btn.innerHTML='&#8635;&nbsp;Update Now';btn.disabled=false;},3000);
}
function togglePwd(id,btn){var i=document.getElementById(id),show=i.type==='password';i.type=show?'text':'password';btn.innerHTML=show?'&#128584;':'&#128065;';}
function selSSID(){var v=document.getElementById('sssid-sel').value;if(v)document.getElementById('sssid').value=v;}
async function scanWifi(){
  var btn=document.getElementById('scan-btn'),st=document.getElementById('scan-st');
  btn.innerHTML='<span class="spin"></span>';btn.disabled=true;st.textContent='Scanning… (5-10s)';
  try{
    var r=await fetch('/api/scan'),nets=await r.json();
    var sel=document.getElementById('sssid-sel');
    sel.innerHTML='<option value="">-- Select network --</option>';
    nets.sort(function(a,b){return b.rssi-a.rssi;});
    nets.forEach(function(n){
      var bars=n.rssi>-60?'████':n.rssi>-70?'███░':n.rssi>-80?'██░░':'█░░░';
      var opt=document.createElement('option');
      opt.value=n.ssid;opt.textContent=n.ssid+'  '+bars+(n.secure?' 🔒':'');
      sel.appendChild(opt);
    });
    st.textContent=nets.length+' network'+(nets.length!==1?'s':'')+' found';
  }catch(e){st.textContent='Scan failed — try again';}
  btn.innerHTML='&#128225; Scan';btn.disabled=false;
}
loadSt();
setInterval(function(){if(curPage==='dash')loadSt();},30000);
</script>
</body></html>
)rawliteral";

// ─────────────────────────────────────────────────────────────────────
// FORWARD DECLARATIONS
// ─────────────────────────────────────────────────────────────────────
void loadConfig(); void saveConfig();
bool connectWiFi(); void startAP();
void doDynUpdate(); void resolveHostIP(); String getPublicIP(); String sendDynRequest(const String&);
void displaySplash(); void displayConnecting(); void displayAPMode();
void displayMain(); void updateCountdownArea();
void drawGrad(int,int,int,int,uint16_t,uint16_t);
void drawCard(int,int,int,int,uint16_t);
void drawLabel(int,int,const char*,uint16_t);
void drawVal(int,int,const char*,uint8_t,uint16_t);
void drawCentered(int,int,const char*,uint8_t,uint16_t);
void drawWifiSignal(int,int,bool);
void drawRotateBtn(int,int);
void handleWifiScan();
void checkTouch(); void cycleInterval(); void cycleRotation(); void showUpdateWarning(int);
void drawStatusBadge(int,int,const String&);
void handleRoot(); void handleStatus(); void handleGetConfig();
void handlePostConfig(); void handleUpdate(); void handleNotFound();
void sendJSON(const String&, int code=200);
String jsonGetStr(const String&, const String&);
int   jsonGetInt(const String&, const String&, int);

// ─────────────────────────────────────────────────────────────────────
// SETUP
// ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  if (TFT_BL >= 0) { pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH); }
  hspi.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin(40000000);
  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(touchSPI);

  // Load config first so saved rotation is applied before any screen draw
  loadConfig();
  tft.setRotation(cfg.screenRotation);

  tft.fillScreen(C_BG);
  displaySplash();
  delay(1800);
  displayConnecting();

  if (strlen(cfg.wifiSSID) > 0 && connectWiFi()) {
    st.wifiOK = true;
    st.apMode = false;
    configTime(0, 0, "pool.ntp.org");
    MDNS.begin(MDNS_NAME);
    MDNS.addService("http", "tcp", 80);
    resolveHostIP();
  } else {
    st.wifiOK = false;
    st.apMode = true;
    startAP();
  }

  server.on("/",           HTTP_GET,  handleRoot);
  server.on("/api/status", HTTP_GET,  handleStatus);
  server.on("/api/config", HTTP_GET,  handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/update", HTTP_POST, handleUpdate);
  server.on("/api/scan",   HTTP_GET,  handleWifiScan);
  server.onNotFound(handleNotFound);
  server.begin();

  if (st.apMode) displayAPMode();
  else           displayMain();
}

// ─────────────────────────────────────────────────────────────────────
// LOOP
// ─────────────────────────────────────────────────────────────────────
void loop() {
  server.handleClient();

  unsigned long now = millis();

  // Monitor WiFi and reconnect if dropped
  if (!st.apMode) {
    bool linked = (WiFi.status() == WL_CONNECTED);
    if (linked && !st.wifiOK) {
      st.wifiOK = true;
      MDNS.begin(MDNS_NAME);
      displayMain();
    } else if (!linked && st.wifiOK) {
      st.wifiOK = false;
      displayMain();          // redraws with red bars
      WiFi.reconnect();
    }
  }

  // DynDNS periodic update
  if (!st.apMode && st.wifiOK && !st.updating) {
    unsigned long intervalMs = (unsigned long)cfg.intervalSec * 1000UL;
    if (st.lastUpdateMs == 0 || (now - st.lastUpdateMs) >= intervalMs) {
      doDynUpdate();
    }
  }

  // Refresh countdown every second
  if (!st.apMode && !st.updating && (now - lastDispMs >= 1000)) {
    lastDispMs = now;
    updateCountdownArea();
  }

  // Touch input
  if (!st.updating) checkTouch();
}

// ─────────────────────────────────────────────────────────────────────
// CONFIG
// ─────────────────────────────────────────────────────────────────────
void loadConfig() {
  prefs.begin("dyn", true);
  strlcpy(cfg.wifiSSID, prefs.getString("ssid", "").c_str(), sizeof(cfg.wifiSSID));
  strlcpy(cfg.wifiPass, prefs.getString("pass", "").c_str(), sizeof(cfg.wifiPass));
  strlcpy(cfg.dynUser,  prefs.getString("user", "").c_str(), sizeof(cfg.dynUser));
  strlcpy(cfg.dynPass,  prefs.getString("dynp", "").c_str(), sizeof(cfg.dynPass));
  strlcpy(cfg.dynHost,  prefs.getString("host", "").c_str(), sizeof(cfg.dynHost));
  cfg.intervalSec    = prefs.getInt("ival", DEFAULT_INTERVAL);
  if (cfg.intervalSec < MIN_INTERVAL) cfg.intervalSec = MIN_INTERVAL;
  cfg.screenRotation = prefs.getInt("rot",  1);
  if (cfg.screenRotation != 1 && cfg.screenRotation != 3) cfg.screenRotation = 1;
  prefs.end();
}

void saveConfig() {
  prefs.begin("dyn", false);
  prefs.putString("ssid", cfg.wifiSSID);
  prefs.putString("pass", cfg.wifiPass);
  prefs.putString("user", cfg.dynUser);
  prefs.putString("dynp", cfg.dynPass);
  prefs.putString("host", cfg.dynHost);
  prefs.putInt("ival", cfg.intervalSec);
  prefs.putInt("rot",  cfg.screenRotation);
  prefs.end();
}

// ─────────────────────────────────────────────────────────────────────
// WIFI
// ─────────────────────────────────────────────────────────────────────
bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.wifiSSID, cfg.wifiPass);
  for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) delay(500);
  return WiFi.status() == WL_CONNECTED;
}

void startAP() {
  WiFi.mode(WIFI_AP_STA);   // AP + STA so WiFi scanning still works
  WiFi.softAP(AP_SSID, AP_PASS);
}

// ─────────────────────────────────────────────────────────────────────
// DYNDNS
// ─────────────────────────────────────────────────────────────────────
String getPublicIP() {
  HTTPClient http;
  http.begin(IP_CHECK_URL);
  http.setTimeout(10000);
  String ip = "";
  if (http.GET() == 200) ip = http.getString();
  http.end();
  ip.trim();
  return ip;
}

String sendDynRequest(const String& ip) {
  WiFiClientSecure client;
  client.setInsecure();

  String url = "https://";
  url += DYNDNS_SERVER;
  url += "/nic/update?hostname=";
  url += String(cfg.dynHost);
  url += "&myip=";
  url += ip;

  HTTPClient http;
  http.begin(client, url);
  http.setAuthorization(cfg.dynUser, cfg.dynPass);
  http.addHeader("User-Agent", USER_AGENT);
  http.setTimeout(15000);

  String resp = "";
  int code = http.GET();
  if (code > 0) { resp = http.getString(); resp.trim(); }
  else resp = "err:" + String(code);
  http.end();
  return resp;
}

void resolveHostIP() {
  if (!st.wifiOK || strlen(cfg.dynHost) == 0) { st.dynIP = ""; return; }
  IPAddress addr;
  st.dynIP = WiFi.hostByName(cfg.dynHost, addr) ? addr.toString() : "";
}

void doDynUpdate() {
  if (strlen(cfg.dynHost) == 0 || strlen(cfg.dynUser) == 0) return;

  st.updating = true;

  // Show "Updating…" overlay on IP card
  tft.fillRoundRect(IP_X, IP_Y, IP_W, IP_H, 6, tft.color565(10, 25, 70));
  tft.drawRoundRect(IP_X, IP_Y, IP_W, IP_H, 6, C_ACCENT);
  drawCentered(SCR_W/2, IP_Y + 10, "CONTACTING DYNDNS", 1, C_GRAY);
  drawCentered(SCR_W/2, IP_Y + 28, "Please wait...", 2, C_CYAN);

  String ip = getPublicIP();
  if (ip.length() == 0) {
    st.lastStatus = "err:no-ip";
    st.updating = false;
    displayMain();
    return;
  }
  st.publicIP = ip;

  // Skip DynDNS call if IP hasn't changed since last confirmed send
  if (ip == st.lastSentIP) {
    st.lastStatus   = "no-change";
    st.lastUpdateMs = millis();
    st.updating = false;
    displayMain();
    return;
  }

  String result = sendDynRequest(ip);
  st.lastStatus   = result;
  st.lastUpdateMs = millis();
  // Track IP we successfully sent so we can skip next time if unchanged.
  // Set dynIP directly from the confirmed response — DNS lookup would return a
  // stale cached value until TTL expires, so trust the API answer instead.
  if (result.startsWith("good") || result.startsWith("nochg")) {
    st.lastSentIP = ip;
    st.dynIP      = ip;
  }

  // Timestamp
  time_t now; struct tm ti; time(&now);
  localtime_r(&now, &ti);
  char buf[24];
  if (now > 1000000000UL) strftime(buf, sizeof(buf), "%H:%M  %d/%m/%y", &ti);
  else snprintf(buf, sizeof(buf), "+%lus", millis()/1000);
  st.lastUpdateTime = String(buf);

  st.updating = false;
  displayMain();

  // Flash status card border to signal result
  bool ok = result.startsWith("good") || result.startsWith("nochg");
  uint16_t flash = ok ? C_GREEN : C_RED;
  for (int i = 0; i < 3; i++) {
    tft.drawRoundRect(CR_X, R2_Y, CR_W, R2_H, 6, flash);
    delay(180);
    tft.drawRoundRect(CR_X, R2_Y, CR_W, R2_H, 6, C_BORDER);
    delay(180);
  }
}

// ─────────────────────────────────────────────────────────────────────
// DISPLAY HELPERS
// ─────────────────────────────────────────────────────────────────────
void drawGrad(int x, int y, int w, int h, uint16_t c1, uint16_t c2) {
  int r1=(c1>>11)&0x1F, g1=(c1>>5)&0x3F, b1=c1&0x1F;
  int r2=(c2>>11)&0x1F, g2=(c2>>5)&0x3F, b2=c2&0x1F;
  for (int i = 0; i < h; i++) {
    int r=r1+(r2-r1)*i/h, g=g1+(g2-g1)*i/h, b=b1+(b2-b1)*i/h;
    tft.drawFastHLine(x, y+i, w, ((uint16_t)r<<11)|((uint16_t)g<<5)|b);
  }
}

void drawCard(int x, int y, int w, int h, uint16_t border = C_BORDER) {
  tft.fillRoundRect(x, y, w, h, 6, C_CARD);
  tft.drawRoundRect(x, y, w, h, 6, border);
}

void drawLabel(int x, int y, const char* text, uint16_t color = C_GRAY) {
  tft.setTextColor(color); tft.setTextSize(1); tft.setCursor(x, y); tft.print(text);
}

void drawVal(int x, int y, const char* text, uint8_t sz = 2, uint16_t color = C_WHITE) {
  tft.setTextColor(color); tft.setTextSize(sz); tft.setCursor(x, y); tft.print(text);
}

void drawCentered(int cx, int y, const char* text, uint8_t sz, uint16_t color) {
  tft.setTextSize(sz); tft.setTextColor(color);
  int16_t x1, y1; uint16_t tw, th;
  tft.getTextBounds(text, 0, y, &x1, &y1, &tw, &th);
  tft.setCursor(cx - tw/2, y); tft.print(text);
}

void drawWifiSignal(int x, int y, bool connected) {
  // Small badge: dark bg, coloured border, centered "XX%" or "---"
  const int W = 38, H = 16;
  tft.fillRoundRect(x, y, W, H, 3, tft.color565(5, 10, 35));
  if (!connected) {
    tft.drawRoundRect(x, y, W, H, 3, C_RED);
    tft.setTextSize(1); tft.setTextColor(C_RED);
    tft.setCursor(x + 8, y + 4); tft.print("---");
    return;
  }
  int rssi = WiFi.RSSI();
  int pct  = constrain(2 * (rssi + 100), 0, 100);
  uint16_t col = pct > 65 ? C_GREEN : pct > 35 ? C_YELLOW : C_RED;
  tft.drawRoundRect(x, y, W, H, 3, col);
  char buf[6]; snprintf(buf, sizeof(buf), "%d%%", pct);
  tft.setTextSize(1); tft.setTextColor(col);
  int16_t x1, y1; uint16_t tw, th;
  tft.getTextBounds(buf, 0, 0, &x1, &y1, &tw, &th);
  tft.setCursor(x + (W - tw) / 2, y + (H - th) / 2);
  tft.print(buf);
}

void drawRotateBtn(int x, int y) {
  tft.fillRoundRect(x, y, 32, 18, 5, tft.color565(10, 30, 80));
  tft.drawRoundRect(x, y, 32, 18, 5, tft.color565(50, 100, 200));

  int cx = x + 16, cy = y + 9;

  // 3/4 circle ↻ arc (9 segments, 30° steps): lower-right → bottom → left → top → upper-right
  // Gap at right side; arrowhead at upper-right continues clockwise direction
  const int8_t px[] = { 5,  2, -2, -5, -7, -7, -5, -2,  2,  5};
  const int8_t py[] = { 5,  7,  7,  5,  2, -2, -5, -7, -7, -5};
  for (int i = 0; i < 9; i++) {
    tft.drawLine(cx+px[i], cy+py[i], cx+px[i+1], cy+py[i+1], C_WHITE);
  }
  // Arrowhead at upper-right (arc end), pointing right = clockwise direction
  tft.fillTriangle(cx+8, cy-5, cx+5, cy-3, cx+5, cy-7, C_WHITE);
}

void drawStatusBadge(int x, int y, const String& s) {
  uint16_t col; const char* lbl;
  if (s.startsWith("good"))      { col = C_GREEN;  lbl = "UPDATED"; }
  else if (s == "no-change")     { col = C_CYAN;   lbl = "NO CHANGE"; }
  else if (s.startsWith("nochg")){ col = C_CYAN;   lbl = "NO CHANGE"; }
  else if (s == "badauth")       { col = C_RED;    lbl = "BAD AUTH"; }
  else if (s == "nohost")        { col = C_RED;    lbl = "NO HOST"; }
  else if (s == "abuse")         { col = C_ORANGE; lbl = "BLOCKED"; }
  else if (s.startsWith("err"))  { col = C_RED;    lbl = "ERROR"; }
  else if (s.length() == 0)      { col = C_GRAY;   lbl = "PENDING"; }
  else                           { col = C_YELLOW; lbl = s.c_str(); }

  const int BW=96, BH=20;
  tft.drawRoundRect(x, y, BW, BH, 4, col);
  tft.setTextSize(1); tft.setTextColor(col);
  int16_t x1,y1; uint16_t tw,th;
  tft.getTextBounds(lbl, 0, 0, &x1, &y1, &tw, &th);
  tft.setCursor(x + (BW-tw)/2, y + (BH-th)/2); tft.print(lbl);
}

// ─────────────────────────────────────────────────────────────────────
// DISPLAY SCREENS
// ─────────────────────────────────────────────────────────────────────
void displaySplash() {
  tft.fillScreen(C_BG);
  drawGrad(0, 0, SCR_W, SCR_H, tft.color565(8,28,90), tft.color565(4,8,25));

  // Globe icon
  tft.fillCircle(160, 88, 40, tft.color565(18,52,160));
  tft.drawCircle(160, 88, 40, C_ACCENT);
  tft.drawCircle(160, 88, 42, tft.color565(50,90,200));
  tft.drawCircle(160, 88, 22, C_WHITE);
  tft.drawFastVLine(160, 48, 80, C_WHITE);
  tft.drawFastHLine(120, 88, 80, C_WHITE);
  tft.drawCircle(160, 88, 11, C_WHITE);

  drawCentered(160, 140, "DYN Updater", 2, C_WHITE);
  drawCentered(160, 162, "by LOVECLM.COM", 1, C_CYAN);
  drawCentered(160, 176, "for ESP32", 1, C_GRAY);
  drawCentered(160, 196, "Starting up...", 1, C_ACCENT);
}

void displayConnecting() {
  tft.fillScreen(C_BG);
  drawGrad(0, 0, SCR_W, HDR_H, tft.color565(10,40,120), tft.color565(8,16,48));
  drawCentered(160, 14, "DYN Updater by LOVECLM.COM", 1, C_WHITE);

  drawCard(20, HDR_H+16, SCR_W-40, 100, C_ACCENT);
  drawCentered(160, HDR_H+28, "Connecting to WiFi", 1, C_GRAY);

  char ssidLine[70];
  snprintf(ssidLine, sizeof(ssidLine), "%.60s", cfg.wifiSSID[0] ? cfg.wifiSSID : "(no SSID set)");
  drawCentered(160, HDR_H+44, ssidLine, 1, C_WHITE);

  // Animated dots
  for (int i = 0; i < 3; i++) {
    tft.fillCircle(148 + i*14, HDR_H+82, 5, C_ACCENT);
    delay(80);
  }
}

void displayAPMode() {
  tft.fillScreen(C_BG);
  drawGrad(0, 0, SCR_W, HDR_H, tft.color565(80,40,10), tft.color565(30,14,4));
  drawCentered(160, 14, "Setup Mode", 2, C_YELLOW);

  drawCard(8, HDR_H+4, SCR_W-16, SCR_H-HDR_H-8, C_ORANGE);
  drawCentered(160, HDR_H+10, "1. Connect phone/PC to:", 1, C_GRAY);

  drawCard(28, HDR_H+22, SCR_W-56, 30, C_ACCENT);
  drawCentered(160, HDR_H+28, AP_SSID, 2, C_WHITE);

  char passLine[48];
  snprintf(passLine, sizeof(passLine), "Password: %s", AP_PASS);
  drawCentered(160, HDR_H+58, passLine, 1, C_GRAY);

  String ip = WiFi.softAPIP().toString();
  char step2[48];
  snprintf(step2, sizeof(step2), "2. Open browser: %s", ip.c_str());
  drawCentered(160, HDR_H+76, step2, 1, C_CYAN);

  drawCentered(160, HDR_H+96,  "3. Settings > Enter WiFi +", 1, C_GRAY);
  drawCentered(160, HDR_H+110, "   DynDNS credentials > Save", 1, C_GRAY);
  drawCentered(160, HDR_H+126, "Device will restart & connect", 1, C_CYAN);
}

void displayMain() {
  tft.fillScreen(C_BG);

  // ── Header ──────────────────────────────────────────────────────────
  drawGrad(0, 0, SCR_W, HDR_H, tft.color565(10,40,120), tft.color565(8,16,48));
  drawVal(10, 10, "DYN Updater", 2, C_WHITE);
  drawVal(10, 30, "by LOVECLM.COM", 1, C_CYAN);
  drawRotateBtn(234, 14);
  drawWifiSignal(274, 14, st.wifiOK);

  // ── IP card ─────────────────────────────────────────────────────────
  drawCard(IP_X, IP_Y, IP_W, IP_H, C_ACCENT);
  drawLabel(IP_X+10, IP_Y+8, "PUBLIC IP", C_GRAY);
  String ip = st.publicIP.length() ? st.publicIP : "Fetching...";
  uint16_t ipColor = st.publicIP.length() ? C_WHITE : C_GRAY;
  drawCentered(SCR_W/2, IP_Y+24, ip.c_str(), 3, ipColor);

  // ── Row 2: Hostname | Status ─────────────────────────────────────────
  drawCard(CL_X, R2_Y, CL_W, R2_H, C_BORDER);
  drawLabel(CL_X+10, R2_Y+8, "HOSTNAME", C_GRAY);
  {
    const int MAX_CHARS = 22;  // size-1 font fits ~22 chars in card width
    String host = String(cfg.dynHost);
    if (host.length() == 0) {
      drawVal(CL_X+10, R2_Y+20, "Not set", 1, C_GRAY);
    } else if (host.length() <= MAX_CHARS) {
      drawVal(CL_X+10, R2_Y+20, host.c_str(), 1, C_WHITE);
    } else {
      // Wrap at a dot boundary for readability
      int cut = host.lastIndexOf('.', MAX_CHARS - 1);
      if (cut <= 0) cut = MAX_CHARS;
      String l1 = host.substring(0, cut);
      String l2 = host.substring(cut);
      if (l2.length() > MAX_CHARS) l2 = l2.substring(0, MAX_CHARS - 1) + "~";
      drawVal(CL_X+10, R2_Y+18, l1.c_str(), 1, C_WHITE);
      drawVal(CL_X+10, R2_Y+30, l2.c_str(), 1, C_GRAY);
    }
    if (st.dynIP.length()) {
      String lbl = "DYN: " + st.dynIP;
      uint16_t col = (st.dynIP == st.publicIP) ? C_GREEN : C_YELLOW;
      drawLabel(CL_X+10, R2_Y+44, lbl.c_str(), col);
    }
  }

  drawCard(CR_X, R2_Y, CR_W, R2_H, C_ACCENT);  // accent = tappable (force update)
  drawLabel(CR_X+10, R2_Y+8, "STATUS", C_GRAY);
  drawStatusBadge(CR_X+10, R2_Y+22, st.lastStatus);

  // ── Row 3: Next Update | Interval ───────────────────────────────────
  drawCard(CL_X, R3_Y, CL_W, R3_H, C_BORDER);
  drawLabel(CL_X+10, R3_Y+6, "NEXT UPDATE", C_GRAY);
  // value drawn by updateCountdownArea()

  drawCard(CR_X, R3_Y, CR_W, R3_H, C_ACCENT);  // accent border = tappable
  drawLabel(CR_X+10, R3_Y+6, "INTERVAL", C_GRAY);
  char ibuf[20];
  int mins = cfg.intervalSec / 60;
  snprintf(ibuf, sizeof(ibuf), "%d min%s", mins, mins==1?"":"s");
  drawVal(CR_X+10, R3_Y+20, ibuf, 2, C_WHITE);

  // ── Footer ──────────────────────────────────────────────────────────
  String devIP = st.wifiOK ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  char foot[48];
  snprintf(foot, sizeof(foot), "Web UI: http://%s", devIP.c_str());
  drawLabel(8, SCR_H-11, foot, C_GRAY);
  drawLabel(SCR_W - 6*strlen(APP_VERSION) - 4, SCR_H-11, APP_VERSION, C_GRAY);

  updateCountdownArea();
}

void updateCountdownArea() {
  if (st.lastUpdateMs == 0) {
    // clear area and show dashes
    tft.fillRect(CL_X+10, R3_Y+18, CL_W-14, 20, C_CARD);
    drawVal(CL_X+10, R3_Y+20, "--:--", 2, C_CYAN);
    return;
  }
  long remMs = (long)((unsigned long)cfg.intervalSec * 1000UL) - (long)(millis() - st.lastUpdateMs);
  if (remMs < 0) remMs = 0;

  int secs = remMs / 1000;
  char buf[10];
  snprintf(buf, sizeof(buf), "%02d:%02d", secs/60, secs%60);

  tft.fillRect(CL_X+10, R3_Y+18, CL_W-14, 20, C_CARD);
  drawVal(CL_X+10, R3_Y+20, buf, 2, C_CYAN);

  // Progress bar at bottom of card
  int barW = CL_W - 18;
  int filled = (cfg.intervalSec > 0)
    ? barW - (int)((long)remMs * barW / ((long)cfg.intervalSec * 1000L))
    : barW;
  tft.fillRect(CL_X+9, R3_Y+R3_H-8, barW, 4, C_BORDER);
  if (filled > 0) tft.fillRect(CL_X+9, R3_Y+R3_H-8, filled, 4, C_ACCENT);
}

// ─────────────────────────────────────────────────────────────────────
// JSON HELPERS
// ─────────────────────────────────────────────────────────────────────
String jsonGetStr(const String& json, const String& key) {
  String search = "\"" + key + "\":\"";
  int pos = json.indexOf(search);
  if (pos < 0) return "";
  pos += search.length();
  int end = pos;
  while (end < (int)json.length() && !(json[end] == '"' && (end == 0 || json[end-1] != '\\'))) end++;
  return json.substring(pos, end);
}

int jsonGetInt(const String& json, const String& key, int def = 0) {
  String search = "\"" + key + "\":";
  int pos = json.indexOf(search);
  if (pos < 0) return def;
  return json.substring(pos + search.length()).toInt();
}

// ─────────────────────────────────────────────────────────────────────
// WEB SERVER HANDLERS
// ─────────────────────────────────────────────────────────────────────
void sendJSON(const String& json, int code) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(code, "application/json", json);
}

void handleNotFound() {
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handleRoot() {
  server.send_P(200, "text/html", HTML_PAGE);
}

void handleStatus() {
  String devIP = st.wifiOK ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  long nextSec = (st.lastUpdateMs == 0) ? 0 :
    max(0L, (long)(cfg.intervalSec) - (long)((millis() - st.lastUpdateMs) / 1000));

  char buf[512];
  snprintf(buf, sizeof(buf),
    "{\"ip\":\"%s\",\"hostname\":\"%s\",\"lastStatus\":\"%s\","
    "\"lastUpdateTime\":\"%s\",\"intervalSec\":%d,\"nextUpdateIn\":%ld,"
    "\"deviceIP\":\"%s\",\"wifiOK\":%s}",
    st.publicIP.c_str(), cfg.dynHost, st.lastStatus.c_str(),
    st.lastUpdateTime.c_str(), cfg.intervalSec, nextSec,
    devIP.c_str(), st.wifiOK ? "true" : "false");
  sendJSON(String(buf));
}

void handleGetConfig() {
  char buf[256];
  snprintf(buf, sizeof(buf),
    "{\"wifiSSID\":\"%s\",\"dynUser\":\"%s\",\"dynHost\":\"%s\",\"intervalSec\":%d}",
    cfg.wifiSSID, cfg.dynUser, cfg.dynHost, cfg.intervalSec);
  sendJSON(String(buf));
}

void handlePostConfig() {
  if (!server.hasArg("plain")) { sendJSON("{\"ok\":false,\"error\":\"No body\"}", 400); return; }
  String body = server.arg("plain");

  String newSSID    = jsonGetStr(body, "wifiSSID");
  String newWPass   = jsonGetStr(body, "wifiPass");
  String newDynHost = jsonGetStr(body, "dynHost");
  String newDynUser = jsonGetStr(body, "dynUser");
  String newDynPass = jsonGetStr(body, "dynPass");
  int    newIval    = jsonGetInt(body, "intervalSec", 0);

  if (newDynHost.length()) strlcpy(cfg.dynHost, newDynHost.c_str(), sizeof(cfg.dynHost));
  if (newDynUser.length()) strlcpy(cfg.dynUser, newDynUser.c_str(), sizeof(cfg.dynUser));
  if (newDynPass.length()) strlcpy(cfg.dynPass, newDynPass.c_str(), sizeof(cfg.dynPass));
  if (newIval >= MIN_INTERVAL) cfg.intervalSec = newIval;

  bool wifiChanged    = newSSID.length() > 0 && newSSID != String(cfg.wifiSSID);
  bool wifiPassChanged = newWPass.length() > 0;

  if (newSSID.length())    strlcpy(cfg.wifiSSID, newSSID.c_str(),  sizeof(cfg.wifiSSID));
  if (newWPass.length())   strlcpy(cfg.wifiPass, newWPass.c_str(), sizeof(cfg.wifiPass));

  saveConfig();

  // Restart to reconnect if: SSID/password changed, OR we're in AP mode
  // with saved credentials (covers "save same SSID with fixed password" case)
  bool shouldRestart = (wifiChanged || wifiPassChanged || st.apMode)
                       && strlen(cfg.wifiSSID) > 0;

  sendJSON("{\"ok\":true}");
  if (shouldRestart) {
    delay(500);
    ESP.restart();
  } else {
    st.lastUpdateMs = 0;
    displayMain();
  }
}

void handleUpdate() {
  if (st.updating) {
    sendJSON("{\"ok\":false,\"result\":\"Already updating\"}");
    return;
  }
  st.lastUpdateMs = 0;  // trigger on next loop cycle
  sendJSON("{\"ok\":true,\"result\":\"Update queued\"}");
}

// ─────────────────────────────────────────────────────────────────────
// TOUCH
// ─────────────────────────────────────────────────────────────────────
void cycleInterval() {
  // Cycle: 5 → 10 → 30 → 60 → 5 min
  if      (cfg.intervalSec <  600) cfg.intervalSec =  600;
  else if (cfg.intervalSec < 1800) cfg.intervalSec = 1800;
  else if (cfg.intervalSec < 3600) cfg.intervalSec = 3600;
  else                             cfg.intervalSec =  300;
  saveConfig();
  st.lastUpdateMs = 0;  // reset countdown

  // Flash card border for feedback
  for (int i = 0; i < 3; i++) {
    tft.drawRoundRect(CR_X, R3_Y, CR_W, R3_H, 6, C_CYAN);
    delay(100);
    tft.drawRoundRect(CR_X, R3_Y, CR_W, R3_H, 6, C_ACCENT);
    delay(100);
  }

  // Redraw interval card with new value (cyan briefly, then white)
  tft.fillRoundRect(CR_X+1, R3_Y+1, CR_W-2, R3_H-2, 5, C_CARD);
  tft.drawRoundRect(CR_X, R3_Y, CR_W, R3_H, 6, C_ACCENT);
  drawLabel(CR_X+10, R3_Y+6, "INTERVAL", C_GRAY);
  char ibuf[20];
  int mins = cfg.intervalSec / 60;
  snprintf(ibuf, sizeof(ibuf), "%d min%s", mins, mins==1?"":"s");
  drawVal(CR_X+10, R3_Y+20, ibuf, 2, C_CYAN);
  delay(700);
  // Settle to normal white
  tft.fillRect(CR_X+10, R3_Y+18, CR_W-14, 18, C_CARD);
  drawVal(CR_X+10, R3_Y+20, ibuf, 2, C_WHITE);
}

void cycleRotation() {
  cfg.screenRotation = (cfg.screenRotation == 1) ? 3 : 1;
  saveConfig();
  tft.setRotation(cfg.screenRotation);
  displayMain();
}

void showUpdateWarning(int secsLeft) {
  tft.fillRoundRect(CR_X+1, R2_Y+1, CR_W-2, R2_H-2, 5, C_CARD);
  tft.drawRoundRect(CR_X, R2_Y, CR_W, R2_H, 6, C_ORANGE);
  drawLabel(CR_X+10, R2_Y+8,  "PLEASE WAIT",     C_ORANGE);
  char buf[24];
  snprintf(buf, sizeof(buf), "Try in %d sec%s", secsLeft, secsLeft==1?"":"s");
  drawLabel(CR_X+10, R2_Y+26, buf,               C_GRAY);
  drawLabel(CR_X+10, R2_Y+44, "Avoid DynDNS ban", C_GRAY);
}

void checkTouch() {
  if (!ts.tirqTouched() || !ts.touched()) return;

  TS_Point p = ts.getPoint();
  if (p.z < 100) return;

  static unsigned long lastTouchMs = 0;
  unsigned long now = millis();
  if (now - lastTouchMs < 600) return;
  lastTouchMs = now;

  // Map raw ADC → screen pixels, accounting for display rotation.
  // Rotation 3 is 180° from rotation 1, so both axes are inverted.
  int sx, sy;
  if (cfg.screenRotation == 1) {
    sx = map(p.x, TS_MINX, TS_MAXX, 0,       SCR_W);
    sy = map(p.y, TS_MINY, TS_MAXY, 0,       SCR_H);
  } else {  // rotation 3
    sx = map(p.x, TS_MINX, TS_MAXX, SCR_W-1, 0);
    sy = map(p.y, TS_MINY, TS_MAXY, SCR_H-1, 0);
  }
  sx = constrain(sx, 0, SCR_W - 1);
  sy = constrain(sy, 0, SCR_H - 1);

  // ── Rotation button (header, top-right) ───────────────────────────
  if (sx >= 230 && sx <= 270 && sy >= 10 && sy <= 32) {
    cycleRotation();
    return;
  }

  // ── Status card (right col, row 2) — tap to force immediate update ─
  if (st.wifiOK && !st.apMode &&
      sx >= CR_X && sx <= CR_X + CR_W &&
      sy >= R2_Y && sy <= R2_Y + R2_H) {
    static unsigned long lastManualMs = 0;
    const unsigned long COOLDOWN = 30000UL;  // 30 s minimum between manual updates
    unsigned long elapsed = millis() - lastManualMs;
    if (lastManualMs != 0 && elapsed < COOLDOWN) {
      int secsLeft = (int)((COOLDOWN - elapsed + 999) / 1000);
      showUpdateWarning(secsLeft);
      delay(1500);
      displayMain();
    } else {
      lastManualMs = millis();
      st.lastSentIP = "";   // bypass local cache; DynDNS API is ground truth
      doDynUpdate();
    }
    return;
  }

  // ── Interval card (bottom-right) ──────────────────────────────────
  if (sx >= CR_X && sx <= CR_X + CR_W && sy >= R3_Y && sy <= R3_Y + R3_H) {
    cycleInterval();
  }
}

void handleWifiScan() {
  // Scan blocks for ~3-6 s; called on demand from Settings page only
  int n = WiFi.scanNetworks(false, true);  // blocking, include hidden
  String json = "[";
  for (int i = 0; i < n && i < 20; i++) {
    if (i > 0) json += ",";
    String ssid = WiFi.SSID(i);
    ssid.replace("\"", "\\\"");
    json += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
            ",\"secure\":" + (WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  json += "]";
  WiFi.scanDelete();
  sendJSON(json);
}

// ─────────────────────────────────────────────────────────────────────
// CHANGELOG
// ─────────────────────────────────────────────────────────────────────
// 2026-06-28 23:xx  Initial build — DynDNS updater for ESP32 + ILI9341,
//                   dark web UI, NVS config, AP setup mode, countdown timer.
// 2026-06-28 23:xx  Fixed white screen: switched TFT from VSPI to HSPI
//                   (GPIO 13/14/12) for Guition ESP32-2432S028 board.
// 2026-06-28 23:xx  Renamed app to "DYN Updater by LOVECLM.COM" on display,
//                   splash screen, web page title/header, and AP hotspot
//                   SSID updated to "DYN-Updater". AP mode screen reworded
//                   as numbered steps (connect → open browser → settings → save).
// 2026-06-28 23:xx  Fixed: LOVECLM all-caps everywhere.
//                   Fixed: AP mode now shows displayAPMode() not displayMain().
//                   Fixed: WiFi signal bars now show real RSSI (1-4 green bars
//                   when connected, 1 red bar when disconnected).
//                   Added: WiFi reconnect monitoring in loop().
//                   Added: /api/scan endpoint + Settings scan button with
//                   dropdown of nearby networks sorted by signal strength.
// 2026-06-28 23:xx  Changed device ID/AP SSID to "LOVECLM-DYN-Updater";
//                   added mDNS (http://LOVECLM-DYN-Updater.local).
//                   WiFi signal now shows percentage badge (e.g. "85%") in
//                   green/yellow/red instead of bars.
//                   Added eye-toggle button on DynDNS + WiFi password fields.
//                   Fixed WiFi reconnect bug: second save (same SSID, new
//                   password, or in AP mode) now always triggers ESP.restart()
//                   so the device actually attempts to connect.
//                   startAP() switched to WIFI_AP_STA so network scan works
//                   while in setup/AP mode.
// 2026-06-28 23:xx  Added XPT2046 touchscreen support (TOUCH_CS=33, IRQ=36,
//                   CLK=25, MISO=39, MOSI=32 on custom VSPI-like bus).
//                   Tapping the INTERVAL card cycles 5→10→30→60→5 min with
//                   flash feedback and < tap > hint. Library required:
//                   XPT2046_Touchscreen by Paul Stoffregen.
// 2026-06-28 23:xx  Skip DynDNS call when public IP unchanged (status shows
//                   "NO CHANGE NEEDED" locally; lastSentIP tracked in state).
//                   Hostname display wraps to 2 lines at dot boundary (size-1
//                   font, 22 chars/line) so full hostname is visible.
//                   Added rotation icon button (↺ pill) in header: tapping
//                   toggles between rotation 1 and 3 (180° flip), persisted
//                   to NVS. Touch mapping inverts axes for rotation 3.
// 2026-06-29 00:00  Redesigned rotation button: replaced two-arrow pill with a
//                   proper ↻ circular arc icon (32×18 pill, 9-segment arc at 30°
//                   steps, arrowhead at upper-right showing clockwise direction).
//                   Removed "< tap >" hint text from the INTERVAL card.
// 2026-06-29 00:01  Status card is now tappable (accent border): tapping forces
//                   an immediate doDynUpdate() call. IP-unchanged guard still
//                   applies — DynDNS API is not hit if IP matches lastSentIP.
//                   Interval card tap only cycles interval, never triggers update.
// 2026-06-29        Fixed status-card force-update: clears lastSentIP before
//                   calling doDynUpdate() so the local IP cache is bypassed and
//                   the DynDNS API is always hit. DynDNS returns "good"/"nochg"
//                   as the true ground truth instead of the cached local value.
// 2026-06-29        Added resolveHostIP(): DNS-resolves the DynDNS hostname and
//                   stores result in st.dynIP. Shown in hostname card as "DYN: x.x.x.x"
//                   in green (matches public IP) or yellow (out of date). Called
//                   at startup after WiFi connects and after each successful API call.
// 2026-06-29        Bug fix: dynIP now set directly from confirmed API response
//                   ("good"/"nochg") instead of DNS lookup, which was returning a
//                   stale cached value due to TTL — so the display now updates
//                   immediately after a successful DynDNS push.
//                   Added 30-second cooldown on manual status-card tap: tapping
//                   within 30 s shows "PLEASE WAIT / Try in Xs / Avoid DynDNS ban"
//                   warning in the status card area, then restores the main view.
// 2026-06-29        Added APP_VERSION constant ("v1.1"); displayed right-aligned
//                   in footer. Versioning: 1.1 → 1.2 → … → 1.9 → 2.0.
