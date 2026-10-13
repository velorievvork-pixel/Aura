// Exposes only what the UI needs; no Node APIs reach the page.
//  - auraDesktop: the OS, so CSS can leave room for window buttons;
//  - auraSecure: token storage encrypted by the OS (safeStorage in the main process).
const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('auraDesktop', {
  platform: process.platform,
});

contextBridge.exposeInMainWorld('auraSecure', {
  get: (key) => ipcRenderer.invoke('secure:get', String(key)),
  set: (key, value) => ipcRenderer.invoke('secure:set', String(key), String(value)),
  remove: (key) => ipcRenderer.invoke('secure:remove', String(key)),
  encrypted: () => ipcRenderer.invoke('secure:encrypted'),
});
