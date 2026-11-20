"use strict";
let csrf="",config={},state={},diagnostics={},pollCount=0;

// Ultimo storico ricevuto: permette il ridisegno immediato al cambio scheda.
let historyCache = [];
// Ancora browser/ESP: la scheda non dispone di timestamp assoluti storici.
let historyTimeAnchor = null;
const TAB_IDS = ["overview", "charts", "settings", "infrared", "diagnostics"];
const $=id=>document.getElementById(id);
const number=(v,d=0)=>v===null||v===undefined||!Number.isFinite(v)?"—":v.toLocaleString("it-IT",{maximumFractionDigits:d});
const notice=text=>{$("notice").textContent=text;};
async function api(path,data){const r=await fetch(path,{cache:"no-store",headers:data?{"Content-Type":"application/json","X-CSRF-Token":csrf}:{},method:data?"POST":"GET",body:data?JSON.stringify(data):undefined});if(!r.ok)throw new Error(await r.text());return r.json();}
function download(name,data){const url=URL.createObjectURL(new Blob([JSON.stringify(data,null,2)],{type:"application/json"}));const a=document.createElement("a");a.href=url;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}
function dl(id,rows){const el=$(id);el.replaceChildren();for(const [label,value]of rows){const dt=document.createElement("dt"),dd=document.createElement("dd");dt.textContent=label;dd.textContent=value;el.append(dt,dd);}}

// ============================================================================
// NAVIGAZIONE A SCHEDE
// I pannelli rimangono nel DOM: pulsanti, campi e API preesistenti non cambiano.
// La posizione della scheda e' mantenuta nell'hash dell'indirizzo, senza
// ricaricare la pagina, e ogni scheda e' accessibile da tastiera.
// ============================================================================
function tabIdFromHash() {
  const requestedId = window.location.hash.substring(1);
  if(TAB_IDS.includes(requestedId)) {
    return requestedId;
  }
  return "overview";
}

function renderHistoryCharts() {
  const chartsPanel = $("tab-charts");
  if(chartsPanel.hidden) {
    // Un canvas in display:none non ha dimensioni: disegnare adesso lo
    // azzererebbe. Disegniamo soltanto quando la scheda e' visibile.
    return;
  }
  draw(
    "energyChart",
    historyCache,
    [["pv", "#b58919"], ["load", "#345c82"], ["battery", "#26866d"]],
    "W"
  );
  drawEnvironmentChart(historyCache);
}

async function updateHistoryForActiveTab() {
  try {
    historyCache = await api("/api/history");
    renderHistoryCharts();
  } catch(error) {
    notice("Storico non disponibile: " + error.message);
  }
}

function activateTab(tabId, focusTab, updateAddress) {
  if(!TAB_IDS.includes(tabId)) {
    tabId = "overview";
  }

  for(const id of TAB_IDS) {
    const button = $("tab-button-" + id);
    const panel = $("tab-" + id);
    const selected = id === tabId;

    button.setAttribute("aria-selected", String(selected));
    panel.hidden = !selected;

    if(selected) {
      button.tabIndex = 0;
    } else {
      button.tabIndex = -1;
    }
  }

  const selectedButton = $("tab-button-" + tabId);

  if(focusTab) {
    selectedButton.focus();
  }

  if(updateAddress) {
    // Non causa reload e permette di riaprire la scheda via URL.
    window.history.replaceState(null, "", "#" + tabId);
  }

  if(tabId === "charts") {
    // Prima mostra immediatamente i campioni gia' disponibili.
    renderHistoryCharts();
    // Poi recupera una versione aggiornata senza attendere il polling.
    updateHistoryForActiveTab();
  }

  if(tabId === "diagnostics") {
    diagnosticsRefresh().catch(function(error) {
      notice("Diagnostica non disponibile: " + error.message);
    });
  }
}

function moveTabByKeyboard(event) {
  let direction = 0;
  const currentId = event.currentTarget.dataset.tab;
  const currentIndex = TAB_IDS.indexOf(currentId);
  let targetIndex = currentIndex;

  if(event.key === "ArrowRight") {
    direction = 1;
  } else if(event.key === "ArrowLeft") {
    direction = -1;
  } else if(event.key === "Home") {
    targetIndex = 0;
  } else if(event.key === "End") {
    targetIndex = TAB_IDS.length - 1;
  } else {
    return;
  }

  event.preventDefault();

  if(direction !== 0) {
    targetIndex = currentIndex + direction;
    if(targetIndex >= TAB_IDS.length) {
      targetIndex = 0;
    }
    if(targetIndex < 0) {
      targetIndex = TAB_IDS.length - 1;
    }
  }

  activateTab(TAB_IDS[targetIndex], true, true);
}

function initializeTabs() {
  for(const id of TAB_IDS) {
    const button = $("tab-button-" + id);
    button.addEventListener("click", function() {
      activateTab(id, false, true);
    });
    button.addEventListener("keydown", moveTabByKeyboard);
  }

  window.addEventListener("hashchange", function() {
    activateTab(tabIdFromHash(), false, false);
  });

  window.addEventListener("resize", renderHistoryCharts);
  activateTab(tabIdFromHash(), false, false);
}

