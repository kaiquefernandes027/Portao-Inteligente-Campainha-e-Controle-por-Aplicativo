const test=require('node:test');
const assert=require('node:assert/strict');
const {normalizeSocketUrl,mergeEvents}=require('../lib/socket');
test('normaliza WSS e permite WS apenas em IP privado do build de bancada',()=>{
  assert.equal(normalizeSocketUrl('https://portao.example'),'wss://portao.example/ws');
  assert.equal(normalizeSocketUrl('wss://portao.example/ws'),'wss://portao.example/ws');
  assert.throws(()=>normalizeSocketUrl('ws://192.168.1.50:3000/ws'));
  assert.equal(normalizeSocketUrl('ws://192.168.1.50:3000/ws',true),'ws://192.168.1.50:3000/ws');
  assert.throws(()=>normalizeSocketUrl('ws://example.com/ws',true));
  assert.throws(()=>normalizeSocketUrl('ws://172.32.0.1/ws',true));
});
test('chaves nao entram na URL e caminhos diferentes sao recusados',()=>{
  assert.throws(()=>normalizeSocketUrl('wss://user:key@example.com/ws'));
  assert.throws(()=>normalizeSocketUrl('wss://example.com/ws?token=secret'));
  assert.throws(()=>normalizeSocketUrl('wss://example.com/api'));
});
test('evento ao vivo se junta ao historico paginado sem duplicar ou perder registros',()=>{
  const merged=mergeEvents([{id:'old',sequence:1},{id:'bell',sequence:2}],[{id:'bell',sequence:2,type:'doorbell'},{id:'open',sequence:3}]);
  assert.deepEqual(merged.map(e=>e.id),['open','bell','old']);assert.equal(merged[1].type,'doorbell');
});
