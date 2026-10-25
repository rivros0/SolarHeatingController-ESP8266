"use strict";
let csrf="",config={},state={},diagnostics={},pollCount=0;
const $=id=>document.getElementById(id);
const number=(v,d=0)=>v===null||v===undefined||!Number.isFinite(v)?"—":v.toLocaleString("it-IT",{maximumFractionDigits:d});
const notice=text=>{$("notice").textContent=text;};
async function api(path,data){const r=await fetch(path,{cache:"no-store",headers:data?{"Content-Type":"application/json","X-CSRF-Token":csrf}:{},method:data?"POST":"GET",body:data?JSON.stringify(data):undefined});if(!r.ok)throw new Error(await r.text());return r.json();}
function download(name,data){const url=URL.createObjectURL(new Blob([JSON.stringify(data,null,2)],{type:"application/json"}));const a=document.createElement("a");a.href=url;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}
function dl(id,rows){const el=$(id);el.replaceChildren();for(const [label,value]of rows){const dt=document.createElement("dt"),dd=document.createElement("dd");dt.textContent=label;dd.textContent=value;el.append(dt,dd);}}
async function refresh(){try{state=await api("/api/state");csrf=state.csrf;$("connection").textContent=state.wifi?(state.mqtt?"Wi-Fi + MQTT connessi":"Wi-Fi · MQTT assente"):"Access point locale";
for(const k of["pv","load"])$(k).textContent=number(state.energy[k].value);
const b=state.energy.battery.value;$("battery").textContent=number(b===null?null:b*(config.energy?.negativeCharge?-1:1));
$("batteryDetail").textContent=`W · ${number(state.energy.voltage.value,1)} V · SOC ${number(state.energy.soc.value)} %`;
$("available").textContent=state.energy.pv.valid&&state.energy.load.valid?number(state.availableW):"—";
$("temperature").textContent=number(state.temperature,1)+" °C";$("humidity").textContent=number(state.humidity)+" %";
$("mode").textContent=({auto:"AUTOMATICO",force_on:"FORZA ON",force_off:"FORZA OFF",diagnostic:"DIAGNOSTICA"})[state.mode];
$("target").textContent=`HEAT ${number(state.setpoint,1)} °C · avvio sotto ${number(config.energy?.tempOn,1)} °C · arresto a ${number(config.energy?.tempOff,1)} °C`;
$("reason").textContent=state.reason;
dl("climate",[["Stato comandato",state.commanded],["Stato stimato",state.estimated],["Affidabilità indizio",number(state.confidence*100)+" %"],["Variazione potenza",number(state.estimatedClimateW)+" W"],["Ultimo comando",state.lastCommand],["Motivo ultimo comando",state.lastActionReason],["Ora comando",state.lastCommand==="none"?"—":`${number(state.lastCommandAtMs/1000)} s dall’avvio`],["Codici discreti verificati",state.irReady?"Sì":"No"]]);
dl("system",[["Uptime",number(state.uptime)+" s"],["Heap libero",number(state.heap)+" byte"],["Spazio OTA",number(state.otaSpace)+" byte"],["Retained ignorati",state.retainedIgnored],["Messaggi rifiutati",state.mqttRejected],["Reset",state.resetReason],["Inverter",state.inverter||"—"]]);
const l=state.learning;$("learnState").textContent=`Fase: ${l.stage} · Spento ${number(l.offW)} W · Acceso ${number(l.onW)} W · Dopo OFF ${number(l.offAfterW)} W · Avvio ${number(l.startupMs/1000)} s · Rumore ${number(l.noiseW)} W`;
$("version").textContent="Firmware "+state.version;
if(state.rebootNeeded)notice("Configurazione salvata. Conferma climatizzatore spento e riavvia per applicarla.");
if(pollCount++%5===0){const h=await api("/api/history");draw("energyChart",h,[["pv","#b58919"],["load","#345c82"],["battery","#26866d"]],"W");draw("envChart",h,[["t","#c96c45"],["h","#5c7ac4"]],"°C / %");}
}catch(e){notice("Connessione o dati non disponibili: "+e.message);}finally{setTimeout(refresh,3000);}}
function draw(id,rows,series,unit){const canvas=$(id),r=canvas.getBoundingClientRect(),dpr=window.devicePixelRatio||1;canvas.width=r.width*dpr;canvas.height=r.height*dpr;const c=canvas.getContext("2d");c.scale(dpr,dpr);const w=r.width,h=r.height,p=35;
const values=rows.flatMap(row=>series.map(([k])=>row[k])).filter(Number.isFinite),lo=Math.min(0,...values),hi=Math.max(1,...values),range=hi-lo;
c.font="10px system-ui";c.fillStyle="#637872";c.fillText(`${number(hi)} ${unit}`,0,12);c.fillText(number(lo),0,h-14);
for(let j=0;j<3;j++){const y=18+j*(h-40)/2;c.strokeStyle="#e4e9e1";c.beginPath();c.moveTo(p,y);c.lineTo(w,y);c.stroke();}
if(rows.length<2){c.fillText("In attesa di campioni…",p,55);return;}const first=rows[0].s,last=rows[rows.length-1].s,dt=Math.max(1,last-first);const x=row=>p+(row.s-first)/dt*(w-p-5),y=v=>18+(hi-v)/range*(h-40);
for(const[k,color]of series){c.strokeStyle=color;c.lineWidth=1.8;c.beginPath();let active=false;for(const row of rows){if(!Number.isFinite(row[k])){active=false;continue;}if(!active)c.moveTo(x(row),y(row[k]));else c.lineTo(x(row),y(row[k]));active=true;}c.stroke();}
for(let i=1;i<rows.length;i++)if(rows[i].known&&rows[i-1].known&&rows[i].on!==rows[i-1].on){c.strokeStyle=rows[i].on?"#21634f":"#bd6d42";c.setLineDash([3,3]);c.beginPath();c.moveTo(x(rows[i]),18);c.lineTo(x(rows[i]),h-22);c.stroke();c.setLineDash([]);}
c.fillStyle="#637872";c.fillText(`ultimi ${number(dt/60,1)} min · secondi dall’avvio`,p,h-2);}
const groups=[
["Wi-Fi e accesso",[["ssid","SSID"],["wifiPassword","Password Wi-Fi","password"],["adminUser","Utente web"],["adminPassword","Nuova password web (min. 12)","password"]]],
["MQTT SolarAssistant",[["mqttHost","Host broker"],["mqttPort","Porta","number"],["mqttUser","Utente MQTT"],["mqttPassword","Password MQTT","password"],["discovery","Filtro diagnostico (vuoto = disattivo)"],["mqttKeepalive","Keepalive (s)","number"]]],
["Topic esatti",[...["pv","load","battery","voltage","current","soc","import","export","inverter"].map(k=>["topics."+k,k])]],
["Energia e AGM",[["automationEnabled","Abilita automazione","checkbox"],["energy.reserveW","Riserva ricarica (W)","number"],["energy.climateW","Potenza clima prevista (W)","number"],["energy.dischargeW","Scarica critica (W)","number"],["energy.minVoltage","Tensione minima (V, 0 disabilita)","number"],["energy.maxDischargeA","Scarica massima (A, 0 disabilita)","number"],["energy.negativeCharge","Potenza negativa = carica","checkbox"],["energy.negativeCurrentCharge","Corrente negativa = carica","checkbox"]]],
["Ambiente",[["energy.tempOn","Accensione sotto (°C)","number"],["energy.tempOff","Temperatura massima (°C)","number"],["setpoint","Setpoint IR HEAT (°C)","number"],["sampleMs","Campionamento (ms)","number"],["filterAlpha","Filtro EMA (0,01–1)","number"]]],
["Tempi e storico",[["energy.stableMs","Stabilità avvio (ms)","number"],["energy.dropMs","Verifica calo (ms)","number"],["energy.restartMs","Minimo tra riavvii (ms)","number"],["energy.minRunMs","Funzionamento minimo (ms)","number"],["energy.staleMs","Scadenza dati (ms)","number"],["historyMs","Intervallo storico RAM (ms)","number"],["forceOnMs","Durata Forza ON (ms)","number"]]],
["Infrarossi",[["protocol","Protocollo",["RAW","KELON","WHIRLPOOL_AC"]],["powerKind","Semantica alimentazione",["unverified","discrete","toggle"]],["irVerified","Ho verificato ON/OFF distinti, HEAT e setpoint sul modello reale","checkbox"],["receiver","Ricevitore GPIO12 abilitato","checkbox"],["model","Modello IR (id libreria)","number"],["fan","Ventola",["auto","low","medium","high"]]]],
["Riconoscimento assorbimento",[["detector.baselineMs","Finestra riferimento (ms)","number"],["detector.startupMs","Attesa compressore (ms)","number"],["detector.observationMs","Finestra verifica (ms)","number"],["detector.deltaW","Variazione minima (W)","number"],["detector.maxNoiseW","Rumore massimo (W)","number"]]]];
function get(obj,path){return path.split(".").reduce((v,k)=>v?.[k],obj);}
function set(obj,path,value){const keys=path.split("."),last=keys.pop();let target=obj;for(const key of keys)target=target[key]||(target[key]={});target[last]=value;}
function fields(){for(const[title,items]of groups){const section=document.createElement("section");section.className="form-section";const h=document.createElement("h3");h.textContent=title;section.append(h);
for(const[path,label,type="text"]of items){const l=document.createElement("label");l.className="field";l.append(document.createTextNode(label));const input=document.createElement(Array.isArray(type)?"select":"input");input.dataset.path=path;if(Array.isArray(type)){for(const value of type){const opt=document.createElement("option");opt.value=opt.textContent=value;input.append(opt);}}else input.type=type;
if(type==="number")input.step="any";if(type==="checkbox")input.checked=!!get(config,path);else input.value=type==="password"?"":get(config,path)??"";if(type==="password")input.autocomplete="new-password";
l.append(input);section.append(l);}$("fields").append(section);}}
async function act(fn){try{await fn();notice("Operazione completata.");}catch(e){notice(e.message);}}
document.querySelectorAll("[data-mode]").forEach(b=>b.onclick=()=>act(()=>api("/api/mode",{mode:b.dataset.mode})));
$("settings").onsubmit=e=>{e.preventDefault();act(async()=>{const next=structuredClone(config);document.querySelectorAll("[data-path]").forEach(i=>{if(i.type==="password"&&!i.value)return;set(next,i.dataset.path,i.type==="checkbox"?i.checked:i.type==="number"?Number(i.value):i.value);});if($("clearMqtt").checked)next.mqttPassword="";await api("/api/config",next);});};
$("reboot").onclick=()=>act(()=>api("/api/reboot",{}));
$("export").onclick=()=>act(async()=>download("solar-config-redacted.json",await api("/api/config")));
for(const[button,on]of[["confirmOff",false],["confirmOn",true]])$(button).onclick=()=>{if(confirm(`Confermi di aver osservato il climatizzatore fisicamente ${on?"acceso":"spento"}?`))act(()=>api("/api/ir",{action:"confirm_state",on,confirmed:true}));};
for(const[button,on]of[["irOn",true],["irOff",false]])$(button).onclick=()=>{const actual=$("actualState").value;if(actual==="unknown"){notice("Selezionare lo stato fisico osservato prima del test.");return;}if(confirm(`Confermi il climatizzatore fisicamente ${actual.toUpperCase()} e l’invio di un solo comando ${on?"ON HEAT":"OFF"}?`)){act(()=>api("/api/ir",{action:on?"on":"off",actualOn:actual==="on",confirmed:true}));$("actualState").value="unknown";}};
$("capture").onclick=()=>act(async()=>{$("irCapture").textContent=JSON.stringify(await api("/api/ir"),null,2);});
for(const[id,action]of[["saveOn","capture_on"],["saveOff","capture_off"]])$(id).onclick=()=>{if(confirm("Salvare questo segnale? Automazione e verifica IR verranno disabilitate. Il codice deve corrispondere alla funzione scelta sul telecomando originale."))act(()=>api("/api/ir",{action,confirmed:true}));};
document.querySelectorAll("[data-learn]").forEach(b=>b.onclick=()=>{const action=b.dataset.learn;if(["on","off"].includes(action)&&!confirm(`Confermi stato fisico ${action==="on"?"SPENTO":"ACCESO"} e invio del comando ${action.toUpperCase()}?`))return;act(()=>api("/api/learn",{action,confirmed:true}));});
async function diagnosticsRefresh(){diagnostics=await api("/api/diagnostics");$("topics").replaceChildren();for(const row of diagnostics.topics){const tr=document.createElement("tr");for(const value of[row.topic,row.value,number(row.ageMs/1000)+" s",row.retained?"Sì · ignorato":"No"]){const td=document.createElement("td");td.textContent=value;tr.append(td);}$("topics").append(tr);}$("events").textContent=diagnostics.events.map(e=>`${(e.atMs/1000).toFixed(0)}s  ${e.message}`).join("\n");}
$("refreshDiagnostics").onclick=()=>act(diagnosticsRefresh);$("exportLogs").onclick=()=>act(async()=>download("solar-log.json",await api("/api/diagnostics")));
(async()=>{try{config=await api("/api/config");fields();await diagnosticsRefresh();}catch(e){notice(e.message);}refresh();})();
