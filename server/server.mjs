import { createServer } from 'node:http';
import { createHash, timingSafeEqual } from 'node:crypto';
import { DatabaseSync } from 'node:sqlite';
import { mkdirSync } from 'node:fs';
import { dirname } from 'node:path';
import WebSocket from 'faye-websocket';
import { createPushWorker } from './push.mjs';

const ID = /^[A-Za-z0-9_-]{8,80}$/;
const STATES = new Set(['closed', 'opening', 'open', 'closing']);
const TYPES = new Set(['boot', 'doorbell', 'command_started', 'gate_opened', 'gate_closed', 'command_done', 'command_rejected']);
const EXPO_TOKEN = /^(ExpoPushToken|ExponentPushToken)\[[A-Za-z0-9_-]{10,200}\]$/;
const activeStates = ['sent', 'running'];
class RpcError extends Error { constructor(code, message) { super(message); this.code = code; } }
function secret(value, name) {
  if (typeof value !== 'string' || !/^[a-f0-9]{64}$/i.test(value)) throw new Error(`${name}: use uma chave de 64 caracteres hexadecimais.`);
  return value;
}
function equal(a, b) {
  if (typeof a !== 'string') return false;
  const hash = (s) => createHash('sha256').update(s).digest();
  return timingSafeEqual(hash(a), hash(b));
}
function validId(value) { return typeof value === 'string' && ID.test(value); }

