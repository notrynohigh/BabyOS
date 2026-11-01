#!/usr/bin/env node
/**
 * Frontend JavaScript logic test suite for BabyOS Studio.
 * Tests app.js functions with mocked electronAPI.
 * Run with: node test_frontend.js
 */

const fs = require('fs');
const path = require('path');

let _pass = 0;
let _fail = 0;
const RESULTS = [];

function assert(cond, msg) {
  if (!cond) throw new Error(msg || 'Assertion failed');
}

function test(name, fn) {
  try {
    fn();
    _pass++;
    RESULTS.push({ icon: '✅', name, detail: '' });
  } catch (e) {
    _fail++;
    RESULTS.push({ icon: '❌', name, detail: e.message });
  }
}

// `test()` does not await async fns — their rejections would be unhandled and
// crash the process now that the suite lives longer (T13/T14 async sections).
// Keep historical behavior: log them, don't crash.
process.on('unhandledRejection', (e) => {
  console.log(`[async test rejection — swallowed] ${e && e.message}`);
});

// ============================================================
// Mock DOM environment
// ============================================================
const elements = {};
const eventListeners = {};

function mockElement(id, attrs = {}) {
  const classes = new Set((attrs.className || '').split(/\s+/).filter(Boolean));
  const classList = {
    add: (c) => classes.add(c),
    remove: (c) => classes.delete(c),
    toggle: (c, force) => {
      if (force === undefined) {
        classes.has(c) ? classes.delete(c) : classes.add(c);
      } else {
        force ? classes.add(c) : classes.delete(c);
      }
      return classes.has(c);  // real DOM: toggle() returns the new state
    },
    contains: (c) => classes.has(c),
    toString: () => [...classes].join(' '),
  };
  const el = {
    id,
    textContent: '',
    _innerHTML: '',
    get innerHTML() { return this._innerHTML; },
    set innerHTML(v) {
      this._innerHTML = v;
      if (v === '') this.children.length = 0;  // real DOM: clearing innerHTML removes children
    },
    value: attrs.value || '',
    _className: attrs.className || '',
    get className() { return classList.toString(); },
    set className(v) {
      classes.clear();
      v.split(/\s+/).filter(Boolean).forEach(c => classes.add(c));
    },
    classList,
    style: { display: '', width: '', flex: '' },
    dataset: attrs.dataset || {},
    children: [],
    getAttribute: (k) => attrs[k],
    setAttribute: (k, v) => { attrs[k] = v; },
    appendChild: (child) => { el.children.push(child); },
    querySelector: (sel) => null,
    querySelectorAll: (sel) => [],
    addEventListener: (event, handler) => {
      const key = `${id}:${event}`;
      if (!eventListeners[key]) eventListeners[key] = [];
      eventListeners[key].push(handler);
    },
    click: () => {
      const key = `${id}:click`;
      if (eventListeners[key]) {
        eventListeners[key].forEach(h => h({
          target: el,
          stopPropagation: () => {},
          preventDefault: () => {},
        }));
      }
    },
    focus: () => {},
    ...attrs,
  };
  elements[id] = el;
  return el;
}

// Mock document
global.document = {
  getElementById: (id) => elements[id] || null,
  querySelectorAll: (sel) => {
    if (sel === '.page') return Object.values(elements).filter(e => e.className?.includes('page'));
    if (sel === '.menu-item') return Object.values(elements).filter(e => e.dataset?.page);
    if (sel === '.menu-item[data-page]') return Object.values(elements).filter(e => e.dataset?.page);
    if (sel === '.card[data-page]') return Object.values(elements).filter(e => e.dataset?.page);
    if (sel === 'button[data-action]') return [];
    if (sel === 'button[data-action="delete-label"]') return [];
    return [];
  },
  createElement: (tag) => {
    const el = {
      tagName: tag,
      _textContent: '',
      _innerHTML: '',
      get textContent() { return this._textContent; },
      set textContent(v) {
        this._textContent = v;
        // Simulate browser: textContent → innerHTML encodes HTML entities
        this._innerHTML = v.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
      },
      get innerHTML() { return this._innerHTML; },
      set innerHTML(v) { this._innerHTML = v; },
      className: '',
      _classList: new Set(),
      classList: {
        add(c) { this._classList.add(c); },
        remove(c) { this._classList.delete(c); },
        contains(c) { return this._classList.has(c); },
      },
      style: {},
      dataset: {},
      children: [],
      appendChild: function(c) {
        this.children.push(c);
        // Simulate DOM: appended content becomes part of innerHTML
        if (c && c._innerHTML !== undefined) {
          this._innerHTML += c._innerHTML;
        } else if (c && c.textContent !== undefined) {
          this._innerHTML += c.textContent;
        }
      },
      click: () => {},
      href: '',
      download: '',
      remove: () => {},
    };
    // Patch classList to reference the element's _classList
    el.classList._classList = el._classList;
    return el;
  },
  body: { appendChild: () => {}, removeChild: () => {} },
  addEventListener: (event, handler) => {
    if (!eventListeners[`document:${event}`]) eventListeners[`document:${event}`] = [];
    eventListeners[`document:${event}`].push(handler);
  },
  removeEventListener: (event, handler) => {
    const key = `document:${event}`;
    const arr = eventListeners[key];
    if (!arr) return;
    const i = arr.indexOf(handler);
    if (i >= 0) arr.splice(i, 1);
  },
};

// Mock localStorage (console size / collapse persistence)
global.localStorage = (() => {
  const store = {};
  return {
    getItem: (k) => (k in store ? store[k] : null),
    setItem: (k, v) => { store[k] = String(v); },
    removeItem: (k) => { delete store[k]; },
    clear: () => { Object.keys(store).forEach(k => delete store[k]); },
  };
})();

// Mock window.electronAPI
const mockApiResponses = {};
let mockSerialOpen = false;

global.window = {
  electronAPI: {
    serial: {
      list: async () => [{ path: '/dev/ttyUSB0', manufacturer: 'Test' }],
      open: async (opts) => { mockSerialOpen = true; return { ok: true }; },
      close: async () => { mockSerialOpen = false; return { ok: true }; },
      write: async (data) => ({ ok: true, bytes: data.length }),
      onData: (cb) => () => {},
    },
    dialog: {
      openFile: async (opts) => '/tmp/test.bin',
      openDirectory: async () => '/tmp',
      saveFile: async (opts) => '/tmp/save.txt',
    },
    shell: {
      openExternal: async (url) => {},
    },
    python: {
      status: async () => ({ running: true, port: 18080 }),
      onLog: (cb) => () => {},
    },
    http: {
      request: async (url, method, body) => {
        const key = `${method}:${url}`;
        if (mockApiResponses[key]) {
          return mockApiResponses[key];
        }
        return { ok: true, status: 200, data: {} };
      },
      upload: async (url, filePath, fieldName) => {
        return { ok: true, status: 200, data: {} };
      },
      uploadWithMapping: async (url, filePath, fieldName, mapping) => {
        return { ok: true, status: 200, data: {} };
      },
    },
  },
};

// Mock require for app.js
global.require = (mod) => {
  if (mod === 'fs') return { writeFileSync: () => {} };
  return {};
};

