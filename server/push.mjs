export function createPushWorker(db, { clock = Date.now, fetchPush = globalThis.fetch, accessToken } = {}) {
  let busy = false;
  async function post(route, payload) {
    const response = await fetchPush(`https://exp.host/--/api/v2/push/${route}`, {
      method: 'POST', headers: { 'Content-Type': 'application/json', ...(accessToken ? { Authorization: `Bearer ${accessToken}` } : {}) },
      body: JSON.stringify(payload), signal: AbortSignal.timeout(10_000),
    });
    if (!response.ok) throw new Error(`Expo HTTP ${response.status}`);
    const result = await response.json();
    if (result.errors?.length) throw new Error(result.errors[0].code || 'ExpoError');
    return result.data;
  }
  function disable(token) {
    db.prepare('DELETE FROM push_tokens WHERE token=?').run(token);
    db.prepare("UPDATE push_jobs SET status='disabled',error='DeviceNotRegistered' WHERE token=? AND status IN ('pending','ticket')").run(token);
  }
  return {
    get busy() { return busy; },
    async drain() {
      if (busy) return;
      busy = true;
      try {
        const jobs = db.prepare("SELECT * FROM push_jobs WHERE status IN ('pending','ticket') AND next_at<=? ORDER BY id LIMIT 10").all(clock());
        for (const job of jobs) {
          try {
            if (job.status === 'pending') {
              const event = db.prepare('SELECT * FROM events WHERE id=?').get(job.event_id);
              const opened = event.type === 'gate_opened';
              const data = await post('send', [{
                to: job.token, title: opened ? 'Portao aberto' : 'Campainha tocou',
                body: opened ? 'O servo concluiu a abertura comandada. Veja o historico no app.' : 'Um toque foi registrado. Veja o horario no app.',
                sound: 'default', channelId: 'campainha', ttl: 3600,
                data: { eventId: event.id, type: event.type, occurredAt: event.occurred_at ?? event.received_at },
              }]);
              const ticket = data?.[0];
              if (ticket?.status === 'ok' && ticket.id) {
                db.prepare("UPDATE push_jobs SET status='ticket',ticket_id=?,next_at=?,attempts=0 WHERE id=?")
                  .run(ticket.id, clock() + 15 * 60_000, job.id);
              } else if (ticket?.details?.error === 'DeviceNotRegistered') disable(job.token);
              else throw new Error(ticket?.details?.error || 'ExpoTicketError');
            } else {
              const data = await post('getReceipts', { ids: [job.ticket_id] });
              const receipt = data?.[job.ticket_id];
              if (receipt?.status === 'ok') db.prepare("UPDATE push_jobs SET status='delivered',error=NULL WHERE id=?").run(job.id);
              else if (receipt?.details?.error === 'DeviceNotRegistered') disable(job.token);
              else if (receipt?.status === 'error') throw new Error(receipt.details?.error || 'ReceiptError');
              else db.prepare('UPDATE push_jobs SET status=?,next_at=? WHERE id=?')
                .run(clock() - job.created_at > 23 * 3600_000 ? 'failed' : 'ticket', clock() + 15 * 60_000, job.id);
            }
          } catch (error) {
            const attempts = job.attempts + 1;
            db.prepare('UPDATE push_jobs SET status=?,attempts=?,next_at=?,error=? WHERE id=?')
              .run(attempts >= 6 ? 'failed' : job.status, attempts, clock() + Math.min(300_000, 2000 * 2 ** attempts), String(error.message).slice(0, 120), job.id);
          }
        }
      } finally { busy = false; }
    },
  };
}