async function refresh(){try{state=await api("/api/state");csrf=state.csrf;
// Associa i secondi dall'avvio dell'ESP8266 all'orario del browser.
// Non richiede RTC, NTP o modifiche al firmware.
historyTimeAnchor = {wallMs:Date.now(),uptimeSec:Number(state.uptime)};$("connection").textContent=state.wifi?(state.mqtt?"Wi-Fi + MQTT connessi":"Wi-Fi · MQTT assente"):"Access point locale";
for(const k of["pv","load"])$(k).textContent=number(state.energy[k].value);
const b=state.energy.battery.value;$("battery").textContent=number(b===null?null:b*(config.energy?.negativeCharge?-1:1));
$("batteryDetail").textContent=`W · ${number(state.energy.voltage.value,1)} V · SOC ${number(state.energy.soc.value)} %`;
$("available").textContent=state.energy.pv.valid&&state.energy.load.valid?number(state.availableW):"—";
$("temperature").textContent=number(state.temperature,1)+" °C";$("humidity").textContent=number(state.humidity)+" %";
$("mode").textContent=({auto:"AUTOMATICO",force_on:"FORZA ON",force_off:"FORZA OFF",diagnostic:"DIAGNOSTICA"})[state.mode];
$("target").textContent=`HEAT ${number(state.setpoint,1)} °C · avvio sotto ${number(config.energy?.tempOn,1)} °C · arresto a ${number(config.energy?.tempOff,1)} °C`;
$("reason").textContent=state.reason;

// Stato delle soglie igrometriche: non presentiamo HEAT come deumidificazione.
const humidityHint=$("humidityHint");
if(!config.energy || !config.energy.humidityAssist) {
  humidityHint.textContent="Assistenza umidità disabilitata. La misura resta disponibile nello storico.";
} else if(state.humidity===null || state.humidity===undefined) {
  humidityHint.textContent="SHT31/umidità non disponibile: nessun nuovo avvio richiesto dall'umidità.";
} else if(state.humidityOnlyRun) {
  humidityHint.textContent="Ciclo HEAT avviato per umidità elevata. Limite termico e protezioni energetiche attivi.";
} else if(state.humidityDemand) {
  humidityHint.textContent="Umidità oltre soglia: richiesta HEAT possibile solo sotto il limite termico e con surplus.";
} else {
  humidityHint.textContent="Umidità monitorata. Richiesta da UR al di sopra di "+
    number(config.energy.humidityOn)+"%; rientro a "+
    number(config.energy.humidityOff)+"%.";
}
dl("climate",[["Stato comandato",state.commanded],["Stato stimato",state.estimated],["Affidabilità indizio",number(state.confidence*100)+" %"],["Variazione potenza",number(state.estimatedClimateW)+" W"],["Ultimo comando",state.lastCommand],["Motivo ultimo comando",state.lastActionReason],["Ora comando",state.lastCommand==="none"?"—":`${number(state.lastCommandAtMs/1000)} s dall’avvio`],["Codici discreti verificati",state.irReady?"Sì":"No"]]);
dl("system",[["Indirizzo IP",state.localIp||"—"],["Assegnazione IP",state.staticIpEnabled?"Statico":"DHCP"],["Uptime",number(state.uptime)+" s"],["Heap libero",number(state.heap)+" byte"],["Spazio OTA",number(state.otaSpace)+" byte"],["Retained ignorati",state.retainedIgnored],["Messaggi rifiutati",state.mqttRejected],["Reset",state.resetReason],["Inverter",state.inverter||"—"]]);
const l=state.learning;$("learnState").textContent=`Fase: ${l.stage} · Spento ${number(l.offW)} W · Acceso ${number(l.onW)} W · Dopo OFF ${number(l.offAfterW)} W · Avvio ${number(l.startupMs/1000)} s · Rumore ${number(l.noiseW)} W`;
$("version").textContent="Firmware "+state.version;
if(state.rebootNeeded)notice("Configurazione salvata. Premi «Riavvia e applica»: comparirà il popup per confermare lo spegnimento.");
if(pollCount++%5===0) {
  historyCache = await api("/api/history");
  renderHistoryCharts();
}
}catch(e){notice("Connessione o dati non disponibili: "+e.message);}finally{setTimeout(refresh,3000);}}
// ============================================================================
// ASSE X TEMPORALE COMUNE AI DUE GRAFICI
//
// L'ESP8266 salva gli istanti dei campioni come secondi dal boot ("s").
// Li convertiamo in orario locale usando una coppia uptime/orologio del
// browser ottenuta al refresh. Gli orari sono indicativi e dipendono
// dall'orologio del dispositivo che visualizza la pagina.
// ============================================================================
function axisTimestampMs(sampleSeconds) {
  if(!historyTimeAnchor || !Number.isFinite(sampleSeconds)) {
    return null;
  }

  const uptimeSeconds = historyTimeAnchor.uptimeSec;
  const wallClockMs = historyTimeAnchor.wallMs;
  if(!Number.isFinite(uptimeSeconds) || !Number.isFinite(wallClockMs)) {
    return null;
  }

  return wallClockMs - (uptimeSeconds - sampleSeconds) * 1000;
}

