/**
 * BabyOS Studio — UI E2E Electron main.
 *
 * Loads the REAL ui/index.html + REAL electron/preload.js and registers the
 * SAME IPC handlers as production electron/main.js, but does NOT spawn
 * Python: the test orchestrator owns FastAPI on :18080 and a software
 * simulated board (pty + MockBabyOSDevice).
 *
 * Drives the UI by executing real renderer JS (navigateTo / button .click())
 * after did-finish-load. Results are written as JSON to BABYOS_E2E_RESULT.
 */
const { app, BrowserWindow, ipcMain, dialog, shell } = require('electron');
const path = require('path');
const fs = require('fs');

const PYTHON_PORT = 18080;
const RESULT_PATH = process.env.BABYOS_E2E_RESULT || '';
const STEPS_PATH = process.env.BABYOS_E2E_STEPS || '';
const GITEE_URL = 'https://gitee.com/notrynohigh/BabyOS';

let mainWindow = null;
let serialPort = null;
let SerialPort = null;

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1400,
    height: 900,
    minWidth: 1000,
    minHeight: 700,
    title: 'BabyOS Studio E2E',
    show: false,
    webPreferences: {
      preload: path.join(__dirname, '..', '..', 'electron', 'preload.js'),
      contextIsolation: true,
      sandbox: false,
    },
  });
  mainWindow.loadFile(path.join(__dirname, '..', '..', 'ui', 'index.html'));
  mainWindow.on('closed', () => {
    mainWindow = null;
  });
}

