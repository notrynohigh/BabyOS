/**
 * BabyOS Studio — 预加载脚本
 *
 * 安全地将 Electron 主进程 API 暴露给渲染进程（Vue 应用）。
 * 使用 contextBridge 确保上下文隔离。
 */

const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('electronAPI', {
  // ---------------------------------------------------------------------------
  // 串口通信
  // ---------------------------------------------------------------------------
  serial: {
    /** 获取可用串口列表 */
    list: () => ipcRenderer.invoke('serial:list'),

    /** 打开串口 @returns {{ ok: boolean, error?: string }} */
    open: (options) => ipcRenderer.invoke('serial:open', options),

    /** 关闭串口 */
    close: () => ipcRenderer.invoke('serial:close'),

    /** 发送数据 @param {number[]} data 字节数组 */
    write: (data) => ipcRenderer.invoke('serial:write', data),

    /** 监听串口数据 @param {function(number[])} callback */
    onData: (callback) => {
      const handler = (_event, data) => callback(data);
      ipcRenderer.on('serial:data', handler);
      return () => ipcRenderer.removeListener('serial:data', handler);
    },
  },

  // ---------------------------------------------------------------------------
  // 文件对话框
  // ---------------------------------------------------------------------------
  dialog: {
    /** 打开文件对话框 @returns {string|null} 文件路径 */
    openFile: (options) => ipcRenderer.invoke('dialog:openFile', options),

    /** 打开目录对话框 @returns {string|null} 目录路径 */
    openDirectory: () => ipcRenderer.invoke('dialog:openDirectory'),

    /** 保存文件对话框 @returns {string|null} 文件路径 */
    saveFile: (options) => ipcRenderer.invoke('dialog:saveFile', options),
  },

  // ---------------------------------------------------------------------------
  // 外部链接
  // ---------------------------------------------------------------------------
  shell: {
    /** 在默认浏览器中打开 URL */
    openExternal: (url) => ipcRenderer.invoke('shell:openExternal', url),
    /** 用系统文件管理器打开本地路径（目录或文件） */
    openPath: (p) => ipcRenderer.invoke('shell:openPath', p),
  },

  // ---------------------------------------------------------------------------
  // Python 后端
  // ---------------------------------------------------------------------------
  python: {
    /** 获取后端状态 @returns {{ running: boolean, port: number }} */
    status: () => ipcRenderer.invoke('python:status'),

    /** 监听后端日志 */
    onLog: (callback) => {
      const handler = (_event, msg) => callback(msg);
      ipcRenderer.on('python-log', handler);
      return () => ipcRenderer.removeListener('python-log', handler);
    },
  },

  // ---------------------------------------------------------------------------
  // HTTP 请求到 Python 后端（AutoML）
  // ---------------------------------------------------------------------------
  http: {
    /** 发送 HTTP 请求 @param {string} url API 路径 @param {string} method 方法 @param {object} body 请求体 @param {object} options 额外选项(如 {responseType:'arraybuffer'}) */
    request: (url, method, body, options) => ipcRenderer.invoke('http:request', { url, method, body, options }),

    /** 上传文件 @param {string} url API 路径 @param {string} filePath 文件路径 @param {string} fieldName 字段名 */
    upload: (url, filePath, fieldName) => ipcRenderer.invoke('http:upload', { url, filePath, fieldName }),

    /** 上传文件（带mapping参数） */
    uploadWithMapping: (url, filePath, fieldName, mapping) => ipcRenderer.invoke('http:uploadWithMapping', { url, filePath, fieldName, mapping }),
  },
});
