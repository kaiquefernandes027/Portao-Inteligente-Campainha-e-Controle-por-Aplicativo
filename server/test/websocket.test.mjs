import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { mkdtempSync,rmSync } from 'node:fs';
import { join } from 'node:path';
import { DatabaseSync } from 'node:sqlite';
import { createGateServer } from '../server.mjs';
const require=createRequire(import.meta.url);
const {GateSocket}=require('../../app/lib/socket.js');
const USER='a'.repeat(64),SERVO='b'.repeat(64);

class Peer{
  constructor(url,auth){
    this.messages=[];this.waiters=[];this.ws=new WebSocket(url);
    this.ws.onopen=()=>this.send(auth);
    this.ws.onmessage=e=>{const m=JSON.parse(e.data);this.messages.push(m);for(const wake of [...this.waiters])wake();};
  }
  send(message){this.ws.send(JSON.stringify(message));}
  wait(predicate,ms=2000){
    return new Promise((resolve,reject)=>{
      const check=()=>{const value=this.messages.find(predicate);if(value){clearTimeout(timer);this.waiters=this.waiters.filter(f=>f!==check);resolve(value);}};
      const timer=setTimeout(()=>{this.waiters=this.waiters.filter(f=>f!==check);reject(new Error('Mensagem WebSocket esperada nao recebida.'));},ms);
      this.waiters.push(check);check();
    });
  }
  async close(){if(this.ws.readyState===3)return;await new Promise(resolve=>{this.ws.onclose=resolve;this.ws.close();});}
}
async function fixture(t,options={}){
  let now=1_790_000_000_000;
  const app=createGateServer({userToken:USER,servoToken:SERVO,dbPath:':memory:',clock:()=>now,backgroundPush:false,...options});
  await app.start(0);const url=`ws://127.0.0.1:${app.server.address().port}/ws`;
  const peers=[],apps=[];
  t.after(async()=>{for(const c of apps)c.stop();for(const c of peers)await c.close();await app.stop();options.cleanup?.();});
  async function servo(){const p=new Peer(url,{type:'auth',protocol:2,role:'servo',token:SERVO,deviceId:'servo-01',bootId:'boot-servo-0001'});peers.push(p);await p.wait(m=>m.type==='ready');return p;}
  async function user(){const packets=[];const c=new GateSocket({url,token:USER,now:()=>now,onPacket:m=>packets.push(m)});apps.push(c);await c.start();return {client:c,packets};}
  async function state(p,extra={}){
    p.send({type:'telemetry',data:{state:'closed',angle:0,targetAngle:0,panelOnline:true,outboxCount:0,...extra}});
    // RPC de status em outro socket pode chegar antes da telemetria. Aguardar
    // um clock na mesma conexao prova que o frame anterior foi processado.
    const before=p.messages.filter(m=>m.type==='clock').length;
    p.send({type:'clock_request'});
    await new Promise((resolve,reject)=>{const deadline=setTimeout(()=>reject(new Error('clock')),2000);const wake=()=>{if(p.messages.filter(m=>m.type==='clock').length>before){clearTimeout(deadline);p.waiters=p.waiters.filter(f=>f!==wake);resolve();}};p.waiters.push(wake);wake();});
  }
  return{app,url,servo,user,state,peers,advance:ms=>{now+=ms;app.tick();}};
}