export function createGateServer(options = {}) {
  const userKey = secret(options.userToken ?? process.env.USER_API_TOKEN, 'USER_API_TOKEN');
  const servoKey = secret(options.servoToken ?? process.env.SERVO_API_TOKEN ?? process.env.DEVICE_API_TOKEN, 'SERVO_API_TOKEN');
  if (userKey === servoKey) throw new Error('As chaves de usuario e servo devem ser diferentes.');
  const servoId = options.servoId ?? process.env.SERVO_ID ?? 'servo-01';
  const clock = options.clock ?? Date.now;
  const path = options.dbPath ?? process.env.DB_PATH ?? './data/portao.sqlite';
  if (path !== ':memory:') mkdirSync(dirname(path), { recursive: true });
  const db = new DatabaseSync(path);
  db.exec(`PRAGMA journal_mode=WAL;
    CREATE TABLE IF NOT EXISTS servo_state_v2 (id TEXT PRIMARY KEY,last_seen INTEGER NOT NULL,boot_id TEXT NOT NULL,payload TEXT NOT NULL);
    CREATE TABLE IF NOT EXISTS commands (id TEXT PRIMARY KEY,action TEXT NOT NULL,status TEXT NOT NULL,created_at INTEGER NOT NULL,expires_at INTEGER NOT NULL,updated_at INTEGER NOT NULL,reason TEXT);
    CREATE TABLE IF NOT EXISTS events (sequence INTEGER PRIMARY KEY AUTOINCREMENT,id TEXT NOT NULL UNIQUE,type TEXT NOT NULL,command_id TEXT,occurred_at INTEGER,received_at INTEGER NOT NULL,detail TEXT,source TEXT);
    CREATE TABLE IF NOT EXISTS push_tokens (token TEXT PRIMARY KEY,updated_at INTEGER NOT NULL);
    CREATE TABLE IF NOT EXISTS push_jobs (id INTEGER PRIMARY KEY AUTOINCREMENT,event_id TEXT NOT NULL,token TEXT NOT NULL,status TEXT NOT NULL DEFAULT 'pending',attempts INTEGER NOT NULL DEFAULT 0,next_at INTEGER NOT NULL,ticket_id TEXT,error TEXT,created_at INTEGER NOT NULL,UNIQUE(event_id,token));`);
  if (!db.prepare('PRAGMA table_info(events)').all().some((c) => c.name === 'source')) db.exec('ALTER TABLE events ADD COLUMN source TEXT');
  // Migracao preserva historico v1; nenhum comando pendente sera reexecutado.
  db.prepare("UPDATE commands SET status='interrupted',reason='Servidor reiniciado',updated_at=? WHERE status IN ('queued','sent','running')").run(clock());
  const clients = new Set();
  let servo = null, stopped = false;
  const previous = db.prepare('SELECT * FROM servo_state_v2 WHERE id=?').get(servoId);
  let telemetry = previous ? JSON.parse(previous.payload) : null;
  let lastSeen = previous?.last_seen ?? null;
  const push = createPushWorker(db, { clock, fetchPush: options.fetchPush, accessToken: options.expoAccessToken ?? process.env.EXPO_ACCESS_TOKEN });
  function transaction(fn) {
    db.exec('BEGIN IMMEDIATE');
    try { const result = fn(); db.exec('COMMIT'); return result; }
    catch (error) { db.exec('ROLLBACK'); throw error; }
  }
  function send(client, packet) {
    if (client?.ws.readyState === 1) client.ws.send(JSON.stringify({ ...packet, serverTime: clock() }));
  }
  function broadcast(packet) { for (const c of clients) if (c.role === 'app') send(c, packet); }
  function command(id) { return db.prepare('SELECT id,action,status,reason,expires_at AS expiresAt FROM commands WHERE id=?').get(id); }
  function status() {
    const online = Boolean(servo && servo.ws.readyState === 1 && servo.lastTelemetry && clock() - servo.lastTelemetry < 5000);
    const pending = db.prepare("SELECT id,action,status FROM commands WHERE status IN ('sent','running') ORDER BY created_at LIMIT 1").get();
    return { deviceId: servoId, online, servoOnline: online, panelOnline: Boolean(online && telemetry?.panelOnline),
      state: telemetry?.state ?? 'unknown', angle: telemetry?.angle ?? null, targetAngle: telemetry?.targetAngle ?? null,
      estimated: true, lastSeen, pendingCommand: pending ?? null, outboxCount: telemetry?.outboxCount ?? 0 };
  }
  function publishStatus() { broadcast({ type: 'state', status: status() }); }
  function history({ before = Number.MAX_SAFE_INTEGER, limit = 30 } = {}) {
    if (!Number.isSafeInteger(before) || before < 1 || !Number.isInteger(limit) || limit < 1 || limit > 100) throw new RpcError('invalid', 'Paginacao invalida.');
    const items = db.prepare(`SELECT sequence,id,type,command_id AS commandId,occurred_at AS occurredAt,received_at AS receivedAt,detail,source
      FROM events WHERE sequence<? ORDER BY sequence DESC LIMIT ?`).all(before, limit);
    return { items, nextBefore: items.length === limit ? items.at(-1).sequence : null };
  }
  function finishCommand(id, phase, detail = null) {
    const existing = command(id);
    if (!existing) return;
    if (existing.status === 'completed' || existing.status === 'rejected') return;
    db.prepare('UPDATE commands SET status=?,reason=?,updated_at=? WHERE id=?').run(phase, detail, clock(), id);
    broadcast({ type: 'command_result', command: command(id) });
  }
  function saveEvent(event) {
    if (!event || !validId(event.id) || !TYPES.has(event.type) || !['servo', 'panel'].includes(event.source) ||
        (event.source === 'panel' && event.type !== 'doorbell') ||
        (event.commandId != null && !validId(event.commandId)) ||
        (event.detail != null && (typeof event.detail !== 'string' || event.detail.length > 120))) throw new RpcError('invalid_event', 'Evento invalido.');
    const now = clock();
    const occurredAt = Number.isSafeInteger(event.occurredAt) && event.occurredAt >= 1_600_000_000_000 && event.occurredAt <= now + 300_000 ? event.occurredAt : null;
    const changed = transaction(() => {
      const n = db.prepare('INSERT OR IGNORE INTO events (id,type,command_id,occurred_at,received_at,detail,source) VALUES (?,?,?,?,?,?,?)')
        .run(event.id, event.type, event.commandId ?? null, occurredAt, now, event.detail ?? null, event.source).changes;
      if (n && ['doorbell', 'gate_opened'].includes(event.type)) db.prepare('INSERT OR IGNORE INTO push_jobs (event_id,token,next_at,created_at) SELECT ?,token,?,? FROM push_tokens').run(event.id, now, now);
      return n;
    });
    if (event.commandId) {
      if (event.type === 'command_started') {
        const c = command(event.commandId);
        if (c && activeStates.includes(c.status)) finishCommand(c.id, 'running');
      } else if (['gate_opened', 'gate_closed', 'command_done'].includes(event.type)) finishCommand(event.commandId, 'completed');
      else if (event.type === 'command_rejected') finishCommand(event.commandId, 'rejected', event.detail);
    }
    if (changed) {
      const saved = db.prepare('SELECT sequence,id,type,command_id AS commandId,occurred_at AS occurredAt,received_at AS receivedAt,detail,source FROM events WHERE id=?').get(event.id);
      broadcast({ type: 'event', event: saved });
    }
    return event.id;
  }
  function acceptTelemetry(client, data) {
    if (!data || !STATES.has(data.state) || !Number.isInteger(data.angle) || data.angle < 0 || data.angle > 180 ||
        !Number.isInteger(data.targetAngle) || data.targetAngle < 0 || data.targetAngle > 180 || typeof data.panelOnline !== 'boolean' ||
        !Number.isInteger(data.outboxCount) || data.outboxCount < 0 || data.outboxCount > 128 ||
        (data.commandId != null && (!validId(data.commandId) || !['running', 'completed'].includes(data.commandPhase)))) throw new RpcError('invalid_telemetry', 'Telemetria invalida.');
    telemetry = data; lastSeen = clock(); client.lastTelemetry = lastSeen;
    db.prepare('INSERT INTO servo_state_v2 VALUES (?,?,?,?) ON CONFLICT(id) DO UPDATE SET last_seen=excluded.last_seen,boot_id=excluded.boot_id,payload=excluded.payload')
      .run(servoId, lastSeen, client.bootId, JSON.stringify(data));
    if (data.commandId) {
      const c = command(data.commandId);
      if (c && c.status !== data.commandPhase) finishCommand(c.id, data.commandPhase);
    }
    publishStatus();
  }
  function rpc(method, data = {}) {
    switch (method) {
      case 'status': return status();
      case 'history': return history(data);
      case 'command_status': {
        if (!validId(data.id)) throw new RpcError('invalid', 'ID invalido.');
        const c = command(data.id); if (!c) throw new RpcError('not_found', 'Comando nao encontrado.'); return c;
      }
      case 'command': {
        if (!validId(data.id) || !['open', 'close'].includes(data.action)) throw new RpcError('invalid', 'Comando invalido.');
        const existing = command(data.id);
        if (existing) {
          if (existing.action !== data.action) throw new RpcError('conflict', 'ID ja usado para outra acao.');
          return { ...existing, duplicate: true };
        }
        if (!Number.isSafeInteger(data.expiresAt) || data.expiresAt <= clock()) throw new RpcError('expired', 'Comando chegou vencido. Verifique a rede e tente novamente.');
        if (data.expiresAt > clock() + 5500) throw new RpcError('invalid', 'Prazo de comando invalido.');
        const current = status();
        if (!current.online) throw new RpcError('offline', 'ESP32 do servo offline; comando nao foi enfileirado.');
        if (current.pendingCommand || ['opening', 'closing'].includes(current.state)) throw new RpcError('busy', 'Aguarde o movimento atual.');
        const now = clock();
        transaction(() => {
          db.prepare("INSERT INTO commands VALUES (?,?,'sent',?,?,?,NULL)").run(data.id, data.action, now, Math.min(data.expiresAt, now + 5000), now);
          db.prepare("INSERT INTO events (id,type,command_id,occurred_at,received_at,detail,source) VALUES (?,'command_requested',?,?,?,?,'app')")
            .run(`request-${data.id}`, data.id, now, now, data.action);
        });
        send(servo, { type: 'command', id: data.id, action: data.action, expiresAt: Math.min(data.expiresAt, now + 5000), serverTime: now });
        const saved = history({ limit: 1 }).items[0]; broadcast({ type: 'event', event: saved }); publishStatus();
        return command(data.id);
      }
      case 'push_register': {
        if (typeof data.token !== 'string' || !EXPO_TOKEN.test(data.token)) throw new RpcError('invalid', 'Token Expo invalido.');
        const exists = db.prepare('SELECT 1 FROM push_tokens WHERE token=?').get(data.token);
        if (!exists && db.prepare('SELECT count(*) AS n FROM push_tokens').get().n >= 10) throw new RpcError('limit', 'Limite de dez celulares.');
        db.prepare('INSERT INTO push_tokens VALUES (?,?) ON CONFLICT(token) DO UPDATE SET updated_at=excluded.updated_at').run(data.token, clock());
        return { registered: true };
      }
      case 'push_remove': {
        if (typeof data.token !== 'string' || !EXPO_TOKEN.test(data.token)) throw new RpcError('invalid', 'Token Expo invalido.');
        db.prepare('DELETE FROM push_tokens WHERE token=?').run(data.token);
        db.prepare("UPDATE push_jobs SET status='disabled' WHERE token=? AND status IN ('pending','ticket')").run(data.token);
        return { removed: true };
      }
      default: throw new RpcError('unknown_method', 'Metodo desconhecido.');
    }
  }
  const server = createServer((req, res) => {
    const health = req.method === 'GET' && req.url === '/health';
    res.writeHead(health ? 200 : 404, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
    res.end(JSON.stringify(health ? { ok: true, protocol: 2 } : { error: 'Use WebSocket /ws.' }));
  });
  server.on('upgrade', (req, socket, head) => {
    if (req.url !== '/ws' || !WebSocket.isWebSocket(req) || clients.size >= 32) {
      socket.end('HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n'); return;
    }
    const ws = new WebSocket(req, socket, head, ['arduino'], { maxLength: 16_384, ping: 10 });
    const client = { ws, role: null, bootId: null, lastTelemetry: 0, window: clock(), count: 0 };
    clients.add(client);
    const timeout = setTimeout(() => ws.close(4003, 'Authentication timeout'), 5000);
    ws.on('error', () => {});
    ws.on('message', ({ data: raw }) => {
      if (stopped) return;
      let message;
      try {
        if (typeof raw !== 'string') throw new RpcError('invalid', 'Envie JSON em texto.');
        message = JSON.parse(raw);
        if (!message || typeof message !== 'object' || Array.isArray(message)) throw new RpcError('invalid', 'Objeto JSON obrigatorio.');
        if (clock() - client.window >= 60_000) { client.window = clock(); client.count = 0; }
        if (++client.count > (client.role === 'servo' ? 1200 : 180)) { ws.close(1008, 'Rate limit'); return; }
        if (!client.role) {
          if (message.type !== 'auth' || message.protocol !== 2 ||
              !((message.role === 'app' && equal(message.token, userKey)) ||
                (message.role === 'servo' && equal(message.token, servoKey) && message.deviceId === servoId && validId(message.bootId)))) {
            send(client, { type: 'error', code: 'auth_failed', message: 'Acesso invalido ou protocolo diferente de 2.' });
            ws.close(4003, 'Authentication failed'); return;
          }
          client.role = message.role; clearTimeout(timeout);
          if (client.role === 'servo') {
            if (servo) { send(client, { type: 'error', code: 'already_connected', message: 'Ja existe um servo conectado.' }); ws.close(4009, 'Duplicate servo'); return; }
            servo = client; client.bootId = message.bootId;
          }
          send(client, { type: 'ready', protocol: 2, role: client.role, serverTime: clock() });
          if (client.role === 'app') send(client, { type: 'snapshot', status: status(), history: history() });
          return;
        }
        if (client.role === 'app') {
          if (message.type !== 'request' || !validId(message.requestId)) throw new RpcError('invalid', 'Requisicao invalida.');
          const result = rpc(message.method, message.data);
          send(client, { type: 'response', requestId: message.requestId, ok: true, data: result });
        } else if (client === servo) {
          if (message.type === 'telemetry') acceptTelemetry(client, message.data);
          else if (message.type === 'event') {
            const id = saveEvent(message.event); send(client, { type: 'event_ack', id }); publishStatus();
          } else if (message.type === 'clock_request') send(client, { type: 'clock', serverTime: clock() });
          else throw new RpcError('invalid', 'Mensagem de servo invalida.');
        }
      } catch (error) {
        const code = error.code ?? 'invalid';
        if (client.role === 'app' && validId(message?.requestId)) send(client, { type: 'response', requestId: message.requestId, ok: false, error: { code, message: error.message } });
        else send(client, { type: 'error', code, message: error instanceof SyntaxError ? 'JSON invalido.' : error.message });
      }
    });
    ws.on('close', () => {
      clearTimeout(timeout); clients.delete(client);
      if (servo === client) {
        servo = null;
        if (stopped) return;
        const pending = db.prepare("SELECT id FROM commands WHERE status IN ('sent','running')").all();
        for (const c of pending) finishCommand(c.id, 'interrupted', 'Conexao com o servo perdida; nao ha reenvio automatico.');
        publishStatus();
      }
    });
  });
  let housekeeping, pushTimer;
  let previousOnline = false;
  function tick() {
    const now = clock();
    const expired = db.prepare("SELECT id FROM commands WHERE (status='sent' AND expires_at<=?) OR (status='running' AND updated_at<?)").all(now, now - 30_000);
    for (const c of expired) finishCommand(c.id, 'expired', 'Sem confirmacao dentro do prazo.');
    const online = status().online;
    if (online !== previousOnline || expired.length) { previousOnline = online; publishStatus(); }
  }
  return { server, db, tick, drainPush: () => push.drain(),
    async start(port = 3000, host = '127.0.0.1') {
      await new Promise((resolve, reject) => { server.once('error', reject); server.listen(port, host, resolve); });
      housekeeping = setInterval(tick, 500); housekeeping.unref();
      if (options.backgroundPush !== false) { pushTimer = setInterval(() => { void push.drain(); }, 2000); pushTimer.unref(); }
    },
    async stop() {
      if (stopped) return; stopped = true; clearInterval(housekeeping); clearInterval(pushTimer);
      for (const c of clients) c.ws.close(1000, 'Server shutdown');
      await new Promise((resolve) => { server.close(resolve); server.closeIdleConnections(); });
      while (push.busy) await new Promise((resolve) => setTimeout(resolve, 20));
      db.close();
    },
  };
}
