// dbg::net::Server against the "ws" package, the WebSocket implementation of the devtools tooling.
//
//   node interop_ws.js <dbg_net_demo binary> <node_modules directory that has ws>
//
// Starts the demo server on a free port and talks to it: text, 8 MiB binary both ways, fragments, ping,
// the Origin and Host rules, the message size limit, the client limit, the closing handshake.
// Exit code 77 when ws is not installed there (ctest then reports the test as skipped).

'use strict';

const { spawn } = require('child_process');
const http = require('http');
const path = require('path');
const crypto = require('crypto');

const [demo, modules] = process.argv.slice(2);
if (!demo) {
  console.error('usage: node interop_ws.js <dbg_net_demo binary> <node_modules directory that has ws>');
  process.exit(2);
}

let WebSocket;
try {
  WebSocket = require(path.join(modules || '', 'ws'));
} catch (error) {
  console.log(`SKIP: no ws package in ${modules || '(no directory given)'}`);
  process.exit(77);
}

const MAX_MESSAGE = 16 << 20;
const BIG = 8 << 20;
let checks = 0;

function check(condition, what) {
  checks++;
  if (!condition) {
    throw new Error(`check failed: ${what}`);
  }
}

function startServer() {
  return new Promise((resolve, reject) => {
    const child = spawn(demo, ['--port', '0', '--max-message', String(MAX_MESSAGE)], { stdio: ['pipe', 'pipe', 'inherit'] });
    let output = '';
    child.on('error', reject);
    child.on('exit', (code) => reject(new Error(`server exited early with ${code}`)));
    child.stdout.on('data', (chunk) => {
      output += chunk;
      const match = /Debugger: http:\/\/127\.0\.0\.1:(\d+)\//.exec(output);
      if (match) {
        child.removeAllListeners('exit');
        resolve({ child, port: Number(match[1]) });
      }
    });
  });
}

function httpGet(port, target, headers = {}) {
  return new Promise((resolve, reject) => {
    http.get({ host: '127.0.0.1', port, path: target, headers }, (response) => {
      const chunks = [];
      response.on('data', (chunk) => chunks.push(chunk));
      response.on('end', () => resolve({ status: response.statusCode, headers: response.headers, body: Buffer.concat(chunks) }));
    }).on('error', reject);
  });
}

// Resolves with the open socket, or with { status } when the server answers the upgrade with an HTTP error.
function connect(port, options = {}) {
  return new Promise((resolve, reject) => {
    const socket = new WebSocket(`ws://127.0.0.1:${port}/ws`, { maxPayload: 64 << 20, ...options });
    socket.on('open', () => resolve(socket));
    socket.on('unexpected-response', (request, response) => {
      response.resume();
      request.destroy();
      resolve({ status: response.statusCode });
    });
    socket.on('error', reject);
  });
}

function nextMessage(socket) {
  return new Promise((resolve, reject) => {
    socket.once('message', (data, isBinary) => resolve({ data, isBinary }));
    socket.once('close', (code) => reject(new Error(`closed with ${code} while waiting for a message`)));
  });
}

function closed(socket) {
  return new Promise((resolve) => socket.once('close', (code, reason) => resolve({ code, reason: reason.toString() })));
}

async function echo(socket, payload, options) {
  const reply = nextMessage(socket);
  socket.send(payload, options);
  return reply;
}

function pattern(size) {
  const bytes = Buffer.alloc(size);
  for (let i = 0; i < size; i++) {
    bytes[i] = (i * 7 + Math.floor(i / 255) + 3) & 255;
  }
  return bytes;
}