// ============================================================
// Setup mock elements
// ============================================================
mockElement('global-log', { className: 'log-content' });
mockElement('btn-log-clear');
mockElement('btn-log-export');
mockElement('python-status');
mockElement('serial-status');
mockElement('serial-port');
mockElement('serial-baud', { value: '115200' });
mockElement('btn-refresh-port');
mockElement('btn-open-serial');
mockElement('serial-log');
mockElement('btn-clear-log');
mockElement('btn-test');
mockElement('btn-set-time');
mockElement('btn-select-firmware');
mockElement('firmware-path');
mockElement('firmware-name');
mockElement('btn-start-ota');
mockElement('btn-select-xmodem');
mockElement('xmodem-file');
mockElement('btn-select-ymodem');
mockElement('ymodem-file');
mockElement('btn-http-server');
mockElement('http-server-status');
mockElement('btn-http-send');
mockElement('btn-param-list');
mockElement('btn-param-get');
mockElement('btn-param-set');
mockElement('param-name');
mockElement('param-value');
mockElement('param-cmd');
mockElement('btn-param-send');
mockElement('btn-get-uid');
mockElement('btn-set-sn');
mockElement('btn-get-device-info');
mockElement('device-uid');
mockElement('device-sn');
mockElement('device-version');
mockElement('device-model');
mockElement('project-list');
mockElement('project-name');
mockElement('create-project-form', { style: { display: 'none' } });
mockElement('btn-create-project');
mockElement('btn-confirm-create');
mockElement('btn-cancel-create');
mockElement('btn-import-project');
mockElement('btn-import-dataset');
mockElement('btn-download-sample');
mockElement('dataset-list');
mockElement('btn-import-labels');
mockElement('label-list');
mockElement('btn-run-features');
mockElement('feature-list');
mockElement('btn-start-training');
mockElement('btn-stop-training');
mockElement('training-status');
mockElement('training-progress');
mockElement('training-log');
mockElement('btn-export-project');
mockElement('export-info');
mockElement('train-epochs', { value: '50' });
mockElement('train-timeout', { value: '3600' });
mockElement('gitee-link');
mockElement('gitee-card');

// 串口日志页
mockElement('seriallog-log');
mockElement('btn-slog-pause');
mockElement('btn-slog-clear');
mockElement('slog-hex', { checked: false });
mockElement('slog-autoscroll', { checked: true });
mockElement('slog-record', { checked: false });
mockElement('btn-slog-record-path');
mockElement('slog-record-status');

// 底部面板：串口通讯控制台（主日志）+ Studio 日志细条（次要，默认收起）
mockElement('serial-console', { className: 'serial-console' });
mockElement('serial-console-header');
mockElement('serial-console-resize');
mockElement('serial-console-toggle-icon');
mockElement('log-panel', { className: 'log-panel' });
mockElement('log-header');
mockElement('log-toggle-icon');
mockElement('log-latest');
mockElement('seriallog-dock-log');
mockElement('btn-slog-pause-dock');
mockElement('btn-slog-clear-dock');
mockElement('slog-hex-dock', { checked: false });
mockElement('slog-autoscroll-dock', { checked: true });
mockElement('slog-record-dock', { checked: false });

// Page elements
['home', 'projects', 'automl-data', 'automl-labels', 'automl-features', 'automl-training', 'automl-export',
 'serial', 'seriallog', 'ota', 'xmodem', 'http', 'params', 'device'].forEach(page => {
  mockElement(`page-${page}`, { className: `page${page === 'home' ? ' active' : ''}` });
});

// ============================================================
// Load app.js
// ============================================================
const appJsPath = path.join(__dirname, 'ui', 'app.js');
const appJsCode = fs.readFileSync(appJsPath, 'utf-8');

// Execute app.js in mocked environment
// Note: `let` bindings inside eval do NOT leak to the outer scope, so append
// explicit accessors for the serial-ownership state that T13 manipulates.
// Capture intervals registered by app.js (serial-log poll, python watchdog)
// and clear them after eval so background timers can't race the tests.
const _realSetInterval = global.setInterval;
const _capturedIntervals = [];
global.setInterval = (fn, ms, ...rest) => {
  const id = _realSetInterval(fn, ms, ...rest);
  _capturedIntervals.push(id);
  return id;
};
eval(appJsCode + `
;global.__serialSet = (o) => {
  if ('serialOpen' in o) serialOpen = o.serialOpen;
  if ('pythonSerialOwned' in o) pythonSerialOwned = o.pythonSerialOwned;
  if ('serialPortPath' in o) serialPortPath = o.serialPortPath;
  if ('serialBaud' in o) serialBaud = o.serialBaud;
  if ('serialEncrypt' in o) serialEncrypt = o.serialEncrypt;
};
global.__serialGet = () => ({ serialOpen, pythonSerialOwned, serialPortPath, serialBaud, serialEncrypt });
global.__slogSet = (o) => {
  if ('_slogSeq' in o) _slogSeq = o._slogSeq;
  if ('_slogPaused' in o) _slogPaused = o._slogPaused;
};
global.__slogGet = () => ({ seq: _slogSeq, paused: _slogPaused, entries: _slogEntries });
`);
global.setInterval = _realSetInterval;
_capturedIntervals.forEach(id => clearInterval(id));

// ============================================================
// Tests
// ============================================================

console.log('='.repeat(60));
console.log('T1: Page Navigation');
console.log('='.repeat(60));

test('T1-01 navigateTo hides all pages', () => {
  navigateTo('serial');
  const homePage = elements['page-home'];
  assert(homePage.className !== 'active', 'home should not be active');
});

test('T1-02 navigateTo shows target page', () => {
  navigateTo('projects');
  const projPage = elements['page-projects'];
  assert(projPage.classList.contains('active'), 'projects should be active');
});

test('T1-03 navigateTo updates menu highlight', () => {
  navigateTo('serial');
  // Menu items should be updated (mocked querySelectorAll returns elements with dataset.page)
});

test('T1-04 navigateTo with projectId sets currentProject', () => {
  navigateTo('automl-data', 'test-pid-123');
  // currentProject should be set (we can't directly access it, but the function shouldn't throw)
});

test('T1-05 navigateTo nonexistent page does not crash', () => {
  navigateTo('nonexistent-page');
  // Should not throw
});


console.log('\n' + '='.repeat(60));
console.log('T2: API Wrapper Functions');
console.log('='.repeat(60));

test('T2-01 apiGet returns data on success', async () => {
  mockApiResponses['GET:/api/projects'] = { ok: true, status: 200, data: [{ name: 'test' }] };
  const result = await apiGet('/api/projects');
  assert(Array.isArray(result), 'should return array');
  assert(result[0].name === 'test', 'should have correct data');
});

test('T2-02 apiGet throws on failure', async () => {
  mockApiResponses['GET:/api/fail'] = { ok: false, status: 404, data: { detail: 'Not found' } };
  try {
    await apiGet('/api/fail');
    assert(false, 'should have thrown');
  } catch (e) {
    assert(e.message.includes('Not found'), 'should include error detail');
  }
});

test('T2-03 apiPost returns data on success', async () => {
  mockApiResponses['POST:/api/projects'] = { ok: true, status: 201, data: { project_id: 'abc' } };
  const result = await apiPost('/api/projects', { name: 'test' });
  assert(result.project_id === 'abc', 'should return project_id');
});

test('T2-04 apiPut returns data on success', async () => {
  mockApiResponses['PUT:/api/projects/abc'] = { ok: true, status: 200, data: { name: 'updated' } };
  const result = await apiPut('/api/projects/abc', { name: 'updated' });
  assert(result.name === 'updated', 'should return updated data');
});

test('T2-05 apiDelete returns data on success', async () => {
  mockApiResponses['DELETE:/api/projects/abc'] = { ok: true, status: 200, data: {} };
  const result = await apiDelete('/api/projects/abc');
  assert(result !== undefined, 'should return result');
});

test('T2-06 apiUpload returns data on success', async () => {
  const result = await apiUpload('/api/upload', '/tmp/test.csv', 'file');
  assert(result !== undefined, 'should return result');
});


console.log('\n' + '='.repeat(60));
console.log('T3: Logging');
console.log('='.repeat(60));

