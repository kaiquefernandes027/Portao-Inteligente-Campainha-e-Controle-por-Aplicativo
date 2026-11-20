function isPrivateHost(host) {
  if (host === 'localhost' || host === '[::1]') return true;
  const pieces = host.split('.');
  if (pieces.length !== 4 || pieces.some((p) => !/^\d{1,3}$/.test(p) || Number(p) > 255)) return false;
  const [a,b] = pieces.map(Number);
  return a === 10 || a === 127 || (a === 192 && b === 168) || (a === 172 && b >= 16 && b <= 31);
}
function normalizeSocketUrl(value, allowLan = false) {
  let url; try { url = new URL(value.trim()); } catch { throw new Error('Informe a URL WebSocket completa.'); }
  if (url.protocol === 'https:') url.protocol = 'wss:';
  if (url.protocol === 'http:') url.protocol = 'ws:';
  if (url.username || url.password || url.search || url.hash || !['/', '/ws'].includes(url.pathname)) throw new Error('Use o endereco do servidor com caminho /ws, sem chave na URL.');
  if (url.protocol !== 'wss:' && !(allowLan && url.protocol === 'ws:' && isPrivateHost(url.hostname))) throw new Error('Use WSS. WS sem TLS so e permitido no build de bancada, para IP privado.');
  url.pathname = '/ws'; return url.toString();
}
function mergeEvents(current, incoming) {
  const map = new Map(current.map((e) => [e.id,e]));
  for (const e of incoming) map.set(e.id,e);
  return [...map.values()].sort((a,b) => b.sequence-a.sequence);
}
class GateSocket {
  constructor({ url, token, WebSocketImpl = globalThis.WebSocket, onPacket = () => {}, onConnection = () => {}, now = Date.now }) {
    this.url=url; this.token=token; this.WebSocketImpl=WebSocketImpl;
    this.onPacket=onPacket; this.onConnection=onConnection;
    this.pending=new Map(); this.ready=false; this.stopped=true; this.counter=0;
    this.now=now;this.serverTime=0;this.clockAt=0;
  }
  start() {
    if (!this.stopped) return this.first;
    this.stopped=false; this.noRetry=false;
    this.first=new Promise((resolve,reject) => { this.firstResolve=resolve; this.firstReject=reject; });
    this._open(); return this.first;
  }
  _settle(error) {
    if (error) this.firstReject?.(error); else this.firstResolve?.();
    this.firstReject=null;this.firstResolve=null;
  }
  _failPending(message) {
    for (const item of this.pending.values()) { clearTimeout(item.timer); const error=new Error(message);error.code='disconnected';item.reject(error); }
    this.pending.clear();
  }
  _open() {
    if (this.stopped) return;
    const ws=new this.WebSocketImpl(this.url); this.socket=ws;
    this.onConnection(false,'Conectando ao servidor…');
    const timeout=setTimeout(() => { if(this.socket===ws && !this.ready){this._settle(new Error('Tempo de conexao esgotado.'));ws.close();} },8000);
    ws.onopen=() => { if(this.socket===ws)ws.send(JSON.stringify({type:'auth',protocol:2,role:'app',token:this.token})); };
    ws.onmessage=({data}) => {
      if(this.socket!==ws)return;
      let packet;try{packet=JSON.parse(data);}catch{return;}
      if(Number.isSafeInteger(packet.serverTime)){this.serverTime=packet.serverTime;this.clockAt=this.now();}
      if(packet.type==='ready') {this.ready=true;clearTimeout(timeout);this._settle();this.onConnection(true,'Conectado');}
      if(packet.type==='error'&&packet.code==='auth_failed') {
        this.noRetry=true;const e=new Error('Chave de acesso invalida ou servidor desatualizado.');this._settle(e);this.onConnection(false,e.message);ws.close();
      }
      if(packet.type==='response') {
        const item=this.pending.get(packet.requestId);
        if(item){clearTimeout(item.timer);this.pending.delete(packet.requestId);if(packet.ok)item.resolve(packet.data);else{const e=new Error(packet.error?.message||'Erro no servidor.');e.code=packet.error?.code;item.reject(e);}}
      }
      this.onPacket(packet);
    };
    ws.onerror=() => { if(this.socket===ws)this.onConnection(false,'Falha na conexao. Confira servidor e rede.'); };
    ws.onclose=() => {
      clearTimeout(timeout);if(this.socket!==ws)return;this.ready=false;
      this._failPending('Conexao perdida. Consulte o estado antes de repetir um comando.');
      this._settle(new Error('Nao foi possivel conectar ao servidor.'));
      if(!this.noRetry)this.onConnection(false,'Sem conexao. Tentando reconectar…');
      if(!this.stopped&&!this.noRetry)this.retry=setTimeout(() => this._open(),2000);
    };
  }
  request(method,data={}) {
    if(!this.ready)return Promise.reject(Object.assign(new Error('WebSocket desconectado.'),{code:'disconnected'}));
    const requestId=`req-${Date.now().toString(36)}-${(++this.counter).toString(36)}`;
    if(method==='command')data={...data,expiresAt:this.serverTime+(this.now()-this.clockAt)+5000};
    return new Promise((resolve,reject) => {
      const timer=setTimeout(() => {this.pending.delete(requestId);const e=new Error('Sem confirmacao. Verifique o historico antes de repetir.');e.code='timeout';reject(e);},8000);
      this.pending.set(requestId,{resolve,reject,timer});
      try{this.socket.send(JSON.stringify({type:'request',requestId,method,data}));}
      catch(error){clearTimeout(timer);this.pending.delete(requestId);reject(error);}
    });
  }
  reconnect() {
    if(this.stopped||this.noRetry)return;
    clearTimeout(this.retry);const previous=this.socket;this.socket=null;this.ready=false;
    this._failPending('Conexao renovada. Consulte o estado do comando.');previous?.close();this._open();
  }
  stop() {
    this.stopped=true;this.ready=false;clearTimeout(this.retry);const ws=this.socket;this.socket=null;
    this._failPending('Conexao encerrada.');this._settle(new Error('Conexao encerrada.'));ws?.close();
  }
}
module.exports={GateSocket,normalizeSocketUrl,isPrivateHost,mergeEvents};