async function run(port) {
  // the page
  let response = await httpGet(port, '/');
  check(response.status === 200, 'GET / is 200');
  check(response.headers['x-content-type-options'] === 'nosniff', 'nosniff');
  check(response.headers['cache-control'] === 'no-cache', 'the page is not cached');
  check(/^text\/html/.test(response.headers['content-type']), 'the page is html');
  response = await httpGet(port, '/', { Host: 'evil.example' });
  check(response.status === 403, 'a foreign Host is refused');
  response = await httpGet(port, '/no/such/file.js');
  check(response.status === 404, 'an unknown file is 404');
  response = await httpGet(port, '/no/such/route');
  check(response.status === 200 && /^text\/html/.test(response.headers['content-type']), 'an unknown path without an extension is the page');

  // no Origin header: a tool, not a browser
  const socket = await connect(port);
  check(socket.readyState === WebSocket.OPEN, 'upgrade without Origin');
  check(socket.extensions === '', 'no extension is negotiated although ws offers permessage-deflate');
  check(socket.protocol === '', 'no subprotocol');

  let reply = await echo(socket, 'hello');
  check(!reply.isBinary && reply.data.toString() === 'hello', 'text echo');
  reply = await echo(socket, 'héllo € 😀');
  check(reply.data.toString() === 'héllo € 😀', 'text echo outside ASCII');
  reply = await echo(socket, '');
  check(!reply.isBinary && reply.data.length === 0, 'empty text echo');

  // 8 MiB of binary, client to server and back
  const big = crypto.randomBytes(BIG);
  reply = await echo(socket, big);
  check(reply.isBinary && reply.data.length === BIG && reply.data.equals(big), '8 MiB binary echo');
  // and 8 MiB the server makes itself
  reply = await echo(socket, `binary:${BIG}`);
  check(reply.isBinary && reply.data.equals(pattern(BIG)), '8 MiB binary from the server');
  reply = await echo(socket, 'queued');
  check(Number(reply.data.toString()) < 64, 'the queue drains');

  // one message in three frames
  const whole = nextMessage(socket);
  socket.send('frag', { fin: false });
  socket.send('men', { fin: false });
  socket.send('ted', { fin: true });
  reply = await whole;
  check(reply.data.toString() === 'fragmented', 'fragmented text');

  const pong = new Promise((resolve) => socket.once('pong', resolve));
  socket.ping('are you there');
  check((await pong).toString() === 'are you there', 'ping is answered with its payload');

  // broadcast reaches a second client
  const second = await connect(port, { origin: `http://127.0.0.1:${port}` });
  check(second.readyState === WebSocket.OPEN, 'upgrade with the page\'s own Origin');
  const both = Promise.all([nextMessage(socket), nextMessage(second)]);
  second.send('broadcast:to all');
  const [first, other] = await both;
  check(first.data.toString() === 'to all' && other.data.toString() === 'to all', 'broadcast');

  // Origin and Host rules
  for (const origin of ['http://localhost:5173', 'https://localhost', 'http://[::1]:5173']) {
    const allowed = await connect(port, { origin });
    check(allowed.readyState === WebSocket.OPEN, `Origin ${origin} is accepted`);
    allowed.terminate();
  }
  for (const origin of ['http://evil.example', `http://evil.example:${port}`, 'http://localhost.evil.example', 'null', 'file://']) {
    const refused = await connect(port, { origin });
    check(refused.status === 403, `Origin ${origin} is refused`);
  }
  const rebound = await connect(port, { headers: { Host: `evil.example:${port}` }, origin: `http://evil.example:${port}` });
  check(rebound.status === 403, 'a rebinding page (its own name as Host and Origin) is refused');

  // closing handshake started by the client
  let end = closed(second);
  second.close(1000, 'bye');
  check((await end).code === 1000, 'close 1000 is echoed');

  // one byte above the limit closes with 1009
  const greedy = await connect(port);
  end = closed(greedy);
  greedy.send(Buffer.alloc(MAX_MESSAGE + 1));
  check((await end).code === 1009, 'an oversize message is closed with 1009');

  // the client limit: 16, of which `socket` is one
  const many = [];
  for (let i = 0; i < 15; i++) {
    many.push(await connect(port));
    check(many[i].readyState === WebSocket.OPEN, `client ${i + 2} of 16`);
  }
  const extra = await connect(port);
  check(extra.status === 503, 'the 17th client is refused with 503');
  for (const one of many) {
    one.terminate();
  }

  end = closed(socket);
  socket.close();
  check((await end).code === 1005, 'close without a status');
}

async function main() {
  const timer = setTimeout(() => {
    console.error('interop_ws: timed out');
    process.exit(1);
  }, 120000);
  const { child, port } = await startServer();
  let failure = null;
  try {
    await run(port);
  } catch (error) {
    failure = error;
  }
  const exited = new Promise((resolve) => child.on('exit', resolve));
  child.stdin.end();
  const code = await exited;
  clearTimeout(timer);
  if (failure) {
    console.error(`interop_ws: FAILED after ${checks} checks: ${failure.message}`);
    process.exit(1);
  }
  if (code !== 0) {
    console.error(`interop_ws: the server exited with ${code}`);
    process.exit(1);
  }
  console.log(`interop_ws: ${checks} checks, 0 failed`);
}

main().catch((error) => {
  console.error(`interop_ws: ${error.stack || error}`);
  process.exit(1);
});