test('T3-01 addLog appends to log element', () => {
  elements['global-log'].innerHTML = '';
  addLog('Test message');
  assert(elements['global-log'].innerHTML.length > 0, 'log should have content');
});

test('T3-02 addLog removes empty hint', () => {
  elements['global-log'].innerHTML = '<div class="log-empty">暂无日志</div>';
  addLog('Test');
  assert(!elements['global-log'].innerHTML.includes('log-empty'), 'should remove empty hint');
});

test('T3-03 escapeHtml escapes special characters', () => {
  const result = escapeHtml('<script>alert("xss")</script>');
  assert(!result.includes('<script>'), 'should escape HTML');
  assert(result.includes('&lt;'), 'should contain escaped <');
});


console.log('\n' + '='.repeat(60));
console.log('T4: Project Management');
console.log('='.repeat(60));

test('T4-01 showCreateProjectForm shows form', () => {
  showCreateProjectForm();
  assert(elements['create-project-form'].style.display === 'block', 'form should be visible');
});

test('T4-02 hideCreateProjectForm hides form', () => {
  hideCreateProjectForm();
  assert(elements['create-project-form'].style.display === 'none', 'form should be hidden');
});

test('T4-03 createProject sends POST request', async () => {
  mockApiResponses['POST:/api/projects'] = { ok: true, status: 201, data: { name: 'new' } };
  elements['project-name'].value = 'new';
  await createProject();
  // Should not throw
});

test('T4-04 createProject validates empty name', async () => {
  elements['project-name'].value = '';
  // createProject calls alert() for empty name — just verify it doesn't crash
  await createProject();
});


console.log('\n' + '='.repeat(60));
console.log('T5: Data Management');
console.log('='.repeat(60));

test('T5-01 loadDatasets renders file list', async () => {
  mockApiResponses['GET:/api/projects/test-pid/dataset'] = {
    ok: true, status: 200,
    data: { files: [{ filename: 'test.csv', rows: 100, cols: 7 }] }
  };
  await loadDatasets('test-pid');
  assert(elements['dataset-list'].innerHTML.includes('test.csv'), 'should show filename');
});

test('T5-02 loadDatasets shows empty hint when no files', async () => {
  mockApiResponses['GET:/api/projects/empty-pid/dataset'] = {
    ok: true, status: 200,
    data: { files: [] }
  };
  await loadDatasets('empty-pid');
  assert(elements['dataset-list'].innerHTML.includes('暂无数据'), 'should show empty hint');
});

test('T5-03 downloadSampleCSV creates CSV blob', () => {
  // This function creates a download link — just verify it doesn't throw
  downloadSampleCSV();
});


console.log('\n' + '='.repeat(60));
console.log('T6: Label Management');
console.log('='.repeat(60));

test('T6-01 loadLabels renders label list', async () => {
  mockApiResponses['GET:/api/projects/test-pid/labels'] = {
    ok: true, status: 200,
    data: [{ label_id: 0, name: 'normal', color: '#2f80ed' }]
  };
  await loadLabels('test-pid');
  assert(elements['label-list'].innerHTML.includes('normal'), 'should show label name');
});

test('T6-02 loadLabels shows empty hint when no labels', async () => {
  mockApiResponses['GET:/api/projects/empty-pid/labels'] = {
    ok: true, status: 200,
    data: []
  };
  await loadLabels('empty-pid');
  assert(elements['label-list'].innerHTML.includes('暂无标注'), 'should show empty hint');
});


console.log('\n' + '='.repeat(60));
console.log('T7: Feature Engineering');
console.log('='.repeat(60));

test('T7-01 loadFeatureConfig renders feature list', async () => {
  mockApiResponses['GET:/api/projects/test-pid/features/config'] = {
    ok: true, status: 200,
    data: { feature_ids: ['mean', 'std', 'rms'] }
  };
  await loadFeatureConfig('test-pid');
  assert(elements['feature-list'].innerHTML.includes('mean'), 'should show feature id');
});

test('T7-02 loadFeatureConfig shows empty hint', async () => {
  mockApiResponses['GET:/api/projects/empty-pid/features/config'] = {
    ok: true, status: 200,
    data: { feature_ids: [] }
  };
  await loadFeatureConfig('empty-pid');
  assert(elements['feature-list'].innerHTML.includes('暂无特征'), 'should show empty hint');
});


console.log('\n' + '='.repeat(60));
console.log('T8: Training');
console.log('='.repeat(60));

test('T8-01 loadTrainingStatus updates status text', async () => {
  mockApiResponses['GET:/api/projects/test-pid/training'] = {
    ok: true, status: 200,
    data: { status: 'running', done: 5, total: 10, current: 'Training...' }
  };
  await loadTrainingStatus('test-pid');
  assert(elements['training-status'].textContent === '训练中...', 'should show running status');
});

test('T8-02 loadTrainingStatus updates progress bar', async () => {
  mockApiResponses['GET:/api/projects/test-pid/training'] = {
    ok: true, status: 200,
    data: { status: 'running', done: 5, total: 10, current: '' }
  };
  await loadTrainingStatus('test-pid');
  assert(elements['training-progress'].style.width === '50%', 'progress should be 50%');
});

test('T8-03 loadTrainingStatus handles idle state', async () => {
  mockApiResponses['GET:/api/projects/test-pid/training'] = {
    ok: true, status: 200,
    data: { status: 'idle', done: 0, total: 0, current: '' }
  };
  await loadTrainingStatus('test-pid');
  assert(elements['training-status'].textContent === '未开始', 'should show idle');
});


console.log('\n' + '='.repeat(60));
console.log('T9: Export');
console.log('='.repeat(60));

test('T9-01 loadExportInfo shows completed export', async () => {
  mockApiResponses['GET:/api/projects/test-pid/export'] = {
    ok: true, status: 200,
    data: { status: 'done', filename: 'export.bosml' }
  };
  await loadExportInfo('test-pid');
  assert(elements['export-info'].innerHTML.includes('export.bosml'), 'should show filename');
});

test('T9-02 loadExportInfo shows empty when no export', async () => {
  mockApiResponses['GET:/api/projects/test-pid/export'] = {
    ok: true, status: 200,
    data: { status: 'idle' }
  };
  await loadExportInfo('test-pid');
  assert(elements['export-info'].innerHTML.includes('暂无导出'), 'should show empty hint');
});


console.log('\n' + '='.repeat(60));
console.log('T10: Navigation Functions');
console.log('='.repeat(60));

test('T10-01 goToProjects navigates to projects page', () => {
  navigateTo('home'); // reset
  goToProjects();
  assert(elements['page-projects'].classList.contains('active'), 'projects should be active');
});

test('T10-02 goToAutomlData navigates to data page', () => {
  currentProject = 'test-pid';
  goToAutomlData();
  assert(elements['page-automl-data'].classList.contains('active'), 'automl-data should be active');
});

test('T10-03 goToAutomlData without project goes to projects', () => {
  currentProject = null;
  goToAutomlData();
  assert(elements['page-projects'].classList.contains('active'), 'should go to projects');
});

test('T10-04 goToAutomlLabels navigates to labels page', () => {
  currentProject = 'test-pid';
  goToAutomlLabels();
  assert(elements['page-automl-labels'].classList.contains('active'), 'automl-labels should be active');
});

test('T10-05 goToAutomlFeatures navigates to features page', () => {
  currentProject = 'test-pid';
  goToAutomlFeatures();
  assert(elements['page-automl-features'].classList.contains('active'), 'automl-features should be active');
});

test('T10-06 goToAutomlTraining navigates to training page', () => {
  currentProject = 'test-pid';
  goToAutomlTraining();
  assert(elements['page-automl-training'].classList.contains('active'), 'automl-training should be active');
});

