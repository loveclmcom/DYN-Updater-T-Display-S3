/*
 * DYN Updater by LOVECLM.COM — Lilygo T-Display-S3  v1.0
 * https://www.loveclm.com
 *
 * Screen : ST7789  320×170 (landscape)  via TFT_eSPI
 * Button : GPIO 0  (front left)  — cycle update interval
 *          GPIO 14 (front right) — force update now
 *          GPIO RST (side)       — hardware reset
 *
 * Required libraries (install via Library Manager):
 *   TFT_eSPI  by Bodmer  (configure User_Setup for T-Display-S3)
 *
 * Built-in ESP32 libraries (no install needed):
 *   WiFi, WebServer, HTTPClient, WiFiClientSecure, Preferences
 *
 * TFT_eSPI setup — in User_Setup_Select.h uncomment:
 *   #include <User_Setups/Setup206_LilyGo_T_Display_S3.h>
 *
 * Critical hardware note:
 *   GPIO 15 (PIN_POWER_ON) must be driven HIGH before tft.init()
 *   or the display power rail stays off → black screen.
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <time.h>

// ─────────────────────────────────────────────────────────────────────
// COMPILE-TIME PIN CHECK — if this fires, User_Setup_Select.h is wrong
// ─────────────────────────────────────────────────────────────────────
#if TFT_WR != 8 || TFT_RD != 9 || TFT_CS != 6 || TFT_DC != 7 || TFT_RST != 5 || \
    TFT_D0 != 39 || TFT_D1 != 40 || TFT_D2 != 41 || TFT_D3 != 42 ||             \
    TFT_D4 != 45 || TFT_D5 != 46 || TFT_D6 != 47 || TFT_D7 != 48 ||             \
    TFT_BL != 38 || TFT_WIDTH != 170 || TFT_HEIGHT != 320
#error "Wrong TFT_eSPI setup! In TFT_eSPI/User_Setup_Select.h comment out #include <User_Setup.h> and uncomment #include <User_Setups/Setup206_LilyGo_T_Display_S3.h>"
#endif

// ─────────────────────────────────────────────────────────────────────
// HARDWARE
// ─────────────────────────────────────────────────────────────────────
#define BTN_INTERVAL    0    // front-left  — cycle interval
#define BTN_UPDATE     14    // front-right — force update
#define PIN_POWER_ON   15    // display power rail — must be HIGH or screen stays black
#define TFT_BL_PIN     38    // backlight (active HIGH)

// ─────────────────────────────────────────────────────────────────────
// ST7789 CUSTOM INIT  (required for newer T-Display-S3 panel revisions)
// Without this sequence the display stays black even with correct pins.
// ─────────────────────────────────────────────────────────────────────
typedef struct { uint8_t cmd; uint8_t data[14]; uint8_t len; } lcd_cmd_t;
static const lcd_cmd_t lcd_st7789v[] = {
  {0x11, {0},                                                                 0 | 0x80},
  {0x3A, {0x05},                                                              1},
  {0xB2, {0x0B, 0x0B, 0x00, 0x33, 0x33},                                     5},
  {0xB7, {0x75},                                                              1},
  {0xBB, {0x28},                                                              1},
  {0xC0, {0x2C},                                                              1},
  {0xC2, {0x01},                                                              1},
  {0xC3, {0x1F},                                                              1},
  {0xC6, {0x13},                                                              1},
  {0xD0, {0xA7},                                                              1},
  {0xD0, {0xA4, 0xA1},                                                        2},
  {0xD6, {0xA1},                                                              1},
  {0xE0, {0xF0,0x05,0x0A,0x06,0x06,0x03,0x2B,0x32,0x43,0x36,0x11,0x10,0x2B,0x32}, 14},
  {0xE1, {0xF0,0x08,0x0C,0x0B,0x09,0x24,0x2B,0x22,0x43,0x38,0x15,0x16,0x2F,0x37}, 14},
};

#define APP_VERSION "v1.0"

// ─────────────────────────────────────────────────────────────────────
// COLOR PALETTE (RGB565)
// ─────────────────────────────────────────────────────────────────────
#define C_BG      0x0841
#define C_CARD    0x10A3
#define C_BORDER  0x2124
#define C_ACCENT  0x3C1E
#define C_WHITE   0xFFFF
#define C_GRAY    0x6B4D
#define C_GREEN   0x07E0
#define C_RED     0xF800
#define C_CYAN    0x07FF
#define C_YELLOW  0xFFE0
#define C_ORANGE  0xFC60

// ─────────────────────────────────────────────────────────────────────
// LAYOUT  (landscape 320×170)
// ─────────────────────────────────────────────────────────────────────
#define SCR_W  320
#define SCR_H  170
#define HDR_H   30   // header bar height

// Row A — IP address card (full width)
#define RA_X   4
#define RA_Y   (HDR_H + 3)
#define RA_W   (SCR_W - 8)
#define RA_H   44

// Row B — two cards side by side
#define RB_Y   (RA_Y + RA_H + 3)
#define RB_H   42
#define BL_X   4
#define BL_W   154
#define BR_X   162
#define BR_W   154

// Row C — two small cards: countdown | interval
#define RC_Y   (RB_Y + RB_H + 3)
#define RC_H   34
#define CL_X   4
#define CL_W   154
#define CR_X   162
#define CR_W   154

// Footer
#define FOOT_Y (SCR_H - 11)

// ─────────────────────────────────────────────────────────────────────
// APP CONSTANTS
// ─────────────────────────────────────────────────────────────────────
#define AP_SSID          "LOVECLM-DYN-Updater"
#define MDNS_NAME        "LOVECLM-DYN-Updater"
#define AP_PASS          "12345678"
#define DYNDNS_SERVER    "members.dyndns.org"
#define IP_CHECK_URL     "http://api.ipify.org"
#define USER_AGENT       "ESP32DynUpdater/1.0 sbhan@esp32"
#define DEFAULT_INTERVAL 300
#define MIN_INTERVAL      60

// ─────────────────────────────────────────────────────────────────────
// OBJECTS
// ─────────────────────────────────────────────────────────────────────
TFT_eSPI   tft;
WebServer  server(80);
Preferences prefs;

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
} cfg;

// ─────────────────────────────────────────────────────────────────────
// RUNTIME STATE
// ─────────────────────────────────────────────────────────────────────
struct AppState {
  String        publicIP;
  String        lastSentIP;
  String        dynIP;
  String        lastStatus;
  String        lastUpdateTime;
  bool          wifiOK;
  bool          apMode;
  bool          updating;
  unsigned long lastUpdateMs;
} st;

unsigned long lastDispMs    = 0;
unsigned long lastBtnIMs    = 0;   // debounce interval button
unsigned long lastBtnUMs    = 0;   // debounce update button

// ─────────────────────────────────────────────────────────────────────
// HTML PAGE  (stored in flash)
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
h1{font-size:1.25rem;font-weight:700}.sub{color:#64748b;font-size:.72rem;margin-top:1px}
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
.hero{background:linear-gradient(135deg,#1e3a5f,#1e1b4b);border:1px solid #3b82f6;border-radius:16px;padding:24px 20px;margin-bottom:14px;text-align:center}
.hero .hl{color:#60a5fa;font-size:.7rem;letter-spacing:.15em;text-transform:uppercase;margin-bottom:8px}
.hero .hv{font-size:2rem;font-weight:800;color:#fff;font-family:'Courier New',monospace;letter-spacing:.06em;margin-bottom:8px}
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
.hint{background:rgba(59,130,246,.08);border:1px solid rgba(59,130,246,.2);border-radius:9px;padding:8px 12px;font-size:.78rem;color:#60a5fa;margin-bottom:14px}
</style></head><body>
<div class="hdr">
  <div class="logo">&#127760;</div>
  <div><h1>DYN Updater</h1><div class="sub">by LOVECLM.COM &mdash; T-Display-S3</div></div>
  <nav>
    <button class="act" onclick="pg('dash')">Dashboard</button>
    <button onclick="pg('set')">Settings</button>
  </nav>
</div>
<div class="con">
  <div id="pdash">
    <div class="hero">
      <div class="hl">Your Public IP Address</div>
      <div class="hv" id="pip">&#8212;</div>
      <div style="color:#64748b;font-size:.8rem">Access via&nbsp;<a id="dlink" href="#" style="color:#60a5fa;text-decoration:none">loading&hellip;</a></div>
    </div>
    <div class="hint">&#9109; Front-left button: cycle interval &nbsp;|&nbsp; &#9109; Front-right button: force update</div>
    <div class="grid">
      <div class="card"><div class="lbl">Hostname</div><div style="font-size:.95rem;font-weight:600;color:#f1f5f9;word-break:break-all" id="phost">&#8212;</div></div>
      <div class="card"><div class="lbl">Update Status</div><div id="pbadge">&#8212;</div><div class="info-row"><span id="ptime">Never updated</span></div></div>
      <div class="card"><div class="lbl">Next Update</div><div class="val" id="pnext">&#8212;</div><div class="pb-bg"><div class="pb" id="ppb" style="width:0%"></div></div></div>
      <div class="card"><div class="lbl">Interval</div><div class="val" id="pival">&#8212;</div></div>
    </div>
    <div class="actions">
      <button class="btn btn-p" onclick="manualUpd()" id="ubtn">&#8635;&nbsp;Update Now</button>
      <button class="btn btn-s" onclick="loadSt()">&#8635;&nbsp;Refresh</button>
    </div>
  </div>
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
        <div style="color:#64748b;font-size:.75rem;margin-top:6px">Minimum 1 min &mdash; DynDNS recommends &ge;5 min.</div>
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
function pg(p){document.getElementById('pdash').style.display=p==='dash'?'':'none';document.getElementById('pset').style.display=p==='set'?'':'none';document.querySelectorAll('nav button').forEach(function(b,i){b.classList.toggle('act',(p==='dash'&&i===0)||(p==='set'&&i===1));});curPage=p;if(p==='set')loadCfg();else loadSt();}
function toast(m,t){var e=document.getElementById('toast');e.textContent=m;e.className='toast '+(t||'')+' show';setTimeout(function(){e.className='toast '+(t||'');},3400);}
function badge(s){if(!s)return '<span class="badge b-inf"><span class="dot"></span>Pending</span>';if(s.indexOf('good')===0)return '<span class="badge b-ok"><span class="dot pulse"></span>Updated</span>';if(s.indexOf('nochg')===0||s==='no-change')return '<span class="badge b-inf"><span class="dot"></span>No Change</span>';if(s==='badauth')return '<span class="badge b-err"><span class="dot"></span>Bad Auth</span>';if(s==='nohost')return '<span class="badge b-err"><span class="dot"></span>No Host</span>';if(s==='abuse')return '<span class="badge b-warn"><span class="dot"></span>Blocked</span>';if(s.indexOf('err')===0)return '<span class="badge b-err"><span class="dot"></span>Error</span>';return '<span class="badge b-warn"><span class="dot"></span>'+s+'</span>';}
function fmtS(s){if(s<=0)return'Due';var m=Math.floor(s/60),r=s%60;return m>0?m+'m '+r+'s':r+'s';}
function fmtI(s){var m=Math.floor(s/60);return m+' min'+(m!==1?'s':'');}
function startCd(rem,tot){clearInterval(cd);var r=Math.max(0,rem);function tick(){document.getElementById('pnext').textContent=fmtS(r);document.getElementById('ppb').style.width=(tot>0?Math.max(0,(1-r/tot)*100):100)+'%';if(r>0)r--;else{clearInterval(cd);if(curPage==='dash')loadSt();}}tick();cd=setInterval(tick,1000);}
async function loadSt(){try{var r=await fetch('/api/status'),d=await r.json();document.getElementById('pip').textContent=d.ip||'—';document.getElementById('phost').textContent=d.hostname||'Not configured';document.getElementById('pbadge').innerHTML=badge(d.lastStatus||'');document.getElementById('ptime').textContent=d.lastUpdateTime?'Updated: '+d.lastUpdateTime:'Never updated';document.getElementById('pival').textContent=fmtI(d.intervalSec||300);itv=d.intervalSec||300;startCd(d.nextUpdateIn||0,itv);var ip=d.deviceIP||location.hostname;var lnk=document.getElementById('dlink');lnk.href='http://'+ip;lnk.textContent=ip;}catch(e){console.warn('status err',e);}}
async function loadCfg(){try{var r=await fetch('/api/config'),d=await r.json();document.getElementById('shost').value=d.dynHost||'';document.getElementById('suser').value=d.dynUser||'';document.getElementById('spass').value='';document.getElementById('sival').value=Math.round((d.intervalSec||300)/60);document.getElementById('sssid').value=d.wifiSSID||'';document.getElementById('swpass').value='';}catch(e){}}
async function saveCfg(){var iv=parseInt(document.getElementById('sival').value)*60;if(iv<60){toast('Interval must be at least 1 minute','terr');return;}var b={dynHost:document.getElementById('shost').value,dynUser:document.getElementById('suser').value,dynPass:document.getElementById('spass').value,intervalSec:iv,wifiSSID:document.getElementById('sssid').value,wifiPass:document.getElementById('swpass').value};try{var r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)}),d=await r.json();if(d.ok)toast('Settings saved!','tok');else toast('Error: '+(d.error||'unknown'),'terr');}catch(e){toast('Save failed — device may be restarting','terr');}}
async function manualUpd(){var btn=document.getElementById('ubtn');btn.innerHTML='<span class="spin"></span> Updating&hellip;';btn.disabled=true;try{var r=await fetch('/api/update',{method:'POST'}),d=await r.json();toast(d.result||'Update triggered',d.ok?'tok':'terr');setTimeout(loadSt,2500);}catch(e){toast('Update failed','terr');}setTimeout(function(){btn.innerHTML='&#8635;&nbsp;Update Now';btn.disabled=false;},3000);}
function togglePwd(id,btn){var i=document.getElementById(id),show=i.type==='password';i.type=show?'text':'password';btn.innerHTML=show?'&#128584;':'&#128065;';}
function selSSID(){var v=document.getElementById('sssid-sel').value;if(v)document.getElementById('sssid').value=v;}
async function scanWifi(){var btn=document.getElementById('scan-btn'),st=document.getElementById('scan-st');btn.innerHTML='<span class="spin"></span>';btn.disabled=true;st.textContent='Scanning… (5-10s)';try{var r=await fetch('/api/scan'),nets=await r.json();var sel=document.getElementById('sssid-sel');sel.innerHTML='<option value="">-- Select network --</option>';nets.sort(function(a,b){return b.rssi-a.rssi;});nets.forEach(function(n){var bars=n.rssi>-60?'████':n.rssi>-70?'███░':n.rssi>-80?'██░░':'█░░░';var opt=document.createElement('option');opt.value=n.ssid;opt.textContent=n.ssid+'  '+bars+(n.secure?' 🔒':'');sel.appendChild(opt);});st.textContent=nets.length+' network'+(nets.length!==1?'s':'')+' found';}catch(e){st.textContent='Scan failed — try again';}btn.innerHTML='&#128225; Scan';btn.disabled=false;}
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
void displaySplash(); void displayConnecting(); void displayAPMode(); void displayMain();
void updateCountdownArea();
void drawCard(int,int,int,int,uint16_t);
void drawLabel(int,int,const char*,uint16_t);
void drawVal(int,int,const char*,uint8_t,uint16_t);
void drawCentered(int,int,const char*,uint8_t,uint16_t);
void drawWifiSignal(int,int,bool);
void drawStatusBadge(int,int,const String&);
void checkButtons();
void cycleInterval(); void triggerUpdate();
void handleRoot(); void handleStatus(); void handleGetConfig();
void handlePostConfig(); void handleUpdate(); void handleNotFound();
void handleWifiScan();
void sendJSON(const String&, int code=200);
String jsonGetStr(const String&, const String&);
int   jsonGetInt(const String&, const String&, int);

// ─────────────────────────────────────────────────────────────────────
// SETUP
// ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  pinMode(BTN_INTERVAL,  INPUT_PULLUP);
  pinMode(BTN_UPDATE,    INPUT_PULLUP);

  // Power on display circuit FIRST, before tft.begin()
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);

  tft.begin();

  // Send custom init sequence — required for newer panel revision
  for (uint8_t i = 0; i < sizeof(lcd_st7789v) / sizeof(lcd_cmd_t); i++) {
    tft.writecommand(lcd_st7789v[i].cmd);
    for (int j = 0; j < (lcd_st7789v[i].len & 0x7F); j++) {
      tft.writedata(lcd_st7789v[i].data[j]);
    }
    if (lcd_st7789v[i].len & 0x80) delay(120);
  }

  tft.setRotation(3);   // landscape: 320 wide, 170 tall
  tft.fillScreen(C_BG);

  // Enable backlight after display is initialised
  pinMode(TFT_BL_PIN, OUTPUT);
  digitalWrite(TFT_BL_PIN, HIGH);

  loadConfig();
  displaySplash();
  delay(1600);
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

  if (!st.apMode) {
    bool linked = (WiFi.status() == WL_CONNECTED);
    if (linked && !st.wifiOK) {
      st.wifiOK = true;
      MDNS.begin(MDNS_NAME);
      displayMain();
    } else if (!linked && st.wifiOK) {
      st.wifiOK = false;
      displayMain();
      WiFi.reconnect();
    }
  }

  if (!st.apMode && st.wifiOK && !st.updating) {
    unsigned long intervalMs = (unsigned long)cfg.intervalSec * 1000UL;
    if (st.lastUpdateMs == 0 || (now - st.lastUpdateMs) >= intervalMs) {
      doDynUpdate();
    }
  }

  if (!st.apMode && !st.updating && (now - lastDispMs >= 1000)) {
    lastDispMs = now;
    updateCountdownArea();
  }

  if (!st.updating) checkButtons();
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
  cfg.intervalSec = prefs.getInt("ival", DEFAULT_INTERVAL);
  if (cfg.intervalSec < MIN_INTERVAL) cfg.intervalSec = MIN_INTERVAL;
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
  WiFi.mode(WIFI_AP_STA);
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

  // Show "Updating..." overlay in the IP card area
  tft.fillRoundRect(RA_X, RA_Y, RA_W, RA_H, 5, tft.color565(10, 25, 70));
  tft.drawRoundRect(RA_X, RA_Y, RA_W, RA_H, 5, C_ACCENT);
  drawCentered(SCR_W/2, RA_Y + 6,  "CONTACTING DYNDNS", 1, C_GRAY);
  drawCentered(SCR_W/2, RA_Y + 20, "Please wait...",    2, C_CYAN);

  String ip = getPublicIP();
  if (ip.length() == 0) {
    st.lastStatus = "err:no-ip";
    st.updating = false;
    displayMain();
    return;
  }
  st.publicIP = ip;

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
  if (result.startsWith("good") || result.startsWith("nochg")) {
    st.lastSentIP = ip;
    st.dynIP      = ip;
  }

  time_t now; struct tm ti; time(&now);
  localtime_r(&now, &ti);
  char buf[24];
  if (now > 1000000000UL) strftime(buf, sizeof(buf), "%H:%M %d/%m/%y", &ti);
  else snprintf(buf, sizeof(buf), "+%lus", millis()/1000);
  st.lastUpdateTime = String(buf);

  st.updating = false;
  displayMain();

  bool ok = result.startsWith("good") || result.startsWith("nochg");
  uint16_t flash = ok ? C_GREEN : C_RED;
  for (int i = 0; i < 3; i++) {
    tft.drawRoundRect(BR_X, RB_Y, BR_W, RB_H, 5, flash);
    delay(160);
    tft.drawRoundRect(BR_X, RB_Y, BR_W, RB_H, 5, C_BORDER);
    delay(160);
  }
}

// ─────────────────────────────────────────────────────────────────────
// DISPLAY HELPERS
// ─────────────────────────────────────────────────────────────────────
void drawCard(int x, int y, int w, int h, uint16_t border = C_BORDER) {
  tft.fillRoundRect(x, y, w, h, 5, C_CARD);
  tft.drawRoundRect(x, y, w, h, 5, border);
}

void drawLabel(int x, int y, const char* text, uint16_t color = C_GRAY) {
  tft.setTextColor(color); tft.setTextSize(1); tft.setCursor(x, y); tft.print(text);
}

void drawVal(int x, int y, const char* text, uint8_t sz = 2, uint16_t color = C_WHITE) {
  tft.setTextColor(color); tft.setTextSize(sz); tft.setCursor(x, y); tft.print(text);
}

void drawCentered(int cx, int y, const char* text, uint8_t sz, uint16_t color) {
  tft.setTextSize(sz); tft.setTextColor(color);
  int tw = tft.textWidth(text);
  tft.setCursor(cx - tw/2, y); tft.print(text);
}

void drawWifiSignal(int x, int y, bool connected) {
  const int W = 34, H = 14;
  tft.fillRoundRect(x, y, W, H, 3, tft.color565(5, 10, 35));
  if (!connected) {
    tft.drawRoundRect(x, y, W, H, 3, C_RED);
    tft.setTextSize(1); tft.setTextColor(C_RED);
    tft.setCursor(x + 7, y + 3); tft.print("---");
    return;
  }
  int rssi = WiFi.RSSI();
  int pct  = constrain(2 * (rssi + 100), 0, 100);
  uint16_t col = pct > 65 ? C_GREEN : pct > 35 ? C_YELLOW : C_RED;
  tft.drawRoundRect(x, y, W, H, 3, col);
  char buf[6]; snprintf(buf, sizeof(buf), "%d%%", pct);
  tft.setTextSize(1); tft.setTextColor(col);
  int tw = tft.textWidth(buf);
  int th = tft.fontHeight();
  tft.setCursor(x + (W - tw) / 2, y + (H - th) / 2);
  tft.print(buf);
}

void drawStatusBadge(int x, int y, const String& s) {
  uint16_t col; const char* lbl;
  if      (s.startsWith("good"))  { col = C_GREEN;  lbl = "UPDATED";   }
  else if (s == "no-change")      { col = C_CYAN;   lbl = "NO CHANGE"; }
  else if (s.startsWith("nochg")) { col = C_CYAN;   lbl = "NO CHANGE"; }
  else if (s == "badauth")        { col = C_RED;    lbl = "BAD AUTH";  }
  else if (s == "nohost")         { col = C_RED;    lbl = "NO HOST";   }
  else if (s == "abuse")          { col = C_ORANGE; lbl = "BLOCKED";   }
  else if (s.startsWith("err"))   { col = C_RED;    lbl = "ERROR";     }
  else if (s.length() == 0)       { col = C_GRAY;   lbl = "PENDING";   }
  else                            { col = C_YELLOW; lbl = s.c_str();   }

  const int BW = 84, BH = 17;
  tft.drawRoundRect(x, y, BW, BH, 4, col);
  tft.setTextSize(1); tft.setTextColor(col);
  int tw = tft.textWidth(lbl);
  int th = tft.fontHeight();
  tft.setCursor(x + (BW - tw) / 2, y + (BH - th) / 2);
  tft.print(lbl);
}

// ─────────────────────────────────────────────────────────────────────
// DISPLAY SCREENS
// ─────────────────────────────────────────────────────────────────────
void displaySplash() {
  tft.fillScreen(C_BG);
  // Gradient header band
  for (int i = 0; i < SCR_H; i++) {
    int r = map(i, 0, SCR_H, 8,  4);
    int g = map(i, 0, SCR_H, 28, 8);
    int b = map(i, 0, SCR_H, 90, 25);
    tft.drawFastHLine(0, i, SCR_W, tft.color565(r, g, b));
  }

  // Globe icon (smaller for 170px height)
  int cx = SCR_W / 2, cy = 68;
  tft.fillCircle(cx, cy, 30, tft.color565(18, 52, 160));
  tft.drawCircle(cx, cy, 30, C_ACCENT);
  tft.drawCircle(cx, cy, 32, tft.color565(50, 90, 200));
  tft.drawCircle(cx, cy, 16, C_WHITE);
  tft.drawFastVLine(cx, cy - 30, 60, C_WHITE);
  tft.drawFastHLine(cx - 30, cy, 60, C_WHITE);
  tft.drawCircle(cx, cy, 8, C_WHITE);

  drawCentered(cx, 108, "DYN Updater",      2, C_WHITE);
  drawCentered(cx, 128, "by LOVECLM.COM",   1, C_CYAN);
  drawCentered(cx, 140, "T-Display-S3",     1, C_GRAY);
  drawCentered(cx, 156, "Starting up...",   1, C_ACCENT);
}

void displayConnecting() {
  tft.fillScreen(C_BG);
  // Header
  for (int i = 0; i < HDR_H; i++) {
    int b = map(i, 0, HDR_H, 120, 48);
    tft.drawFastHLine(0, i, SCR_W, tft.color565(10, 40, b));
  }
  drawCentered(SCR_W/2, 9, "DYN Updater by LOVECLM.COM", 1, C_WHITE);

  drawCard(16, HDR_H+10, SCR_W-32, 90, C_ACCENT);
  drawCentered(SCR_W/2, HDR_H+18, "Connecting to WiFi", 1, C_GRAY);
  char ssidLine[70];
  snprintf(ssidLine, sizeof(ssidLine), "%.50s", cfg.wifiSSID[0] ? cfg.wifiSSID : "(no SSID)");
  drawCentered(SCR_W/2, HDR_H+32, ssidLine, 1, C_WHITE);

  for (int i = 0; i < 3; i++) {
    tft.fillCircle(146 + i*14, HDR_H+70, 4, C_ACCENT);
    delay(70);
  }
}

void displayAPMode() {
  tft.fillScreen(C_BG);
  for (int i = 0; i < HDR_H; i++) {
    int r = map(i, 0, HDR_H, 80, 30);
    int g = map(i, 0, HDR_H, 40, 14);
    tft.drawFastHLine(0, i, SCR_W, tft.color565(r, g, 4));
  }
  drawCentered(SCR_W/2, 9, "Setup Mode", 2, C_YELLOW);

  drawCard(6, HDR_H+3, SCR_W-12, SCR_H-HDR_H-6, C_ORANGE);
  drawCentered(SCR_W/2, HDR_H+9, "1. Connect to WiFi:", 1, C_GRAY);
  drawCard(20, HDR_H+20, SCR_W-40, 22, C_ACCENT);
  drawCentered(SCR_W/2, HDR_H+26, AP_SSID, 1, C_WHITE);

  char passLine[40];
  snprintf(passLine, sizeof(passLine), "Password: %s", AP_PASS);
  drawCentered(SCR_W/2, HDR_H+48, passLine, 1, C_GRAY);

  String ip = WiFi.softAPIP().toString();
  char step2[48];
  snprintf(step2, sizeof(step2), "2. Browser: http://%s", ip.c_str());
  drawCentered(SCR_W/2, HDR_H+62, step2, 1, C_CYAN);
  drawCentered(SCR_W/2, HDR_H+76, "3. Settings > Credentials > Save", 1, C_GRAY);
  drawCentered(SCR_W/2, HDR_H+90, "Device restarts & connects",       1, C_CYAN);
}

void displayMain() {
  tft.fillScreen(C_BG);

  // ── Header ──────────────────────────────────────────────────────────
  for (int i = 0; i < HDR_H; i++) {
    int b = map(i, 0, HDR_H, 120, 48);
    tft.drawFastHLine(0, i, SCR_W, tft.color565(10, 40, b));
  }
  drawVal(8, 8, "DYN Updater", 2, C_WHITE);
  drawVal(8, 22, "LOVECLM.COM", 1, C_CYAN);
  // Button hints in header (right side)
  drawLabel(SCR_W - 108, 7,  "[L] Interval", C_GRAY);
  drawLabel(SCR_W - 108, 17, "[R] Update",   C_GRAY);
  drawWifiSignal(SCR_W - 40, 8, st.wifiOK);

  // ── Row A: Public IP (full width) ────────────────────────────────────
  drawCard(RA_X, RA_Y, RA_W, RA_H, C_ACCENT);
  drawLabel(RA_X + 8, RA_Y + 5, "PUBLIC IP", C_GRAY);

  String ip = st.publicIP.length() ? st.publicIP : "Fetching...";
  uint16_t ipColor = st.publicIP.length() ? C_WHITE : C_GRAY;
  // IP in size-3 font, centered vertically in remaining card space
  drawCentered(SCR_W/2, RA_Y + 18, ip.c_str(), 3, ipColor);

  // ── Row B: Hostname | Status ─────────────────────────────────────────
  drawCard(BL_X, RB_Y, BL_W, RB_H, C_BORDER);
  drawLabel(BL_X + 7, RB_Y + 5, "HOSTNAME", C_GRAY);
  {
    String host = String(cfg.dynHost);
    if (host.length() == 0) {
      drawVal(BL_X + 7, RB_Y + 16, "Not set", 1, C_GRAY);
    } else if (host.length() <= 20) {
      drawVal(BL_X + 7, RB_Y + 16, host.c_str(), 1, C_WHITE);
    } else {
      int cut = host.lastIndexOf('.', 19);
      if (cut <= 0) cut = 20;
      String l1 = host.substring(0, cut);
      String l2 = host.substring(cut);
      if (l2.length() > 20) l2 = l2.substring(0, 19) + "~";
      drawVal(BL_X + 7, RB_Y + 15, l1.c_str(), 1, C_WHITE);
      drawVal(BL_X + 7, RB_Y + 25, l2.c_str(), 1, C_GRAY);
    }
    if (st.dynIP.length()) {
      String lbl = "DNS: " + st.dynIP;
      uint16_t col = (st.dynIP == st.publicIP) ? C_GREEN : C_YELLOW;
      drawLabel(BL_X + 7, RB_Y + RB_H - 13, lbl.c_str(), col);
    }
  }

  drawCard(BR_X, RB_Y, BR_W, RB_H, C_BORDER);
  drawLabel(BR_X + 7, RB_Y + 5, "STATUS", C_GRAY);
  drawStatusBadge(BR_X + 7, RB_Y + 16, st.lastStatus);

  // ── Row C: Countdown | Interval ──────────────────────────────────────
  drawCard(CL_X, RC_Y, CL_W, RC_H, C_BORDER);
  drawLabel(CL_X + 7, RC_Y + 4, "NEXT UPDATE", C_GRAY);
  // countdown value filled by updateCountdownArea()

  drawCard(CR_X, RC_Y, CR_W, RC_H, C_ACCENT);
  drawLabel(CR_X + 7, RC_Y + 4, "INTERVAL", C_GRAY);
  char ibuf[20];
  int mins = cfg.intervalSec / 60;
  snprintf(ibuf, sizeof(ibuf), "%d min%s", mins, mins == 1 ? "" : "s");
  drawVal(CR_X + 7, RC_Y + 16, ibuf, 2, C_WHITE);

  // ── Footer ───────────────────────────────────────────────────────────
  String devIP = st.wifiOK ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  char foot[48];
  snprintf(foot, sizeof(foot), "http://%s", devIP.c_str());
  drawLabel(8, FOOT_Y, foot, C_GRAY);
  drawLabel(SCR_W - 6 * strlen(APP_VERSION) - 4, FOOT_Y, APP_VERSION, C_GRAY);

  updateCountdownArea();
}

void updateCountdownArea() {
  if (st.lastUpdateMs == 0) {
    tft.fillRect(CL_X + 7, RC_Y + 14, CL_W - 11, 16, C_CARD);
    drawVal(CL_X + 7, RC_Y + 16, "--:--", 2, C_CYAN);
    return;
  }
  long remMs = (long)((unsigned long)cfg.intervalSec * 1000UL) - (long)(millis() - st.lastUpdateMs);
  if (remMs < 0) remMs = 0;

  int secs = remMs / 1000;
  char buf[10];
  snprintf(buf, sizeof(buf), "%02d:%02d", secs / 60, secs % 60);

  tft.fillRect(CL_X + 7, RC_Y + 14, CL_W - 11, 16, C_CARD);
  drawVal(CL_X + 7, RC_Y + 16, buf, 2, C_CYAN);

  // Progress bar
  int barW  = CL_W - 14;
  int filled = (cfg.intervalSec > 0)
    ? barW - (int)((long)remMs * barW / ((long)cfg.intervalSec * 1000L))
    : barW;
  tft.fillRect(CL_X + 7, RC_Y + RC_H - 5, barW, 3, C_BORDER);
  if (filled > 0) tft.fillRect(CL_X + 7, RC_Y + RC_H - 5, filled, 3, C_ACCENT);
}

// ─────────────────────────────────────────────────────────────────────
// BUTTONS
// ─────────────────────────────────────────────────────────────────────
void cycleInterval() {
  if      (cfg.intervalSec <  600) cfg.intervalSec =  600;
  else if (cfg.intervalSec < 1800) cfg.intervalSec = 1800;
  else if (cfg.intervalSec < 3600) cfg.intervalSec = 3600;
  else                             cfg.intervalSec =  300;
  saveConfig();
  st.lastUpdateMs = 0;

  // Flash interval card
  for (int i = 0; i < 3; i++) {
    tft.drawRoundRect(CR_X, RC_Y, CR_W, RC_H, 5, C_CYAN);
    delay(90);
    tft.drawRoundRect(CR_X, RC_Y, CR_W, RC_H, 5, C_ACCENT);
    delay(90);
  }
  tft.fillRoundRect(CR_X + 1, RC_Y + 1, CR_W - 2, RC_H - 2, 4, C_CARD);
  tft.drawRoundRect(CR_X, RC_Y, CR_W, RC_H, 5, C_ACCENT);
  drawLabel(CR_X + 7, RC_Y + 4, "INTERVAL", C_GRAY);
  char ibuf[20];
  int mins = cfg.intervalSec / 60;
  snprintf(ibuf, sizeof(ibuf), "%d min%s", mins, mins == 1 ? "" : "s");
  drawVal(CR_X + 7, RC_Y + 16, ibuf, 2, C_CYAN);
  delay(600);
  tft.fillRect(CR_X + 7, RC_Y + 14, CR_W - 11, 16, C_CARD);
  drawVal(CR_X + 7, RC_Y + 16, ibuf, 2, C_WHITE);
}

void triggerUpdate() {
  static unsigned long lastManualMs = 0;
  const unsigned long COOLDOWN = 30000UL;
  unsigned long elapsed = millis() - lastManualMs;

  if (lastManualMs != 0 && elapsed < COOLDOWN) {
    // Show cooldown warning in status card
    int secsLeft = (int)((COOLDOWN - elapsed + 999) / 1000);
    tft.fillRoundRect(BR_X + 1, RB_Y + 1, BR_W - 2, RB_H - 2, 4, C_CARD);
    tft.drawRoundRect(BR_X, RB_Y, BR_W, RB_H, 5, C_ORANGE);
    drawLabel(BR_X + 7, RB_Y + 5,  "PLEASE WAIT", C_ORANGE);
    char buf[24];
    snprintf(buf, sizeof(buf), "Try in %ds", secsLeft);
    drawLabel(BR_X + 7, RB_Y + 18, buf, C_GRAY);
    drawLabel(BR_X + 7, RB_Y + 28, "Avoid DynDNS ban", C_GRAY);
    delay(1400);
    // Restore status card
    tft.fillRoundRect(BR_X + 1, RB_Y + 1, BR_W - 2, RB_H - 2, 4, C_CARD);
    tft.drawRoundRect(BR_X, RB_Y, BR_W, RB_H, 5, C_BORDER);
    drawLabel(BR_X + 7, RB_Y + 5, "STATUS", C_GRAY);
    drawStatusBadge(BR_X + 7, RB_Y + 16, st.lastStatus);
    return;
  }

  lastManualMs = millis();
  st.lastSentIP = "";
  doDynUpdate();
}

void checkButtons() {
  unsigned long now = millis();

  // Front-left: cycle interval
  if (digitalRead(BTN_INTERVAL) == LOW && (now - lastBtnIMs) > 400) {
    lastBtnIMs = now;
    cycleInterval();
  }

  // Front-right: force update
  if (digitalRead(BTN_UPDATE) == LOW && (now - lastBtnUMs) > 400) {
    lastBtnUMs = now;
    triggerUpdate();
  }
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

  if (newSSID.length())  strlcpy(cfg.wifiSSID, newSSID.c_str(),  sizeof(cfg.wifiSSID));
  if (newWPass.length()) strlcpy(cfg.wifiPass, newWPass.c_str(), sizeof(cfg.wifiPass));

  saveConfig();

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
  st.lastUpdateMs = 0;
  sendJSON("{\"ok\":true,\"result\":\"Update queued\"}");
}

void handleWifiScan() {
  int n = WiFi.scanNetworks(false, true);
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
// 2026-06-29  v1.0  Port of DYN_Loveclm to Lilygo T-Display-S3.
//                   ST7789 320×170 via TFT_eSPI. No touch — two physical
//                   buttons: GPIO 0 (cycle interval), GPIO 14 (force update).
//                   Layout re-packed for 170px height: 30px header, full-width
//                   IP card (RA), two-column hostname+status row (RB), two-column
//                   countdown+interval row (RC), footer.
//                   Rotation support removed (fixed landscape, no screen flip needed).
