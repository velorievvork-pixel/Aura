// Exposes only what the UI needs to adapt its layout; no Node APIs reach the page.
const { contextBridge } = require('electron');

contextBridge.exposeInMainWorld('auraDesktop', {
  platform: process.platform,
});