function formatChartTime(sampleSeconds, rangeSeconds) {
  const timestamp = axisTimestampMs(sampleSeconds);

  if(timestamp === null) {
    // Ripiego leggibile se non e' ancora arrivata la prima risposta /api/state.
    const seconds = Math.max(0, Math.floor(sampleSeconds));
    const hours = String(Math.floor(seconds / 3600)).padStart(2, "0");
    const minutes = String(Math.floor((seconds % 3600) / 60)).padStart(2, "0");
    const remainingSeconds = String(seconds % 60).padStart(2, "0");
    return "T+" + hours + ":" + minutes + ":" + remainingSeconds;
  }

  const date = new Date(timestamp);
  const hour = String(date.getHours()).padStart(2, "0");
  const minute = String(date.getMinutes()).padStart(2, "0");

  if(rangeSeconds < 300) {
    const second = String(date.getSeconds()).padStart(2, "0");
    return hour + ":" + minute + ":" + second;
  }

  if(rangeSeconds >= 86400) {
    const day = String(date.getDate()).padStart(2, "0");
    const month = String(date.getMonth() + 1).padStart(2, "0");
    return day + "/" + month + " " + hour + ":" + minute;
  }

  return hour + ":" + minute;
}

function drawChartTimeAxis(context, rows, left, right, top, bottom, labelY) {
  if(rows.length === 0 || right <= left) {
    return;
  }

  const firstSeconds = rows[0].s;
  const lastSeconds = rows[rows.length - 1].s;
  if(!Number.isFinite(firstSeconds) || !Number.isFinite(lastSeconds)) {
    return;
  }

  const rangeSeconds = Math.max(0, lastSeconds - firstSeconds);
  const axisWidth = right - left;
  let tickCount = 5;

  // Riduce il numero di etichette sui display piccoli per evitare
  // sovrapposizioni, conservando sempre l'inizio e la fine dell'intervallo.
  if(axisWidth < 260) {
    tickCount = 2;
  } else if(axisWidth < 550) {
    tickCount = 3;
  }
  if(rangeSeconds === 0) {
    tickCount = 1;
  }

  context.save();
  context.font = "11px system-ui";
  context.fillStyle = "#637872";
  context.strokeStyle = "#e4e9e1";
  context.lineWidth = 1;
  context.textBaseline = "middle";

  for(let index = 0; index < tickCount; index++) {
    let fraction = 0.5;
    if(tickCount > 1) {
      fraction = index / (tickCount - 1);
    }

    const x = left + fraction * axisWidth;
    const seconds = firstSeconds + fraction * rangeSeconds;

    // Griglia verticale, tacche e orari si riferiscono alla medesima
    // posizione temporale delle curve.
    context.beginPath();
    context.moveTo(x, top);
    context.lineTo(x, bottom + 5);
    context.stroke();

    if(index === 0 && tickCount > 1) {
      context.textAlign = "left";
    } else if(index === tickCount - 1 && tickCount > 1) {
      context.textAlign = "right";
    } else {
      context.textAlign = "center";
    }
    context.fillText(formatChartTime(seconds, rangeSeconds), x, labelY);
  }

  context.restore();
}

// ============================================================================
// GRAFICO ENERGETICO - linee, transizioni IR e asse X con orario
// ============================================================================
function draw(id, rows, series, unit) {
  const canvas = $(id);
  const rectangle = canvas.getBoundingClientRect();
  const pixelRatio = window.devicePixelRatio || 1;
  const width = rectangle.width;
  const height = rectangle.height;

  if(width <= 0 || height <= 0) {
    return;
  }

  canvas.width = Math.round(width * pixelRatio);
  canvas.height = Math.round(height * pixelRatio);

  const context = canvas.getContext("2d");
  context.scale(pixelRatio, pixelRatio);

  const left = 42;
  const right = width - 12;
  const top = 20;
  const bottom = height - 41;

  const values = rows.flatMap(function(row) {
    return series.map(function(entry) {
      return row[entry[0]];
    });
  }).filter(Number.isFinite);

  const minimum = Math.min(0, ...values);
  const maximum = Math.max(1, ...values);
  const valueRange = maximum - minimum;

  context.font = "10px system-ui";
  context.fillStyle = "#637872";
  context.fillText(number(maximum) + " " + unit, 0, 12);
  context.fillText(number(minimum), 0, bottom + 4);

  for(let index = 0; index <= 2; index++) {
    const y = top + index * (bottom - top) / 2;
    context.strokeStyle = "#e4e9e1";
    context.beginPath();
    context.moveTo(left, y);
    context.lineTo(right, y);
    context.stroke();
  }

  drawChartTimeAxis(context, rows, left, right, top, bottom, height - 15);

  if(rows.length < 2) {
    context.fillText("In attesa di campioni…", left, 55);
    return;
  }

  const firstSeconds = rows[0].s;
  const rangeSeconds = Math.max(1, rows[rows.length - 1].s - firstSeconds);

  function xAt(row) {
    return left + (row.s - firstSeconds) / rangeSeconds * (right - left);
  }

  function yAt(value) {
    return top + (maximum - value) / valueRange * (bottom - top);
  }

  for(const [key, color] of series) {
    context.strokeStyle = color;
    context.lineWidth = 1.8;
    context.beginPath();

    let connected = false;
    for(const row of rows) {
      if(!Number.isFinite(row[key])) {
        connected = false;
        continue;
      }

      if(!connected) {
        context.moveTo(xAt(row), yAt(row[key]));
      } else {
        context.lineTo(xAt(row), yAt(row[key]));
      }
      connected = true;
    }
    context.stroke();
  }

  // Eventi di passaggio ON/OFF mantenuti come nella dashboard precedente.
  for(let index = 1; index < rows.length; index++) {
    const current = rows[index];
    const previous = rows[index - 1];

    if(current.known && previous.known && current.on !== previous.on) {
      context.strokeStyle = current.on ? "#21634f" : "#bd6d42";
      context.setLineDash([3, 3]);
      context.beginPath();
      context.moveTo(xAt(current), top);
      context.lineTo(xAt(current), bottom);
      context.stroke();
      context.setLineDash([]);
    }
  }
}

