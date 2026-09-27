/**
 * BabyOS Studio — Electron 主进程
 * 纯桌面应用，无服务器依赖
 */

const { app, BrowserWindow, ipcMain, dialog, shell } = require('electron');
const path = require('path');
const { spawn } = require('child_process');
const fs = require('fs');

// ---------------------------------------------------------------------------
// 全局状态
// ---------------------------------------------------------------------------
let mainWindow = null;
let pythonProcess = null;
let serialPort = null;
let SerialPort = null;

const PYTHON_PORT = 18080;
const GITEE_URL = 'https://gitee.com/notrynohigh/BabyOS';

// ---------------------------------------------------------------------------
// 窗口创建
// ---------------------------------------------------------------------------
function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1400,
    height: 900,
    minWidth: 1000,
    minHeight: 700,
    title: 'BabyOS Studio',
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      sandbox: false,
    },
  });

  // 直接加载本地 HTML，无需任何服务器
  mainWindow.loadFile(path.join(__dirname, '..', 'ui', 'index.html'));

  mainWindow.on('closed', () => {
    mainWindow = null;
  });
}

// ---------------------------------------------------------------------------
// Python 后端管理（AutoML 专用，可选）
// ---------------------------------------------------------------------------
function startPythonBackend() {
  const pythonDir = path.join(__dirname, '..', 'python');
  const appDir = path.join(pythonDir, 'app');
  const mainPy = path.join(appDir, 'main.py');

  if (!fs.existsSync(mainPy)) {
    console.log('[BabyOS Studio] Python 后端未找到，跳过启动');
    return;
  }

  const venvPython = path.join(pythonDir, '.venv', 'bin', 'python');
  const pythonCmd = fs.existsSync(venvPython) ? venvPython : 'python3';

  pythonProcess = spawn(pythonCmd, [
    '-m', 'uvicorn', 'app.main:app',
    '--host', '127.0.0.1',
    '--port', String(PYTHON_PORT),
  ], {
    cwd: pythonDir,
    stdio: ['ignore', 'pipe', 'pipe'],
  });

  pythonProcess.stdout.on('data', (data) => {
    const msg = data.toString().trim();
    if (msg) {
      console.log(`[Python] ${msg}`);
      mainWindow?.webContents.send('python-log', msg);
    }
  });

  pythonProcess.stderr.on('data', (data) => {
    const msg = data.toString().trim();
    if (msg) {
      console.error(`[Python] ${msg}`);
      mainWindow?.webContents.send('python-log', msg);
    }
  });

  pythonProcess.on('exit', (code) => {
    console.log(`[Python] 进程退出 code=${code}`);
    pythonProcess = null;
  });

  console.log(`[BabyOS Studio] Python 后端启动中... http://127.0.0.1:${PYTHON_PORT}`);
}

function stopPythonBackend() {
  if (pythonProcess) {
    pythonProcess.kill('SIGTERM');
    pythonProcess = null;
  }
}