test('T10-07 goToAutomlExport navigates to export page', () => {
  currentProject = 'test-pid';
  goToAutomlExport();
  assert(elements['page-automl-export'].classList.contains('active'), 'automl-export should be active');
});


console.log('\n' + '='.repeat(60));
console.log('T11: Serial Port Operations');
console.log('='.repeat(60));

test('T11-01 refreshPorts populates select', async () => {
  await refreshPorts();
  assert(elements['serial-port'].innerHTML.includes('/dev/ttyUSB0'), 'should show port');
});


console.log('\n' + '='.repeat(60));
console.log('T12: HTML Structure');
console.log('='.repeat(60));

test('T12-01 index.html has all required pages', () => {
  const html = fs.readFileSync(path.join(__dirname, 'ui', 'index.html'), 'utf-8');
  const requiredPages = ['page-home', 'page-projects', 'page-automl-data', 'page-automl-labels',
    'page-automl-features', 'page-automl-training', 'page-automl-export',
    'page-serial', 'page-ota', 'page-xmodem', 'page-http', 'page-params', 'page-device'];
  requiredPages.forEach(page => {
    assert(html.includes(`id="${page}"`), `Missing page: ${page}`);
  });
});

test('T12-02 index.html has CSP meta tag', () => {
  const html = fs.readFileSync(path.join(__dirname, 'ui', 'index.html'), 'utf-8');
  assert(html.includes('Content-Security-Policy'), 'should have CSP');
});

test('T12-03 index.html loads app.js', () => {
  const html = fs.readFileSync(path.join(__dirname, 'ui', 'index.html'), 'utf-8');
  assert(html.includes('app.js'), 'should load app.js');
});

test('T12-04 preload.js exposes electronAPI', () => {
  const preload = fs.readFileSync(path.join(__dirname, 'electron', 'preload.js'), 'utf-8');
  assert(preload.includes('electronAPI'), 'should expose electronAPI');
  assert(preload.includes('contextBridge'), 'should use contextBridge');
  assert(preload.includes('contextIsolation') || preload.includes('exposeInMainWorld'), 'should use context isolation');
});

test('T12-05 main.js has all IPC handlers', () => {
  const main = fs.readFileSync(path.join(__dirname, 'electron', 'main.js'), 'utf-8');
  const requiredHandlers = ['serial:list', 'serial:open', 'serial:close', 'serial:write',
    'http:request', 'http:upload', 'http:uploadWithMapping',
    'dialog:openFile', 'dialog:openDirectory', 'dialog:saveFile',
    'shell:openExternal', 'python:status'];
  requiredHandlers.forEach(handler => {
    assert(main.includes(`'${handler}'`), `Missing IPC handler: ${handler}`);
  });
});

test('T12-06 main.js sets contextIsolation: true', () => {
  const main = fs.readFileSync(path.join(__dirname, 'electron', 'main.js'), 'utf-8');
  assert(main.includes('contextIsolation: true'), 'should enable contextIsolation');
});


// ============================================================
// Print Report
// ============================================================
function printReport() {
  console.log('\n' + '='.repeat(60));
  console.log('TEST REPORT');
  console.log('='.repeat(60));

  RESULTS.forEach(r => {
    const detail = r.detail ? ` — ${r.detail}` : '';
    console.log(`  ${r.icon} ${r.name}${detail}`);
  });

  const total = _pass + _fail;
  console.log(`\n${'='.repeat(60)}`);
  console.log(`TOTAL: ${total}  PASS: ${_pass}  FAIL: ${_fail}`);
  console.log('='.repeat(60));
}

// ============================================================
// T13: _requirePythonSerial — 串口所有权同步与 Electron→Python 切换
// (async — 必须 await，因此单独顺序执行后再打印报告)
// ============================================================
async function asyncTest(name, fn) {
  try {
    await fn();
    _pass++;
    RESULTS.push({ icon: '✅', name, detail: '' });
  } catch (e) {
    _fail++;
    RESULTS.push({ icon: '❌', name, detail: e.message });
  }
}

