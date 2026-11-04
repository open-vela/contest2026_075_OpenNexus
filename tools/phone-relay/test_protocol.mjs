import test from 'node:test';
import assert from 'node:assert/strict';
import { LineDecoder, RoundtripProof, validateRequest, encodeLine } from './protocol.js';

test('通知在任意 UTF-8 字节处分割仍恢复完整消息', () => {
  const lines = [];
  const decoder = new LineDecoder(line => lines.push(line));
  const bytes = new TextEncoder().encode('{"任务":"写报告"}\r\n{"type":"pong"}\n');
  for (const byte of bytes) decoder.push([byte]);
  assert.deepEqual(lines, ['{"任务":"写报告"}', '{"type":"pong"}']);
});
test('丢弃超长/NUL 消息，不把残尾当成新消息', () => {
  const lines = [], errors = [];
  const decoder = new LineDecoder(line => lines.push(line), error => errors.push(error));
  decoder.push(new TextEncoder().encode('x'.repeat(1050) + '\ntail\u0000bad\n{"type":"pong"}\n'));
  assert.deepEqual(lines, ['{"type":"pong"}']);assert.equal(errors.length, 2);
});
test('重连清空半条消息，分包上限为 20 字节', () => {
  const lines = [], decoder = new LineDecoder(line => lines.push(line));
  decoder.push(new TextEncoder().encode('old'));decoder.reset();
  for (const chunk of encodeLine({ type: 'test', text: '中文'.repeat(25) })) {
    assert.ok(chunk.length <= 20);decoder.push(chunk);
  }
  assert.equal(JSON.parse(lines[0]).text, '中文'.repeat(25));
});
test('BLE 连接、单独 ACK 或过期 ACK 均不能冒充 HTTPS 往返通过', () => {
  const proof = new RoundtripProof();proof.begin('fm-1');
  assert.equal(proof.ack({ type: 'net.ack', id: 'fm-1', ok: true }), false);
  assert.equal(proof.https('fm-1', 200, 'wrong'), false);
  assert.equal(proof.https('fm-1', 200, 'fm-1'), true);
  assert.equal(proof.ack({ type: 'net.ack', id: 'fm-old', ok: true }), false);
  assert.equal(proof.ack({ type: 'net.ack', id: 'fm-1', ok: true }), true);
  proof.reset();assert.equal(proof.ack({ type: 'net.ack', id: 'fm-1', ok: true }), false);
});
test('测试中转仅允许携带匹配编号的指定 HTTPS 回显请求', () => {
  const message = { v: 1, type: 'net.request', id: 'fm-123', url: 'https://httpbin.org/get?focusmate_id=fm-123' };
  assert.equal(validateRequest(message), message.url);
  for (const url of ['http://httpbin.org/get?focusmate_id=fm-123', 'https://example.com/get?focusmate_id=fm-123', 'https://httpbin.org/get?focusmate_id=old'])
    assert.throws(() => validateRequest({ ...message, url }));
});