function registerIpcHandlers() {
  // Same contract as production main.js (http relative-path only → :18080)
  ipcMain.handle('serial:list', async () => {
    if (!SerialPort) {
      try {
        SerialPort = require('serialport').SerialPort;
      } catch (err) {
        return [];
      }
    }
    try {
      const ports = await SerialPort.list();
      return ports.map((p) => ({
        path: p.path,
        manufacturer: p.manufacturer || '',
        serialNumber: p.serialNumber || '',
      }));
    } catch (err) {
      return [];
    }
  });

  ipcMain.handle('serial:open', async (_event, options) => {
    if (!SerialPort) {
      try {
        SerialPort = require('serialport').SerialPort;
      } catch (err) {
        return { ok: false, error: 'serialport 未安装' };
      }
    }
    try {
      if (serialPort && serialPort.isOpen) {
        serialPort.close();
      }
      serialPort = new SerialPort({
        path: options.path,
        baudRate: options.baudRate || 115200,
        dataBits: 8,
        stopBits: 1,
        parity: 'none',
      });
      return { ok: true };
    } catch (err) {
      return { ok: false, error: err.message };
    }
  });

  ipcMain.handle('serial:close', async () => {
    try {
      if (serialPort && serialPort.isOpen) {
        serialPort.close();
      }
      serialPort = null;
      return { ok: true };
    } catch (err) {
      return { ok: false, error: err.message };
    }
  });

  ipcMain.handle('serial:write', async (_event, data) => {
    try {
      if (!serialPort || !serialPort.isOpen) {
        return { ok: false, error: '串口未打开' };
      }
      const buf = Buffer.from(data);
      serialPort.write(buf);
      return { ok: true, bytes: buf.length };
    } catch (err) {
      return { ok: false, error: err.message };
    }
  });

  ipcMain.handle('http:request', async (_event, { url, method, body, options }) => {
    try {
      if (!url || !url.startsWith('/')) {
        return { ok: false, status: 0, data: { detail: '请求被拒绝: 仅允许本地相对路径' } };
      }
      const fetchOptions = {
        method: method || 'GET',
        headers: { 'Content-Type': 'application/json' },
      };
      if (body) {
        fetchOptions.body = JSON.stringify(body);
      }
      const response = await fetch(`http://127.0.0.1:${PYTHON_PORT}${url}`, fetchOptions);
      if (options?.responseType === 'arraybuffer') {
        const buffer = await response.arrayBuffer();
        const base64 = Buffer.from(buffer).toString('base64');
        return { ok: response.ok, status: response.status, data: base64 };
      }
      if (response.status === 204) {
        return { ok: response.ok, status: response.status, data: null };
      }
      const data = await response.json();
      return { ok: response.ok, status: response.status, data };
    } catch (err) {
      return { ok: false, status: 0, data: { detail: err.message } };
    }
  });

  ipcMain.handle('http:upload', async (_event, { url, filePath, fieldName }) => {
    try {
      const http = require('http');
      if (!fs.existsSync(filePath)) {
        return { ok: false, status: 0, data: { detail: `文件不存在: ${filePath}` } };
      }
      const fileBuffer = fs.readFileSync(filePath);
      const fileName = path.basename(filePath);
      const boundary = '----FormBoundary' + Math.random().toString(36).slice(2);
      const fieldNameClean = fieldName || 'file';
      let body = '';
      body += `--${boundary}\r\n`;
      body += `Content-Disposition: form-data; name="${fieldNameClean}"; filename="${fileName}"\r\n`;
      body += `Content-Type: application/octet-stream\r\n\r\n`;
      const bodyEnd = `\r\n--${boundary}--\r\n`;
      const bodyStart = Buffer.from(body, 'utf-8');
      const bodyEndBuf = Buffer.from(bodyEnd, 'utf-8');
      const fullBody = Buffer.concat([bodyStart, fileBuffer, bodyEndBuf]);
      return new Promise((resolve) => {
        const options = {
          hostname: '127.0.0.1',
          port: PYTHON_PORT,
          path: url,
          method: 'POST',
          headers: {
            'Content-Type': `multipart/form-data; boundary=${boundary}`,
            'Content-Length': fullBody.length,
          },
          timeout: 30000,
        };
        const req = http.request(options, (res) => {
          let data = '';
          res.on('data', (chunk) => { data += chunk; });
          res.on('end', () => {
            try {
              resolve({
                ok: res.statusCode >= 200 && res.statusCode < 300,
                status: res.statusCode,
                data: JSON.parse(data),
              });
            } catch (e) {
              resolve({ ok: false, status: res.statusCode, data: { detail: '解析响应失败' } });
            }
          });
        });
        req.on('error', (err) => resolve({ ok: false, status: 0, data: { detail: err.message } }));
        req.on('timeout', () => {
          req.destroy();
          resolve({ ok: false, status: 0, data: { detail: '上传超时' } });
        });
        req.write(fullBody);
        req.end();
      });
    } catch (err) {
      return { ok: false, status: 0, data: { detail: err.message } };
    }
  });

  ipcMain.handle('http:uploadWithMapping', async (_event, { url, filePath, fieldName, mapping }) => {
    try {
      const http = require('http');
      if (!fs.existsSync(filePath)) {
        return { ok: false, status: 0, data: { detail: `文件不存在: ${filePath}` } };
      }
      const fileBuffer = fs.readFileSync(filePath);
      const fileName = path.basename(filePath);
      const boundary = '----FormBoundary' + Math.random().toString(36).slice(2);
      const fieldNameClean = fieldName || 'file';
      const parts = [];
      parts.push(Buffer.from(
        `--${boundary}\r\n` +
        `Content-Disposition: form-data; name="${fieldNameClean}"; filename="${fileName}"\r\n` +
        `Content-Type: application/octet-stream\r\n\r\n`,
        'utf-8'
      ));
      parts.push(fileBuffer);
      parts.push(Buffer.from(
        `\r\n--${boundary}\r\n` +
        `Content-Disposition: form-data; name="mapping"\r\n\r\n` +
        JSON.stringify(mapping),
        'utf-8'
      ));
      parts.push(Buffer.from(
        `\r\n--${boundary}\r\n` +
        `Content-Disposition: form-data; name="import_kind"\r\n\r\n` +
        `append`,
        'utf-8'
      ));
      parts.push(Buffer.from(`\r\n--${boundary}--\r\n`, 'utf-8'));
      const fullBody = Buffer.concat(parts);
      return new Promise((resolve) => {
        const options = {
          hostname: '127.0.0.1',
          port: PYTHON_PORT,
          path: url,
          method: 'POST',
          headers: {
            'Content-Type': `multipart/form-data; boundary=${boundary}`,
            'Content-Length': fullBody.length,
          },
          timeout: 30000,
        };
        const req = http.request(options, (res) => {
          let data = '';
          res.on('data', (chunk) => { data += chunk; });
          res.on('end', () => {
            try {
              resolve({
                ok: res.statusCode >= 200 && res.statusCode < 300,
                status: res.statusCode,
                data: JSON.parse(data),
              });
            } catch (e) {
              resolve({ ok: false, status: res.statusCode, data: { detail: '解析响应失败' } });
            }
          });
        });
        req.on('error', (err) => resolve({ ok: false, status: 0, data: { detail: err.message } }));
        req.on('timeout', () => {
          req.destroy();
          resolve({ ok: false, status: 0, data: { detail: '上传超时' } });
        });
        req.write(fullBody);
        req.end();
      });
    } catch (err) {
      return { ok: false, status: 0, data: { detail: err.message } };
    }
  });

  ipcMain.handle('dialog:openFile', async (_event, options) => {
    try {
      const win = mainWindow || BrowserWindow.getFocusedWindow();
      const result = await dialog.showOpenDialog(win, {
        properties: ['openFile'],
        filters: options?.filters || [{ name: 'All Files', extensions: ['*'] }],
      });
      return result.canceled ? null : result.filePaths[0];
    } catch (err) {
      return null;
    }
  });

  ipcMain.handle('dialog:openDirectory', async () => {
    const result = await dialog.showOpenDialog(mainWindow, {
      properties: ['openDirectory'],
    });
    return result.canceled ? null : result.filePaths[0];
  });

  ipcMain.handle('dialog:saveFile', async (_event, options) => {
    const result = await dialog.showSaveDialog(mainWindow, {
      filters: options?.filters || [{ name: 'All Files', extensions: ['*'] }],
    });
    return result.canceled ? null : result.filePath;
  });

  ipcMain.handle('shell:openExternal', async (_event, url) => {
    await shell.openExternal(url);
  });

  ipcMain.handle('shell:openPath', async (_event, p) => {
    await shell.openPath(p);
  });

  ipcMain.handle('python:status', async () => {
    let running = false;
    try {
      const http = require('http');
      await new Promise((resolve) => {
        const req = http.get(`http://127.0.0.1:${PYTHON_PORT}/api/version`, { timeout: 1500 }, (res) => {
          res.resume();
          running = res.statusCode === 200;
          resolve();
        });
        req.on('error', () => resolve());
        req.on('timeout', () => { req.destroy(); resolve(); });
      });
    } catch (e) { /* ignore */ }
    return { running, port: PYTHON_PORT };
  });
}

