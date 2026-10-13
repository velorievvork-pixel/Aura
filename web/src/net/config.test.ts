import { describe, expect, it } from 'vitest';
import { normalizeServerUrl, platformDefault, serverCandidates, type ServerEnv } from './config';

const web: ServerEnv = { platform: 'web', location: { protocol: 'http:', hostname: 'localhost', host: 'localhost:5173' } };

describe('normalizeServerUrl', () => {
  it('голый IP — ws:// и порт сервера по умолчанию', () => {
    expect(normalizeServerUrl('192.168.1.5')).toBe('ws://192.168.1.5:9000');
    expect(normalizeServerUrl('  192.168.1.5:9100 ')).toBe('ws://192.168.1.5:9100');
  });
  it('http(s) превращается в ws(s), лишний слэш убирается', () => {
    expect(normalizeServerUrl('https://aura.example.com/')).toBe('wss://aura.example.com');
    expect(normalizeServerUrl('http://10.0.2.2:9000')).toBe('ws://10.0.2.2:9000');
    expect(normalizeServerUrl('wss://aura.example.com/ws')).toBe('wss://aura.example.com/ws');
  });
  it('мусор и чужие схемы отклоняются', () => {
    expect(normalizeServerUrl('')).toBeNull();
    expect(normalizeServerUrl('ftp://host')).toBeNull();
    expect(normalizeServerUrl('ws://')).toBeNull();
  });
});

describe('platformDefault', () => {
  it('Android-эмулятор ходит на компьютер разработчика через 10.0.2.2', () => {
    expect(platformDefault({ platform: 'android' })).toBe('ws://10.0.2.2:9000');
  });
  it('веб на настоящем https-хосте — wss того же хоста', () => {
    expect(platformDefault({ platform: 'web', location: { protocol: 'https:', hostname: 'aura.example.com', host: 'aura.example.com' } })).toBe(
      'wss://aura.example.com',
    );
  });
  it('локальная разработка и Electron — 127.0.0.1, не localhost', () => {
    expect(platformDefault(web)).toBe('ws://127.0.0.1:9000');
    expect(platformDefault({ platform: 'electron', location: { protocol: 'file:', hostname: '', host: '' } })).toBe('ws://127.0.0.1:9000');
  });
});

describe('serverCandidates', () => {
  it('основной → запасной → дефолт, без повторов', () => {
    expect(serverCandidates(null, { ...web, primary: 'wss://a.example', fallback: 'wss://b.example' })).toEqual([
      'wss://a.example',
      'wss://b.example',
      'ws://127.0.0.1:9000',
    ]);
    expect(serverCandidates(null, { ...web, primary: '127.0.0.1' })).toEqual(['ws://127.0.0.1:9000']);
  });
  it('адрес, заданный пользователем, — единственный', () => {
    expect(serverCandidates('192.168.0.10', { ...web, primary: 'wss://a.example' })).toEqual(['ws://192.168.0.10:9000']);
  });
  it('неразборчивый адрес пользователя не ломает список', () => {
    expect(serverCandidates('ftp://x', { ...web, primary: 'wss://a.example' })).toEqual(['wss://a.example', 'ws://127.0.0.1:9000']);
  });
  it('без переменных сборки остаётся платформенный дефолт', () => {
    expect(serverCandidates(null, { platform: 'android' })).toEqual(['ws://10.0.2.2:9000']);
  });
});