// ============================================================================
// STORICO AMBIENTALE - DUE ASSI E STATO DEL CLIMATIZZATORE
//
// L'asse sinistro misura i gradi Celsius, quello destro l'umidita' relativa.
// La fascia inferiore riporta lo stato comandato/dichiarato dall'operatore:
// NON e' una prova indipendente dello stato fisico del climatizzatore.
// ============================================================================
function drawEnvironmentChart(rows) {
  const canvas = $("envChart");
  const rect = canvas.getBoundingClientRect();
  const pixelRatio = window.devicePixelRatio || 1;
  const width = rect.width;
  const height = rect.height;

  if(width <= 0 || height <= 0) {
    return;
  }

  canvas.width = Math.round(width * pixelRatio);
  canvas.height = Math.round(height * pixelRatio);

  const context = canvas.getContext("2d");
  context.scale(pixelRatio, pixelRatio);
  context.clearRect(0, 0, width, height);

  const left = 46;
  const right = 44;
  const top = 16;
  // Spazio inferiore riservato allo stato IR e alle etichette orarie.
  const bottom = height - 79;
  const stateTop = height - 59;
  const stateHeight = 13;
  const plotHeight = bottom - top;
  const plotWidth = width - left - right;

  context.font = "11px system-ui";
  context.textAlign = "left";

  if(rows.length === 0 || plotWidth <= 0 || plotHeight <= 0) {
    context.fillStyle = "#637872";
    context.fillText("In attesa di campioni ambientali…", left, 42);
    return;
  }

  // Temperatura: scala adattiva. Umidita': scala fissa 0-100% su asse destro.
  const temperatures = [];
  for(const row of rows) {
    if(Number.isFinite(row.t)) {
      temperatures.push(row.t);
    }
  }
  let minTemperature = 15;
  let maxTemperature = 30;
  if(temperatures.length > 0) {
    minTemperature = Math.floor(Math.min(...temperatures) - 1);
    maxTemperature = Math.ceil(Math.max(...temperatures) + 1);
    if(maxTemperature - minTemperature < 4) {
      minTemperature -= 2;
      maxTemperature += 2;
    }
  }

  const firstSecond = rows[0].s;
  const lastSecond = rows[rows.length - 1].s;
  let timeRange = lastSecond - firstSecond;
  if(timeRange < 1) {
    timeRange = 1;
  }

  function xAt(row) {
    return left + ((row.s - firstSecond) / timeRange) * plotWidth;
  }

  function temperatureY(value) {
    return bottom - ((value - minTemperature) /
      (maxTemperature - minTemperature)) * plotHeight;
  }

  function humidityY(value) {
    return bottom - (value / 100) * plotHeight;
  }

  // Linee guida e scale in gradi e percentuale (non condividono l'unita').
  for(let index = 0; index <= 4; index++) {
    const fraction = index / 4;
    const y = bottom - fraction * plotHeight;
    const temperatureLabel = minTemperature +
      fraction * (maxTemperature - minTemperature);
    const humidityLabel = fraction * 100;

    context.strokeStyle = "#e4e9e1";
    context.lineWidth = 1;
    context.beginPath();
    context.moveTo(left, y);
    context.lineTo(width - right, y);
    context.stroke();

    context.fillStyle = "#c96c45";
    context.textAlign = "right";
    context.fillText(number(temperatureLabel, 1), left - 7, y + 3);

    context.fillStyle = "#5c7ac4";
    context.textAlign = "left";
    context.fillText(number(humidityLabel) + "%", width - right + 7, y + 3);
  }

  // Soglie UR di riferimento: guide visive quando la funzione e' abilitata.
  if(config.energy && config.energy.humidityAssist) {
    const thresholdValues = [
      {value: config.energy.humidityOn, text: "UR ON"},
      {value: config.energy.humidityOff, text: "UR OFF"}
    ];

    for(const threshold of thresholdValues) {
      if(!Number.isFinite(threshold.value)) {
        continue;
      }

      context.strokeStyle = "#b6c3d5";
      context.lineWidth = 1;
      context.setLineDash([4, 4]);
      context.beginPath();
      context.moveTo(left, humidityY(threshold.value));
      context.lineTo(width - right, humidityY(threshold.value));
      context.stroke();
      context.setLineDash([]);
    }
  }

  // Rappresentazione a intervalli: ON, OFF e stato sconosciuto.
  for(let index = 0; index < rows.length; index++) {
    const row = rows[index];
    let startX = left;
    let endX = width - right;

    if(index > 0) {
      startX = (xAt(rows[index - 1]) + xAt(row)) / 2;
    }
    if(index + 1 < rows.length) {
      endX = (xAt(row) + xAt(rows[index + 1])) / 2;
    }

    if(!row.known) {
      context.fillStyle = "#c4c9cf";
    } else if(row.on) {
      context.fillStyle = "#21634f";
    } else {
      context.fillStyle = "#91a8a4";
    }

    context.fillRect(startX, stateTop, Math.max(0, endX - startX), stateHeight);
  }

  // Disegno delle misure: i null/non validi producono veri intervalli vuoti.
  function drawMetric(key, color, converter) {
    context.beginPath();
    context.strokeStyle = color;
    context.lineWidth = 2;
    let active = false;

    for(const row of rows) {
      if(!Number.isFinite(row[key])) {
        active = false;
        continue;
      }

      const x = xAt(row);
      const y = converter(row[key]);
      if(active) {
        context.lineTo(x, y);
      } else {
        context.moveTo(x, y);
      }
      active = true;
    }

    context.stroke();
  }

  drawMetric("t", "#c96c45", temperatureY);
  drawMetric("h", "#5c7ac4", humidityY);

  // Segmenti verticali per le transizioni comandate ON/OFF.
  for(let index = 1; index < rows.length; index++) {
    const current = rows[index];
    const previous = rows[index - 1];

    if(current.known && previous.known && current.on !== previous.on) {
      const x = xAt(current);
      context.strokeStyle = "#637872";
      context.lineWidth = 1;
      context.setLineDash([3, 3]);
      context.beginPath();
      context.moveTo(x, top);
      context.lineTo(x, stateTop + stateHeight);
      context.stroke();
      context.setLineDash([]);
    }
  }

  // Le etichette orarie sono allineate ai campioni e agli eventi.
  // La fascia ON/OFF rimane distinguibile dall'asse temporale.
  drawChartTimeAxis(
    context,
    rows,
    left,
    width - right,
    top,
    stateTop + stateHeight,
    height - 11
  );

  context.fillStyle = "#637872";
  context.textAlign = "left";
  context.font = "10px system-ui";
  context.fillText("Clima ON / OFF / ?", left, stateTop + stateHeight + 12);
}

