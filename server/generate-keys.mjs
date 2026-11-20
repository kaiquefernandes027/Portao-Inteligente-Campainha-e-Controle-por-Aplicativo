import { randomBytes } from 'node:crypto';
for (const name of ['USER_API_TOKEN', 'SERVO_API_TOKEN', 'PEER_SHARED_KEY']) {
  console.log(`${name}=${randomBytes(32).toString('hex')}`);
}
