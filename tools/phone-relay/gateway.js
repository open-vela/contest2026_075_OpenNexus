import { SERVICE, RX, TX, LineDecoder, RoundtripProof, validateRequest, encodeLine } from './protocol.js';
const $ = id => document.getElementById(id);
let device, rx, tx, generation = 0, writeQueue = Promise.resolve(), controller, ackTimer, pingTimer, pingWaiting = false;
const proof = new RoundtripProof(), entries = [];
function log(event, detail = {}) {
  entries.push({ time: new Date().toISOString(), event, ...detail });
  if (entries.length > 120) entries.shift();
  $('log').textContent = entries.map(entry => JSON.stringify(entry)).join('\n');
}
function status(id, text, kind = '') { $(id).textContent = text; $(id).className = 'badge ' + kind; }
function notice(text) { $('notice').textContent = text; }
function clearTest() {
  controller?.abort();controller = null;clearTimeout(ackTimer);clearTimeout(pingTimer);proof.reset();
  status('httpState', '未测试');status('ackState', '未测试');
}
function disconnected() {
  generation++;clearTest();decoder.reset();rx = tx = null;
  status('bleState', '已断开');$('connect').disabled = false;
  for (const id of ['ping', 'internet', 'disconnect']) $(id).disabled = true;
  notice('设备已断开，未完成的请求已取消。');log('ble_disconnected');
}
async function send(message, epoch = generation) {
  const characteristic = rx;
  const chunks = encodeLine(message);
  const operation = writeQueue.catch(() => {}).then(async () => {
    for (const chunk of chunks) {
      if (epoch !== generation || !device?.gatt?.connected || rx !== characteristic) throw new Error('连接已变更，请重新测试');
      if (characteristic.writeValueWithResponse) await characteristic.writeValueWithResponse(chunk);
      else await characteristic.writeValue(chunk);
    }
  });
  writeQueue = operation;return operation;
}
async function relay(message) {
  if (document.hidden) { log('background_request_ignored', { id: message.id });return; }
  const epoch = generation;
  if (proof.current?.id === message.id) return;
  const url = validateRequest(message);
  controller?.abort();clearTimeout(ackTimer);proof.begin(message.id);
  status('httpState', '请求中');status('ackState', '等待结果');notice('手机正在请求 HTTPS 测试服务…');
  log('https_started', { id: message.id, url });
  const abort = new AbortController();controller = abort;
  const timer = setTimeout(() => abort.abort(), 8000);
  try {
    const response = await fetch(url, { signal: abort.signal, cache: 'no-store', credentials: 'omit', redirect: 'error', referrerPolicy: 'no-referrer' });
    const data = await response.json();
    if (epoch !== generation || document.hidden || proof.current?.id !== message.id) return;
    const echo = data?.args?.focusmate_id;
    if (!proof.https(message.id, response.status, echo)) throw new Error('HTTPS 状态或回显编号不匹配');
    status('httpState', 'HTTPS 200', 'ok');log('https_verified', { id: message.id, status: response.status, echo_id: echo });
    await send({ type: 'net.reply', id: message.id, http_status: response.status, echo_id: echo }, epoch);
    if (proof.current?.ack) return;
    status('ackState', '等待设备确认');
    ackTimer = setTimeout(() => {
      if (proof.current?.id === message.id && !proof.current.ack) {
        status('ackState', '未收到确认', 'bad');notice('HTTPS 请求成功，但完整设备往返尚未通过。');
      }
    }, 6000);
  } catch (error) {
    if (epoch !== generation || document.hidden || proof.current?.id !== message.id) return;
    status('httpState', '请求未通过', 'bad');status('ackState', '未通过', 'bad');
    notice('HTTPS 请求失败或超时。请查看日志后重试。');log('https_failed', { id: message.id, error: error.message });
    await send({ type: 'net.reply', id: message.id, http_status: 0, echo_id: '' }, epoch).catch(e => log('reply_failed', { error: e.message }));
  } finally { clearTimeout(timer);if (controller === abort) controller = null; }
}
function receive(line) {
  try {
    const message = JSON.parse(line);log('device_message', message);
    if (message.type === 'pong') { pingWaiting = false;clearTimeout(pingTimer);notice('蓝牙往返通过。HTTPS 需要单独测试。'); }
    else if (message.type === 'net.request') relay(message).catch(error => notice(error.message));
    else if (message.type === 'net.ack' && proof.ack(message)) {
      clearTimeout(ackTimer);status('ackState', '已确认', 'ok');
      notice('通过：开发板 → 手机 → HTTPS → 手机 → 开发板。');log('phone_https_roundtrip_pass', { id: message.id });
    }
  } catch (error) { log('invalid_message', { error: error.message }); }
}
const decoder = new LineDecoder(receive, error => log('framing_error', { error: error.message }));
function onNotification(event) {
  const v = event.target.value;decoder.push(new Uint8Array(v.buffer, v.byteOffset, v.byteLength));
}
$('connect').addEventListener('click', async () => {
  if (!window.isSecureContext || !navigator.bluetooth) {
    notice('请在支持 Web Bluetooth 的浏览器中，通过 HTTPS 打开本页。');return;
  }
  $('connect').disabled = true;notice('请选择 FocusMate 并允许蓝牙连接。');
  try {
    device = await navigator.bluetooth.requestDevice({ filters: [{ namePrefix: 'FocusMate', services: [SERVICE] }] });
    device.addEventListener('gattserverdisconnected', disconnected);
    const server = await device.gatt.connect(), service = await server.getPrimaryService(SERVICE);
    rx = await service.getCharacteristic(RX);tx = await service.getCharacteristic(TX);
    decoder.reset();writeQueue = Promise.resolve();generation++;clearTest();
    tx.addEventListener('characteristicvaluechanged', onNotification);await tx.startNotifications();
    status('bleState', device.name || 'FocusMate', 'ok');
    for (const id of ['ping', 'internet', 'disconnect']) $(id).disabled = false;
    notice('设备已连接。先测试蓝牙往返，再测试 HTTPS。');log('ble_connected', { device: device.name });
  } catch (error) {
    if (device?.gatt?.connected) device.gatt.disconnect();
    disconnected();notice(error.message);log('ble_connect_failed', { error: error.message });
  }
});
$('ping').addEventListener('click', async () => {
  notice('等待设备回复…');clearTimeout(pingTimer);pingWaiting = true;
  try { await send({ type: 'ping' });if (pingWaiting) pingTimer = setTimeout(() => notice('蓝牙回复超时，请查看设备状态。'), 5000); }
  catch (error) { notice(error.message); }
});
$('internet').addEventListener('click', async () => {
  clearTest();notice('等待设备发出本次测试请求…');
  try { await send({ type: 'net.test' }); }
  catch (error) { notice(error.message); }
});
$('disconnect').addEventListener('click', () => { device?.gatt?.disconnect(); });
$('copy').addEventListener('click', async () => {
  try { await navigator.clipboard.writeText(JSON.stringify(entries, null, 2));notice('日志已复制。'); }
  catch { notice('当前浏览器不允许复制，请直接查看下方日志。'); }
});
document.addEventListener('visibilitychange', () => {
  if (document.hidden) { clearTest();log('backgrounded_test_cancelled'); }
});
log('page_ready', { secure: window.isSecureContext, bluetooth_api: Boolean(navigator.bluetooth) });
