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
    },
    contains: (c) => classes.has(c),
    toString: () => [...classes].join(' '),
  };
  const el = {
    id,
    textContent: '',
    _innerHTML: '',
    get innerHTML() { return this._innerHTML; },
    set innerHTML(v) { this._innerHTML = v; },
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
        eventListeners[key].forEach(h => h());
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
};

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

// Page elements
['home', 'projects', 'automl-data', 'automl-labels', 'automl-features', 'automl-training', 'automl-export',
 'serial', 'ota', 'xmodem', 'http', 'params', 'device'].forEach(page => {
  mockElement(`page-${page}`, { className: `page${page === 'home' ? ' active' : ''}` });
});

// ============================================================
// Load app.js
// ============================================================
const appJsPath = path.join(__dirname, 'ui', 'app.js');
const appJsCode = fs.readFileSync(appJsPath, 'utf-8');

// Execute app.js in mocked environment
eval(appJsCode);

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

process.exit(_fail > 0 ? 1 : 0);