const groups=[
["Wi-Fi e accesso",[["staticIp","IPv4 statico (vuoto = DHCP)"],["staticGateway","Gateway"],["staticSubnet","Subnet mask"],["staticDns1","DNS primario"],["staticDns2","DNS secondario"],["ssid","SSID"],["wifiPassword","Password Wi-Fi","password"],["adminUser","Utente web"],["adminPassword","Nuova password web (min. 12)","password"]]],
["MQTT SolarAssistant",[["mqttHost","Host broker"],["mqttPort","Porta","number"],["mqttUser","Utente MQTT"],["mqttPassword","Password MQTT","password"],["discovery","Filtro diagnostico (vuoto = disattivo)"],["mqttKeepalive","Keepalive (s)","number"]]],
["Topic esatti",[...["pv","load","battery","voltage","current","soc","import","export","inverter"].map(k=>["topics."+k,k])]],
["Energia e AGM",[["automationEnabled","Abilita automazione","checkbox"],["energy.reserveW","Riserva ricarica (W)","number"],["energy.climateW","Potenza clima prevista (W)","number"],["energy.dischargeW","Scarica critica (W)","number"],["energy.minVoltage","Tensione minima (V, 0 disabilita)","number"],["energy.maxDischargeA","Scarica massima (A, 0 disabilita)","number"],["energy.negativeCharge","Potenza negativa = carica","checkbox"],["energy.negativeCurrentCharge","Corrente negativa = carica","checkbox"]]],
["Ambiente",[
  ["energy.tempOn","Accensione sotto (°C)","number"],
  ["energy.tempOff","Temperatura massima (°C)","number"],
  ["energy.humidityAssist","Abilita assistenza umidità in HEAT","checkbox"],
  ["energy.humidityOn","UR elevata: soglia richiesta (%)","number"],
  ["energy.humidityOff","UR rientrata: soglia arresto (%)","number"],
  ["setpoint","Setpoint IR HEAT (°C)","number"],
  ["sampleMs","Campionamento (ms)","number"],
  ["filterAlpha","Filtro EMA (0,01–1)","number"]
]],
["Tempi e storico",[["energy.stableMs","Stabilità avvio (ms)","number"],["energy.dropMs","Verifica calo (ms)","number"],["energy.restartMs","Minimo tra riavvii (ms)","number"],["energy.minRunMs","Funzionamento minimo (ms)","number"],["energy.staleMs","Scadenza dati (ms)","number"],["historyMs","Intervallo storico RAM (ms)","number"],["forceOnMs","Durata Forza ON (ms)","number"]]],
["Infrarossi",[["protocol","Protocollo",["RAW","KELON","WHIRLPOOL_AC"]],["powerKind","Semantica alimentazione",["unverified","discrete","toggle"]],["irVerified","Ho verificato ON/OFF distinti, HEAT e setpoint sul modello reale","checkbox"],["receiver","Ricevitore GPIO12 abilitato","checkbox"],["model","Modello IR (id libreria)","number"],["fan","Ventola",["auto","low","medium","high"]]]],
["Riconoscimento assorbimento",[["detector.baselineMs","Finestra riferimento (ms)","number"],["detector.startupMs","Attesa compressore (ms)","number"],["detector.observationMs","Finestra verifica (ms)","number"],["detector.deltaW","Variazione minima (W)","number"],["detector.maxNoiseW","Rumore massimo (W)","number"]]]];
function get(obj,path){return path.split(".").reduce((v,k)=>v?.[k],obj);}
function set(obj,path,value){const keys=path.split("."),last=keys.pop();let target=obj;for(const key of keys)target=target[key]||(target[key]={});target[last]=value;}
// ============================================================================
// CONFIGURAZIONE A GRUPPI ESPANDIBILI
// Ogni campo originale conserva data-path, tipo, selezione e salvataggio.
// I <details> riducono lo scorrimento senza escludere campi dal form.
// ============================================================================
function fields() {
  const container = $("fields");
  container.replaceChildren();

  for(const [title, items] of groups) {
    const section = document.createElement("details");
    section.className = "form-section";

    // I parametri d'uso frequente sono visibili subito.
    if(title === "MQTT SolarAssistant" || title === "Ambiente") {
      section.open = true;
    }

    const summary = document.createElement("summary");
    summary.textContent = title;
    section.append(summary);

    const body = document.createElement("div");
    body.className = "form-fields";

    for(const [path, label, type = "text"] of items) {
      const fieldLabel = document.createElement("label");
      fieldLabel.className = "field";
      fieldLabel.append(document.createTextNode(label));

      let input;
      if(Array.isArray(type)) {
        input = document.createElement("select");
        for(const value of type) {
          const option = document.createElement("option");
          option.value = value;
          option.textContent = value;
          input.append(option);
        }
      } else {
        input = document.createElement("input");
        input.type = type;
      }
      input.dataset.path = path;

      if(type === "number") {
        input.step = "any";
      }

      if(type === "checkbox") {
        input.checked = Boolean(get(config, path));
      } else if(type === "password") {
        input.value = "";
        input.autocomplete = "new-password";
      } else {
        const currentValue = get(config, path);
        if(currentValue === undefined || currentValue === null) {
          input.value = "";
        } else {
          input.value = currentValue;
        }
      }

      fieldLabel.append(input);
      body.append(fieldLabel);
    }

    section.append(body);
    container.append(section);
  }
}
// ============================================================================
// CONFERMA OFF CONTESTUALE
//
// Ogni operazione che richiede il climatizzatore fisicamente spento mostra
// direttamente un popup, senza dover cambiare scheda. L'utente puo' annullare
// senza effettuare alcuna richiesta di modifica al dispositivo.
//
// Il browser non puo' verificare la condizione fisica del climatizzatore:
// l'operatore deve averla effettivamente controllata prima di confermare.
// Il firmware riceve una dichiarazione esplicita, autenticata e con CSRF.
// NON viene trasmesso alcun comando infrarosso.
// ============================================================================
async function confirmPhysicalOffForAction(actionDescription) {
  const accepted = window.confirm(
    "CONFERMA STATO FISICO DEL CLIMATIZZATORE\n\n" +
    "Operazione richiesta: " + actionDescription + "\n\n" +
    "Hai verificato personalmente che il climatizzatore sia " +
    "FISICAMENTE SPENTO?\n\n" +
    "Premi OK solo dopo averlo controllato. " +
    "Questa conferma non invia comandi infrarossi.\n\n" +
    "Premi Annulla se il climatizzatore è acceso o non puoi verificarlo."
  );

  if(!accepted) {
    notice("Operazione annullata: stato fisico OFF non confermato.");
    return false;
  }

  const result = await api("/api/confirm-physical-off", {confirmed: true});
  if(!result || result.commanded !== "off" || result.confirmed !== true) {
    throw new Error("Il dispositivo non ha registrato lo stato fisico OFF.");
  }

  // Evita che un successivo click faccia riferimento a uno stato vecchio.
  state.commanded = "off";
  return true;
}