// ---------------------------------------------------------------------------
// 注册 IPC 处理器
// ---------------------------------------------------------------------------
function registerIpcHandlers() {
  // 串口通信
  ipcMain.handle('serial:list', async () => {
    if (!SerialPort) {
      try {
        SerialPort = require('serialport').SerialPort;
      } catch (err) {
        console.warn('[Serial] serialport 未安装');
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
      console.error('[Serial] list error:', err.message);
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

  // HTTP 请求到 Python 后端（AutoML）
  ipcMain.handle('http:request', async (_event, { url, method, body, options }) => {
    try {
      // BUG-7: URL validation — only allow relative paths to local backend
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

      // BUG-2: Handle binary responses (e.g. zip downloads)
      if (options?.responseType === 'arraybuffer') {
        const buffer = await response.arrayBuffer();
        const base64 = Buffer.from(buffer).toString('base64');
        return { ok: response.ok, status: response.status, data: base64 };
      }

      // 204 No Content — empty body, don't try to parse JSON
      if (response.status === 204) {
        return { ok: response.ok, status: response.status, data: null };
      }

      const data = await response.json();
      return { ok: response.ok, status: response.status, data };
    } catch (err) {
      return { ok: false, status: 0, data: { detail: err.message } };
    }
  });

  // 文件上传到 Python 后端
  ipcMain.handle('http:upload', async (_event, { url, filePath, fieldName }) => {
    try {
      const fs = require('fs');
      const path = require('path');
      const http = require('http');

      console.log('[Upload] url:', url, 'filePath:', filePath);
      if (!fs.existsSync(filePath)) {
        console.error('[Upload] file not found:', filePath);
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

      return new Promise((resolve, reject) => {
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
              const jsonData = JSON.parse(data);
              resolve({ ok: res.statusCode >= 200 && res.statusCode < 300, status: res.statusCode, data: jsonData });
            } catch (e) {
              resolve({ ok: false, status: res.statusCode, data: { detail: '解析响应失败' } });
            }
          });
        });

        req.on('error', (err) => {
          console.error('[Upload] request error:', err.message);
          resolve({ ok: false, status: 0, data: { detail: err.message } });
        });

        req.on('timeout', () => {
          req.destroy();
          resolve({ ok: false, status: 0, data: { detail: '上传超时' } });
        });

        req.write(fullBody);
        req.end();
      });
    } catch (err) {
      console.error('[Upload] error:', err.message);
      return { ok: false, status: 0, data: { detail: err.message } };
    }
  });

  // 文件上传（带mapping参数）
  ipcMain.handle('http:uploadWithMapping', async (_event, { url, filePath, fieldName, mapping }) => {
    try {
      const fs = require('fs');
      const path = require('path');
      const http = require('http');

      const fileBuffer = fs.readFileSync(filePath);
      const fileName = path.basename(filePath);

      const boundary = '----FormBoundary' + Math.random().toString(36).slice(2);
      const fieldNameClean = fieldName || 'file';

      // multipart 顺序：文件头 → 文件数据 → mapping → import_kind → 结束
      const parts = [];
      // 文件部分头部
      parts.push(Buffer.from(
        `--${boundary}\r\n` +
        `Content-Disposition: form-data; name="${fieldNameClean}"; filename="${fileName}"\r\n` +
        `Content-Type: application/octet-stream\r\n\r\n`,
        'utf-8'
      ));
      // 文件数据
      parts.push(fileBuffer);
      // mapping 部分
      parts.push(Buffer.from(
        `\r\n--${boundary}\r\n` +
        `Content-Disposition: form-data; name="mapping"\r\n\r\n` +
        JSON.stringify(mapping),
        'utf-8'
      ));
      // import_kind 部分
      parts.push(Buffer.from(
        `\r\n--${boundary}\r\n` +
        `Content-Disposition: form-data; name="import_kind"\r\n\r\n` +
        `append`,
        'utf-8'
      ));
      // 结束
      parts.push(Buffer.from(`\r\n--${boundary}--\r\n`, 'utf-8'));

      const fullBody = Buffer.concat(parts);

      return new Promise((resolve, reject) => {
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
              const jsonData = JSON.parse(data);
              resolve({ ok: res.statusCode >= 200 && res.statusCode < 300, status: res.statusCode, data: jsonData });
            } catch (e) {
              resolve({ ok: false, status: res.statusCode, data: { detail: '解析响应失败' } });
            }
          });
        });

        req.on('error', (err) => {
          resolve({ ok: false, status: 0, data: { detail: err.message } });
        });

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

  // 文件对话框
  ipcMain.handle('dialog:openFile', async (_event, options) => {
    try {
      console.log('[Dialog] openFile called, mainWindow:', !!mainWindow);
      const win = mainWindow || BrowserWindow.getFocusedWindow();
      console.log('[Dialog] using window:', !!win);
      const result = await dialog.showOpenDialog(win, {
        properties: ['openFile'],
        filters: options?.filters || [{ name: 'All Files', extensions: ['*'] }],
      });
      console.log('[Dialog] result:', result.canceled ? 'canceled' : result.filePaths[0]);
      return result.canceled ? null : result.filePaths[0];
    } catch (err) {
      console.error('[Dialog] openFile error:', err.message);
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

  // 外部链接
  ipcMain.handle('shell:openExternal', async (_event, url) => {
    await shell.openExternal(url);
  });

  // 用系统文件管理器打开本地路径
  ipcMain.handle('shell:openPath', async (_event, p) => {
    await shell.openPath(p);
  });

  // Python 后端状态（同时检测端口是否可达）
  ipcMain.handle('python:status', async () => {
    let running = pythonProcess !== null;
    // 如果子进程不存在，尝试检测端口
    if (!running) {
      try {
        const http = require('http');
        await new Promise((resolve, reject) => {
          const req = http.get(`http://127.0.0.1:${PYTHON_PORT}/api/projects`, { timeout: 2000 }, (res) => {
            res.resume();
            running = res.statusCode === 200;
            resolve();
          });
          req.on('error', () => resolve());
          req.on('timeout', () => { req.destroy(); resolve(); });
        });
      } catch (e) { /* 忽略 */ }
    }
    return { running, port: PYTHON_PORT };
  });
}

// ---------------------------------------------------------------------------
// 应用生命周期
// ---------------------------------------------------------------------------
app.whenReady().then(() => {
  registerIpcHandlers();
  createWindow();
  startPythonBackend();

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
});

app.on('window-all-closed', () => {
  stopPythonBackend();
  if (process.platform !== 'darwin') {
    app.quit();
  }
});

app.on('before-quit', () => {
  stopPythonBackend();
  if (serialPort && serialPort.isOpen) {
    serialPort.close();
  }
});