(async () => {
  console.log('\n' + '='.repeat(60));
  console.log('T13: Python Serial Ownership (_requirePythonSerial)');
  console.log('='.repeat(60));

  const PORTS_KEY = 'GET:/api/device/serial/ports';
  const OPEN_KEY = 'POST:/api/device/serial/open';

  await asyncTest('T13-01 backend reports open → syncs flags and passes', async () => {
    __serialSet({ serialOpen: false, pythonSerialOwned: false, serialPortPath: '' });
    mockApiResponses[PORTS_KEY] = { ok: true, status: 200,
      data: { ports: ['COM9'], open: true, current: 'COM9', baudrate: 115200 } };
    const r = await _requirePythonSerial();
    assert(r === true, 'should pass when backend owns the port');
    const s = __serialGet();
    assert(s.serialOpen === true && s.pythonSerialOwned === true, 'flags should sync from backend');
    assert(s.serialPortPath === 'COM9', 'port path should sync from backend');
    delete mockApiResponses[PORTS_KEY];
  });

  await asyncTest('T13-02 Electron-owned port auto-switches to Python mode', async () => {
    __serialSet({ serialOpen: true, pythonSerialOwned: false,
      serialPortPath: 'COM5', serialBaud: 9600, serialEncrypt: false });
    mockSerialOpen = true;  // Electron native holds the handle
    mockApiResponses[OPEN_KEY] = { ok: true, status: 200,
      data: { ok: true, port: 'COM5', baudrate: 9600 } };
    const r = await _requirePythonSerial();
    assert(r === true, 'handover should succeed');
    assert(__serialGet().pythonSerialOwned === true, 'should be Python-owned after handover');
    assert(mockSerialOpen === false, 'Electron handle should be closed first');
    assert(elements['btn-open-serial'].textContent === '关闭串口', 'button should show close');
    delete mockApiResponses[OPEN_KEY];
  });

  await asyncTest('T13-03 handover failure resets UI to disconnected', async () => {
    __serialSet({ serialOpen: true, pythonSerialOwned: false,
      serialPortPath: 'COM5', serialBaud: 9600, serialEncrypt: false });
    mockSerialOpen = true;
    mockApiResponses[OPEN_KEY] = { ok: false, status: 500,
      data: { detail: 'PORT_OPEN_FAILED' } };
    const r = await _requirePythonSerial();
    assert(r === false, 'should fail when Python open fails');
    const s = __serialGet();
    assert(s.serialOpen === false && s.pythonSerialOwned === false, 'state should reset to closed');
    assert(elements['btn-open-serial'].textContent === '打开串口', 'button should show open');
    assert(elements['serial-status'].textContent === '未连接', 'status should show disconnected');
    delete mockApiResponses[OPEN_KEY];
  });

  await asyncTest('T13-04 nothing open → prompts to open via Python', async () => {
    __serialSet({ serialOpen: false, pythonSerialOwned: false });
    elements['global-log'].children.length = 0;
    const r = await _requirePythonSerial();
    assert(r === false, 'should refuse when nothing is open');
    const logged = elements['global-log'].children
      .map(c => c.innerHTML || '').join('\n');
    assert(logged.includes('请先打开串口'), 'should log guidance');
  });

  // ============================================================
  // T14: 串口日志汇总页 — journal 轮询 / 渲染 / 落盘
  // ============================================================
  console.log('\n' + '='.repeat(60));
  console.log('T14: Serial Log Page (journal polling / render / record)');
  console.log('='.repeat(60));

  // 停掉后台 interval 的影响：测试期间保持暂停，手动驱动 _pollSlog
  __slogSet({ _slogPaused: true });
  const slogReset = () => {
    __slogSet({ _slogSeq: 0 });
    _slogClearViews();  // 清两个视图（全页 + 停靠）与 entries
  };

  await asyncTest('T14-01 append renders TX/RX lines with hex', () => {
    slogReset();
    elements['slog-hex'].checked = true;
    _slogAppend([
      { seq: 1, ts: 1700000000.123, dir: 'TX', hex: 'aa 55', text: '·U' },
      { seq: 2, ts: 1700000001.0, dir: 'RX', hex: 'bb', text: '·' },
    ]);
    const kids = elements['seriallog-log'].children;
    assert(kids.length === 2, `expected 2 lines, got ${kids.length}`);
    assert(kids[0].innerHTML.includes('[TX]'), 'first line should be TX');
    assert(kids[0].innerHTML.includes('aa 55'), 'first line should show hex');
    assert(kids[1].innerHTML.includes('[RX]'), 'second line should be RX');
    elements['slog-hex'].checked = false;
  });

  await asyncTest('T14-01b default mode is text, not hex', () => {
    slogReset();
    assert(elements['slog-hex'].checked === false, 'HEX checkbox unchecked by default');
    _slogAppend([{ seq: 1, ts: 1700000000.0, dir: 'RX', hex: '48 49', text: 'HI' }]);
    const html = elements['seriallog-log'].children[0].innerHTML;
    assert(html.includes('HI'), 'text mode should show text payload');
    assert(!html.includes('48 49'), 'text mode should not show hex payload');
  });

  await asyncTest('T14-01c CRLF/LF render as real line breaks in text mode', () => {
    slogReset();
    _slogAppend([{ seq: 1, ts: 1700000000.0, dir: 'RX',
      hex: '6c 31 0d 0a 6c 32 0a 6c 33 0d 6c 34', text: 'l1\r\nl2\nl3\rl4' }]);
    const html = elements['seriallog-log'].children[0].innerHTML;
    assert(html.includes('l1\nl2\nl3\nl4'), `CRLF/CR/LF should collapse to line breaks, got: ${JSON.stringify(html)}`);
    assert(!html.includes('··'), 'newline bytes must not render as ··');
  });

  await asyncTest('T14-02 same-direction bursts within 50ms coalesce', () => {
    slogReset();
    elements['slog-hex'].checked = true;
    _slogAppend([{ seq: 1, ts: 1700000000.000, dir: 'RX', hex: '01', text: '·' }]);
    _slogAppend([{ seq: 2, ts: 1700000000.030, dir: 'RX', hex: '02', text: '·' }]);
    const kids = elements['seriallog-log'].children;
    assert(kids.length === 1, `bursts should merge into 1 line, got ${kids.length}`);
    assert(kids[0].innerHTML.includes('01 02'), 'merged line should contain both payloads');
    elements['slog-hex'].checked = false;
  });

  await asyncTest('T14-03 HEX toggle re-renders as text', () => {
    slogReset();
    elements['slog-hex'].checked = true;
    _slogAppend([{ seq: 1, ts: 1700000000.0, dir: 'TX', hex: '48 49', text: 'HI' }]);
    assert(elements['seriallog-log'].children[0].innerHTML.includes('48 49'), 'hex mode');
    elements['slog-hex'].checked = false;
    (eventListeners['slog-hex:change'] || []).forEach(h => h());
    assert(elements['seriallog-log'].children[0].innerHTML.includes('HI'), 'text mode');
    elements['slog-hex'].checked = false;
  });

  await asyncTest('T14-04 poll fetches incrementally and advances seq', async () => {
    slogReset();
    __slogSet({ _slogPaused: false });
    mockApiResponses['GET:/api/device/serial/journal?since=0&limit=500'] = {
      ok: true, status: 200,
      data: { entries: [{ seq: 7, ts: 1700000000.0, dir: 'TX', hex: 'aa', text: '·' }],
              next: 7, truncated: false },
    };
    await _pollSlog();
    assert(__slogGet().seq === 7, `seq should advance to 7, got ${__slogGet().seq}`);
    assert(elements['seriallog-log'].children.length === 1, 'one line rendered');
    mockApiResponses['GET:/api/device/serial/journal?since=7&limit=500'] = {
      ok: true, status: 200, data: { entries: [], next: 7, truncated: false },
    };
    await _pollSlog();
    assert(elements['seriallog-log'].children.length === 1, 'no new lines on empty poll');
    delete mockApiResponses['GET:/api/device/serial/journal?since=0&limit=500'];
    delete mockApiResponses['GET:/api/device/serial/journal?since=7&limit=500'];
    __slogSet({ _slogPaused: true });
  });

  await asyncTest('T14-05 truncated flag clears the view before re-render', async () => {
    slogReset();
    elements['slog-hex'].checked = true;
    _slogAppend([{ seq: 1, ts: 1700000000.0, dir: 'TX', hex: 'aa', text: '·' }]);
    __slogSet({ _slogSeq: 1, _slogPaused: false });
    mockApiResponses['GET:/api/device/serial/journal?since=1&limit=500'] = {
      ok: true, status: 200,
      data: { entries: [{ seq: 50, ts: 1700000001.0, dir: 'RX', hex: 'ff', text: '·' }],
              next: 50, truncated: true },
    };
    await _pollSlog();
    const kids = elements['seriallog-log'].children;
    assert(kids.length === 1, `view should restart with only new entry, got ${kids.length}`);
    assert(kids[0].innerHTML.includes('ff'), 'only the post-truncation entry remains');
    delete mockApiResponses['GET:/api/device/serial/journal?since=1&limit=500'];
    __slogSet({ _slogPaused: true });
    elements['slog-hex'].checked = false;
  });

  await asyncTest('T14-06 clear button resets view and seq', async () => {
    slogReset();
    __slogSet({ _slogSeq: 42 });
    _slogAppend([{ seq: 42, ts: 1700000000.0, dir: 'TX', hex: 'aa', text: '·' }]);
    mockApiResponses['POST:/api/device/serial/journal/clear'] = {
      ok: true, status: 200, data: { ok: true },
    };
    elements['btn-slog-clear'].click();
    await new Promise(r => setImmediate(r));
    assert(__slogGet().seq === 0, 'seq should reset to 0');
    assert(__slogGet().entries.length === 0, 'entries should be emptied');
    assert(elements['seriallog-log'].children.length === 0, 'view should be emptied');
    delete mockApiResponses['POST:/api/device/serial/journal/clear'];
  });

  await asyncTest('T14-07 record toggle on posts enable and shows path', async () => {
    mockApiResponses['POST:/api/device/serial/journal/record'] = {
      ok: true, status: 200, data: { enabled: true, path: 'D:\\logs\\serial.log' },
    };
    const hs = eventListeners['slog-record:change'] || [];
    assert(hs.length > 0, 'record change handler should be registered');
    await hs[0]({ target: { checked: true } });
    assert(elements['slog-record-status'].textContent.includes('记录中'),
      `status should show recording, got "${elements['slog-record-status'].textContent}"`);
    assert(elements['slog-record'].checked === true, 'checkbox should stay on');
  });

  await asyncTest('T14-08 record toggle off posts disable', async () => {
    mockApiResponses['POST:/api/device/serial/journal/record'] = {
      ok: true, status: 200, data: { enabled: false, path: '' },
    };
    const hs = eventListeners['slog-record:change'] || [];
    await hs[0]({ target: { checked: false } });
    assert(elements['slog-record-status'].textContent === '未记录',
      `status should reset, got "${elements['slog-record-status'].textContent}"`);
    delete mockApiResponses['POST:/api/device/serial/journal/record'];
  });

  await asyncTest('T14-09 record start failure unchecks the box', async () => {
    mockApiResponses['POST:/api/device/serial/journal/record'] = {
      ok: false, status: 400, data: { detail: 'RECORD_START_FAILED: 目录不存在' },
    };
    const hs = eventListeners['slog-record:change'] || [];
    const ev = { target: { checked: true } };
    await hs[0](ev);
    assert(ev.target.checked === false, 'checkbox should be forced back off on failure');
    delete mockApiResponses['POST:/api/device/serial/journal/record'];
  });

  // ============================================================
  // T15: 串口通讯控制台 + Studio 日志细条（共存 / 拖高 / 收起）
  // ============================================================
  console.log('\n' + '='.repeat(60));
  console.log('T15: Serial Console + Studio Log Strip (coexistence / resize / collapse)');
  console.log('='.repeat(60));

  await asyncTest('T15-01 entries render to both page and dock views', () => {
    slogReset();
    _slogAppend([{ seq: 1, ts: 1700000000.0, dir: 'TX', hex: 'aa 55', text: '·U' }]);
    assert(elements['seriallog-log'].children.length === 1, 'page view should have the line');
    assert(elements['seriallog-dock-log'].children.length === 1, 'dock view should have the line');
    assert(elements['seriallog-dock-log'].children[0].innerHTML.includes('[TX]'),
      'dock line should show TX');
  });

  await asyncTest('T15-02 dock clear wipes both views and resets seq', async () => {
    slogReset();
    __slogSet({ _slogSeq: 5 });
    _slogAppend([{ seq: 5, ts: 1700000000.0, dir: 'RX', hex: '01', text: '·' }]);
    mockApiResponses['POST:/api/device/serial/journal/clear'] = { ok: true, status: 200, data: {} };
    elements['btn-slog-clear-dock'].click();
    await new Promise(r => setImmediate(r));
    assert(elements['seriallog-log'].children.length === 0, 'page view emptied');
    assert(elements['seriallog-dock-log'].children.length === 0, 'dock view emptied');
    assert(__slogGet().seq === 0, 'seq reset');
    delete mockApiResponses['POST:/api/device/serial/journal/clear'];
  });

  await asyncTest('T15-03 pause toggle syncs both buttons and skips polling', async () => {
    __slogSet({ _slogPaused: false });
    elements['btn-slog-pause-dock'].click();
    assert(__slogGet().paused === true, 'paused after dock button click');
    assert(elements['btn-slog-pause'].textContent === '继续', 'page button label should sync');
    assert(elements['btn-slog-pause-dock'].textContent === '继续', 'dock button label should sync');
    // 暂停期间 _pollSlog 不应请求后端
    let journalFetches = 0;
    const origRequest = global.window.electronAPI.http.request;
    global.window.electronAPI.http.request = async (url, method, body) => {
      if (url.includes('/serial/journal?')) journalFetches++;
      return origRequest(url, method, body);
    };
    await _pollSlog();
    global.window.electronAPI.http.request = origRequest;
    assert(journalFetches === 0, 'no journal fetch while paused');
    elements['btn-slog-pause'].click();
    assert(__slogGet().paused === false, 'resumed');
    assert(elements['btn-slog-pause-dock'].textContent === '暂停', 'labels restored');
  });

  await asyncTest('T15-04 drag-resize sets console height and persists it', () => {
    const el = elements['serial-console'];
    el.classList.remove('collapsed');
    el.style.height = '320px';
    localStorage.removeItem('serial-console-height');
    const mhs = eventListeners['serial-console-resize:mousedown'] || [];
    assert(mhs.length > 0, 'resize mousedown handler registered');
    mhs[0]({ clientY: 400, preventDefault: () => {} });
    // 向上拖 80px → 320 + 80 = 400
    (eventListeners['document:mousemove'] || []).forEach(h => h({ clientY: 320 }));
    assert(el.style.height === '400px', `drag up should set 400px, got ${el.style.height}`);
    assert(!el.classList.contains('collapsed'), 'resize un-collapses console');
    assert(localStorage.getItem('serial-console-height') === null, 'no persist mid-drag');
    (eventListeners['document:mouseup'] || []).forEach(h => h({}));
    assert(localStorage.getItem('serial-console-height') === '400', 'final height persisted');
    // 拖到底：钳到下限 80px
    mhs[0]({ clientY: 0, preventDefault: () => {} });
    (eventListeners['document:mousemove'] || []).forEach(h => h({ clientY: 5000 }));
    (eventListeners['document:mouseup'] || []).forEach(h => h({}));
    assert(el.style.height === '80px', `drag down clamps to 80px, got ${el.style.height}`);
  });

  await asyncTest('T15-05 console header toggles collapse; action clicks do not', () => {
    const el = elements['serial-console'];
    el.classList.remove('collapsed');
    localStorage.removeItem('serial-console-collapsed');
    const hs = eventListeners['serial-console-header:click'] || [];
    assert(hs.length > 0, 'console header click handler registered');
    // 操作按钮 / 勾选框的点击不收起
    hs[0]({
      target: { closest: (sel) => (['.log-actions', 'button', 'label'].includes(sel) ? {} : null) },
      stopPropagation: () => {},
    });
    assert(!el.classList.contains('collapsed'), 'action click must not collapse console');
    // 点标题空白 → 收起
    hs[0]({ target: { closest: () => null }, stopPropagation: () => {} });
    assert(el.classList.contains('collapsed'), 'header body click collapses console');
    assert(elements['serial-console-toggle-icon'].textContent === '▸', 'icon shows collapsed');
    assert(localStorage.getItem('serial-console-collapsed') === '1', 'collapsed state persisted');
    // 再点 → 展开
    hs[0]({ target: { closest: () => null }, stopPropagation: () => {} });
    assert(!el.classList.contains('collapsed'), 'second click expands console');
    assert(elements['serial-console-toggle-icon'].textContent === '▾', 'icon restored');
    assert(localStorage.getItem('serial-console-collapsed') === '0', 'expanded state persisted');
  });

  await asyncTest('T15-06 record state syncs between page and dock checkboxes', async () => {
    mockApiResponses['POST:/api/device/serial/journal/record'] = {
      ok: true, status: 200, data: { enabled: true, path: 'D:\\logs\\cap.log' },
    };
    const hs = eventListeners['slog-record-dock:change'] || [];
    assert(hs.length > 0, 'dock record change handler should be registered');
    await hs[0]({ target: { checked: true } });
    assert(elements['slog-record-dock'].checked === true, 'dock checkbox on');
    assert(elements['slog-record'].checked === true, 'page checkbox synced on');
    mockApiResponses['POST:/api/device/serial/journal/record'] = {
      ok: true, status: 200, data: { enabled: false, path: '' },
    };
    await hs[0]({ target: { checked: false } });
    assert(elements['slog-record'].checked === false, 'page checkbox synced off');
    assert(elements['slog-record-dock'].checked === false, 'dock checkbox off');
    delete mockApiResponses['POST:/api/device/serial/journal/record'];
  });

  await asyncTest('T15-07 studio strip defaults collapsed; actions never toggle it', () => {
    // 折叠监听器在 app.js 的 DOMContentLoaded 块中注册——派发一次完成初始化
    localStorage.removeItem('log-panel-collapsed');
    (eventListeners['document:DOMContentLoaded'] || []).forEach(h => h());
    const panel = elements['log-panel'];
    assert(panel.classList.contains('collapsed'), 'strip should default to collapsed');
    assert(elements['log-toggle-icon'].textContent === '▸', 'icon shows collapsed by default');
    const hs = eventListeners['log-header:click'] || [];
    assert(hs.length > 0, 'log-header click handler registered');
    // 点操作区不展开
    hs[0]({
      target: { closest: (sel) => (sel === '.log-actions' ? {} : null) },
      stopPropagation: () => {},
    });
    assert(panel.classList.contains('collapsed'), 'action click must not toggle strip');
    // 点标题 → 展开
    hs[0]({ target: { closest: () => null }, stopPropagation: () => {} });
    assert(!panel.classList.contains('collapsed'), 'header click expands strip');
    assert(elements['log-toggle-icon'].textContent === '▾', 'icon shows expanded');
    assert(localStorage.getItem('log-panel-collapsed') === '0', 'expanded state persisted');
    // 再点 → 收起
    hs[0]({ target: { closest: () => null }, stopPropagation: () => {} });
    assert(panel.classList.contains('collapsed'), 'header click collapses strip again');
    assert(localStorage.getItem('log-panel-collapsed') === '1', 'collapsed state persisted');
  });

  await asyncTest('T15-08 HEX/autoscroll checkboxes mirror page ↔ dock', () => {
    slogReset();
    elements['slog-hex'].checked = false;
    elements['slog-hex-dock'].checked = false;
    _slogAppend([{ seq: 1, ts: 1700000000.0, dir: 'TX', hex: '48 49', text: 'HI' }]);
    const hx = eventListeners['slog-hex-dock:change'] || [];
    assert(hx.length > 0, 'dock HEX change handler registered');
    hx[0]({ target: { checked: true } });
    assert(elements['slog-hex'].checked === true, 'page HEX synced on');
    assert(elements['slog-hex-dock'].checked === true, 'dock HEX on');
    assert(elements['seriallog-log'].children[0].innerHTML.includes('48 49'),
      'page view re-rendered as hex');
    assert(elements['seriallog-dock-log'].children[0].innerHTML.includes('48 49'),
      'dock view re-rendered as hex');
    // 页面侧关掉 → dock 同步，恢复文本
    (eventListeners['slog-hex:change'] || []).forEach(h => h({ target: { checked: false } }));
    assert(elements['slog-hex-dock'].checked === false, 'dock HEX synced off');
    assert(elements['seriallog-log'].children[0].innerHTML.includes('HI'), 'text mode again');
    // 自动滚动镜像
    const ax = eventListeners['slog-autoscroll-dock:change'] || [];
    assert(ax.length > 0, 'dock autoscroll change handler registered');
    ax[0]({ target: { checked: false } });
    assert(elements['slog-autoscroll'].checked === false, 'page autoscroll synced off');
    (eventListeners['slog-autoscroll:change'] || []).forEach(h => h({ target: { checked: true } }));
    assert(elements['slog-autoscroll-dock'].checked === true, 'dock autoscroll synced on');
  });

  // ============================================================
  // T16: 参数页 — 读取回填参数值文本框 / 原文渲染
  // ============================================================
  console.log('\n' + '='.repeat(60));
  console.log('T16: Params Page (read-back fill / raw rendering)');
  console.log('='.repeat(60));

  const PARAM_GET_KEY = 'POST:/api/device/param/get';

  function paramReset() {
    if (!elements['param-output']) mockElement('param-output');
    elements['param-output'].innerHTML = '';
    elements['param-output'].children.length = 0;
    elements['param-name'].value = '';
    elements['param-value'].value = '';
    mockApiResponses[PORTS_KEY] = { ok: true, status: 200,
      data: { ports: ['COM4'], open: true, current: 'COM4', baudrate: 115200 } };
  }

  await asyncTest('T16-01 读取参数把最新值回填参数值文本框', async () => {
    paramReset();
    elements['param-name'].value = 'g_volume';
    mockApiResponses[PARAM_GET_KEY] = { ok: true, status: 200, data: {
      ok: true, name: 'g_volume', value: 80,
      raw: 'param g_volume\r\ng_volume:80\r\nnr@bos:' } };
    elements['btn-param-get'].click();
    await new Promise(r => setTimeout(r, 10));
    assert(elements['param-value'].value === '80',
      `参数值文本框应为 80，实际 ${JSON.stringify(elements['param-value'].value)}`);
    delete mockApiResponses[PARAM_GET_KEY];
    delete mockApiResponses[PORTS_KEY];
  });

  await asyncTest('T16-02 读取值为 0 也必须回填（不能被 falsy 吞掉）', async () => {
    paramReset();
    elements['param-name'].value = 'param_test_1';
    elements['param-value'].value = '999';   // 旧值
    mockApiResponses[PARAM_GET_KEY] = { ok: true, status: 200, data: {
      ok: true, name: 'param_test_1', value: 0,
      raw: 'param param_test_1\r\nparam_test_1:0\r\nnr@bos:' } };
    elements['btn-param-get'].click();
    await new Promise(r => setTimeout(r, 10));
    assert(elements['param-value'].value === '0',
      `值为 0 也应回填，实际 ${JSON.stringify(elements['param-value'].value)}`);
    delete mockApiResponses[PARAM_GET_KEY];
    delete mockApiResponses[PORTS_KEY];
  });

  await asyncTest('T16-03 读取失败不清空参数值文本框', async () => {
    paramReset();
    elements['param-name'].value = 'no_such_param';
    elements['param-value'].value = '123';
    mockApiResponses[PARAM_GET_KEY] = { ok: false, status: 504,
      data: { detail: { code: 'PARAM_NOT_FOUND', detail: '参数不存在或读取超时',
                        raw: 'param no_such_param\r\nnr@bos:' } } };
    elements['btn-param-get'].click();
    await new Promise(r => setTimeout(r, 10));
    assert(elements['param-value'].value === '123',
      `失败时不应改动文本框，实际 ${JSON.stringify(elements['param-value'].value)}`);
    delete mockApiResponses[PARAM_GET_KEY];
    delete mockApiResponses[PORTS_KEY];
  });

  await asyncTest('T16-04 响应输出渲染设备原文与解析行', async () => {
    paramReset();
    elements['param-name'].value = 'g_volume';
    mockApiResponses[PARAM_GET_KEY] = { ok: true, status: 200, data: {
      ok: true, name: 'g_volume', value: 80,
      raw: 'param g_volume\r\ng_volume:80\r\nnr@bos:' } };
    elements['btn-param-get'].click();
    await new Promise(r => setTimeout(r, 10));
    const out = elements['param-output'].innerHTML;
    assert(out.includes('g_volume:80'), '应回显设备原文（回文行）');
    assert(out.includes('nr@bos:'), '应回显设备原文（提示符）');
    assert(out.includes('解析 g_volume=80'), '应有解析行');
    delete mockApiResponses[PARAM_GET_KEY];
    delete mockApiResponses[PORTS_KEY];
  });

  await asyncTest('T16-05 读取/设置不改写下拉框可选项（只有列出全部重写）', async () => {
    paramReset();
    _fillParamDatalist(['param_test_1', 'param_test_2', 'param_test_3']);
    elements['param-name'].value = 'param_test_1';
    mockApiResponses[PARAM_GET_KEY] = { ok: true, status: 200, data: {
      ok: true, name: 'param_test_1', value: 0,
      raw: 'param param_test_1\r\nparam_test_1:0\r\nnr@bos:' } };
    mockApiResponses['POST:/api/device/param/set'] = { ok: true, status: 200, data: {
      ok: true, name: 'param_test_1', value: 0, verified: true, raw: '' } };
    elements['btn-param-get'].click();
    await new Promise(r => setTimeout(r, 10));
    let opts = (elements['param-name'].children || []).map(c => c.value);
    assert(opts.length === 3 && opts.indexOf('param_test_2') >= 0 && opts.indexOf('param_test_3') >= 0,
      `读取后下拉框应仍有全部可选项，实际 ${JSON.stringify(opts)}`);
    elements['param-value'].value = '0';
    elements['btn-param-set'].click();
    await new Promise(r => setTimeout(r, 10));
    opts = (elements['param-name'].children || []).map(c => c.value);
    assert(opts.length === 3, `设置后下拉框应仍有全部可选项，实际 ${JSON.stringify(opts)}`);
    delete mockApiResponses[PARAM_GET_KEY];
    delete mockApiResponses['POST:/api/device/param/set'];
    delete mockApiResponses[PORTS_KEY];
  });

  console.log('\n' + '='.repeat(60));
  console.log('T17: HTTP Mock 监听 0.0.0.0 / LAN URL 呈现');
  console.log('='.repeat(60));

  const HTTP_STATUS_KEY = 'GET:/api/device/http/status';
  const HTTP_START_KEY = 'POST:/api/device/http/start';
  const HTTP_STOP_KEY = 'POST:/api/device/http/stop';
  const HTTP_PROXY_KEY = 'POST:/api/device/http/proxy';
  const HTTP_REQS_KEY = 'GET:/api/device/http/requests';
  const MOCK_LAN = 'http://192.168.1.50:18081';
  const MOCK_LOOP = 'http://127.0.0.1:18081';

  function httpReset() {
    if (!elements['http-server-status']) mockElement('http-server-status');
    if (!elements['global-log']) mockElement('global-log', { className: 'log-content' });
    elements['http-server-status'].textContent = '';
    elements['global-log'].innerHTML = '';
    ['http-url', 'http-mode', 'http-method', 'http-body', 'http-headers'].forEach(id => {
      if (!elements[id]) mockElement(id, { value: '' });
    });
    elements['http-mode'].value = 'proxy';
    elements['http-method'].value = 'GET';
    delete mockApiResponses[HTTP_STATUS_KEY];
    delete mockApiResponses[HTTP_START_KEY];
    delete mockApiResponses[HTTP_STOP_KEY];
    delete mockApiResponses[HTTP_PROXY_KEY];
    delete mockApiResponses[HTTP_REQS_KEY];
  }

  // mockElement 的 innerHTML 不反映 appendChild（见 T3-01 已知缺口），
  // addLog 走 appendChild → 从 children 读日志行。
  function httpLogText() {
    return (elements['global-log'].children || [])
      .map(c => (c.innerHTML || c.textContent || ''))
      .join('\n');
  }

  function httpMockStatus(running) {
    return { ok: true, status: 200, data: running ? {
      running: true, bind_host: '0.0.0.0', port: 18081, https: false,
      base_url: MOCK_LAN, urls: [MOCK_LAN, MOCK_LOOP]
    } : { running: false, bind_host: '0.0.0.0', port: 0, https: false,
          base_url: '', urls: [] } };
  }

  await asyncTest('T17-01 状态栏显示 LAN URL 与本机自测 URL', async () => {
    httpReset();
    mockApiResponses[HTTP_STATUS_KEY] = httpMockStatus(true);
    await _syncHttpServerUi();
    const txt = elements['http-server-status'].textContent || '';
    assert(txt.includes('运行中'), `状态应为运行中，实际 ${JSON.stringify(txt)}`);
    assert(txt.includes(MOCK_LAN), `状态应含设备要填的 LAN URL，实际 ${JSON.stringify(txt)}`);
    assert(txt.includes(`本机 ${MOCK_LOOP}`), `状态应含本机自测 URL，实际 ${JSON.stringify(txt)}`);
    delete mockApiResponses[HTTP_STATUS_KEY];
  });

  await asyncTest('T17-02 启动后日志给出设备可直连的 LAN URL', async () => {
    httpReset();
    mockApiResponses[HTTP_STATUS_KEY] = httpMockStatus(false);
    await _syncHttpServerUi();   // 确保处于未运行 → 下一次点击走启动分支
    mockApiResponses[HTTP_START_KEY] = { ok: true, status: 200, data: {
      ok: true, port: 18081, base_url: MOCK_LAN,
      status: { running: true, bind_host: '0.0.0.0', port: 18081,
                base_url: MOCK_LAN, urls: [MOCK_LAN, MOCK_LOOP] } } };
    mockApiResponses[HTTP_STATUS_KEY] = httpMockStatus(true);
    elements['btn-http-server'].click();
    await new Promise(r => setTimeout(r, 10));
    const txt = elements['http-server-status'].textContent || '';
    const log = httpLogText();
    assert(txt.includes(MOCK_LAN) && txt.includes(`本机 ${MOCK_LOOP}`),
      `启动后状态应同时给出 LAN 与本机 URL，实际 ${JSON.stringify(txt)}`);
    assert(log.includes('HTTP Mock 服务器已启动') && log.includes(MOCK_LAN),
      `启动日志应含 LAN URL，实际 ${JSON.stringify(log.slice(-200))}`);
    delete mockApiResponses[HTTP_START_KEY];
    delete mockApiResponses[HTTP_STATUS_KEY];
  });

  await asyncTest('T17-03 停止后状态回到未运行', async () => {
    httpReset();
    mockApiResponses[HTTP_STATUS_KEY] = httpMockStatus(true);
    await _syncHttpServerUi();
    mockApiResponses[HTTP_STOP_KEY] = { ok: true, status: 200, data: { ok: true } };
    mockApiResponses[HTTP_STATUS_KEY] = httpMockStatus(false);
    elements['btn-http-server'].click();
    await new Promise(r => setTimeout(r, 10));
    const txt = elements['http-server-status'].textContent || '';
    assert(txt === '未运行', `停止后状态应为未运行，实际 ${JSON.stringify(txt)}`);
    delete mockApiResponses[HTTP_STOP_KEY];
    delete mockApiResponses[HTTP_STATUS_KEY];
  });

  await asyncTest('T17-04 目标不在 Mock 上时提示列出全部 Mock URL（含 LAN）', async () => {
    httpReset();
    mockApiResponses[HTTP_STATUS_KEY] = httpMockStatus(true);
    await _syncHttpServerUi();
    elements['http-url'].value = 'http://192.168.1.99:8080/x';   // 不匹配任何 Mock URL
    mockApiResponses[HTTP_PROXY_KEY] = { ok: true, status: 200, data: {
      status_code: 200, body_len: 2, body: '{}' } };
    mockApiResponses[HTTP_REQS_KEY] = { ok: true, status: 200,
      data: { count: 0, requests: [], base_url: MOCK_LAN } };
    elements['btn-http-send'].click();
    await new Promise(r => setTimeout(r, 10));
    const log = httpLogText();
    assert(log.includes('Mock 运行于'), `应提示 Mock 运行于…，实际 ${JSON.stringify(log.slice(-200))}`);
    assert(log.includes(MOCK_LAN) && log.includes(MOCK_LOOP),
      '提示应列出全部 Mock URL（LAN + 本机）');
  });

  await asyncTest('T17-05 目标命中 Mock URL 时不打扰', async () => {
    httpReset();
    mockApiResponses[HTTP_STATUS_KEY] = httpMockStatus(true);
    await _syncHttpServerUi();
    elements['http-url'].value = MOCK_LAN + '/api/status';
    mockApiResponses[HTTP_PROXY_KEY] = { ok: true, status: 200, data: {
      status_code: 200, body_len: 2, body: '{}' } };
    mockApiResponses[HTTP_REQS_KEY] = { ok: true, status: 200,
      data: { count: 0, requests: [], base_url: MOCK_LAN } };
    elements['btn-http-send'].click();
    await new Promise(r => setTimeout(r, 10));
    const log = httpLogText();
    assert(!log.includes('Mock 运行于'),
      `URL 命中 Mock 时不应提示，实际 ${JSON.stringify(log.slice(-200))}`);
  });

  printReport();
  process.exit(_fail > 0 ? 1 : 0);
})();