// Il valore false segnala una cancellazione volontaria dell'utente.
// Tutte le altre azioni mantengono il feedback e la gestione errori originali.
async function act(fn) {
  try {
    const result = await fn();
    if(result !== false) {
      notice("Operazione completata.");
    }
  } catch(error) {
    notice(error.message);
  }
}

document.querySelectorAll("[data-mode]").forEach(function(button) {
  button.onclick = function() {
    act(async function() {
      const requestedMode = button.dataset.mode;

      // Auto e Forza ON richiedono uno stato fisico noto. Quando e'
      // sconosciuto proponiamo la verifica OFF direttamente qui.
      if((requestedMode === "auto" || requestedMode === "force_on") &&
         state.commanded !== "off" && state.commanded !== "on") {
        const accepted = await confirmPhysicalOffForAction(
          "Attivare la modalità " + requestedMode.toUpperCase()
        );
        if(!accepted) {
          return false;
        }
      }

      return await api("/api/mode", {mode: requestedMode});
    });
  };
});

// ============================================================================
// PROFILO MQTT SOLARASSISTANT - ASSOCIAZIONE MISURE VERIFICATA
//
// Il pulsante salva soltanto host, porta, filtro e associazioni misure
// individuati sul broker domestico. Le credenziali MQTT, Wi-Fi e web non
// vengono lette o riscritte: il firmware conserva i valori gia' salvati.
//
// Un solo popup contestuale conferma lo spegnimento fisico prima del POST.
// Il firmware richiede comunque CSRF, autenticazione e stato OFF noto.
// L'automazione viene disabilitata finche' non si verificano i nuovi dati.
// ============================================================================
function knownSolarAssistantConfig(currentConfig) {
  const updated = structuredClone(currentConfig);

  updated.mqttHost = "192.168.1.240";
  updated.mqttPort = 1883;
  updated.discovery = "solar_assistant/#";

  updated.topics = {
    pv: "solar_assistant/inverter_1/pv_power/state",
    load: "solar_assistant/inverter_1/load_power/state",
    battery: "solar_assistant/total/battery_power/state",
    voltage: "solar_assistant/battery_1/voltage/state",
    current: "solar_assistant/battery_1/current/state",
    soc: "solar_assistant/total/battery_state_of_charge/state",
    import: "",
    export: "",
    inverter: "solar_assistant/inverter_1/device_mode/state"
  };

  updated.energy.negativeCharge = false;
  updated.energy.negativeCurrentCharge = false;
  updated.automationEnabled = false;

  return updated;
}

