import { createGateServer } from './server.mjs';
try {
  const app = createGateServer();
  await app.start(Number(process.env.PORT || 3000), process.env.HOST || '127.0.0.1');
  console.log(`Portao v2: WebSocket /ws na porta ${app.server.address().port}.`);
  let stopping = false;
  const stop = async () => { if (!stopping) { stopping = true; await app.stop(); } };
  process.on('SIGINT', stop); process.on('SIGTERM', stop);
} catch (error) { console.error(error.message); process.exitCode = 1; }
