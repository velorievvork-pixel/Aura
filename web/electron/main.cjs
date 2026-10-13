// Desktop shell for Aura (macOS, Windows, Linux). Loads the built web app from dist/ in a
// sandboxed window; in development set AURA_DEV_URL to load the Vite dev server instead.
const { app, BrowserWindow, Menu, shell, nativeTheme, ipcMain, safeStorage } = require('electron');
const fs = require('node:fs');
const path = require('node:path');

const DEV_URL = process.env.AURA_DEV_URL;
const isMac = process.platform === 'darwin';
const BG = '#0b0c10';

nativeTheme.themeSource = 'dark';

function createWindow() {
  const win = new BrowserWindow({
    width: 1360,
    height: 880,
    minWidth: 380,
    minHeight: 620,
    backgroundColor: BG,
    show: false,
    title: 'Aura',
    // The app draws its own glass chrome: hide the title bar but keep native window controls.
    titleBarStyle: 'hidden',
    ...(isMac ? { trafficLightPosition: { x: 46, y: 46 } } : { titleBarOverlay: { color: BG, symbolColor: '#a1a4ae', height: 32 } }),
    icon: path.join(__dirname, '..', 'build', 'icon.png'),
    webPreferences: {
      preload: path.join(__dirname, 'preload.cjs'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
      spellcheck: false,
    },
  });

  win.once('ready-to-show', () => win.show());

  // Links to other sites open in the system browser, never inside the app window.
  win.webContents.setWindowOpenHandler(({ url }) => {
    if (/^https?:/.test(url)) shell.openExternal(url);
    return { action: 'deny' };
  });
  win.webContents.on('will-navigate', (e, url) => {
    if (!url.startsWith('file:') && !(DEV_URL && url.startsWith(DEV_URL))) {
      e.preventDefault();
      if (/^https?:/.test(url)) shell.openExternal(url);
    }
  });

  if (DEV_URL) win.loadURL(DEV_URL);
  else win.loadFile(path.join(__dirname, '..', 'dist', 'index.html'));
}

// Token storage for the renderer (window.auraSecure): values are encrypted by the OS
// (Keychain / DPAPI / libsecret) and kept in userData. Without an OS keyring
// (bare Linux) safeStorage is unavailable and values are stored as-is — the UI is told so.
const secureFile = () => path.join(app.getPath('userData'), 'secure.json');

function readSecure() {
  try {
    return JSON.parse(fs.readFileSync(secureFile(), 'utf8'));
  } catch {
    return {};
  }
}

function writeSecure(data) {
  fs.mkdirSync(path.dirname(secureFile()), { recursive: true });
  fs.writeFileSync(secureFile(), JSON.stringify(data), { mode: 0o600 });
}

function registerSecureStore() {
  const canEncrypt = () => safeStorage.isEncryptionAvailable();
  ipcMain.handle('secure:get', (_e, key) => {
    const entry = readSecure()[key];
    if (!entry) return null;
    try {
      return entry.enc ? safeStorage.decryptString(Buffer.from(entry.value, 'base64')) : entry.value;
    } catch {
      return null;
    }
  });
  ipcMain.handle('secure:set', (_e, key, value) => {
    const data = readSecure();
    data[key] = canEncrypt()
      ? { enc: true, value: safeStorage.encryptString(value).toString('base64') }
      : { enc: false, value };
    writeSecure(data);
  });
  ipcMain.handle('secure:remove', (_e, key) => {
    const data = readSecure();
    delete data[key];
    writeSecure(data);
  });
  ipcMain.handle('secure:encrypted', () => canEncrypt());
}

function buildMenu() {
  const template = [
    ...(isMac ? [{ role: 'appMenu' }] : []),
    { role: 'editMenu' },
    { role: 'viewMenu' },
    { role: 'windowMenu' },
  ];
  Menu.setApplicationMenu(Menu.buildFromTemplate(template));
}

app.setAppUserModelId('app.aura.network');

if (!app.requestSingleInstanceLock()) {
  app.quit();
} else {
  app.on('second-instance', () => {
    const [win] = BrowserWindow.getAllWindows();
    if (win) {
      if (win.isMinimized()) win.restore();
      win.focus();
    }
  });

  app.whenReady().then(() => {
    registerSecureStore();
    buildMenu();
    createWindow();
    app.on('activate', () => {
      if (BrowserWindow.getAllWindows().length === 0) createWindow();
    });
  });

  app.on('window-all-closed', () => {
    if (!isMac) app.quit();
  });
}
