const mqtt = require('mqtt');
const path = require('path');
const http = require('http');
const fs = require('fs');

const ROOT = process.env.MQTT_ROOT || 'harsh_smartpark';
const BROKER = process.env.MQTT_URL || 'mqtt://broker.hivemq.com:1883';
const PORT = Number(process.env.PORT || 3000);
// Heartbeats arrive every 3 seconds. Four missed heartbeats means offline;
// the broker's retained last-will marks a detected unexpected disconnect immediately.
const OFFLINE_MS = 12000;
const slots = {1:[0,0,0,0], 2:[0,0,0,0], 3:[0,0,0,0]};
const lastSeen = {1:0, 2:0, 3:0};
const reportedOnline = {1:false, 2:false, 3:false};
const knownSlots = {1:false, 2:false, 3:false};
let lastPublishedState = '';

const client = mqtt.connect(BROKER, {
  clientId: `smartpark-edge-${Math.random().toString(16).slice(2, 10)}`,
  reconnectPeriod: 2000,
  connectTimeout: 10000,
  clean: true
});

function makeSummary() {
  const now = Date.now();
  const floors = [1, 2, 3].map(floor => {
    const offline = !reportedOnline[floor] || !lastSeen[floor] || now - lastSeen[floor] > OFFLINE_MS;
    const current = slots[floor].slice();
    const free = offline || !knownSlots[floor] ? 0 : current.filter(s => s === 0).length;
    return { floor, slots: current, free, occupied: offline || !knownSlots[floor] ? 0 : 4 - free, offline };
  });
  const totalFree = floors.reduce((sum, f) => sum + f.free, 0);
  const best = floors.filter(f => !f.offline && f.free > 0)
    .sort((a, b) => b.free - a.free || a.floor - b.floor)[0];
  return {
    ts: now,
    floors,
    totalFree,
    offlineFloors: floors.filter(f => f.offline).map(f => f.floor),
    bestFloor: best ? best.floor : 0
  };
}

function publishSummary(force = false) {
  if (!client.connected) return;
  const summary = makeSummary();
  // Ignore the changing timestamp when deciding whether parking state changed.
  const state = JSON.stringify({ floors: summary.floors, totalFree: summary.totalFree,
    offlineFloors: summary.offlineFloors, bestFloor: summary.bestFloor });
  if (!force && state === lastPublishedState) return;
  lastPublishedState = state;
  client.publish(`${ROOT}/summary`, JSON.stringify(summary), { retain: true });
}

client.on('connect', () => {
  console.log(`Edge connected to ${BROKER}; prefix=${ROOT}/`);
  client.subscribe(`${ROOT}/+/slots`);
  client.subscribe(`${ROOT}/+/status`);
  publishSummary(true);
});
client.on('reconnect', () => console.log('Broker reconnecting...'));
client.on('error', err => console.error('MQTT:', err.message));
client.on('message', (topic, payload) => {
  const parts = topic.split('/');
  const floor = Number((parts[1] || '').replace('floor', ''));
  if (![1, 2, 3].includes(floor)) return;
  if (parts[2] === 'slots') {
    try {
      const data = JSON.parse(payload.toString());
      if (Array.isArray(data.slots) && data.slots.length === 4) {
        if (!data.slots.every(value => value === 0 || value === 1 || value === false || value === true)) {
          console.error(`Invalid slot values on ${topic}`);
          return;
        }
        const nextSlots = data.slots.map(value => Number(value) ? 1 : 0);
        const slotsChanged = !knownSlots[floor] || nextSlots.some((value, i) => value !== slots[floor][i]);
        slots[floor] = nextSlots;
        knownSlots[floor] = true;
        if (reportedOnline[floor]) lastSeen[floor] = Date.now();
        if (slotsChanged) console.log(`F${floor}: ${slots[floor].map(v => v ? 'OCCUPIED' : 'FREE').join(' | ')}`);
      }
    } catch {
      console.error(`Bad JSON on ${topic}`);
    }
  } else if (parts[2] === 'status') {
    const status = payload.toString();
    if (status === 'online') {
      const justCameOnline = !reportedOnline[floor];
      reportedOnline[floor] = true;
      lastSeen[floor] = Date.now();
      if (justCameOnline) console.log(`F${floor} online`);
    } else if (status === 'offline') {
      // Honor MQTT's retained last-will immediately; don't show stale free bays.
      const wasOnline = reportedOnline[floor];
      reportedOnline[floor] = false;
      lastSeen[floor] = 0;
      if (wasOnline) console.log(`F${floor} offline (last will)`);
    }
  }
  publishSummary();
});

const dashboardDir = path.join(__dirname, '..', 'dashboard');
const httpServer = http.createServer((req, res) => {
  const pathname = new URL(req.url, `http://${req.headers.host}`).pathname;
  const file = pathname === '/' ? 'index.html' : path.basename(pathname);
  fs.readFile(path.join(dashboardDir, file), (err, body) => {
    if (err) { res.writeHead(404); return res.end('Not found'); }
    const type = file.endsWith('.html') ? 'text/html; charset=utf-8' : 'text/plain';
    res.writeHead(200, { 'Content-Type': type });
    res.end(body);
  });
});
httpServer.listen(PORT, () => console.log(`Dashboard: http://localhost:${PORT}`));
// Recheck only for heartbeat timeouts; publishSummary suppresses unchanged data.
setInterval(() => publishSummary(), 1000);
setInterval(() => {
  const s = makeSummary();
  console.log(`Edge: ${s.totalFree}/12 free; offline floors: ${s.offlineFloors.join(',') || 'none'}`);
}, 5000);
