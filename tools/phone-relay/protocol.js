export const SERVICE = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
export const RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e';
export const TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e';

export class LineDecoder {
  constructor(onLine, onError = () => {}) { this.onLine = onLine; this.onError = onError; this.reset(); }
  reset() { this.bytes = []; this.dropping = false; }
  push(chunk) {
    for (const byte of chunk) {
      if (byte === 10) {
        if (!this.dropping && this.bytes.length) {
          if (this.bytes.at(-1) === 13) this.bytes.pop();
          try { this.onLine(new TextDecoder('utf-8', { fatal: true }).decode(new Uint8Array(this.bytes))); }
          catch (error) { this.onError(error); }
        }
        this.reset();
      } else if (!this.dropping) {
        if (!byte || this.bytes.length >= 1023) {
          this.bytes = []; this.dropping = true; this.onError(new Error('消息超长或包含 NUL，等待下一条完整消息'));
        } else this.bytes.push(byte);
      }
    }
  }
}

export function validateRequest(message) {
  if (message?.v !== 1 || message?.type !== 'net.request' ||
      typeof message.id !== 'string' || !/^fm-[a-zA-Z0-9-]{1,32}$/.test(message.id))
    throw new Error('设备请求格式无效');
  const url = new URL(message.url);
  if (url.protocol !== 'https:' || url.hostname !== 'httpbin.org' ||
      (url.port && url.port !== '443') || url.pathname !== '/get' || url.username || url.password ||
      url.searchParams.get('focusmate_id') !== message.id)
    throw new Error('当前只允许 HTTPS 回显测试，不转发任意网址');
  return url.href;
}

export class RoundtripProof {
  reset() { this.current = null; }
  begin(id) { this.current = { id, https: false, ack: false }; }
  https(id, status, echo) {
    if (!this.current || this.current.id !== id) return false;
    this.current.https = status === 200 && echo === id;
    return this.current.https;
  }
  ack(message) {
    if (!this.current?.https || message?.type !== 'net.ack' ||
        message.id !== this.current.id || message.ok !== true) return false;
    this.current.ack = true; return true;
  }
}

export function encodeLine(message) {
  const bytes = new TextEncoder().encode(JSON.stringify(message) + '\n');
  if (bytes.length > 1024) throw new Error('请求超过当前协议长度');
  const chunks = [];
  for (let i = 0; i < bytes.length; i += 20) chunks.push(bytes.slice(i, i + 20));
  return chunks;
}