test('autenticacao por frame WebSocket recusa chave e papel invalidos',async t=>{
  const f=await fixture(t);
  for(const auth of [{role:'app',token:SERVO},{role:'panel',token:SERVO}]){
    const p=new Peer(f.url,{type:'auth',protocol:2,...auth});f.peers.push(p);
    assert.equal((await p.wait(m=>m.type==='error')).code,'auth_failed');
  }
});
test('app recebe estado e controla o servo por WebSocket, com confirmacao de abertura e historico',async t=>{
  const f=await fixture(t);const motor=await f.servo();await f.state(motor);
  const{client,packets}=await f.user();assert.equal((await client.request('status')).panelOnline,true);
  const id='command-open-0001';const result=await client.request('command',{id,action:'open'});
  assert.equal(result.status,'sent');assert.equal((await motor.wait(m=>m.type==='command')).id,id);
  await f.state(motor,{state:'opening',angle:30,targetAngle:90,commandId:id,commandPhase:'running'});
  assert.equal((await client.request('status')).state,'opening');
  await f.state(motor,{state:'open',angle:90,targetAngle:90,commandId:id,commandPhase:'completed'});
  motor.send({type:'event',event:{id:'event-open-0001',type:'gate_opened',source:'servo',commandId:id}});
  await motor.wait(m=>m.type==='event_ack'&&m.id==='event-open-0001');
  const history=await client.request('history',{limit:30});
  assert(history.items.some(e=>e.type==='gate_opened'));
  assert.equal((await client.request('command_status',{id})).status,'completed');
  assert(packets.some(p=>p.type==='state'&&p.status.state==='open'));
  assert(packets.some(p=>p.type==='command_result'&&p.command.status==='completed'));
});
test('nao repete comando de mesmo ID e impede movimentos concorrentes',async t=>{
  const f=await fixture(t);const motor=await f.servo();await f.state(motor);const{client}=await f.user();
  const data={id:'command-unique-01',action:'open'};
  await client.request('command',data);await motor.wait(m=>m.type==='command');
  assert.equal((await client.request('command',data)).duplicate,true);
  await assert.rejects(client.request('command',{...data,action:'close'}),e=>e.code==='conflict');
  await assert.rejects(client.request('command',{id:'command-another-01',action:'close'}),e=>e.code==='busy');
  assert.equal(motor.messages.filter(m=>m.type==='command').length,1);
});
test('servo offline ou sem telemetria recente recusa comando sem enfileirar',async t=>{
  const f=await fixture(t);const{client}=await f.user();
  await assert.rejects(client.request('command',{id:'offline-command-01',action:'open'}),e=>e.code==='offline');
  const motor=await f.servo();await f.state(motor);f.advance(5001);
  await assert.rejects(client.request('command',{id:'offline-command-02',action:'open'}),e=>e.code==='offline');
  assert.equal(f.app.db.prepare('SELECT count(*) AS n FROM commands').get().n,0);
});
test('expira comando sem confirmacao e interrompe ao desconectar, sem reenvio automatico',async t=>{
  const f=await fixture(t);const motor=await f.servo();await f.state(motor);const{client}=await f.user();
  await client.request('command',{id:'expire-command-01',action:'open'});f.advance(5001);
  assert.equal((await client.request('command_status',{id:'expire-command-01'})).status,'expired');
  await f.state(motor);await client.request('command',{id:'disconnect-command-01',action:'open'});
  await motor.close();
  assert.equal((await client.request('command_status',{id:'disconnect-command-01'})).status,'interrupted');
  const fresh=await f.servo();await f.state(fresh);
  assert.equal(fresh.messages.some(m=>m.type==='command'),false);
});
test('toque retransmitido pelo servo e deduplicado e agenda push de campainha e abertura',async t=>{
  const payloads=[];const f=await fixture(t,{fetchPush:async(url,request)=>{
    const body=JSON.parse(request.body);payloads.push({url,body});return {ok:true,json:async()=>({data:[{status:'ok',id:`ticket-${payloads.length}`}]})};
  }});
  const motor=await f.servo();await f.state(motor);const{client}=await f.user();
  await client.request('push_register',{token:'ExpoPushToken[abcdefghijklmnop]'});
  const bell={id:'panel-bell-0001',type:'doorbell',source:'panel',occurredAt:null};
  motor.send({type:'event',event:bell});await motor.wait(m=>m.type==='event_ack'&&m.id===bell.id);
  motor.send({type:'event',event:bell});
  motor.send({type:'event',event:{id:'servo-open-0001',type:'gate_opened',source:'servo'}});
  await motor.wait(m=>m.type==='event_ack'&&m.id==='servo-open-0001');
  assert.equal(f.app.db.prepare('SELECT count(*) AS n FROM events').get().n,2);
  assert.equal(f.app.db.prepare('SELECT count(*) AS n FROM push_jobs').get().n,2);
  await f.app.drainPush();assert.equal(payloads.length,2);
  assert.deepEqual(payloads.map(x=>x.body[0].data.type),['doorbell','gate_opened']);
});
test('painel nao pode forjar evento de abertura e paginacao conserva historico',async t=>{
  const f=await fixture(t);const motor=await f.servo();const{client}=await f.user();
  motor.send({type:'event',event:{id:'fake-panel-open-01',type:'gate_opened',source:'panel'}});
  assert.equal((await motor.wait(m=>m.type==='error')).code,'invalid_event');
  for(let i=0;i<3;i++){const id=`panel-bell-000${i}`;motor.send({type:'event',event:{id,type:'doorbell',source:'panel'}});await motor.wait(m=>m.type==='event_ack'&&m.id===id);}
  const page=await client.request('history',{limit:2});assert.equal(page.items.length,2);
  const next=await client.request('history',{limit:2,before:page.nextBefore});assert.equal(next.items.length,1);
  assert.notEqual(page.items[1].id,next.items[0].id);
});
test('historico SQLite v1 migra sem perder eventos e nao retoma comandos antigos',async t=>{
  const dir=mkdtempSync(join(process.cwd(),'.test-portao-v2-'));
  const path=join(dir,'portao.sqlite'),db=new DatabaseSync(path);
  db.exec("CREATE TABLE events(sequence INTEGER PRIMARY KEY AUTOINCREMENT,id TEXT UNIQUE,type TEXT,command_id TEXT,occurred_at INTEGER,received_at INTEGER,detail TEXT);INSERT INTO events(id,type,received_at) VALUES('old-event-0001','doorbell',1790000000000);");db.close();
  const f=await fixture(t,{dbPath:path,cleanup:()=>rmSync(dir,{recursive:true,force:true})});const{client}=await f.user();
  assert.equal((await client.request('history')).items[0].id,'old-event-0001');
  assert.equal((await client.request('status')).online,false);
});
test('DeviceNotRegistered remove token e conserva toque no historico',async t=>{
  const f=await fixture(t,{fetchPush:async()=>({ok:true,json:async()=>({data:[{status:'error',details:{error:'DeviceNotRegistered'}}]})})});
  const motor=await f.servo();const{client}=await f.user();await client.request('push_register',{token:'ExpoPushToken[abcdefghijklmnop]'});
  motor.send({type:'event',event:{id:'invalid-token-bell',type:'doorbell',source:'panel'}});await motor.wait(m=>m.type==='event_ack');await f.app.drainPush();
  assert.equal(f.app.db.prepare('SELECT count(*) AS n FROM push_tokens').get().n,0);
  assert.equal((await client.request('history')).items.length,1);
});

test('requisicao de abrir atrasada no transporte nao causa uma abertura tardia',async t=>{
  const f=await fixture(t);const motor=await f.servo();await f.state(motor);
  const app=new Peer(f.url,{type:'auth',protocol:2,role:'app',token:USER});f.peers.push(app);await app.wait(m=>m.type==='ready');
  app.send({type:'request',requestId:'req-expired-0001',method:'command',data:{id:'late-command-0001',action:'open',expiresAt:1_789_999_999_000}});
  const response=await app.wait(m=>m.type==='response');assert.equal(response.ok,false);assert.equal(response.error.code,'expired');
  assert.equal(f.app.db.prepare('SELECT count(*) AS n FROM commands').get().n,0);
  assert.equal(motor.messages.some(m=>m.type==='command'),false);
});