function writeResult(payload) {
  if (!RESULT_PATH) return;
  try {
    fs.writeFileSync(RESULT_PATH, JSON.stringify(payload, null, 2), 'utf-8');
  } catch (err) {
    console.error('[E2E] write result failed', err.message);
  }
}

async function runSteps() {
  if (!STEPS_PATH || !fs.existsSync(STEPS_PATH) || !mainWindow) {
    writeResult({ ok: false, error: 'missing steps or window' });
    app.exit(1);
    return;
  }
  const steps = JSON.parse(fs.readFileSync(STEPS_PATH, 'utf-8'));
  const results = [];
  let allOk = true;

  for (const step of steps) {
    const name = step.name || 'step';
    const expr = step.expr;
    const timeoutMs = step.timeout_ms || 20000;
    const t0 = Date.now();
    try {
      const value = await mainWindow.webContents.executeJavaScript(expr, true);
      const duration_ms = Date.now() - t0;
      const ok = value && value.ok === true;
      if (!ok) allOk = false;
      results.push({
        name,
        ok,
        duration_ms,
        detail: value && typeof value === 'object' ? value : { value },
      });
      console.log(`[E2E] ${ok ? 'PASS' : 'FAIL'} ${name} (${duration_ms}ms)`);
    } catch (err) {
      allOk = false;
      results.push({
        name,
        ok: false,
        duration_ms: Date.now() - t0,
        detail: { error: String(err && err.message ? err.message : err) },
      });
      console.log(`[E2E] FAIL ${name}: ${err.message || err}`);
    }
  }

  writeResult({ ok: allOk, steps: results });
  app.exit(allOk ? 0 : 1);
}

app.whenReady().then(() => {
  registerIpcHandlers();
  createWindow();
  mainWindow.webContents.on('did-finish-load', () => {
    // Give app.js a tick to bind handlers
    setTimeout(() => {
      runSteps().catch((err) => {
        writeResult({ ok: false, error: String(err) });
        app.exit(1);
      });
    }, 800);
  });
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});

app.on('before-quit', () => {
  if (serialPort && serialPort.isOpen) {
    serialPort.close();
  }
});