$("applySolarAssistantTopics").onclick = function() {
  // Il profilo e' applicato alla configurazione gia' presente nel firmware,
  // non a un oggetto vuoto: password e parametri estranei restano invariati.
  // Il backend fa parseConfig() su una copia della configurazione corrente.
  async function applyProfile() {
    try {
      const next = knownSolarAssistantConfig(config);

      const accepted = await confirmPhysicalOffForAction(
        "Applicare il profilo MQTT SolarAssistant del broker 192.168.1.240. " +
        "I topic e il segno batteria saranno aggiornati; " +
        "le credenziali resteranno invariate e l'automazione sarà disabilitata."
      );

      if(!accepted) {
        return;
      }

      await api("/api/config", next);
      config = next;
      fields();

      notice(
        "Profilo SolarAssistant salvato. Le credenziali sono conservate. " +
        "Premi «Riavvia e applica» per aggiornare le sottoscrizioni MQTT. " +
        "Mantieni l'automazione disabilitata fino alla verifica dei valori."
      );
    } catch(error) {
      notice("Profilo SolarAssistant non applicato: " + error.message);
    }
  }

  applyProfile();
};

$("settings").onsubmit = function(event) {
  event.preventDefault();
  act(async function() {
    const next = structuredClone(config);

    document.querySelectorAll("[data-path]").forEach(function(input) {
      if(input.type === "password" && !input.value) {
        return;
      }

      let value = input.value;
      if(input.type === "checkbox") {
        value = input.checked;
      } else if(input.type === "number") {
        value = Number(input.value);
      }

      set(next, input.dataset.path, value);
    });

    if($("clearMqtt").checked) {
      next.mqttPassword = "";
    }

    const accepted = await confirmPhysicalOffForAction(
      "Salvare la configurazione del controller"
    );
    if(!accepted) {
      return false;
    }

    return await api("/api/config", next);
  });
};

$("reboot").onclick = function() {
  act(async function() {
    const accepted = await confirmPhysicalOffForAction(
      "Riavviare il controller e applicare la configurazione"
    );
    if(!accepted) {
      return false;
    }

    return await api("/api/reboot", {});
  });
};

$("export").onclick = function() {
  act(async function() {
    download("solar-config-redacted.json", await api("/api/config"));
  });
};

$("confirmOff").onclick = function() {
  act(async function() {
    const accepted = await confirmPhysicalOffForAction(
      "Confermare lo stato fisico OFF"
    );
    if(!accepted) {
      return false;
    }
    return {};
  });
};

$("confirmOn").onclick = function() {
  if(window.confirm(
    "Hai verificato personalmente che il climatizzatore sia FISICAMENTE ACCESO?"
  )) {
    act(function() {
      return api("/api/ir", {action: "confirm_state", on: true, confirmed: true});
    });
  }
};
for(const[button,on]of[["irOn",true],["irOff",false]])$(button).onclick=()=>{const actual=$("actualState").value;if(actual==="unknown"){notice("Selezionare lo stato fisico osservato prima del test.");return;}if(confirm(`Confermi il climatizzatore fisicamente ${actual.toUpperCase()} e l’invio di un solo comando ${on?"ON HEAT":"OFF"}?`)){act(()=>api("/api/ir",{action:on?"on":"off",actualOn:actual==="on",confirmed:true}));$("actualState").value="unknown";}};
// Una cattura e' un segnale ricevuto, non la conferma dello stato reale.
// Il JSON RAW rimane consultabile per il collaudo senza perdere informazioni.
async function readIrCapture() {
  const capture = await api("/api/ir");
  $("irCapture").textContent = JSON.stringify(capture, null, 2);

  const receivedLength = Number(capture.length);
  const protocol = String(capture.protocol);
  const savedOnLength = Number(capture.onLength);
  const savedOffLength = Number(capture.offLength);

  if(!Number.isFinite(receivedLength) || receivedLength <= 0) {
    $("irCaptureSummary").textContent =
      "Nessuna cattura RAW valida disponibile. Controllare G/R/Y, " +
      "abilitazione ricevitore GPIO12, riavvio e orientamento del telecomando.";
    return;
  }

  $("irCaptureSummary").textContent =
    "Ricevuto: protocollo " + protocol +
    ", " + receivedLength + " durate RAW. Slot HEAT ON: " +
    savedOnLength + " durate; slot OFF: " + savedOffLength +
    " durate. Non salvare come ON/OFF i comandi DRY.";
}

$("capture").onclick = function() {
  act(readIrCapture);
};
// Salvataggio RAW: una sola conferma contestuale comprende sia lo stato
// fisico OFF sia la conseguenza sui codici di automazione.
for(const [buttonId, action] of [
  ["saveOn", "capture_on"],
  ["saveOff", "capture_off"]
]) {
  $(buttonId).onclick = function() {
    act(async function() {
      const accepted = await confirmPhysicalOffForAction(
        "Salvare il codice infrarosso " + action +
        ". La verifica IR e l'automazione saranno disabilitate fino al collaudo."
      );
      if(!accepted) {
        return false;
      }
      return await api("/api/ir", {action, confirmed: true});
    });
  };
}

// L'apprendimento dei consumi conserva i passaggi precedenti.
// Inizio riferimento e salvataggio finale richiedono ora OFF contestuale.
document.querySelectorAll("[data-learn]").forEach(function(button) {
  button.onclick = function() {
    act(async function() {
      const action = button.dataset.learn;

      if(action === "baseline" || action === "save") {
        let message = "Salvare l'apprendimento con climatizzatore spento";
        if(action === "baseline") {
          message = "Misurare il consumo di riferimento con climatizzatore spento";
        }

        const accepted = await confirmPhysicalOffForAction(message);
        if(!accepted) {
          return false;
        }
      }

      if(action === "on" || action === "off") {
        let expectedPhysicalState = "ACCESO";
        if(action === "on") {
          expectedPhysicalState = "SPENTO";
        }
        const accepted = window.confirm(
          "Hai osservato il climatizzatore fisicamente " +
          expectedPhysicalState + " prima di inviare il comando " +
          action.toUpperCase() + "?"
        );
        if(!accepted) {
          notice("Comando annullato: stato fisico non confermato.");
          return false;
        }
      }

      return await api("/api/learn", {action, confirmed: true});
    });
  };
});
async function diagnosticsRefresh(){diagnostics=await api("/api/diagnostics");$("topics").replaceChildren();for(const row of diagnostics.topics){const tr=document.createElement("tr");for(const value of[row.topic,row.value,number(row.ageMs/1000)+" s",row.retained?"Sì · ignorato":"No"]){const td=document.createElement("td");td.textContent=value;tr.append(td);}$("topics").append(tr);}$("events").textContent=diagnostics.events.map(e=>`${(e.atMs/1000).toFixed(0)}s  ${e.message}`).join("\n");}
$("refreshDiagnostics").onclick=()=>act(diagnosticsRefresh);$("exportLogs").onclick=()=>act(async()=>download("solar-log.json",await api("/api/diagnostics")));
initializeTabs();

(async()=>{
  try {
    config=await api("/api/config");
    fields();

    // Il valore configurato puo' essere in attesa del riavvio: esplicitarlo.
    if(config.receiver) {
      $("irReceiverStatus").textContent =
        "Ricevitore GPIO12 abilitato nella configurazione. " +
        "Se hai modificato questa opzione, riavvia prima di acquisire.";
    } else {
      $("irReceiverStatus").textContent =
        "Ricevitore GPIO12 disabilitato. Attivalo in Configurazione → " +
        "Infrarossi, salva e riavvia prima dell'acquisizione.";
    }

    await diagnosticsRefresh();
  } catch(error) {
    notice(error.message);
  }
  refresh();
})();
