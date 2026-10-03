/**
 * BabyOS Studio — 纯桌面应用逻辑
 * 无服务器，直接在 Electron 中运行
 */

// ---------------------------------------------------------------------------
// 辅助函数
// ---------------------------------------------------------------------------
function hexToRgba(hex, alpha) {
  const h = hex.replace('#', '');
  const r = parseInt(h.substring(0, 2), 16);
  const g = parseInt(h.substring(2, 4), 16);
  const b = parseInt(h.substring(4, 6), 16);
  return `rgba(${r},${g},${b},${alpha})`;
}

// ---------------------------------------------------------------------------
// 波形图渲染引擎（Canvas）
// ---------------------------------------------------------------------------
const Waveform = {
  canvas: null, ctx: null,
  data: null, columns: [], segments: [], labels: [],
  // 视口：当前显示的数据范围 [startRow, endRow)
  viewStart: 0, viewEnd: 500, totalRows: 0,
  // 交互状态
  dragging: false, dragStartX: 0, dragViewStart: 0,
  selectStart: -1, selectEnd: -1,
  selectionMode: false, // false = pan mode, true = selection mode
  // 颜色
  CHAN_COLORS: ['#409eff','#67c23a','#e6a23c','#f56c6c','#909399','#b37feb','#36cfc9','#ff85c0'],
  BG_COLORS: ['#409eff22','#67c23a22','#e6a23c22','#f56c6c22','#9b59b622','#1abc9c22','#e74c3c22','#f39c1222'],

  init() {
    this.canvas = document.getElementById('waveform-canvas');
    if (!this.canvas) return;
    this.ctx = this.canvas.getContext('2d');
    // Always re-query DPR and canvas size (handles multi-monitor changes)
    const dpr = window.devicePixelRatio || 1;
    const rect = this.canvas.getBoundingClientRect();
    // 如果 canvas 尚未布局（宽高为0），跳过尺寸设置，保留上次的有效值
    if (rect.width > 0 && rect.height > 0) {
      this.canvas.width = rect.width * dpr;
      this.canvas.height = rect.height * dpr;
      this.ctx.scale(dpr, dpr);
      this.W = rect.width;
      this.H = rect.height;
    }
    // 设置初始光标（与 selectionMode 一致）
    this.canvas.style.cursor = this.selectionMode ? 'crosshair' : 'grab';
    // 事件 — only bind once to prevent duplicate handlers
    if (this._initialized) return;
    this._initialized = true;
    this.canvas.addEventListener('wheel', e => this.onWheel(e), { passive: false });
    this.canvas.addEventListener('mousedown', e => this.onMouseDown(e));
    this.canvas.addEventListener('mousemove', e => this.onMouseMove(e));
    this.canvas.addEventListener('mouseup', () => this.onMouseUp());
    this.canvas.addEventListener('mouseleave', () => this.onMouseUp());
    this.canvas.addEventListener('dblclick', e => this.onDblClick(e));
    window.addEventListener('resize', () => {
      const r2 = this.canvas.getBoundingClientRect();
      if (r2.width < 10 || r2.height < 10) return;
      const dpr2 = window.devicePixelRatio || 1;
      this.canvas.width = r2.width * dpr2;
      this.canvas.height = r2.height * dpr2;
      this.ctx.scale(dpr2, dpr2);
      this.W = r2.width; this.H = r2.height;
      this.draw();
    });
  },

  setData(data, columns, segments, labels) {
    // Validate incoming data structure (fix: PM page-automl-labels)
    if (!Array.isArray(data) || data.length === 0) {
      this.data = [];
      this.columns = [];
      this.segments = segments || [];
      this.labels = labels || [];
      this.totalRows = 0;
      this.draw();
      return;
    }
    this.data = data;
    this.columns = columns || [];
    this.segments = segments || [];
    this.labels = labels || [];
    this.totalRows = data.length;
    if (this.viewEnd > this.totalRows) this.viewEnd = this.totalRows;
    this.draw();
    this.updateInfo();
  },

  updateInfo() {
    const el = document.getElementById('chart-info');
    if (el) el.textContent = `${this.totalRows}行 · 显示 ${this.viewStart}-${this.viewEnd} · ${this.columns.length}通道`;
  },

  // ---------- 坐标转换 ----------
  rowToX(row) {
    const pad = 60; // 左侧留空给Y轴标签
    return pad + (row - this.viewStart) / (this.viewEnd - this.viewStart) * (this.W - pad - 10);
  },
  xToRow(x) {
    const pad = 60;
    return Math.round(this.viewStart + (x - pad) / (this.W - pad - 10) * (this.viewEnd - this.viewStart));
  },

  // ---------- Y 范围缓存 ----------
  _yRangeCache: {},
  _yRangeCacheKey: '',

  _computeYRange(ch) {
    let min = Infinity, max = -Infinity;
    for (let i = this.viewStart; i < Math.min(this.viewEnd, this.totalRows); i++) {
      const v = this.data[i][ch];
      if (v < min) min = v;
      if (v > max) max = v;
    }
    if (max === min) { max = min + 1; }
    return { min, max };
  },

  _getCachedYRange(ch) {
    const cacheKey = `${this.viewStart}-${this.viewEnd}`;
    if (this._yRangeCacheKey !== cacheKey) {
      this._yRangeCache = {};
      this._yRangeCacheKey = cacheKey;
    }
    if (!(ch in this._yRangeCache)) {
      this._yRangeCache[ch] = this._computeYRange(ch);
    }
    return this._yRangeCache[ch];
  },

  chanToY(ch, val) {
    const n = this.columns.length;
    if (n === 0) return 0;
    const bandH = (this.H - 30) / n;
    const top = ch * bandH + 15;
    const { min, max } = this._getCachedYRange(ch);
    return top + bandH - 10 - (val - min) / (max - min) * (bandH - 20);
  },

  // ---------- 绘制 ----------
  draw() {
    if (!this.ctx || !this.data) return;
    const ctx = this.ctx;
    const W = this.W, H = this.H;
    const n = this.columns.length;

    // Show empty state placeholder when no data (must be before n===0 return)
    if (n === 0 || !this.data) {
      ctx.clearRect(0, 0, W, H);
      ctx.fillStyle = '#1a1a2e';
      ctx.fillRect(0, 0, W, H);
      ctx.fillStyle = '#666';
      ctx.font = '16px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText('请先导入数据文件', W / 2, H / 2);
      ctx.textAlign = 'left';
      return;
    }

    // Reset Y range cache for this draw pass
    this._yRangeCache = {};
    this._yRangeCacheKey = '';

    ctx.clearRect(0, 0, W, H);
    ctx.fillStyle = '#1a1a2e';
    ctx.fillRect(0, 0, W, H);

    const bandH = (H - 30) / n;
    const viewLen = this.viewEnd - this.viewStart;

    // 1. 画分段背景色
    for (const seg of this.segments) {
      if (seg.end <= this.viewStart || seg.start >= this.viewEnd) continue;
      const lbl = this.labels.find(l => l.label_id === seg.label_id);
      const color = lbl ? lbl.color : '#666';
      const x1 = Math.max(0, this.rowToX(seg.start));
      const x2 = Math.min(W, this.rowToX(seg.end));
      // 背景色：0.25 透明度在深色背景上清晰可见
      ctx.fillStyle = hexToRgba(color, 0.25);
      ctx.fillRect(x1, 0, x2 - x1, H);
      // 分段边界线
      ctx.strokeStyle = hexToRgba(color, 0.6);
      ctx.lineWidth = 1.5;
      ctx.setLineDash([4, 4]);
      if (seg.start >= this.viewStart) { ctx.beginPath(); ctx.moveTo(x1, 0); ctx.lineTo(x1, H); ctx.stroke(); }
      if (seg.end <= this.viewEnd) { ctx.beginPath(); ctx.moveTo(x2, 0); ctx.lineTo(x2, H); ctx.stroke(); }
      ctx.setLineDash([]);
      // 标注文字
      if (lbl && (x2 - x1) > 30) {
        ctx.fillStyle = color;
        ctx.font = 'bold 12px sans-serif';
        ctx.fillText(lbl.name, x1 + 6, 16);
      }
    }

    // 2. 选区高亮
    if (this.selectStart >= 0 && this.selectEnd >= 0) {
      const sx1 = this.rowToX(Math.min(this.selectStart, this.selectEnd));
      const sx2 = this.rowToX(Math.max(this.selectStart, this.selectEnd));
      ctx.fillStyle = 'rgba(64,158,255,0.15)';
      ctx.fillRect(sx1, 0, sx2 - sx1, H);
      ctx.strokeStyle = '#409eff';
      ctx.lineWidth = 2;
      ctx.strokeRect(sx1, 0, sx2 - sx1, H);
    }
    // First-click pending cursor line (orange dashed)
    if (this.selectStart >= 0 && this.selectEnd === this.selectStart) {
      const sx = this.rowToX(this.selectStart);
      ctx.strokeStyle = '#e6a23c';
      ctx.lineWidth = 2;
      ctx.setLineDash([6, 3]);
      ctx.beginPath();
      ctx.moveTo(sx, 0);
      ctx.lineTo(sx, H);
      ctx.stroke();
      ctx.setLineDash([]);
    }

    // 3. 画各通道波形
    for (let ch = 0; ch < n; ch++) {
      const top = ch * bandH + 15;
      // 通道分隔线
      if (ch > 0) {
        ctx.strokeStyle = '#333';
        ctx.lineWidth = 1;
        ctx.beginPath(); ctx.moveTo(60, top - 5); ctx.lineTo(W, top - 5); ctx.stroke();
      }
      // 通道名
      ctx.fillStyle = this.CHAN_COLORS[ch % this.CHAN_COLORS.length];
      ctx.font = 'bold 11px sans-serif';
      ctx.fillText(this.columns[ch], 4, top + bandH / 2);

      // 自适应 Y 范围 (cached)
      const { min, max } = this._getCachedYRange(ch);
      // Y 轴刻度
      ctx.fillStyle = '#666';
      ctx.font = '10px monospace';
      ctx.fillText(max.toFixed(2), 2, top + 12);
      ctx.fillText(min.toFixed(2), 2, top + bandH - 6);

      // Draw waveform as connected polyline (handles sparse data points too)
      const usableW = this.W - 70; // left padding for Y-axis labels
      const color = this.CHAN_COLORS[ch % this.CHAN_COLORS.length];
      ctx.strokeStyle = color;
      ctx.lineWidth = 2.5;
      ctx.shadowColor = color;
      ctx.shadowBlur = 3;
      const range = max - min;
      const scale = range !== 0 ? (bandH - 20) / range : 0;
      const yBase = top + bandH - 10;
      const vStart = this.viewStart;
      const vEnd = Math.min(this.viewEnd, this.totalRows);

      // For large datasets, use min-max downsampling per pixel column
      // For small datasets (rows <= usableW), draw each data point directly
      ctx.beginPath();
      let started = false;
      if (viewLen <= usableW) {
        // Sparse: map each row to its own X position
        for (let r = vStart; r < vEnd; r++) {
          const x = this.rowToX(r);
          const y = yBase - (this.data[r][ch] - min) * scale;
          if (!started) { ctx.moveTo(x, y); started = true; }
          else ctx.lineTo(x, y);
        }
      } else {
        // Dense: min-max downsample per pixel column
        const colCount = Math.max(1, usableW);
        const step = Math.max(1, Math.floor(viewLen / usableW));
        let prevY = null;
        for (let col = 0; col < colCount; col++) {
          const rowStart = Math.floor(vStart + col * viewLen / colCount);
          const rowEnd = Math.floor(vStart + (col + 1) * viewLen / colCount);
          const rEnd = Math.min(rowEnd, vEnd);
          if (rowStart >= rEnd) continue;
          let pMin = this.data[rowStart][ch];
          let pMax = pMin;
          for (let r = rowStart + 1; r < rEnd; r += step) {
            const v = this.data[r][ch];
            if (v < pMin) pMin = v;
            if (v > pMax) pMax = v;
          }
          const x = 60 + col;
          const yMid = yBase - ((pMin + pMax) / 2 - min) * scale;
          if (!started) { ctx.moveTo(x, yMid); started = true; }
          else ctx.lineTo(x, yMid);
          prevY = yMid;
        }
      }
      ctx.stroke();
      ctx.shadowBlur = 0;
    }

    // 4. X 轴刻度
    ctx.fillStyle = '#666';
    ctx.font = '10px monospace';
    const tickStep = Math.max(1, Math.floor(viewLen / 10));
    for (let r = Math.ceil(this.viewStart / tickStep) * tickStep; r <= this.viewEnd; r += tickStep) {
      const x = this.rowToX(r);
      ctx.fillText(String(r), x, H - 4);
      ctx.strokeStyle = '#333';
      ctx.lineWidth = 0.5;
      ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, H - 15); ctx.stroke();
    }
  },

  // ---------- 交互 ----------
  onWheel(e) {
    e.preventDefault();
    const rect = this.canvas.getBoundingClientRect();
    const mouseX = e.clientX - rect.left;
    const mouseRow = this.xToRow(mouseX);
    const viewLen = this.viewEnd - this.viewStart;
    const factor = e.deltaY > 0 ? 1.2 : 0.8;
    const newLen = Math.max(20, Math.min(this.totalRows, Math.round(viewLen * factor)));
    const ratio = (mouseRow - this.viewStart) / viewLen;
    this.viewStart = Math.max(0, Math.round(mouseRow - newLen * ratio));
    this.viewEnd = Math.min(this.totalRows, this.viewStart + newLen);
    this.viewStart = Math.max(0, this.viewEnd - newLen);
    this.syncInputs();
    this.draw();
    this.updateInfo();
  },

  onMouseDown(e) {
    if (e.button !== 0) return;
    if (this.selectionMode) {
      // Selection mode: start a drag-selection
      const rect = this.canvas.getBoundingClientRect();
      const x = e.clientX - rect.left;
      const row = this.xToRow(x);
      if (row < 0 || row >= this.totalRows) return;
      this.selectStart = row;
      this.selectEnd = row;
      this._selectDrag = true;
    } else {
      // Pan mode: start dragging
      this.dragging = true;
      this.dragStartX = e.clientX;
      this.dragViewStart = this.viewStart;
      this.canvas.style.cursor = 'grabbing';
    }
  },

  _rafPending: false,
  _pendingDragDx: 0,

  onMouseMove(e) {
    if (this._selectDrag) {
      // Selection drag mode
      const rect = this.canvas.getBoundingClientRect();
      const x = e.clientX - rect.left;
      const row = this.xToRow(x);
      this.selectEnd = Math.max(0, Math.min(this.totalRows - 1, row));
      this.draw();
      return;
    }
    if (!this.dragging) return;
    this._pendingDragDx = e.clientX - this.dragStartX;
    if (!this._rafPending) {
      this._rafPending = true;
      requestAnimationFrame(() => {
        this._rafPending = false;
        const dx = this._pendingDragDx;
        const rowDelta = -dx / (this.W - 70) * (this.viewEnd - this.viewStart);
        const newStart = Math.round(this.dragViewStart + rowDelta);
        const viewLen = this.viewEnd - this.viewStart;
        this.viewStart = Math.max(0, Math.min(this.totalRows - viewLen, newStart));
        this.viewEnd = this.viewStart + viewLen;
        this.syncInputs();
        this.draw();
      });
    }
  },

  onMouseUp() {
    if (this._selectDrag) {
      this._selectDrag = false;
      // Finalize selection
      if (this.selectStart >= 0 && this.selectEnd >= 0 && this.selectStart !== this.selectEnd) {
        const s = Math.min(this.selectStart, this.selectEnd);
        const end = Math.max(this.selectStart, this.selectEnd);
        const startEl = document.getElementById('seg-start');
        const endEl = document.getElementById('seg-end');
        if (startEl) startEl.value = s;
        if (endEl) endEl.value = end;
        addLog(`已选择行 ${s}~${end}，请选择标注后点击添加`, 'AutoML');
      } else if (this.selectStart >= 0 && this.selectEnd === this.selectStart) {
        // Zero-width selection, clear it
        this.selectStart = -1;
        this.selectEnd = -1;
      }
      this.draw();
      return;
    }
    this.dragging = false;
    this.canvas.style.cursor = this.selectionMode ? 'crosshair' : 'grab';
  },

  onDblClick(e) {
    // Double-click always creates a selection range, regardless of pan/selection mode
    const rect = this.canvas.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const row = this.xToRow(x);
    if (row < 0 || row >= this.totalRows) return;
    // Selection range scales with viewport width (10% of current view)
    const halfRange = Math.max(5, Math.floor((this.viewEnd - this.viewStart) * 0.1));
    // Clamp selection to viewport bounds (extend at most 5 rows beyond viewport edge)
    const margin = 5;
    let s = Math.max(0, row - halfRange);
    let end = Math.min(this.totalRows - 1, row + halfRange);
    // Clamp to viewport range with small margin
    s = Math.max(this.viewStart - margin, Math.min(s, this.viewStart));
    end = Math.max(this.viewEnd - margin, Math.min(end, this.viewEnd + margin));
    this.selectStart = s;
    this.selectEnd = end;
    const startEl = document.getElementById('seg-start');
    const endEl = document.getElementById('seg-end');
    if (startEl) startEl.value = s;
    if (endEl) endEl.value = end;
    addLog(`双击选取: 已选择行 ${s}~${end}，可拖拽调整`, 'AutoML');
    this.draw();
  },

  syncInputs() {
    const s = document.getElementById('chart-start');
    const e = document.getElementById('chart-end');
    if (s) s.value = this.viewStart;
    if (e) e.value = this.viewEnd;
  },

  // ---------- 外部调用 ----------
  setRange(start, end) {
    this.viewStart = Math.max(0, start);
    this.viewEnd = Math.min(this.totalRows, end);
    this.syncInputs();
    this.draw();
    this.updateInfo();
  },
  fitAll() {
    this.viewStart = 0;
    this.viewEnd = this.totalRows;
    this.syncInputs();
    this.draw();
    this.updateInfo();
  },
  clearSelection() {
    this.selectStart = -1;
    this.selectEnd = -1;
    this.draw();
  },
};

// ---------------------------------------------------------------------------
// 全局状态
// ---------------------------------------------------------------------------
let serialOpen = false;
let serialPortPath = '';
let currentProject = null;
let currentProjectMode = 'timeseries'; // 'timeseries' | 'table'
let _projectSamplingRate = 100;  // 项目采样率缓存，供 updateWindowInfo 等使用
let pythonRunning = false;

// Update serial status on home page whenever serialOpen changes
function updateSerialStatusDisplay() {
  const el = document.getElementById('serial-status');
  if (el) {
    if (serialOpen) {
      el.textContent = `已连接: ${serialPortPath}`;
      el.className = 'status-text running';
    } else {
      el.textContent = '未连接';
      el.className = 'status-text';
    }
  }
}

// ---------------------------------------------------------------------------
// 调试工具
// ---------------------------------------------------------------------------
function debug(msg) {
  console.log(`[DEBUG] ${msg}`);
}

// ---------------------------------------------------------------------------
// Toast 通知
// ---------------------------------------------------------------------------
function showToast(message, color = '#409eff') {
  const toast = document.createElement('div');
  toast.style.cssText = `position:fixed;top:20px;right:20px;z-index:10001;padding:12px 20px;border-radius:6px;color:#fff;font-size:14px;box-shadow:0 4px 12px rgba(0,0,0,0.15);background:${color};transition:opacity 0.3s;`;
  toast.textContent = message;
  document.body.appendChild(toast);
  setTimeout(() => { toast.style.opacity = '0'; setTimeout(() => toast.remove(), 300); }, 3000);
}

// ---------------------------------------------------------------------------
// 自定义确认对话框（替代原生 confirm）
// ---------------------------------------------------------------------------
function showConfirm(message) {
  return new Promise((resolve) => {
    const overlay = document.createElement('div');
    overlay.style.cssText = 'position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,0.4);z-index:10000;display:flex;align-items:center;justify-content:center;';
    overlay.innerHTML = `
      <div style="background:#fff;border-radius:8px;padding:24px;min-width:360px;max-width:480px;box-shadow:0 8px 32px rgba(0,0,0,0.2);">
        <div style="font-size:15px;color:#303133;margin-bottom:20px;line-height:1.5;">${escapeHtml(message)}</div>
        <div style="display:flex;justify-content:flex-end;gap:8px;">
          <button class="btn" id="confirm-cancel-btn">取消</button>
          <button class="btn btn-danger" id="confirm-ok-btn">确认</button>
        </div>
      </div>`;
    document.body.appendChild(overlay);
    const okBtn = document.getElementById('confirm-ok-btn');
    const cancelBtn = document.getElementById('confirm-cancel-btn');
    const cleanup = (result) => {
      document.removeEventListener('keydown', onKeydown);
      overlay.remove();
      resolve(result);
    };
    const onKeydown = (e) => { if (e.key === 'Escape') cleanup(false); };
    document.addEventListener('keydown', onKeydown);
    okBtn.addEventListener('click', () => cleanup(true));
    cancelBtn.addEventListener('click', () => cleanup(false));
    overlay.addEventListener('click', (e) => { if (e.target === overlay) cleanup(false); });
    cancelBtn.focus();
  });
}

// ---------------------------------------------------------------------------
// 折叠面板
// ---------------------------------------------------------------------------
function toggleCollapse(titleEl) {
  const body = titleEl.nextElementSibling;
  if (!body) return;
  titleEl.classList.toggle('open');
  body.classList.toggle('open');
  // 持久化折叠状态
  const key = 'collapse-' + (titleEl.id || titleEl.textContent.trim().substring(0, 30));
  try { localStorage.setItem(key, titleEl.classList.contains('open') ? '1' : '0'); } catch (e) {}
}

function restoreCollapseStates() {
  document.querySelectorAll('.collapse-toggle').forEach(titleEl => {
    const key = 'collapse-' + (titleEl.id || titleEl.textContent.trim().substring(0, 30));
    try {
      const saved = localStorage.getItem(key);
      const body = titleEl.nextElementSibling;
      if (saved === '1') {
        titleEl.classList.add('open');
        if (body) body.classList.add('open');
      } else if (saved === '0') {
        // Remove "open" class for sections that default to open in HTML
        titleEl.classList.remove('open');
        if (body) body.classList.remove('open');
      }
      // If saved is null (never visited), keep the HTML default
    } catch (e) {}
  });
}

// ---------------------------------------------------------------------------
// 波形图控制函数
// ---------------------------------------------------------------------------
async function loadChartFileList(pid) {
  try {
    const ds = await apiGet(`/api/projects/${pid}/dataset`);
    const sel = document.getElementById('chart-file-select');
    if (!sel) return;
    sel.innerHTML = '<option value="">-- 选择文件 --</option>';
    (ds.files || []).forEach(f => {
      const opt = document.createElement('option');
      opt.value = f.file_id;
      opt.textContent = `${f.filename} (${f.rows}行)`;
      opt.dataset.rows = f.rows;
      sel.appendChild(opt);
    });
  } catch (err) { addLog('加载文件列表失败: ' + err.message, 'AutoML'); }
}

async function onChartFileChange() {
  const sel = document.getElementById('chart-file-select');
  if (!sel || !sel.value || !currentProject) return;
  const fid = sel.value;
  const opt = sel.options[sel.selectedIndex];
  const totalRows = parseInt(opt.dataset.rows) || 500;

  // Clear previous selection
  Waveform.clearSelection();
  const segStartEl = document.getElementById('seg-start');
  const segEndEl = document.getElementById('seg-end');
  if (segStartEl) segStartEl.value = 0;
  if (segEndEl) { segEndEl.value = totalRows; segEndEl.min = 1; }

  // Sync seg-file-select to match
  const segSel = document.getElementById('seg-file-select');
  if (segSel && segSel.value !== fid) {
    segSel.value = fid;
    // Update max attributes directly
    if (segStartEl) segStartEl.max = totalRows;
    if (segEndEl) segEndEl.max = totalRows;
    const hint = document.getElementById('seg-range-hint');
    if (hint && totalRows) hint.textContent = `(共 ${totalRows} 行)`;
    // Dispatch change event so any listeners update
    segSel.dispatchEvent(new Event('change'));
  }
  try {
    // 加载全部数据（用于绘图，大数据自动降采样）
    // Draw loading message on canvas before API call
    Waveform.init();
    if (Waveform.ctx && Waveform.W > 0 && Waveform.H > 0) {
      Waveform.ctx.clearRect(0, 0, Waveform.W, Waveform.H);
      Waveform.ctx.fillStyle = '#1a1a2e';
      Waveform.ctx.fillRect(0, 0, Waveform.W, Waveform.H);
      Waveform.ctx.fillStyle = '#409eff';
      Waveform.ctx.font = '16px sans-serif';
      Waveform.ctx.textAlign = 'center';
      Waveform.ctx.fillText('正在加载数据...', Waveform.W / 2, Waveform.H / 2);
      Waveform.ctx.textAlign = 'left';
    }
    const limit = Math.min(totalRows, 50000); // Canvas 性能上限
    const data = await apiGet(`/api/projects/${currentProject}/dataset/file/${fid}/data?limit=${limit}`);
    const segs = await apiGet(`/api/projects/${currentProject}/segments`);
    const labels = await apiGet(`/api/projects/${currentProject}/labels`);
    Waveform.init();
    Waveform.setData(data.data, data.columns, segs, labels);
    // 默认显示前500行
    const viewEnd = Math.min(500, totalRows);
    document.getElementById('chart-end').value = viewEnd;
    document.getElementById('chart-start').value = 0;
    // Ensure segment inputs are reset for the new file
    if (segStartEl) { segStartEl.value = 0; segStartEl.max = totalRows; }
    if (segEndEl) { segEndEl.value = totalRows; segEndEl.max = totalRows; }
    Waveform.setRange(0, viewEnd);
  } catch (err) {
    addLog(`加载波形数据失败: ${err.message}`, 'AutoML');
    // Draw error message on canvas (fix: PM page-automl-labels)
    Waveform.init();
    if (Waveform.ctx && Waveform.W > 0 && Waveform.H > 0) {
      Waveform.ctx.clearRect(0, 0, Waveform.W, Waveform.H);
      Waveform.ctx.fillStyle = '#1a1a2e';
      Waveform.ctx.fillRect(0, 0, Waveform.W, Waveform.H);
      Waveform.ctx.fillStyle = '#f56c6c';
      Waveform.ctx.font = '16px sans-serif';
      Waveform.ctx.textAlign = 'center';
      Waveform.ctx.fillText('数据加载失败: ' + err.message, Waveform.W / 2, Waveform.H / 2);
      Waveform.ctx.textAlign = 'left';
    }
  }
}

function onChartRangeChange() {
  if (!Waveform.data) return;
  let s = parseInt(document.getElementById('chart-start')?.value) || 0;
  let e = parseInt(document.getElementById('chart-end')?.value) || 500;
  s = Math.max(0, s);
  e = Math.min(Waveform.totalRows, e);
  if (s >= e) e = s + 1;
  document.getElementById('chart-start').value = s;
  document.getElementById('chart-end').value = e;
  Waveform.setRange(s, e);
}

function chartZoomIn() {
  const len = Waveform.viewEnd - Waveform.viewStart;
  const mid = (Waveform.viewStart + Waveform.viewEnd) / 2;
  const newLen = Math.max(20, Math.round(len * 0.5));
  Waveform.setRange(Math.max(0, Math.round(mid - newLen / 2)), Math.min(Waveform.totalRows, Math.round(mid + newLen / 2)));
}

function chartZoomOut() {
  const len = Waveform.viewEnd - Waveform.viewStart;
  const mid = (Waveform.viewStart + Waveform.viewEnd) / 2;
  const newLen = Math.min(Waveform.totalRows, Math.round(len * 2));
  Waveform.setRange(Math.max(0, Math.round(mid - newLen / 2)), Math.min(Waveform.totalRows, Math.round(mid + newLen / 2)));
}

function chartFitAll() { Waveform.fitAll(); }
function clearChartSelection() { Waveform.clearSelection(); }

function toggleChartMode() {
  Waveform.selectionMode = !Waveform.selectionMode;
  const btn = document.getElementById('chart-mode-btn');
  if (btn) {
    btn.textContent = Waveform.selectionMode ? '选择' : '平移';
    btn.classList.toggle('btn-primary', Waveform.selectionMode);
  }
  // Update cursor to reflect mode
  const canvas = document.getElementById('waveform-canvas');
  if (canvas) {
    canvas.style.cursor = Waveform.selectionMode ? 'crosshair' : 'grab';
  }
  // Clear any active selection when switching modes
  if (!Waveform.selectionMode) {
    Waveform.clearSelection();
  }
}

// ---------------------------------------------------------------------------
// HTTP 请求工具
// ---------------------------------------------------------------------------
/** 从后端错误响应中提取可读消息。后端格式：{"detail":{"detail":"msg","code":"CODE"}} */
function _extractErrMsg(data, status) {
  if (!data) return `请求失败 (${status})`;
  // FastAPI AppError: {detail: {detail: str, code: str}}
  if (data.detail && typeof data.detail === 'object' && data.detail.detail) {
    return data.detail.detail;
  }
  // FastAPI default: {detail: str}
  if (typeof data.detail === 'string') return data.detail;
  return `请求失败 (${status})`;
}

async function apiGet(url) {
  debug(`GET ${url}`);
  const result = await window.electronAPI?.http.request(url, 'GET');
  debug(`Response: ok=${result?.ok}, status=${result?.status}`);
  if (!result?.ok) {
    throw new Error(_extractErrMsg(result?.data, result?.status));
  }
  return result.data;
}

async function apiPost(url, body) {
  debug(`POST ${url} body=${JSON.stringify(body)}`);
  const result = await window.electronAPI?.http.request(url, 'POST', body);
  debug(`Response: ok=${result?.ok}, status=${result?.status}`);
  if (!result?.ok) {
    throw new Error(_extractErrMsg(result?.data, result?.status));
  }
  return result.data;
}

async function apiPut(url, body) {
  debug(`PUT ${url} body=${JSON.stringify(body)}`);
  const result = await window.electronAPI?.http.request(url, 'PUT', body);
  debug(`Response: ok=${result?.ok}, status=${result?.status}`);
  if (!result?.ok) {
    throw new Error(_extractErrMsg(result?.data, result?.status));
  }
  return result.data;
}

async function apiPatch(url, body) {
  debug(`PATCH ${url} body=${JSON.stringify(body)}`);
  const result = await window.electronAPI?.http.request(url, 'PATCH', body);
  debug(`Response: ok=${result?.ok}, status=${result?.status}`);
  if (!result?.ok) {
    throw new Error(_extractErrMsg(result?.data, result?.status));
  }
  return result.data;
}

async function apiDelete(url) {
  debug(`DELETE ${url}`);
  const result = await window.electronAPI?.http.request(url, 'DELETE');
  debug(`Response: ok=${result?.ok}, status=${result?.status}`);
  if (!result?.ok) {
    throw new Error(_extractErrMsg(result?.data, result?.status));
  }
  return result.data;
}

async function apiUpload(url, filePath, fieldName = 'file') {
  debug(`UPLOAD ${url}, file=${filePath}`);
  const result = await window.electronAPI?.http.upload(url, filePath, fieldName);
  debug(`Upload response: ok=${result?.ok}, status=${result?.status}`);
  if (!result?.ok) {
    throw new Error(result?.data?.detail || `上传失败 (${result?.status})`);
  }
  return result.data;
}

// ---------------------------------------------------------------------------
// 页面导航
// ---------------------------------------------------------------------------
function navigateTo(page, projectId) {
  debug(`navigateTo: page=${page}, projectId=${projectId}`);

  // Clean up training refresh timer when navigating away from training page
  const prevActive = document.querySelector('.page.active');
  if (prevActive && prevActive.id === 'page-automl-training' && page !== 'automl-training') {
    // 仅在训练确实还在跑时才弹确认——后端状态不是 running 时静默离开。
    // （之前只看 timer 引用，对 cleared-but-not-nulled 的句柄会误触发。）
    const stillRunning = trainingRefreshTimer !== null && _lastTrainingStatus === 'running';
    if (stillRunning) {
      showConfirm('训练仍在进行中，离开此页面将停止进度监控。是否继续？').then(confirmed => {
        if (confirmed) {
          clearInterval(trainingRefreshTimer);
          trainingRefreshTimer = null;
          _doNavigateTo(page, projectId);
        }
      });
      return;
    }
    // 不再跑——清掉残留 timer 引用并直接走
    if (trainingRefreshTimer !== null) {
      clearInterval(trainingRefreshTimer);
      trainingRefreshTimer = null;
    }
  }

  _doNavigateTo(page, projectId);
}

function _doNavigateTo(page, projectId) {

  // 隐藏所有页面
  document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));

  // 显示目标页面
  const target = document.getElementById('page-' + page);
  if (target) {
    target.classList.add('active');
  } else {
    debug(`未找到页面: page-${page}`);
  }

  // 更新菜单高亮（AutoML 子页面映射到 projects 菜单项）
  const menuPage = page.startsWith('automl-') ? 'projects' : page;
  document.querySelectorAll('.menu-item').forEach(item => {
    item.classList.toggle('active', item.dataset.page === menuPage);
  });

  // 处理 AutoML 子页面导航
  if (projectId) {
    currentProject = projectId;
  }

  // 如果是 AutoML 子页面，加载对应数据
  if (page.startsWith('automl-') && currentProject) {
    loadProjectData(page, currentProject);
  }

  // 如果是项目列表页面，加载项目列表
  if (page === 'projects') {
    loadProjects();
  }

  // Sync serial status display on home page navigation
  if (page === 'home') {
    updateSerialStatusDisplay();
  }

  // 配网 Web 调试：进入时刷新工具状态 + 串口列表
  if (page === 'webconfig') {
    _refreshWebconfigStatus().catch(() => {});
    _refreshWcPorts().catch(() => {});
  }

  // 参数页：同步轮询状态
  if (page === 'params') {
    _syncParamPollUi().catch(() => {});
  }
}

// 绑定菜单点击
document.querySelectorAll('.menu-item[data-page]').forEach(item => {
  item.addEventListener('click', (e) => {
    e.preventDefault();
    navigateTo(item.dataset.page);
  });
});

// 绑定卡片点击
document.querySelectorAll('.card[data-page]').forEach(card => {
  card.addEventListener('click', () => {
    navigateTo(card.dataset.page);
  });
});

// Gitee 链接
document.getElementById('gitee-link')?.addEventListener('click', (e) => {
  e.preventDefault();
  window.electronAPI?.shell.openExternal('https://gitee.com/notrynohigh/BabyOS');
});
document.getElementById('gitee-card')?.addEventListener('click', () => {
  window.electronAPI?.shell.openExternal('https://gitee.com/notrynohigh/BabyOS');
});

// ---------------------------------------------------------------------------
// 日志
// ---------------------------------------------------------------------------
function addLog(text, source = 'System') {
  const logEl = document.getElementById('global-log');
  if (!logEl) return;

  const empty = logEl.querySelector('.log-empty');
  if (empty) empty.remove();

  const line = document.createElement('div');
  const time = new Date().toLocaleTimeString('zh-CN', { hour12: false });
  line.innerHTML = `<span style="color:#6a9955">[${time}]</span> <span style="color:#569cd6">[${source}]</span> ${escapeHtml(text)}`;
  logEl.appendChild(line);
  logEl.scrollTop = logEl.scrollHeight;
}

function escapeHtml(text) {
  const div = document.createElement('div');
  div.textContent = text;
  return div.innerHTML;
}

document.getElementById('btn-log-clear')?.addEventListener('click', () => {
  document.getElementById('global-log').innerHTML = '<div class="log-empty">暂无日志</div>';
});

document.getElementById('btn-log-export')?.addEventListener('click', async () => {
  const text = document.getElementById('global-log').innerText;
  const path = await window.electronAPI?.dialog.saveFile({
    filters: [{ name: 'Log Files', extensions: ['txt', 'log'] }]
  });
  if (path) {
    require('fs').writeFileSync(path, text);
    addLog(`日志已导出: ${path}`);
  }
});

// ---------------------------------------------------------------------------
// AutoML — 项目管理
// ---------------------------------------------------------------------------
async function loadProjects() {
  debug('loadProjects: 开始加载');

  const listEl = document.getElementById('project-list');
  if (!listEl) {
    debug('loadProjects: 未找到 project-list');
    return;
  }

  if (!pythonRunning) {
    addLog('Python 后端未启动，无法加载项目', 'AutoML');
    showProjectEmpty(listEl, true, 'Python 后端未启动，请等待启动完成');
    return;
  }

  try {
    // 后端刚启动时偶发 fetch failed：再试 2 次（每次间隔 1s）
    let projects;
    let _lastErr;
    for (let _i = 0; _i < 3; _i++) {
      try {
        projects = await apiGet('/api/projects');
        _lastErr = null;
        break;
      } catch (e) {
        _lastErr = e;
        if (_i < 2) await new Promise(r => setTimeout(r, 1000));
      }
    }
    if (_lastErr) throw _lastErr;
    debug(`loadProjects: 获取到 ${projects.length} 个项目`);

    // 清空列表（保留空提示元素）
    const emptyEl = listEl.querySelector('.project-empty');
    listEl.innerHTML = '';
    if (emptyEl) listEl.appendChild(emptyEl);

    if (projects.length === 0) {
      showProjectEmpty(listEl, true, '暂无项目，点击"新建项目"开始');
    } else {
      showProjectEmpty(listEl, false);

      projects.forEach(p => {
        const card = document.createElement('div');
        card.className = 'project-card';
        // Build metadata badges
        let metaHtml = '';
        // Mode badge
        const modeText = p.mode === 'table' ? '📊 表格' : '📈 时序';
        const modeCls = p.mode === 'table' ? 'status-running' : 'status-idle';
        metaHtml += `<span class="status-badge ${modeCls}">${modeText}</span> `;
        if (p.file_count !== undefined) metaHtml += `<span class="status-badge status-idle">${p.file_count || 0} 文件</span> `;
        if (p.segment_count !== undefined) metaHtml += `<span class="status-badge status-idle">${p.segment_count || 0} 分段</span> `;
        if (p.training_status && p.training_status !== 'idle') {
          const statusCls = { running: 'status-running', done: 'status-done', failed: 'status-failed' }[p.training_status] || 'status-idle';
          const statusText = { running: '训练中', done: '训练完成', failed: '训练失败' }[p.training_status] || p.training_status;
          metaHtml += `<span class="status-badge ${statusCls}">${statusText}</span>`;
        }
        card.innerHTML = `
          <div class="project-info">
            <div class="project-name">${escapeHtml(p.name)}</div>
            <div class="project-meta">创建于 ${p.created_at || '未知'} <span style="margin:0 4px;">|</span> ${metaHtml}</div>
          </div>
          <div class="project-actions" style="gap:12px;align-items:center;">
            <button class="btn btn-small" data-action="open" data-pid="${escapeHtml(p.project_id)}" style="font-size:13px;padding:6px 14px;">打开</button>
            <button class="btn btn-small btn-danger" data-action="delete" data-pid="${escapeHtml(p.project_id)}" data-name="${escapeHtml(p.name)}" style="opacity:0.7;font-size:12px;" title="删除项目">🗑</button>
          </div>
        `;
        // Click on card body opens project (delete button uses stopPropagation)
        card.addEventListener('click', () => openProject(p.project_id));
        listEl.appendChild(card);
      });

      listEl.querySelectorAll('button[data-action]').forEach(btn => {
        btn.addEventListener('click', (e) => {
          e.stopPropagation();
          const action = btn.dataset.action;
          const pid = btn.dataset.pid;

          if (action === 'open') {
            openProject(pid);
          } else if (action === 'delete') {
            deleteProject(pid, btn.dataset.name || '');
          }
        });
      });
    }
  } catch (err) {
    addLog(`加载项目失败: ${err.message}`, 'AutoML');
    showProjectEmpty(listEl, true, `加载失败: ${err.message}`);
  }
}

function showProjectEmpty(listEl, show, text) {
  let emptyEl = listEl.querySelector('.project-empty');
  if (!emptyEl && show) {
    emptyEl = document.createElement('div');
    emptyEl.className = 'project-empty empty-hint';
    listEl.appendChild(emptyEl);
  }
  if (emptyEl) {
    emptyEl.style.display = show ? 'block' : 'none';
    if (text) emptyEl.textContent = text;
  }
}

let _existingProjectNames = [];

function showCreateProjectForm() {
  const form = document.getElementById('create-project-form');
  if (form) {
    form.classList.add('open');
    setTimeout(() => document.getElementById('project-name')?.focus(), 100);
  }
  // 采样率行：仅时序模式显示
  const srRow = document.getElementById('sampling-rate-row');
  const modeRadios = document.querySelectorAll('input[name="project-mode"]');
  const toggleSR = () => {
    const isTS = document.querySelector('input[name="project-mode"]:checked')?.value === 'timeseries';
    if (srRow) srRow.style.display = isTS ? '' : 'none';
  };
  toggleSR();
  modeRadios.forEach(r => r.addEventListener('change', toggleSR));
  // Fetch existing project names for duplicate validation
  if (pythonRunning) {
    apiGet('/api/projects').then(projects => {
      _existingProjectNames = (projects || []).map(p => p.name);
    }).catch(() => { _existingProjectNames = []; });
  }
}

function hideCreateProjectForm() {
  const form = document.getElementById('create-project-form');
  if (form) {
    form.classList.remove('open');
    document.getElementById('project-name').value = '';
    const hintEl = document.getElementById('project-name-hint');
    if (hintEl) {
      hintEl.textContent = '';
      hintEl.style.color = '';
    }
  }
}

async function createProject() {
  const nameInput = document.getElementById('project-name');
  const hintEl = document.getElementById('project-name-hint');
  const name = nameInput?.value.trim();

  if (!name) {
    if (hintEl) { hintEl.textContent = '项目名称不能为空'; hintEl.style.color = '#f56c6c'; }
    nameInput?.focus();
    return;
  }

  if (name.length > 50) {
    if (hintEl) { hintEl.textContent = '项目名称不能超过50个字符'; hintEl.style.color = '#f56c6c'; }
    nameInput?.focus();
    return;
  }

  // Check for forbidden characters (only allow alphanumeric, Chinese, spaces, hyphens, underscores)
  if (/[\/\\:*?"<>|]/.test(name)) {
    if (hintEl) { hintEl.textContent = '项目名称不能包含 / \\ : * ? " < > |'; hintEl.style.color = '#f56c6c'; }
    nameInput?.focus();
    return;
  }

  // Check for duplicate project names
  if (_existingProjectNames.includes(name)) {
    if (hintEl) { hintEl.textContent = '已存在同名项目，请更换名称'; hintEl.style.color = '#f56c6c'; }
    nameInput?.focus();
    return;
  }

  if (hintEl) hintEl.textContent = '';

  try {
    const modeRadio = document.querySelector('input[name="project-mode"]:checked');
    const mode = modeRadio?.value || 'timeseries';
    const taskTypeRadio = document.querySelector('input[name="project-task-type"]:checked');
    const task_type = taskTypeRadio?.value || 'classification';
    const samplingRate = parseFloat(document.getElementById('project-sampling-rate')?.value) || 0;
    const body = { name, mode, task_type };
    if (mode === 'timeseries' && samplingRate > 0) {
      body.sampling_rate = samplingRate;
    }
    const result = await apiPost('/api/projects', body);
    addLog(`项目已创建: ${result.name} [${mode}] [${task_type}]`, 'AutoML');
    hideCreateProjectForm();
    loadProjects();
  } catch (err) {
    addLog(`创建项目失败: ${err.message}`, 'AutoML');
    if (hintEl) { hintEl.textContent = err.message; hintEl.style.color = '#f56c6c'; }
  }
}

async function deleteProject(pid, name) {
  if (!await showConfirm(`确定要删除项目「${name}」吗？所有数据文件、标注、分段和训练结果都将被删除，此操作不可撤销。`)) return;

  try {
    await apiDelete(`/api/projects/${pid}`);
    addLog(`项目已删除: ${pid}`, 'AutoML');
    showToast(`项目已删除`, '#67c23a');
    await loadProjects();
  } catch (err) {
    addLog(`删除项目失败: ${err.message}`, 'AutoML');
    showToast(`删除失败: ${err.message}`, '#f56c6c');
  }
}

async function clearAllProjects() {
  const projects = await apiGet('/api/projects').catch(() => []);
  if (!projects.length) {
    showToast('当前没有项目', '#e6a23c');
    return;
  }
  if (!await showConfirm(`确定要清空所有 ${projects.length} 个项目吗？所有数据将移到回收站，可从回收站恢复。`)) return;

  try {
    const result = await apiPost('/api/projects/clear-all', {});
    addLog(`已清空 ${result?.deleted || 0} 个项目`, 'AutoML');
    showToast(`已清空 ${result?.deleted || 0} 个项目`, '#67c23a');
    loadProjects();
  } catch (err) {
    addLog(`清空项目失败: ${err.message}`, 'AutoML');
    showToast(`清空失败: ${err.message}`, '#f56c6c');
  }
}

async function openProject(pid) {
  currentProject = pid;
  // Fetch project meta to get mode
  try {
    const meta = await apiGet(`/api/projects/${pid}`);
    currentProjectMode = meta.mode || 'timeseries';
  } catch (e) {
    currentProjectMode = 'timeseries';
  }
  navigateTo('automl-data', pid);
}

async function loadProjectData(page, pid) {
  if (!pid) return;

  try {
    const info = await apiGet(`/api/projects/${pid}`);

    document.querySelectorAll('.project-title').forEach(el => {
      el.textContent = info.name;
    });

    switch (page) {
      case 'automl-data':
        await loadDatasets(pid);
        hideDataPreview();
        break;
      case 'automl-labels':
        // Sync mode from project meta
        currentProjectMode = info.mode || 'timeseries';

        if (currentProjectMode === 'table') {
          // Table mode: hide waveform/segments, show data summary
          document.getElementById('labels-ts-panel')?.style.setProperty('display', 'none');
          document.getElementById('segment-panel')?.style.setProperty('display', 'none');
          document.getElementById('labels-table-panel')?.style.setProperty('display', '');
          document.getElementById('labels-two-col')?.style.setProperty('grid-template-columns', '1fr');
          await loadTableLabelSummary(pid);
        } else {
          // Timeseries mode: show waveform/segments
          document.getElementById('labels-ts-panel')?.style.setProperty('display', '');
          document.getElementById('segment-panel')?.style.setProperty('display', '');
          document.getElementById('labels-table-panel')?.style.setProperty('display', 'none');
          document.getElementById('labels-two-col')?.style.setProperty('grid-template-columns', '1fr 1fr');
          await loadSegments(pid);
          await loadSegFormLabels(pid);
          await loadSegFormFiles(pid);
          await loadChartFileList(pid);
          // Auto-load first file waveform
          {
            const sel = document.getElementById('chart-file-select');
            if (sel && sel.options.length > 1) {
              sel.selectedIndex = 1;
              await onChartFileChange();
            } else {
              document.getElementById('chart-end').value = 500;
              document.getElementById('chart-start').value = 0;
            }
          }
        }
        await loadLabels(pid);
        // Onboarding hint
        {
          const labelListEl = document.getElementById('label-list');
          const hasLabels = labelListEl && labelListEl.querySelectorAll('.label-item').length > 0;
          if (!hasLabels && labelListEl) {
            const hint = document.createElement('div');
            hint.className = 'empty-hint onboarding-hint';
            hint.style.cssText = 'padding:16px;margin:8px 0;background:#ecf5ff;border:1px solid #b3d8ff;border-radius:6px;color:#409eff;font-size:13px;line-height:1.6;';
            if (currentProjectMode === 'table') {
              hint.innerHTML = '<b>快速开始：</b><br>1. 创建标注名称（如 class_a, class_b）<br>2. 导入数据时选择标签列<br>3. 进入特征工程';
            } else {
              hint.innerHTML = '<b>快速开始：</b><br>1. 在左侧创建标注名称（如 normal, abnormal）<br>2. 在右侧为每段数据分配标注';
            }
            const parentEl = labelListEl.parentElement;
            const existingHint = parentEl?.querySelector('.onboarding-hint');
            if (existingHint) existingHint.remove();
            if (parentEl) parentEl.appendChild(hint);
          }
        }
        break;
      case 'automl-features':
        await loadFeatureConfig(pid);
        break;
      case 'automl-training':
        await loadTrainingStatus(pid);
        break;
      case 'automl-export':
        currentProjectMode = info.mode || 'timeseries';
        await loadExportInfo(pid);
        // Table mode hint
        if (currentProjectMode === 'table') {
          const hintEl = document.getElementById('export-info');
          if (hintEl && !hintEl.querySelector('.table-mode-hint')) {
            const hint = document.createElement('div');
            hint.className = 'table-mode-hint';
            hint.style.cssText = 'padding:8px 12px;margin-bottom:8px;background:#ecf5ff;border:1px solid #b3d8ff;border-radius:4px;color:#409eff;font-size:12px;';
            hint.textContent = '📊 表格模式导出的 C 代码无需窗特征提取链，仅包含标准化和模型推理。';
            hintEl.prepend(hint);
          }
        }
        break;
    }

    // Update wizard step status dynamically based on actual project state
    const stepName = page.replace('automl-', '');
    updateWizardSteps(pid, stepName);
  } catch (err) {
    addLog(`加载项目信息失败: ${err.message}`, 'AutoML');
  }
}

// ---------------------------------------------------------------------------
// AutoML — 数据管理
// ---------------------------------------------------------------------------
async function loadDatasets(pid) {
  try {
    const info = await apiGet(`/api/projects/${pid}/dataset`);
    const listEl = document.getElementById('dataset-list');
    if (!listEl) return;

    const files = info.files || [];
    if (files.length === 0) {
      listEl.innerHTML = '<div class="empty-hint">暂无数据文件，点击"导入数据"添加</div>';
    } else {
      listEl.innerHTML = files.map(f => `
        <div class="dataset-item" style="cursor:pointer;display:flex;justify-content:space-between;align-items:center;" data-file-id="${escapeHtml(f.file_id)}" data-filename="${escapeHtml(f.filename)}" data-rows="${f.rows}">
          <span>📄 <b>${escapeHtml(f.filename)}</b> — ${f.rows} 行, ${f.cols} 列</span>
          <span style="display:flex;align-items:center;gap:8px;">
            <span class="hint">点击预览</span>
            <button class="btn btn-small btn-danger" data-action="delete-file" data-file-id="${escapeHtml(f.file_id)}" data-filename="${escapeHtml(f.filename)}">删除</button>
          </span>
        </div>
      `).join('');

      listEl.querySelectorAll('.dataset-item').forEach(item => {
        item.addEventListener('click', () => {
          previewFile(item.dataset.fileId, item.dataset.filename, parseInt(item.dataset.rows));
        });
      });

      listEl.querySelectorAll('button[data-action="delete-file"]').forEach(btn => {
        btn.addEventListener('click', (e) => {
          e.stopPropagation();
          deleteFile(btn.dataset.fileId, btn.dataset.filename);
        });
      });
    }

    // Enable/disable the "下一步: 标注" button based on whether files exist
    const nextBtn = document.getElementById('btn-next-to-labels');
    if (nextBtn) {
      nextBtn.disabled = files.length === 0;
      nextBtn.title = files.length === 0 ? '请先导入数据文件' : '';
    }
    // Show/hide inline hint when button is disabled
    const nextHint = document.getElementById('btn-next-to-labels-hint');
    if (nextHint) {
      nextHint.style.display = files.length === 0 ? 'inline' : 'none';
    }
  } catch (err) {
    addLog(`加载数据集失败: ${err.message}`, 'AutoML');
  }
}

async function deleteFile(fileId, filename) {
  if (!currentProject) return;
  if (!await showConfirm(`确定要删除文件「${filename}」吗？该文件关联的所有分段也将被删除。`)) return;
  try {
    await apiDelete(`/api/projects/${currentProject}/dataset/file/${fileId}`);
    addLog(`文件已删除: ${filename}`, 'AutoML');
    loadDatasets(currentProject);
    loadSegments(currentProject);
  } catch (err) {
    addLog(`删除文件失败: ${err.message}`, 'AutoML');
  }
}

async function previewFile(fid, filename, totalRows) {
  const section = document.getElementById('data-preview-section');
  const title = document.getElementById('data-preview-title');
  const wrapper = document.getElementById('data-preview-wrapper');
  if (!section || !wrapper) return;

  // Highlight the selected dataset item
  document.querySelectorAll('.dataset-item').forEach(item => item.classList.remove('selected'));
  const targetItem = document.querySelector(`.dataset-item[data-file-id="${fid}"]`);
  if (targetItem) targetItem.classList.add('selected');

  section.style.display = 'block';
  const PAGE_SIZE = 200;

  // Use a state object to persist state across calls
  const previewState = {
    fileId: fid,
    fileName: filename,
    totalRows: totalRows,
    currentOffset: 0,
    allRows: [],
    isLoading: false, // Lock to prevent concurrent loads
  };

  async function loadPage(offset) {
    if (previewState.isLoading) return; // Prevent concurrent loads
    previewState.isLoading = true;

    if (title) title.textContent = `数据预览 — ${filename} (第${offset + 1}~${Math.min(offset + PAGE_SIZE, totalRows)}行，共${totalRows}行)`;
    if (offset === 0) wrapper.innerHTML = '<div class="empty-hint">加载中...</div>';
    try {
      const data = await apiGet(`/api/projects/${currentProject}/dataset/file/${fid}/data?limit=${PAGE_SIZE}&offset=${offset}`);
      if (!data.data || data.data.length === 0) {
        if (offset === 0) wrapper.innerHTML = '<div class="empty-hint">无数据</div>';
        previewState.isLoading = false;
        return;
      }
      if (offset === 0) previewState.allRows = [];
      previewState.allRows = previewState.allRows.concat(data.data);
      previewState.currentOffset = offset;

      let html = '<table class="data-table"><thead><tr><th>#</th>';
      (data.columns || []).forEach(col => { html += `<th>${escapeHtml(col)}</th>`; });
      html += '</tr></thead><tbody>';
      previewState.allRows.forEach((row, i) => {
        html += `<tr><td>${offset + i}</td>`;
        row.forEach(val => { html += `<td>${typeof val === 'number' ? val.toFixed(4) : val}</td>`; });
        html += '</tr>';
      });
      html += '</tbody></table>';

      const endRow = offset + data.data.length;
      if (endRow < totalRows) {
        html += `<div style="text-align:center;padding:8px;display:flex;justify-content:center;align-items:center;gap:12px;">`;
        html += `<span class="hint" style="margin:0;">已加载 ${endRow} / ${totalRows} 行</span>`;
        html += `<button class="btn btn-sm" data-action="load-more" data-offset="${endRow}">加载更多</button>`;
        html += `</div>`;
      } else {
        html += `<div class="hint" style="text-align:center;padding:8px;">已显示全部 ${totalRows} 行</div>`;
      }
      wrapper.innerHTML = html;
      wrapper.scrollIntoView({ behavior: 'smooth', block: 'nearest' });

      // Add event listener for load more button (instead of inline onclick)
      const loadMoreBtn = wrapper.querySelector('[data-action="load-more"]');
      if (loadMoreBtn) {
        loadMoreBtn.addEventListener('click', () => {
          const nextOffset = parseInt(loadMoreBtn.dataset.offset) || 0;
          _loadMorePreviewRows(previewState, title, wrapper);
        });
      }
    } catch (err) {
      wrapper.innerHTML = `<div class="empty-hint">加载失败: ${escapeHtml(err.message)}</div>`;
    } finally {
      previewState.isLoading = false;
    }
  }

  await loadPage(0);
}

// Helper function for loading more preview rows
async function _loadMorePreviewRows(state, title, wrapper) {
  if (state.isLoading) return; // Prevent concurrent loads
  state.isLoading = true;

  const nextOffset = state.currentOffset + 200; // PAGE_SIZE = 200
  const pageSize = 200;

  if (title) title.textContent = `数据预览 — ${state.fileName} (第${nextOffset + 1}~${Math.min(nextOffset + pageSize, state.totalRows)}行，共${state.totalRows}行)`;
  try {
    const data = await apiGet(`/api/projects/${currentProject}/dataset/file/${state.fileId}/data?limit=${pageSize}&offset=${nextOffset}`);
    if (!data.data || data.data.length === 0) {
      state.isLoading = false;
      return;
    }
    state.allRows = state.allRows.concat(data.data);
    state.currentOffset = nextOffset;

    let html = '<table class="data-table"><thead><tr><th>#</th>';
    (data.columns || []).forEach(col => { html += `<th>${escapeHtml(col)}</th>`; });
    html += '</tr></thead><tbody>';
    state.allRows.forEach((row, i) => {
      // Row number should be the absolute position in the full dataset (0-based index of all loaded rows)
      html += `<tr><td>${i}</td>`;
      row.forEach(val => { html += `<td>${typeof val === 'number' ? val.toFixed(4) : val}</td>`; });
      html += '</tr>';
    });
    html += '</tbody></table>';

    const endRow = nextOffset + data.data.length;
    if (endRow < state.totalRows) {
      html += `<div style="text-align:center;padding:8px;display:flex;justify-content:center;align-items:center;gap:12px;">`;
      html += `<span class="hint" style="margin:0;">已加载 ${endRow} / ${state.totalRows} 行</span>`;
      html += `<button class="btn btn-sm" data-action="load-more" data-offset="${endRow}">加载更多</button>`;
      html += `</div>`;
    } else {
      html += `<div class="hint" style="text-align:center;padding:8px;">已显示全部 ${state.totalRows} 行</div>`;
    }
    wrapper.innerHTML = html;

    // Add event listener for load more button (instead of inline onclick)
    const loadMoreBtn = wrapper.querySelector('[data-action="load-more"]');
    if (loadMoreBtn) {
      loadMoreBtn.addEventListener('click', () => {
        _loadMorePreviewRows(state, title, wrapper);
      });
    }
  } catch (err) {
    wrapper.innerHTML = `<div class="empty-hint">加载失败: ${escapeHtml(err.message)}</div>`;
  } finally {
    state.isLoading = false;
  }
}

function hideDataPreview() {
  const section = document.getElementById('data-preview-section');
  if (section) section.style.display = 'none';
  document.querySelectorAll('.dataset-item').forEach(item => item.classList.remove('selected'));
}

function downloadSampleCSV() {
  const csvContent = `timestamp,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z
0.000,0.12,-0.45,9.81,0.01,-0.02,0.00
0.010,0.15,-0.42,9.79,0.02,-0.01,0.01
0.020,0.18,-0.38,9.82,0.01,0.00,-0.01
0.030,0.21,-0.35,9.80,0.00,0.01,0.02
0.040,0.25,-0.32,9.78,-0.01,0.02,0.01
0.050,0.28,-0.29,9.81,0.00,0.01,-0.01
0.060,0.32,-0.26,9.79,0.01,0.00,0.02
0.070,0.35,-0.23,9.82,0.02,-0.01,0.01
0.080,0.38,-0.20,9.80,0.01,0.01,0.00
0.090,0.42,-0.17,9.78,0.00,0.02,-0.02
0.100,0.45,-0.14,9.81,-0.01,0.01,0.01`;

  const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = 'sample_timeseries.csv';
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);

  // 延迟释放URL，确保下载完成
  setTimeout(() => URL.revokeObjectURL(url), 1000);

  addLog('示例CSV已下载: sample_timeseries.csv', 'AutoML');
}

async function importDataset() {
  if (!currentProject) {
    showToast('请先打开一个项目', '#e6a23c');
    return;
  }

  const btn = document.getElementById('btn-import-dataset');
  const origText = btn?.textContent;
  if (btn) btn.textContent = '选择文件中...';

  try {
    const filePath = await window.electronAPI?.dialog.openFile({
      filters: [
        { name: 'Data Files', extensions: ['csv', 'npz'] },
      ]
    });

    if (!filePath) {
      if (btn) btn.textContent = origText;
      return;
    }

    if (btn) btn.textContent = '导入中...';
    addLog(`正在导入: ${filePath.split(/[/\\]/).pop()}`, 'AutoML');

    const fileName = filePath.split(/[/\\]/).pop();

    // 先预览CSV获取列信息
    const preview = await apiUpload(`/api/projects/${currentProject}/dataset/preview`, filePath, 'file');
    const columns = preview.columns || [];

    // Auto-detect label column for both table and timeseries modes
    let detectedLabelCol = '';
    {
      const labelCandidates = columns.filter(c => /^(label|标签|class|类别|target|标签列)$/i.test(c));
      detectedLabelCol = labelCandidates.length > 0 ? labelCandidates[0] : '';

      if (!detectedLabelCol && columns.length > 0) {
        // Prompt user to select label column
        detectedLabelCol = await _promptLabelColumn(columns, filePath);
      }
    }

    // 构建mapping配置 - 自动选择所有非时间戳/标签列作为通道
    const excludeCols = new Set(['timestamp', 'time', detectedLabelCol].filter(Boolean));
    const channels = columns.filter(col => !excludeCols.has(col));
    const mapping = { channels: channels };

    if (detectedLabelCol) {
      mapping.label_col = detectedLabelCol;
      addLog(`标签列 = ${detectedLabelCol}`, 'AutoML');
    } else {
      addLog('未选择标签列，将需要手动创建标注', 'AutoML');
    }

    await apiUploadWithMapping(`/api/projects/${currentProject}/dataset`, filePath, 'files', mapping);
    addLog(`数据导入成功: ${fileName}`, 'AutoML');
    showToast(`数据导入成功: ${fileName}`, '#67c23a');
    loadDatasets(currentProject);
  } catch (err) {
    addLog(`数据导入失败: ${err.message}`, 'AutoML');
    showToast(`导入失败: ${err.message}`, '#f56c6c');
  } finally {
    if (btn) btn.textContent = origText || '📥 导入数据';
  }
}

// Prompt user to select a label column for import
function _promptLabelColumn(columns, filePath) {
  return new Promise((resolve) => {
    const fileName = filePath.split(/[/\\]/).pop();
    const modeLabel = currentProjectMode === 'table' ? '表格' : '时序';
    // Create overlay
    const overlay = document.createElement('div');
    overlay.style.cssText = 'position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,0.5);z-index:9999;display:flex;align-items:center;justify-content:center;';
    const dialog = document.createElement('div');
    dialog.style.cssText = 'background:#fff;border-radius:8px;padding:24px;min-width:360px;max-width:480px;box-shadow:0 4px 20px rgba(0,0,0,0.3);';
    dialog.innerHTML = `
      <h3 style="margin:0 0 12px;">选择标签列</h3>
      <p style="color:#606266;font-size:13px;margin:0 0 12px;">
        文件 <b>${escapeHtml(fileName)}</b> 导入为${modeLabel}模式。<br>
        请选择用作分类标签的列（可跳过，稍后手动标注）：
      </p>
      <select id="_label-col-select" style="width:100%;padding:6px 8px;border:1px solid #dcdfe6;border-radius:4px;margin-bottom:12px;">
        <option value="">-- 跳过，不选择标签列 --</option>
        ${columns.filter(c => c !== 'timestamp' && c !== 'time').map(c => `<option value="${escapeHtml(c)}" ${/^label|class|target|标签|类别/i.test(c) ? 'selected' : ''}>${escapeHtml(c)}</option>`).join('')}
      </select>
      <div style="display:flex;justify-content:flex-end;gap:8px;">
        <button class="btn btn-sm" id="_label-col-skip">跳过</button>
        <button class="btn btn-primary btn-sm" id="_label-col-ok">确认</button>
      </div>
    `;
    overlay.appendChild(dialog);
    document.body.appendChild(overlay);

    const cleanup = (val) => {
      overlay.remove();
      resolve(val);
    };

    dialog.querySelector('#_label-col-ok').onclick = () => {
      cleanup(document.getElementById('_label-col-select')?.value || '');
    };
    dialog.querySelector('#_label-col-skip').onclick = () => cleanup('');
    overlay.onclick = (e) => { if (e.target === overlay) cleanup(''); };
  });
}

async function apiUploadWithMapping(url, filePath, fieldName, mapping) {
  debug(`UPLOAD WITH MAPPING ${url}, file=${filePath}, mapping=${JSON.stringify(mapping)}`);
  const result = await window.electronAPI?.http.uploadWithMapping(url, filePath, fieldName, mapping);
  debug(`Upload response: ok=${result?.ok}, status=${result?.status}`);
  if (!result?.ok) {
    throw new Error(result?.data?.detail || `上传失败 (${result?.status})`);
  }
  return result.data;
}

// ---------------------------------------------------------------------------
// AutoML — 标注管理
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// AutoML — 表格模式标注摘要
// ---------------------------------------------------------------------------
async function loadTableLabelSummary(pid) {
  const summaryEl = document.getElementById('table-data-summary');
  if (!summaryEl) return;
  try {
    const ds = await apiGet(`/api/projects/${pid}/dataset`);
    const files = ds.files || [];
    const totalRows = ds.total_rows || 0;
    const columns = ds.columns || [];
    const perClass = ds.per_class || {};
    const classCounts = ds.class_counts || [];

    let html = `<div style="margin-bottom:8px;"><b>文件数:</b> ${files.length}　<b>总行数:</b> ${totalRows}　<b>列数:</b> ${columns.length}</div>`;
    html += `<div style="margin-bottom:8px;"><b>特征列:</b> ${columns.join(', ') || '无'}</div>`;

    if (classCounts.length > 0) {
      html += '<div style="margin-bottom:4px;"><b>标签分布:</b></div>';
      html += '<div style="display:flex;flex-wrap:wrap;gap:8px;">';
      for (const cc of classCounts) {
        html += `<span style="padding:3px 10px;background:#f0f2f5;border-radius:12px;font-size:12px;">${escapeHtml(cc.name)}: ${cc.count}行</span>`;
      }
      html += '</div>';
    } else {
      html += '<div style="color:#e6a23c;margin-top:8px;">⚠ 未检测到标签列。导入时请指定 label_col，或手动创建标注。</div>';
    }

    summaryEl.innerHTML = html;
  } catch (err) {
    summaryEl.innerHTML = `<span style="color:#f56c6c;">加载数据摘要失败: ${err.message}</span>`;
  }
}

async function loadLabels(pid) {
  try {
    const labels = await apiGet(`/api/projects/${pid}/labels`);
    const listEl = document.getElementById('label-list');
    if (!listEl) return;

    if (labels.length === 0) {
      listEl.innerHTML = '<div class="empty-hint">暂无标注</div>';
    } else {
      listEl.innerHTML = labels.map(l => `
        <div class="label-item">
          <span><span style="display:inline-block;width:10px;height:10px;border-radius:50%;background:${l.color || '#2f80ed'};margin-right:4px;vertical-align:middle;"></span>${escapeHtml(l.name)}</span>
          <button class="btn btn-small btn-danger" data-action="delete-label" data-label-id="${l.label_id}">删除</button>
        </div>
      `).join('');

      listEl.querySelectorAll('button[data-action="delete-label"]').forEach(btn => {
        btn.addEventListener('click', () => {
          const labelId = btn.dataset.labelId;
          if (labelId) deleteLabel(pid, labelId);
        });
      });
    }
  } catch (err) {
    addLog(`加载标注失败: ${err.message}`, 'AutoML');
  }
}

async function addLabel() {
  if (!currentProject) return;

  const input = document.getElementById('label-name-input');
  const name = input?.value.trim();
  if (!name) { showToast('请输入标注名称', '#f56c6c'); return; }

  // Check for duplicate label names (server enforces uniqueness)
  try {
    const existingLabels = await apiGet(`/api/projects/${currentProject}/labels`);
    if (existingLabels.some(l => l.name === name)) {
      addLog(`标注名称已存在: ${name}`, 'AutoML');
      if (input) { input.style.borderColor = '#f56c6c'; setTimeout(() => { input.style.borderColor = ''; }, 2000); }
      return;
    }
  } catch (e) {
    addLog(`检查标注名称失败: ${e.message}`, 'AutoML');
    return;
  }

  const btn = document.getElementById('btn-add-label');
  const origText = btn?.textContent;
  if (btn) { btn.disabled = true; btn.textContent = '添加中...'; }

  const colorInput = document.getElementById('label-color-picker');
  const color = colorInput?.value || '#409eff';

  try {
    await apiPost(`/api/projects/${currentProject}/labels`, { name, color });
    addLog(`标注已添加: ${name}`, 'AutoML');
    if (input) input.value = '';
    loadLabels(currentProject);
    loadSegFormLabels(currentProject);
  } catch (err) {
    addLog(`添加标注失败: ${err.message}`, 'AutoML');
  } finally {
    if (btn) { btn.disabled = false; btn.textContent = origText || '+ 添加标注'; }
  }
}

async function deleteLabel(pid, labelId) {
  if (!await showConfirm('确定要删除此标注吗？')) return;

  try {
    await apiDelete(`/api/projects/${pid}/labels/${labelId}`);
    addLog('标注已删除', 'AutoML');
    loadLabels(pid);
    loadSegFormLabels(pid);
    loadSegments(pid);
  } catch (err) {
    addLog(`删除标注失败: ${err.message}`, 'AutoML');
  }
}

// ---------------------------------------------------------------------------
// AutoML — 分段管理
// ---------------------------------------------------------------------------
async function loadSegments(pid) {
  try {
    const segs = await apiGet(`/api/projects/${pid}/segments`);
    const labels = await apiGet(`/api/projects/${pid}/labels`);
    const listEl = document.getElementById('segment-list');
    if (!listEl) return;

    const labelMap = {};
    labels.forEach(l => { labelMap[l.label_id] = l.name; });

    if (segs.length === 0) {
      listEl.innerHTML = '<div class="empty-hint">暂无分段。CSV含label列时会自动生成，也可手动添加。</div>';
    } else {
      listEl.innerHTML = segs.map(s => `
        <div class="dataset-item" style="display:flex;justify-content:space-between;align-items:center;">
          <span>
            <b style="color:${(labels.find(l => l.label_id === s.label_id) || {}).color || '#666'}">${escapeHtml(labelMap[s.label_id] || '未标注')}</b>
            · 行 ${s.start}~${s.end} (${s.end - s.start}行)
            ${s.source === 'rle' ? '<span style="color:#6a9955">[自动]</span>' : '<span style="color:#569cd6">[手动]</span>'}
          </span>
          <button class="btn btn-small btn-danger" data-action="delete-seg" data-seg-id="${escapeHtml(s.id)}" data-pid="${escapeHtml(pid)}">删除</button>
        </div>
      `).join('');

      listEl.querySelectorAll('button[data-action="delete-seg"]').forEach(btn => {
        btn.addEventListener('click', () => {
          deleteSegment(btn.dataset.pid, btn.dataset.segId);
        });
      });
    }
    // 更新波形图的分段数据
    if (Waveform.data) {
      Waveform.segments = segs;
      Waveform.labels = labels;
      Waveform.draw();
    }

    // Enable/disable btn-next-to-features based on labels + segments
    const btnFeatures = document.getElementById('btn-next-to-features');
    if (btnFeatures) {
      btnFeatures.disabled = !(labels && labels.length > 0 && segs && segs.length > 0);
      btnFeatures.title = btnFeatures.disabled ? '请先添加标注和分段' : '';
    }
  } catch (err) {
    addLog(`加载分段失败: ${err.message}`, 'AutoML');
  }
}

async function deleteSegment(pid, segId) {
  if (!await showConfirm('确定要删除此分段吗？')) return;
  try {
    await apiDelete(`/api/projects/${pid}/segments/${segId}`);
    addLog('分段已删除', 'AutoML');
    loadSegments(pid);
  } catch (err) {
    addLog(`删除分段失败: ${err.message}`, 'AutoML');
  }
}

// ---------------------------------------------------------------------------
// AutoML — 分段管理（标注页表单）
// ---------------------------------------------------------------------------
async function loadSegFormFiles(pid) {
  try {
    const ds = await apiGet(`/api/projects/${pid}/dataset`);
    const sel = document.getElementById('seg-file-select');
    if (!sel) return;
    sel.innerHTML = '<option value="">-- 选择文件 --</option>';
    (ds.files || []).forEach(f => {
      const opt = document.createElement('option');
      opt.value = f.file_id;
      opt.textContent = `${f.filename} (${f.rows}行)`;
      opt.dataset.rows = f.rows;
      sel.appendChild(opt);
    });
    // 选择文件时更新行数提示
    sel.onchange = () => {
      const opt = sel.options[sel.selectedIndex];
      const hint = document.getElementById('seg-range-hint');
      if (opt && opt.dataset.rows) {
        const totalRows = parseInt(opt.dataset.rows);
        if (hint) hint.textContent = `(共 ${totalRows} 行)`;
        document.getElementById('seg-end').max = totalRows;
        // Don't force seg-end to totalRows; only update max attribute
        document.getElementById('seg-start').max = totalRows;
        // Sync chart-file-select and load the waveform for the selected file
        const chartSel = document.getElementById('chart-file-select');
        if (chartSel && chartSel.value !== opt.value) {
          chartSel.value = opt.value;
          onChartFileChange();
        }
      } else {
        if (hint) hint.textContent = '';
      }
    };
  } catch (err) { /* 静默 */ }
}

async function loadSegFormLabels(pid) {
  try {
    const labels = await apiGet(`/api/projects/${pid}/labels`);
    const sel = document.getElementById('seg-label-select');
    if (!sel) return;
    sel.innerHTML = '<option value="">-- 选择标注 --</option>';
    labels.forEach(l => {
      const opt = document.createElement('option');
      opt.value = l.label_id;
      opt.textContent = l.name;
      opt.style.color = l.color || '#409eff';
      sel.appendChild(opt);
    });
  } catch (err) { /* 静默 */ }
}

async function addSegmentFromForm() {
  if (!currentProject) return;
  // Always use the file shown in the waveform (chart-file-select) as the source of truth
  const chartSel = document.getElementById('chart-file-select');
  const segSel = document.getElementById('seg-file-select');
  const fid = chartSel?.value || segSel?.value;
  if (!fid) { showToast('请选择文件', '#f56c6c'); return; }

  // If seg-file-select differs from chart-file-select, sync it
  if (segSel && segSel.value !== fid) {
    segSel.value = fid;
  }

  const start = parseInt(document.getElementById('seg-start')?.value);
  const end = parseInt(document.getElementById('seg-end')?.value);
  const labelId = parseInt(document.getElementById('seg-label-select')?.value);
  if (isNaN(start) || start < 0) { showToast('起始行无效', '#f56c6c'); return; }
  if (isNaN(end) || end <= start) { showToast('结束行必须大于起始行', '#f56c6c'); return; }
  // Validate end row does not exceed file's total rows
  const fileOpt = chartSel?.options[chartSel.selectedIndex];
  const totalRows = fileOpt ? parseInt(fileOpt.dataset.rows) : 0;
  if (totalRows > 0 && end > totalRows) {
    showToast(`结束行 ${end} 超出文件总行数 ${totalRows}`, '#f56c6c');
    return;
  }
  if (isNaN(labelId)) { showToast('请选择标注', '#f56c6c'); return; }
  try {
    const existingSegs = await apiGet(`/api/projects/${currentProject}/segments`);
    const fileSegs = existingSegs.filter(s => s.file_id === fid);
    for (const seg of fileSegs) {
      if (start < seg.end && end > seg.start) {
        showToast(`警告: 同文件内行 ${start}~${end} 与已有分段 [行 ${seg.start}~${seg.end}] 重叠`, '#e6a23c');
        return;
      }
    }
  } catch (e) {
    // If we can't check, proceed anyway
  }

  const btn = document.getElementById('btn-add-segment');
  const origText = btn?.textContent;
  if (btn) { btn.disabled = true; btn.textContent = '添加中...'; }

  try {
    await apiPost(`/api/projects/${currentProject}/segments`, {
      file_id: fid, start, end, label_id: labelId,
    });
    addLog(`分段已添加: 行 ${start}-${end}`, 'AutoML');
    loadSegments(currentProject);
    // 刷新波形图
    onChartFileChange();
  } catch (err) {
    addLog(`添加分段失败: ${err.message}`, 'AutoML');
  } finally {
    if (btn) { btn.disabled = false; btn.textContent = origText || '+ 添加'; }
  }
}


// ---------------------------------------------------------------------------
// AutoML — 特征工程
// ---------------------------------------------------------------------------
async function loadFeatureConfig(pid) {
  try {
    const config = await apiGet(`/api/projects/${pid}/features/config`);
    const ds = await apiGet(`/api/projects/${pid}/dataset`);
    const meta = await apiGet(`/api/projects/${pid}`);

    // Sync mode
    currentProjectMode = meta.mode || 'timeseries';
    const isTable = currentProjectMode === 'table';

    // Toggle UI sections based on mode
    document.getElementById('feat-ts-left')?.style.setProperty('display', isTable ? 'none' : '');
    document.getElementById('feat-ts-right')?.style.setProperty('display', isTable ? 'none' : '');
    document.getElementById('feat-table-left')?.style.setProperty('display', isTable ? '' : 'none');

    if (isTable) {
      // Table mode: populate column checkboxes
      await loadTableFeatureColumns(pid, config.feature_ids || []);
    } else {
      // Timeseries mode: build categorized panels
      await buildFeatureCategories(pid);

      // Populate feature selections from config
      const channelFeatures = config.channel_features || null;
      const allFeatureIds = config.feature_ids || [];

      if (channelFeatures) {
        // Per-channel config: set each category's feature and channel checkboxes
        for (const [catKey, catInfo] of Object.entries(_featureCategories || {})) {
          const feats = catInfo.features || [];
          // Feature checkboxes: check if any channel in this category has this feature
          const container = document.querySelector(`.feat-cat-${catKey}-cb`)?.closest('.feat-cat-section');
          if (!container) continue;

          // Determine which features are used across all channels in this category
          const usedFeatures = new Set();
          const usedChannels = new Set();
          for (const [ch, chFeats] of Object.entries(channelFeatures)) {
            for (const f of chFeats) {
              if (feats.includes(f)) {
                usedFeatures.add(f);
                usedChannels.add(ch);
              }
            }
          }

          // Set feature checkboxes
          container.querySelectorAll('.feat-cb').forEach(cb => {
            cb.checked = usedFeatures.size > 0 ? usedFeatures.has(cb.value) : allFeatureIds.includes(cb.value);
          });

          // Set channel checkboxes
          container.querySelectorAll('.feat-ch-cb').forEach(cb => {
            cb.checked = usedChannels.size > 0 ? usedChannels.has(cb.value) : true;
          });
          // Update select-all
          const selectAll = container.querySelector('.feat-ch-select-all');
          if (selectAll) {
            const allChCbs = container.querySelectorAll('.feat-ch-cb');
            selectAll.checked = allChCbs.length > 0 && Array.from(allChCbs).every(cb => cb.checked);
          }
        }
      } else {
        // Legacy flat config: populate categories from feature_ids
        for (const [catKey, catInfo] of Object.entries(_featureCategories || {})) {
          const container = document.querySelector(`.feat-cat-${catKey}-cb`)?.closest('.feat-cat-section');
          if (!container) continue;
          container.querySelectorAll('.feat-cb').forEach(cb => {
            cb.checked = allFeatureIds.includes(cb.value);
          });
          // All channels selected by default
          container.querySelectorAll('.feat-ch-cb').forEach(cb => { cb.checked = true; });
          const selectAll = container.querySelector('.feat-ch-select-all');
          if (selectAll) selectAll.checked = true;
        }
      }

      // Freq domain
      const freqEnabled = config.freq_enabled || false;
      const freqCb = document.getElementById('freq-enabled');
      if (freqCb) freqCb.checked = freqEnabled;
      const freqBandsRow = document.getElementById('freq-bands-row');
      if (freqBandsRow) freqBandsRow.style.display = freqEnabled ? 'flex' : 'none';
      setInputVal('freq-bands', config.freq_bands);

      // Set window params
      if (meta.sampling_rate > 0) {
        _projectSamplingRate = meta.sampling_rate;
        const sr = document.getElementById('feat-sampling-rate');
        if (sr) sr.value = meta.sampling_rate;
      }
      setInputVal('feat-window-s', config.window_len_s);
      setInputVal('feat-n-per-window', config.n_per_window);
      setInputVal('feat-step', config.step);
      updateWindowInfo();
    }

    const norm = document.getElementById('feat-norm');
    if (norm) norm.value = config.norm || 'zscore';

    // Restore feature scoring section if previously computed
    loadFeatureScoring();

    // Update "下一步: 训练" button
    try {
      const scoring = await apiGet(`/api/projects/${pid}/features/scoring`);
      const hasComputedFeatures = !!(scoring.ranking && scoring.ranking.length > 0);
      const nextBtn = document.getElementById('btn-next-to-training');
      if (nextBtn) {
        nextBtn.disabled = !hasComputedFeatures;
        nextBtn.title = hasComputedFeatures ? '' : '请先计算特征';
      }
      const nextHint = document.getElementById('btn-next-to-training-hint');
      if (nextHint) {
        nextHint.style.display = hasComputedFeatures ? 'none' : 'inline';
      }
    } catch (e) {
      const nextBtn = document.getElementById('btn-next-to-training');
      if (nextBtn) {
        nextBtn.disabled = true;
        nextBtn.title = '请先计算特征';
      }
      const nextHint = document.getElementById('btn-next-to-training-hint');
      if (nextHint) nextHint.style.display = 'inline';
    }
  } catch (err) {
    addLog(`加载特征配置失败: ${err.message}`, 'AutoML');
  }
}

// Load column checkboxes for table mode feature selection
async function loadTableFeatureColumns(pid, selectedIds) {
  const container = document.getElementById('table-feature-columns');
  if (!container) return;
  try {
    const ds = await apiGet(`/api/projects/${pid}/dataset`);
    const columns = ds.columns || [];
    if (columns.length === 0) {
      container.innerHTML = '<div class="empty-hint" style="padding:12px;">无可选列，请先导入数据</div>';
      return;
    }
    // If selectedIds is empty, select all columns
    const selectAll = selectedIds.length === 0;
    container.innerHTML = columns.map(col => {
      const checked = selectAll || selectedIds.includes(col);
      return `<label class="feature-check" style="display:block;padding:2px 0;"><input type="checkbox" value="${escapeHtml(col)}" class="table-col-cb" ${checked ? 'checked' : ''}> ${escapeHtml(col)}</label>`;
    }).join('');

    // Select-all toggle
    const selectAllCb = document.getElementById('table-select-all');
    if (selectAllCb) {
      selectAllCb.checked = selectAll;
      selectAllCb.onchange = () => {
        container.querySelectorAll('.table-col-cb').forEach(cb => {
          cb.checked = selectAllCb.checked;
        });
      };
    }
  } catch (err) {
    container.innerHTML = `<div class="empty-hint" style="padding:12px;color:#f56c6c;">加载列信息失败: ${err.message}</div>`;
  }
}

// Build categorized feature panels dynamically (Piccolo-style)
let _featureCategories = null;  // cached from API
let _featureChannels = [];      // project channel list

async function buildFeatureCategories(pid) {
  const container = document.getElementById('feature-categories-container');
  if (!container) return;

  try {
    const catData = await apiGet(`/api/projects/${pid}/features/categories`);
    _featureCategories = catData.categories;
    _featureChannels = catData.channels || [];
  } catch (err) {
    container.innerHTML = `<div class="empty-hint" style="padding:12px;color:#f56c6c;">加载特征分类失败: ${err.message}</div>`;
    return;
  }

  if (!_featureCategories || Object.keys(_featureCategories).length === 0) {
    container.innerHTML = '<div class="empty-hint" style="padding:12px;">无可用特征分类</div>';
    return;
  }

  // Feature display names (Chinese + English)
  const featureLabels = {
    mean: '均值', std: '标准差', variance: '方差', min: '最小值', max: '最大值',
    skew: '偏度', kurt: '峰度', rms: '均方根', abs_mean: '绝对值均值', ptp: '峰峰值',
    zcr: '过零率', autocorr: '自相关',
    spec_centroid: '频谱质心', spec_energy: '频谱能量', dominant_freq: '主频率', band_ratio: '频带能量比',
  };

  let html = '';
  for (const [catKey, catInfo] of Object.entries(_featureCategories)) {
    const feats = catInfo.features || [];
    const defaultExpanded = catKey !== 'frequency'; // 频域默认折叠

    html += `<div class="section feat-cat-section" style="margin-bottom:10px;" data-cat="${catKey}">`;
    // Header (collapsible)
    html += `<div class="feat-cat-header" style="display:flex;align-items:center;justify-content:space-between;padding:6px 10px;cursor:pointer;background:#f5f7fa;border-radius:4px;user-select:none;" onclick="this.parentElement.querySelector('.feat-cat-body').classList.toggle('collapsed'); this.querySelector('.feat-cat-arrow').textContent = this.parentElement.querySelector('.feat-cat-body').classList.contains('collapsed') ? '▶' : '▼';">`;
    html += `<span style="font-weight:600;font-size:13px;">${catInfo.label}</span>`;
    html += `<span class="feat-cat-arrow" style="font-size:10px;color:#909399;">${defaultExpanded ? '▼' : '▶'}</span>`;
    html += `</div>`;
    // Body
    html += `<div class="feat-cat-body" style="padding:8px 10px;${defaultExpanded ? '' : 'display:none;'}">`;
    // Feature checkboxes
    html += `<div style="display:grid;grid-template-columns:1fr 1fr;gap:3px;margin-bottom:8px;">`;
    for (const f of feats) {
      const label = featureLabels[f] || f;
      html += `<label class="feature-check" style="font-size:12px;"><input type="checkbox" value="${f}" class="feat-cb feat-cat-${catKey}-cb"> ${f} <span style="color:#909399;">(${label})</span></label>`;
    }
    html += `</div>`;
    // Channel checkboxes
    html += `<div style="border-top:1px solid #eee;padding-top:6px;">`;
    html += `<div style="display:flex;align-items:center;gap:4px;margin-bottom:4px;">`;
    html += `<span style="font-size:11px;color:#909399;">通道:</span>`;
    html += `<label class="feature-check" style="font-size:11px;"><input type="checkbox" class="feat-ch-select-all feat-ch-${catKey}-select-all" checked> 全选</label>`;
    html += `</div>`;
    html += `<div style="display:flex;flex-wrap:wrap;gap:4px;">`;
    for (const ch of _featureChannels) {
      html += `<label class="feature-check" style="font-size:11px;"><input type="checkbox" value="${escapeHtml(ch)}" class="feat-ch-cb feat-ch-${catKey}-cb" checked> ${escapeHtml(ch)}</label>`;
    }
    html += `</div></div>`;
    html += `</div></div>`;
  }

  container.innerHTML = html;

  // Bind select-all toggles for each category
  for (const catKey of Object.keys(_featureCategories)) {
    const selectAllCb = container.querySelector(`.feat-ch-${catKey}-select-all`);
    if (selectAllCb) {
      selectAllCb.addEventListener('change', () => {
        container.querySelectorAll(`.feat-ch-${catKey}-cb`).forEach(cb => {
          cb.checked = selectAllCb.checked;
        });
      });
    }
  }
}

function setInputVal(id, val) {
  const el = document.getElementById(id);
  if (el && val !== undefined && val !== null) el.value = val;
}

function toggleFreqFeatures() {
  const enabled = document.getElementById('freq-enabled')?.checked;
  const freqBandsRow = document.getElementById('freq-bands-row');
  if (freqBandsRow) freqBandsRow.style.display = enabled ? 'flex' : 'none';
  // Also toggle the frequency category section
  const freqSection = document.querySelector('[data-cat="frequency"]');
  if (freqSection) {
    const body = freqSection.querySelector('.feat-cat-body');
    if (body && !enabled) {
      body.classList.add('collapsed');
      body.style.display = 'none';
      const arrow = freqSection.querySelector('.feat-cat-arrow');
      if (arrow) arrow.textContent = '▶';
    }
  }
  updateWindowInfo();
}

function updateWindowInfo() {
  const sr = parseFloat(document.getElementById('feat-sampling-rate')?.value) || _projectSamplingRate;
  const ws = parseFloat(document.getElementById('feat-window-s')?.value) || 2.0;
  const rawN = Math.round(sr * ws);
  let n = Math.max(2, rawN);  // 与后端 effective_n 一致，最少 2 点
  if (n % 2 !== 0) n += 1;    // 奇数→偶数，镜像 effective_n() 的偶数对齐
  const el = document.getElementById('feat-n-per-window');
  if (el) el.value = n;
  const info = document.getElementById('feat-window-info');
  if (!info) return;
  // 检查2的幂
  const freqEnabled = document.getElementById('freq-enabled')?.checked;
  const isPow2 = n > 0 && (n & (n - 1)) === 0;
  let calcText = `= ${sr} × ${ws} = ${rawN}点`;
  if (rawN < 2) {
    calcText += ` (最少2点)`;
  }
  // 自动调整步进：步进不得小于窗口点数的 1/10
  const stepEl = document.getElementById('feat-step');
  const stepHint = document.getElementById('feat-step-hint');
  if (stepEl && n > 0) {
    const minStep = Math.max(1, Math.floor(n / 10));
    const curStep = parseInt(stepEl.value) || 1;
    if (curStep < minStep) {
      window._stepAutoAdjusting = true;
      stepEl.value = minStep;
      window._stepAutoAdjusting = false;
    }
    stepEl.min = minStep;
    // 更新步进提示，显示最小值
    if (stepHint) {
      const minStepSec = (minStep / sr).toFixed(2);
      stepHint.textContent = `(点数，最小 ${minStep} 点 ≈ ${minStepSec}s)`;
    }
  }
  if (freqEnabled && !isPow2) {
    info.innerHTML = calcText + ' <span style="padding:2px 6px;background:#fdf6ec;border-radius:3px;color:#e6a23c;margin-left:6px;">频域特征要求窗口点数为2的幂</span>';
    // Add red border to window params section for prominence
    const windowSection = info.closest('.section');
    if (windowSection) windowSection.style.borderColor = '#f56c6c';
  } else {
    info.textContent = calcText;
    const windowSection = info.closest('.section');
    if (windowSection) windowSection.style.borderColor = '';
  }
}

async function saveFeatureConfig() {
  if (!currentProject) return;
  const featureIds = [];
  let channelFeatures = null;

  if (currentProjectMode === 'table') {
    // Table mode: collect selected column names
    document.querySelectorAll('.table-col-cb:checked').forEach(cb => featureIds.push(cb.value));
  } else {
    // Timeseries mode: collect per-category feature + channel selections
    channelFeatures = {};
    const allSelectedFeatures = new Set();

    if (_featureCategories) {
      for (const [catKey, catInfo] of Object.entries(_featureCategories)) {
        const section = document.querySelector(`.feat-cat-${catKey}-cb`)?.closest('.feat-cat-section');
        if (!section) continue;

        // Get selected features in this category
        const selectedFeats = [];
        section.querySelectorAll('.feat-cb:checked').forEach(cb => {
          selectedFeats.push(cb.value);
          allSelectedFeatures.add(cb.value);
        });

        // Get selected channels in this category
        const selectedChannels = [];
        section.querySelectorAll('.feat-ch-cb:checked').forEach(cb => {
          selectedChannels.push(cb.value);
        });

        // Build per-channel mapping for this category
        if (selectedFeats.length > 0 && selectedChannels.length > 0) {
          for (const ch of selectedChannels) {
            if (!channelFeatures[ch]) channelFeatures[ch] = [];
            channelFeatures[ch].push(...selectedFeats);
          }
        }
      }
    }

    // Freq-domain features
    const freqEnabled = document.getElementById('freq-enabled')?.checked;
    if (freqEnabled) {
      // Add freq features to all channels that have any time-domain features
      const freqFeats = [];
      document.querySelectorAll('#time-features input:checked, .feat-cb:checked').forEach(cb => {
        // freq features come from the frequency category
      });
      // Get freq features from the frequency category section
      const freqSection = document.querySelector('.feat-cat-frequency-cb')?.closest('.feat-cat-section');
      if (freqSection) {
        freqSection.querySelectorAll('.feat-cb:checked').forEach(cb => {
          freqFeats.push(cb.value);
          allSelectedFeatures.add(cb.value);
        });
        // Add freq features to all channels selected in frequency category
        const freqChannels = [];
        freqSection.querySelectorAll('.feat-ch-cb:checked').forEach(cb => {
          freqChannels.push(cb.value);
        });
        for (const ch of freqChannels) {
          if (!channelFeatures[ch]) channelFeatures[ch] = [];
          channelFeatures[ch].push(...freqFeats);
        }
      }
    }

    // Build flat feature_ids from all selected features (backward compat)
    featureIds.push(...allSelectedFeatures);

    // Fallback: if featureIds empty but channelFeatures has data, derive from it
    if (featureIds.length === 0 && channelFeatures && Object.keys(channelFeatures).length > 0) {
      const fromCh = new Set();
      for (const feats of Object.values(channelFeatures)) {
        for (const f of feats) fromCh.add(f);
      }
      featureIds.push(...fromCh);
    }

    // If all channels have identical features, simplify to None (backward compat)
    const chValues = Object.values(channelFeatures);
    if (chValues.length > 1) {
      const first = JSON.stringify(chValues[0].sort());
      const allSame = chValues.every(v => JSON.stringify(v.sort()) === first);
      if (allSame) channelFeatures = null;
    }
    if (chValues.length === 0) channelFeatures = null;
  }

  const cfg = {
    window_len_s: parseFloat(document.getElementById('feat-window-s')?.value) || 2.0,
    n_per_window: parseInt(document.getElementById('feat-n-per-window')?.value) || 512,
    step: parseInt(document.getElementById('feat-step')?.value) || 1,
    feature_ids: featureIds,
    channel_features: channelFeatures,
    freq_enabled: document.getElementById('freq-enabled')?.checked || false,
    freq_bands: parseInt(document.getElementById('freq-bands')?.value) || 5,
    norm: document.getElementById('feat-norm')?.value || 'zscore',
  };
  try {
    // 同步采样率到后端项目元数据（前端可能已修改，后端需要一致）
    const srVal = parseFloat(document.getElementById('feat-sampling-rate')?.value);
    if (srVal > 0 && currentProjectMode === 'timeseries') {
      await apiPatch(`/api/projects/${currentProject}`, { sampling_rate: srVal });
    }
    await apiPut(`/api/projects/${currentProject}/features/config`, cfg);
    addLog('特征配置已保存', 'AutoML');
    return true;
  } catch (err) {
    addLog(`保存配置失败: ${err.message}`, 'AutoML');
    throw err;
  }
}

async function runFeatureEngine() {
  if (!currentProject) {
    showToast('请先打开一个项目', '#e6a23c');
    return;
  }

  // Validate at least one feature is selected
  const featureIds = [];
  if (currentProjectMode === 'table') {
    document.querySelectorAll('.table-col-cb:checked').forEach(cb => featureIds.push(cb.value));
  } else {
    // Collect from categorized panels
    document.querySelectorAll('.feat-cb:checked').forEach(cb => featureIds.push(cb.value));
    if (featureIds.length === 0) {
      // Fallback: try old selectors
      document.querySelectorAll('#time-features input:checked').forEach(cb => featureIds.push(cb.value));
    }
  }
  if (featureIds.length === 0) {
    addLog('请至少选择一个特征后再计算', 'AutoML');
    return;
  }

  // Guard: freq-domain features require power-of-2 window size (timeseries only)
  if (currentProjectMode !== 'table') {
    const freqEnabled = document.getElementById('freq-enabled')?.checked;
    const nVal = parseInt(document.getElementById('feat-n-per-window')?.value) || 0;
    if (freqEnabled && nVal > 0 && (nVal & (nVal - 1)) !== 0) {
      showToast('频域特征要求窗口点数为2的幂，请调整采样率或窗长', '#e6a23c');
      return;
    }
  }

  const btn = document.getElementById('btn-run-features');
  const origText = btn?.textContent || '▶ 计算特征';
  if (btn) { btn.disabled = true; btn.textContent = '计算中...'; }

  try {
    await saveFeatureConfig();
    addLog('开始计算特征...', 'AutoML');
    const result = await apiPost(`/api/projects/${currentProject}/features/compute`, {});
    addLog(`特征计算完成: ${result.n_samples}样本 × ${result.n_features}特征`, 'AutoML');
    // 显示结果
    const resultEl = document.getElementById('feature-result');
    if (resultEl) {
      resultEl.innerHTML = `<span style="color:#67c23a;">✓ ${result.n_samples} 样本 × ${result.n_features} 特征</span>`;
    }
    // 加载评分
    loadFeatureScoring();

    // Enable "下一步: 训练" button after successful feature computation (fix: PM automl-features)
    const nextBtn = document.getElementById('btn-next-to-training');
    if (nextBtn) {
      nextBtn.disabled = false;
      nextBtn.title = '';
    }
    const nextHint = document.getElementById('btn-next-to-training-hint');
    if (nextHint) nextHint.style.display = 'none';
  } catch (err) {
    addLog(`特征计算失败: ${err.message}`, 'AutoML');
    showToast(`特征计算失败: ${err.message}`, '#f56c6c');
    const resultEl = document.getElementById('feature-result');
    if (resultEl) resultEl.innerHTML = `<span style="color:#f56c6c;">✗ ${err.message}</span>`;
  } finally {
    if (btn) { btn.disabled = false; btn.textContent = origText; }
  }
}

async function loadFeatureScoring() {
  if (!currentProject) return;
  try {
    const result = await apiGet(`/api/projects/${currentProject}/features/scoring`);
    const ranking = result.ranking || [];
    const section = document.getElementById('feature-scoring-section');
    const list = document.getElementById('feature-scoring-list');
    if (!section || !list || ranking.length === 0) return;
    section.style.display = 'block';
    const maxScore = Math.max(...ranking.map(r => r.score), 0.001);
    const showCount = Math.min(ranking.length, 20);
    const footerHtml = ranking.length > 20
      ? `<div style="text-align:center;color:#999;font-size:12px;padding:4px 0;">显示前${showCount}项，共${ranking.length}项</div>`
      : '';
    list.innerHTML = ranking.slice(0, 20).map((r, i) => {
      const pct = Math.round(r.score / maxScore * 100);
      return `<div style="display:flex;align-items:center;gap:8px;padding:3px 0;font-size:13px;">
        <span style="width:20px;text-align:right;color:#999;">${i + 1}</span>
        <span style="width:140px;font-family:monospace;">${escapeHtml(r.feature)}</span>
        <div style="flex:1;height:14px;background:#ebeef5;border-radius:3px;overflow:hidden;">
          <div style="width:${pct}%;height:100%;background:#409eff;border-radius:3px;"></div>
        </div>
        <span style="width:60px;text-align:right;font-size:11px;color:#999;">${r.score.toFixed(3)}</span>
      </div>`;
    }).join('') + footerHtml;
  } catch (err) {
    addLog(`加载特征评分失败: ${err.message}`, 'AutoML');
  }
}
// ---------------------------------------------------------------------------
async function loadTrainingStatus(pid) {
  try {
    const status = await apiGet(`/api/projects/${pid}/training`);

    const statusEl = document.getElementById('training-status');
    const progressEl = document.getElementById('training-progress');
    const logEl = document.getElementById('training-log');

    if (statusEl) {
      const statusText = {
        'idle': '未开始',
        'running': '训练中...',
        'done': '已完成',
        'failed': '失败',
        'cancelled': '已取消',
        'interrupted': '已中断'
      }[status.status] || status.status;

      statusEl.textContent = statusText;
      statusEl.className = `status-badge status-${status.status}`;
    }

    if (progressEl && status.total > 0) {
      const progress = Math.round((status.done / status.total) * 100);
      progressEl.style.width = `${progress}%`;
      const progressTextEl = document.getElementById('training-progress-text');
      if (progressTextEl) progressTextEl.textContent = `${progress}%`;
    }

    if (logEl && status.current) {
      // Only clear log when a new training starts (handled in startTraining),
      // not on every poll or page navigation. Preserve logs across navigation.
      // Remove placeholder on first log entry
      const placeholder = logEl.querySelector('.log-empty');
      if (placeholder) placeholder.remove();
      // Deduplicate: skip if last log entry is identical
      const lastChild = logEl.lastElementChild;
      if (!lastChild || lastChild.textContent !== status.current) {
        // Use insertAdjacentHTML instead of innerHTML += for better DOM performance
        // This avoids rebuilding all existing DOM nodes on each append
        const logEntry = document.createElement('div');
        logEntry.textContent = status.current;
        logEl.appendChild(logEntry);
      }
      // Limit log DOM to prevent performance degradation
      while (logEl.children.length > 500) {
        logEl.removeChild(logEl.firstChild);
      }
      logEl.scrollTop = logEl.scrollHeight;
    }

    // 根据训练状态控制按钮可见性
    const btnStop = document.getElementById('btn-stop-training');
    const btnStart = document.getElementById('btn-start-training');
    if (btnStop) {
      btnStop.disabled = (status.status !== 'running');
      btnStop.title = status.status === 'running' ? '' : '请先开始训练';
    }
    if (btnStart) {
      btnStart.disabled = (status.status === 'running');
      if (status.status !== 'running') btnStart.textContent = '▶ 开始训练';
      btnStart.title = status.status === 'running' ? '训练进行中' : '';
    }

    // Enable/disable the "下一步: 导出" button based on training status
    const btnNextExport = document.getElementById('btn-next-to-export');
    if (btnNextExport) btnNextExport.disabled = (status.status !== 'done');

    // Show/hide training log activity indicator
    const logIndicator = document.getElementById('training-log-indicator');
    if (logIndicator) {
      logIndicator.style.display = status.status === 'running' ? 'inline' : 'none';
    }

    // Display training metrics summary when training completes
    const resultsSection = document.getElementById('training-results-section');
    const resultsEl = document.getElementById('training-results');
    if (resultsSection && resultsEl) {
      if (status.status !== 'running' && status.status !== 'idle') {
        let metricsHtml = '';
        if (status.best_accuracy !== undefined && status.best_accuracy !== null) {
          metricsHtml += `<div class="form-row"><label>最佳准确率:</label><span style="font-weight:bold;color:#67c23a;">${(status.best_accuracy * 100).toFixed(1)}%</span></div>`;
        }
        if (status.best_loss !== undefined && status.best_loss !== null) {
          metricsHtml += `<div class="form-row"><label>最低损失:</label><span style="font-weight:bold;">${status.best_loss.toFixed(4)}</span></div>`;
        }
        if (status.best_model) {
          metricsHtml += `<div class="form-row"><label>最佳模型:</label><span style="font-family:monospace;">${escapeHtml(String(status.best_model))}</span></div>`;
        }
        if (status.n_iter !== undefined) {
          metricsHtml += `<div class="form-row"><label>搜索轮数:</label><span>${status.n_iter}</span></div>`;
        }
        if (status.duration_s !== undefined && status.duration_s !== null) {
          metricsHtml += `<div class="form-row"><label>训练耗时:</label><span>${Math.round(status.duration_s)}秒</span></div>`;
        }
        if (metricsHtml) {
          resultsEl.innerHTML = metricsHtml;
          resultsSection.style.display = 'block';
        } else {
          resultsSection.style.display = 'none';
        }
      } else {
        resultsSection.style.display = 'none';
      }
    }

    // If training is running, start polling to update progress
    if (status.status === 'running') {
      refreshTrainingStatus();
    }

    // 训练完成后加载报告
    if (status.status === 'done') {
      loadTrainingReport(currentProject);
    }
  } catch (err) {
    addLog(`加载训练状态失败: ${err.message}`, 'AutoML');
  }
}

async function loadTrainingReport(pid) {
  try {
    const report = await apiGet(`/api/projects/${pid}/training/report`);
    const section = document.getElementById('training-report-section');
    const el = document.getElementById('training-report');
    if (!section || !el || !report.summary) return;

    section.style.display = 'block';
    const isRegression = report.task_type === 'regression';
    let html = '';

    // 摘要卡片
    html += '<div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:12px;margin-bottom:16px;">';
    html += `<div class="card" style="padding:12px;"><div class="card-title" style="font-size:12px;color:#909399;">总候选数</div><div style="font-size:20px;font-weight:bold;">${report.summary.total_candidates}</div></div>`;
    html += `<div class="card" style="padding:12px;"><div class="card-title" style="font-size:12px;color:#909399;">失败候选</div><div style="font-size:20px;font-weight:bold;color:${report.summary.failed_candidates > 0 ? '#f56c6c' : '#67c23a'};">${report.summary.failed_candidates}</div></div>`;
    html += `<div class="card" style="padding:12px;"><div class="card-title" style="font-size:12px;color:#909399;">最佳模型</div><div style="font-size:16px;font-weight:bold;font-family:monospace;">${report.summary.best_model_type || '-'}</div></div>`;
    html += `<div class="card" style="padding:12px;"><div class="card-title" style="font-size:12px;color:#909399;">最佳 ${report.summary.main_metric}</div><div style="font-size:20px;font-weight:bold;color:#409eff;">${report.summary.main_value != null ? (typeof report.summary.main_value === 'number' ? report.summary.main_value.toFixed(4) : report.summary.main_value) : '-'}</div></div>`;
    html += `<div class="card" style="padding:12px;"><div class="card-title" style="font-size:12px;color:#909399;">训练耗时</div><div style="font-size:20px;font-weight:bold;">${Math.round(report.summary.duration_s || 0)}s</div></div>`;
    html += '</div>';

    // 测试集指标
    const metricsTest = report.best_model?.metrics_test;
    if (metricsTest) {
      html += '<h4 style="margin:16px 0 8px;">📊 测试集指标</h4>';
      html += '<div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:8px;margin-bottom:16px;">';
      if (isRegression) {
        const regMetrics = [
          { key: 'mse', label: 'MSE', fmt: v => v.toFixed(6) },
          { key: 'mae', label: 'MAE', fmt: v => v.toFixed(6) },
          { key: 'r2', label: 'R²', fmt: v => v.toFixed(4) },
          { key: 'mape', label: 'MAPE', fmt: v => v.toFixed(2) + '%' },
        ];
        for (const m of regMetrics) {
          if (metricsTest[m.key] != null) {
            html += `<div style="padding:8px;background:#f5f7fa;border-radius:6px;text-align:center;"><div style="font-size:11px;color:#909399;">${m.label}</div><div style="font-size:16px;font-weight:bold;">${m.fmt(metricsTest[m.key])}</div></div>`;
          }
        }
      } else {
        const clsMetrics = [
          { key: 'accuracy', label: '准确率', fmt: v => (v * 100).toFixed(1) + '%' },
          { key: 'f1_macro', label: 'F1 宏', fmt: v => v.toFixed(4) },
          { key: 'precision_macro', label: '精确率 宏', fmt: v => v.toFixed(4) },
          { key: 'recall_macro', label: '召回率 宏', fmt: v => v.toFixed(4) },
        ];
        for (const m of clsMetrics) {
          if (metricsTest[m.key] != null) {
            html += `<div style="padding:8px;background:#f5f7fa;border-radius:6px;text-align:center;"><div style="font-size:11px;color:#909399;">${m.label}</div><div style="font-size:16px;font-weight:bold;">${m.fmt(metricsTest[m.key])}</div></div>`;
          }
        }
      }
      html += '</div>';
    }

    // 候选排行表
    if (report.all_candidates && report.all_candidates.length > 0) {
      html += '<h4 style="margin:16px 0 8px;">🏆 模型排行</h4>';
      html += '<div style="overflow-x:auto;"><table class="data-table" style="width:100%;font-size:13px;">';
      html += '<thead><tr><th>#</th><th>模型</th><th>CV 均值</th><th>CV 标准差</th><th>特征数</th><th>耗时(s)</th></tr></thead><tbody>';
      for (const c of report.all_candidates.slice(0, 20)) {
        const rowStyle = c.is_best ? 'background:#ecf5ff;font-weight:bold;' : '';
        html += `<tr style="${rowStyle}">`;
        html += `<td>${c.rank}</td>`;
        html += `<td style="font-family:monospace;">${c.model_type}${c.is_best ? ' ⭐' : ''}</td>`;
        html += `<td>${c.cv_mean != null ? c.cv_mean.toFixed(4) : '-'}</td>`;
        html += `<td>${c.cv_std != null ? c.cv_std.toFixed(4) : '-'}</td>`;
        html += `<td>${c.n_features}</td>`;
        html += `<td>${c.elapsed_s != null ? c.elapsed_s.toFixed(1) : '-'}</td>`;
        html += '</tr>';
      }
      html += '</tbody></table></div>';
    }

    // 数据摘要
    if (report.data_summary) {
      const ds = report.data_summary;
      html += '<h4 style="margin:16px 0 8px;">📁 数据摘要</h4>';
      html += '<div style="display:flex;gap:16px;flex-wrap:wrap;font-size:13px;color:#606266;">';
      html += `<span>样本数: <b>${ds.n_samples}</b></span>`;
      html += `<span>特征数: <b>${ds.n_features}</b></span>`;
      html += `<span>模式: <b>${ds.mode === 'timeseries' ? '时序' : '表格'}</b></span>`;
      if (ds.labels && ds.labels.length > 0) {
        html += `<span>标签: <b>${ds.labels.map(l => l.name).join(', ')}</b></span>`;
      }
      html += '</div>';
    }

    el.innerHTML = html;
  } catch (err) {
    // 报告不可用时静默忽略
  }
}

async function startTraining() {
  if (!currentProject) {
    showToast('请先打开一个项目', '#e6a23c');
    return;
  }

  const btn = document.getElementById('btn-start-training');
  const origText = btn?.textContent;
  if (btn) { btn.disabled = true; btn.textContent = '启动中...'; }

  // 立刻重置进度条/状态文本——避免上一个训练轮的"已完成/100%"在 POST 返回前残留。
  // 后端 start() 会先 write_state('running', done=0) 再返回，所以 POST 一回来就是新值。
  const statusEl = document.getElementById('training-status');
  if (statusEl) {
    statusEl.textContent = '训练中...';
    statusEl.className = 'status-badge status-running';
  }
  const progressEl = document.getElementById('training-progress');
  if (progressEl) progressEl.style.width = '0%';
  const progressTextEl = document.getElementById('training-progress-text');
  if (progressTextEl) progressTextEl.textContent = '0%';
  // 隐藏上一轮的 metrics summary
  const resultsSection = document.getElementById('training-results-section');
  if (resultsSection) resultsSection.style.display = 'none';

  const epochs = parseInt(document.getElementById('train-epochs')?.value || '30');
  const timeout = parseInt(document.getElementById('train-timeout')?.value || '600');

  if (isNaN(epochs) || epochs < 1) {
    showToast('搜索轮数必须 >= 1', '#f56c6c');
    if (btn) { btn.disabled = false; btn.textContent = origText || '▶ 开始训练'; }
    return;
  }
  if (isNaN(timeout) || timeout < 10) {
    showToast('超时时间必须 >= 10 秒', '#f56c6c');
    if (btn) { btn.disabled = false; btn.textContent = origText || '▶ 开始训练'; }
    return;
  }

  try {
    // Clear previous training log when starting a new training session
    const trainingLogEl = document.getElementById('training-log');
    if (trainingLogEl) {
      trainingLogEl.innerHTML = '';
    }
    // 根据项目 task_type 选择合适的默认指标
    let projectMeta = null;
    try {
      projectMeta = await apiGet(`/api/projects/${currentProject}`);
    } catch (_) {}
    const taskType = projectMeta?.task_type || 'classification';
    const defaultMetric = taskType === 'regression' ? 'r2' : 'f1_macro';
    addLog(`开始训练 (n_iter=${epochs}, timeout=${timeout}s, task=${taskType})...`, 'AutoML');
    await apiPost(`/api/projects/${currentProject}/training`, {
      n_iter: epochs,
      budget_s: timeout,
      task_type: taskType,
      metric: defaultMetric,
    });
    addLog('训练任务已启动', 'AutoML');
    if (btn) btn.textContent = '训练中...';
    // 立刻同步一次 UI（不依赖 3s 轮询）：后端 start() 已先 write_state('running',done=0)
    // 再返回，POST 一回来文件里就是新值。loadTrainingStatus 还会接管进度条/按钮。
    loadTrainingStatus(currentProject).catch(() => {});
    refreshTrainingStatus();
  } catch (err) {
    addLog(`启动训练失败: ${err.message}`, 'AutoML');
    if (btn) { btn.disabled = false; btn.textContent = origText || '▶ 开始训练'; }
  }
}

async function stopTraining() {
  if (!currentProject) return;

  try {
    await apiPost(`/api/projects/${currentProject}/training/cancel`, {});
    addLog('训练已取消', 'AutoML');
  } catch (err) {
    addLog(`取消训练失败: ${err.message}`, 'AutoML');
  }
}

let trainingRefreshTimer = null;
// 跟踪后端报告的最新训练状态——navigateTo 离开训练页时只有真正在跑才弹确认
let _lastTrainingStatus = null;
function refreshTrainingStatus() {
  if (trainingRefreshTimer) clearInterval(trainingRefreshTimer);
  trainingRefreshTimer = setInterval(async () => {
    if (!currentProject) {
      clearInterval(trainingRefreshTimer);
      trainingRefreshTimer = null;
      return;
    }
    try {
      const status = await apiGet(`/api/projects/${currentProject}/training`);
      _lastTrainingStatus = status.status;
      if (status.status !== 'running') {
        clearInterval(trainingRefreshTimer);
        trainingRefreshTimer = null;  // 必须清零，否则 navigateTo 的 showConfirm 守卫
                                       // 看到的是已清空 interval 的句柄（truthy），仍会弹窗
                                       // 挡住离开，导致 loadExportInfo 永远不被调用。
        addLog(`训练完成: ${status.status}`, 'AutoML');
        // Show a brief toast notification
        showToast(`训练${status.status === 'done' ? '完成' : status.status === 'failed' ? '失败' : '已' + status.status}`, status.status === 'done' ? '#67c23a' : '#f56c6c');
        // Update all wizard steppers to reflect current project state
        // This ensures badges on all pages (not just the current one) update immediately
        updateWizardSteps(currentProject, 'training');
      }
      loadTrainingStatus(currentProject);
    } catch (err) {
      // 忽略轮询错误
    }
  }, 3000);
}

// ---------------------------------------------------------------------------
// AutoML — 导出
// ---------------------------------------------------------------------------
async function loadExportInfo(pid) {
  try {
    const info = await apiGet(`/api/projects/${pid}/export`);
    const infoEl = document.getElementById('export-info');
    const exportBtn = document.getElementById('btn-export-project');

    // 区分 readiness 失败原因，避免静默吞错后误导用户"还在训练中"
    let projectReady = false;
    let blockReason = '';
    try {
      const [scoring, training, project] = await Promise.all([
        apiGet(`/api/projects/${pid}/features/scoring`),
        apiGet(`/api/projects/${pid}/training`),
        apiGet(`/api/projects/${pid}`),
      ]);
      if (training.status !== 'done') {
        blockReason = `训练未完成（当前状态: ${training.status || '未知'}）`;
      } else if (!scoring.ranking || scoring.ranking.length === 0) {
        blockReason = '特征评分结果为空，请回到特征页重新计算';
      } else if (project.stage !== 'trained' && project.stage !== 'exported') {
        // 训练文件说 done 但项目元数据落后——通常是训练中途异常，
        // 真实导出后端会拒（require_stage）。此处直接提示而非误导。
        blockReason = `项目阶段未到 trained（当前: ${project.stage}），请重新训练`;
      } else {
        projectReady = true;
      }
    } catch (e) {
      // 任何一个端点失败——显示真实错误而非固定文案
      const detail = e?.message || String(e);
      addLog(`导出就绪检查失败: ${detail}`, 'AutoML');
      blockReason = `就绪检查失败: ${detail}`;
    }

    if (exportBtn) {
      exportBtn.disabled = !projectReady;
      exportBtn.title = projectReady ? '' : blockReason || '请先完成训练后再导出';
    }

    if (infoEl) {
      if (info.report && info.report.ok) {
        const filename = info.zips && info.zips.length > 0 ? info.zips[0] : null;
        infoEl.innerHTML = `
          <div>导出状态: 已完成</div>
          <div>导出格式: BabyOS C 代码包 (.zip)</div>
          ${filename ? `<div>导出文件: ${escapeHtml(filename)}</div>
          <div style="margin-top:16px;display:flex;gap:12px;">
            <button class="btn btn-primary" id="btn-download-export" data-filename="${escapeHtml(filename)}">下载 C 代码包</button>
            <button class="btn" id="btn-open-export-dir">📂 打开代码目录</button>
          </div>` : ''}
        `;
        infoEl.style.marginBottom = '16px';
        // Add event listener for download button (instead of inline onclick)
        const dlBtn = document.getElementById('btn-download-export');
        if (dlBtn) {
          dlBtn.addEventListener('click', () => {
            const fname = dlBtn.dataset.filename;
            if (fname) downloadExport(fname);
          });
        }
        // Add event listener for open directory button
        const dirBtn = document.getElementById('btn-open-export-dir');
        if (dirBtn) {
          dirBtn.addEventListener('click', () => openExportDir(pid));
        }
      } else if (info.exported) {
        infoEl.innerHTML = '<div class="empty-hint">导出已完成</div>';
      } else if (info.report && info.report.errors && info.report.errors.length > 0) {
        infoEl.innerHTML = `<div style="color:#f56c6c;">导出失败: ${escapeHtml(info.report.errors[0])}</div>`;
      } else if (!projectReady) {
        infoEl.innerHTML = `<div class="empty-hint" style="color:#e6a23c;">${escapeHtml(blockReason || '请先完成训练后再导出')}</div>`;
      } else {
        infoEl.innerHTML = '<div class="empty-hint">暂无导出记录</div>';
      }
    }
  } catch (err) {
    addLog(`加载导出信息失败: ${err.message}`, 'AutoML');
    const infoEl = document.getElementById('export-info');
    if (infoEl) {
      infoEl.innerHTML = '';
      const msgDiv = document.createElement('div');
      msgDiv.style.color = '#f56c6c';
      msgDiv.textContent = '加载失败: ' + err.message;
      const retryLink = document.createElement('a');
      retryLink.href = '#';
      retryLink.style.cssText = 'color:#409eff;margin-left:8px;';
      retryLink.textContent = '重试';
      retryLink.addEventListener('click', (e) => {
        e.preventDefault();
        loadExportInfo(pid);
      });
      msgDiv.appendChild(retryLink);
      infoEl.appendChild(msgDiv);
    }
  }
}

/** 打开导出代码目录（Electron 用文件管理器打开；Web 模式复制路径到剪贴板） */
async function openExportDir(pid) {
  try {
    const dirInfo = await apiGet(`/api/projects/${pid || currentProject}/export/dir`);
    if (window.electronAPI?.shell?.openPath) {
      await window.electronAPI.shell.openPath(dirInfo.path);
    } else {
      await navigator.clipboard.writeText(dirInfo.path);
      showToast('目录路径已复制到剪贴板');
    }
  } catch (e) {
    showToast('打开目录失败: ' + e.message, '#f56c6c');
  }
}

async function exportProject() {
  if (!currentProject) {
    showToast('请先打开一个项目', '#e6a23c');
    return;
  }

  const btn = document.getElementById('btn-export-project');
  const origText = btn?.textContent;
  if (btn) { btn.textContent = '导出中...'; btn.disabled = true; }

  try {
    addLog('正在导出项目...', 'AutoML');
    const result = await apiPost(`/api/projects/${currentProject}/export`, {});

    if (result.filename) {
      const downloadUrl = `/api/projects/${currentProject}/export/download/${result.filename}`;
      try {
        const dlResult = await window.electronAPI?.http.request(downloadUrl, 'GET', null, { responseType: 'arraybuffer' });
        if (!dlResult?.ok) {
          addLog(`下载失败: HTTP ${dlResult?.status}`, 'AutoML');
          if (btn) { btn.textContent = origText || '📦 导出 C 代码'; btn.disabled = false; }
          return;
        }
        // Use Electron IPC to save to user-chosen path
        const savePath = await window.electronAPI?.dialog.saveFile({
          defaultPath: result.filename,
          filters: [{ name: 'BabyOS C Code', extensions: ['zip'] }]
        });
        if (savePath) {
          const fs = require('fs');
          const data = dlResult.data;
          if (data instanceof ArrayBuffer || ArrayBuffer.isView(data)) {
            fs.writeFileSync(savePath, Buffer.from(data));
          } else if (typeof data === 'string') {
            // Base64 encoded binary data from IPC
            fs.writeFileSync(savePath, Buffer.from(data, 'base64'));
          } else {
            // Last resort: try to write as buffer
            fs.writeFileSync(savePath, Buffer.from(String(data)));
          }
          addLog(`项目已导出: ${savePath}`, 'AutoML');
          // Immediately update the export wizard step to show completion
          document.querySelectorAll('.wizard-stepper').forEach(stepper => {
            const exportStep = stepper.querySelectorAll('.wizard-step')[4]; // 5th step (index 4)
            if (exportStep) {
              exportStep.classList.add('done');
              const numEl = exportStep.querySelector('.wizard-step-num');
              if (numEl && numEl.textContent !== '✓') numEl.textContent = '✓';
            }
          });
          // Refresh export page state and re-enable button only after page is fully loaded
          loadExportInfo(currentProject).finally(() => {
            if (btn) { btn.textContent = origText || '📦 导出 C 代码'; btn.disabled = false; }
          });
          return; // Button will be re-enabled by loadExportInfo's finally above
        }
      } catch (dlErr) {
        addLog(`下载失败: ${dlErr.message}`, 'AutoML');
        // Show download failure feedback in export-info area
        const infoEl = document.getElementById('export-info');
        if (infoEl) {
          infoEl.innerHTML = '';
          const msgDiv = document.createElement('div');
          msgDiv.style.cssText = 'color:#f56c6c;';
          msgDiv.textContent = '下载失败: ' + dlErr.message;
          infoEl.appendChild(msgDiv);
          const btnDiv = document.createElement('div');
          btnDiv.style.cssText = 'margin-top:8px;';
          const refreshBtn = document.createElement('button');
          refreshBtn.className = 'btn btn-sm';
          refreshBtn.textContent = '刷新状态';
          refreshBtn.addEventListener('click', () => loadExportInfo(currentProject));
          const retryBtn = document.createElement('button');
          retryBtn.className = 'btn btn-sm btn-primary';
          retryBtn.textContent = '重新导出';
          retryBtn.style.marginLeft = '8px';
          retryBtn.addEventListener('click', () => exportProject());
          btnDiv.appendChild(refreshBtn);
          btnDiv.appendChild(retryBtn);
          infoEl.appendChild(btnDiv);
        }
        // Re-enable button after download error
        if (btn) { btn.textContent = origText || '📦 导出 C 代码'; btn.disabled = false; }
      }
    } else {
      addLog('导出已启动，请稍后下载', 'AutoML');
      // 刷新导出信息区域，显示下载按钮 + 打开代码目录按钮
      loadExportInfo(currentProject).finally(() => {
        if (btn) { btn.textContent = origText || '📦 导出 C 代码'; btn.disabled = false; }
      });
    }
  } catch (err) {
    addLog(`导出失败: ${err.message}`, 'AutoML');
    if (btn) { btn.textContent = origText || '📦 导出 C 代码'; btn.disabled = false; }
  }
  // Note: no finally block — button re-enable is handled by each path individually
  // (success path defers to loadExportInfo's completion to avoid race condition)
}

async function downloadExport(filename) {
  if (!currentProject || !filename) return;
  const dlBtn = document.getElementById('btn-download-export');
  const origBtnText = dlBtn?.textContent;
  if (dlBtn) { dlBtn.disabled = true; dlBtn.textContent = '下载中...'; }
  try {
    const downloadUrl = `/api/projects/${currentProject}/export/download/${filename}`;
    const dlResult = await window.electronAPI?.http.request(downloadUrl, 'GET', null, { responseType: 'arraybuffer' });
    if (!dlResult?.ok) {
      addLog(`下载失败: HTTP ${dlResult?.status}`, 'AutoML');
      return;
    }
    const savePath = await window.electronAPI?.dialog.saveFile({
      defaultPath: filename,
      filters: [{ name: 'BabyOS C Code', extensions: ['zip'] }]
    });
    if (savePath) {
      const fs = require('fs');
      const data = dlResult.data;
      if (data instanceof ArrayBuffer || ArrayBuffer.isView(data)) {
        fs.writeFileSync(savePath, Buffer.from(data));
      } else if (typeof data === 'string') {
        fs.writeFileSync(savePath, Buffer.from(data, 'base64'));
      } else {
        fs.writeFileSync(savePath, Buffer.from(String(data)));
      }
      addLog(`项目已下载: ${savePath}`, 'AutoML');
    }
  } catch (dlErr) {
    addLog(`下载失败: ${dlErr.message}`, 'AutoML');
  } finally {
    if (dlBtn) { dlBtn.disabled = false; dlBtn.textContent = origBtnText || '下载 C 代码包'; }
  }
}

async function importProject() {
  const filePath = await window.electronAPI?.dialog.openFile({
    filters: [
      { name: 'BabyOS AutoML', extensions: ['bosml', 'zip'] },
    ]
  });

  if (!filePath) return;

  try {
    const fileName = filePath.split(/[/\\]/).pop();
    const result = await apiUpload('/api/projects/import', filePath, 'file');
    addLog(`项目导入成功: ${result.name || fileName}`, 'AutoML');
    loadProjects();
  } catch (err) {
    addLog(`导入失败: ${err.message}`, 'AutoML');
  }
}

// ---------------------------------------------------------------------------
// Wizard step status management
// ---------------------------------------------------------------------------

async function updateWizardSteps(pid, activeStep) {
  if (!pid) return;
  const steps = { data: false, labels: false, features: false, training: false, export: false };
  try {
    const [ds, labels, segments] = await Promise.all([
      apiGet(`/api/projects/${pid}/dataset`).catch(() => ({ files: [] })),
      apiGet(`/api/projects/${pid}/labels`).catch(() => []),
      apiGet(`/api/projects/${pid}/segments`).catch(() => []),
    ]);
    steps.data = !!(ds.files && ds.files.length > 0);
    steps.labels = !!(labels && labels.length > 0) && !!(segments && segments.length > 0);
    try {
      const scoring = await apiGet(`/api/projects/${pid}/features/scoring`);
      steps.features = !!(scoring && scoring.ranking && scoring.ranking.length > 0);
    } catch (e) { steps.features = false; }
    try {
      const training = await apiGet(`/api/projects/${pid}/training`);
      steps.training = training.status === 'done';
    } catch (e) { steps.training = false; }
    try {
      const exportInfo = await apiGet(`/api/projects/${pid}/export`);
      steps.export = !!(exportInfo.report && exportInfo.report.ok);
    } catch (e) { steps.export = false; }
  } catch (e) { /* ignore */ }

  const allSteps = document.querySelectorAll('.wizard-step');
  const stepKeys = ['data', 'labels', 'features', 'training', 'export'];
  const stepData = [
    { key: 'data', fn: goToAutomlData },
    { key: 'labels', fn: goToAutomlLabels },
    { key: 'features', fn: goToAutomlFeatures },
    { key: 'training', fn: goToAutomlTraining },
    { key: 'export', fn: goToAutomlExport },
  ];

  // Find the index of the active step in each stepper
  // Preserve existing 'done' classes to avoid flicker while async calls are pending
  document.querySelectorAll('.wizard-stepper').forEach(stepper => {
    const stepEls = stepper.querySelectorAll('.wizard-step');
    stepEls.forEach((stepEl, i) => {
      // Only remove 'active' and 'future', keep 'done' to avoid visual flicker
      stepEl.classList.remove('active', 'future');
      if (stepKeys[i] === activeStep) {
        stepEl.classList.add('active');
      }
    });
  });

  // Update each step's clickable state and done status based on actual project state
  allSteps.forEach(stepEl => {
    const stepIdx = Array.from(stepEl.parentElement.querySelectorAll('.wizard-step')).indexOf(stepEl);
    const key = stepKeys[stepIdx];
    if (!key) return;

    // Determine if this step's prerequisite is met
    const prerequisiteMet = stepIdx === 0 || Object.values(steps).slice(0, stepIdx).every(v => v);

    // If clicking this step would require prerequisite, check it
    if (!prerequisiteMet) {
      stepEl.style.opacity = '0.5';
      stepEl.style.cursor = 'not-allowed';
      stepEl.onclick = (e) => {
        e.preventDefault();
        e.stopPropagation();
        // Show a toast message about missing prerequisite
        const missingNames = { data: '数据', labels: '标注和分段', features: '特征评分', training: '训练' };
        const missing = Object.entries(steps).filter(([k, v]) => !v && stepKeys.indexOf(k) < stepIdx).pop();
        if (missing) {
          showToast(`请先完成${missingNames[missing[0]] || '前置步骤'}后再进入此步骤`, '#e6a23c');
        }
      };
    } else {
      stepEl.style.opacity = '1';
      stepEl.style.cursor = 'pointer';
      // Re-attach the real navigation function
      if (stepData[stepIdx]) {
        stepEl.onclick = stepData[stepIdx].fn;
      }
    }

    // Add done class if step is actually completed; remove if not
    if (steps[key]) {
      // Update step number to checkmark BEFORE applying 'done' class to avoid flicker
      const numEl = stepEl.querySelector('.wizard-step-num');
      if (numEl && numEl.textContent !== '✓') {
        numEl.textContent = '✓';
      }
      stepEl.classList.add('done');
    } else {
      // Restore step number BEFORE removing 'done' class
      const numEl = stepEl.querySelector('.wizard-step-num');
      if (numEl && numEl.textContent === '✓') {
        numEl.textContent = String(stepKeys.indexOf(key) + 1);
      }
      stepEl.classList.remove('done');
    }
  });
}

// ---------------------------------------------------------------------------
// AutoML 子页面导航函数
// ---------------------------------------------------------------------------
function goToProjects() {
  navigateTo('projects');
}

function goToAutomlData() {
  if (currentProject) {
    navigateTo('automl-data', currentProject);
  } else {
    navigateTo('projects');
  }
}

function goToAutomlLabels() {
  if (!currentProject) { navigateTo('projects'); return; }
  // 异步检查数据是否存在
  apiGet(`/api/projects/${currentProject}/dataset`).then(ds => {
    if (!ds.files || ds.files.length === 0) {
      addLog('请先导入数据文件', 'AutoML');
      // Show inline warning near dataset list
      const listEl = document.getElementById('dataset-list');
      if (listEl) {
        const warn = document.createElement('div');
        warn.style.cssText = 'padding:12px;margin:8px 0;background:#fdf6ec;border:1px solid #e6a23c;border-radius:4px;color:#e6a23c;font-size:13px;';
        warn.textContent = '请先导入数据文件，再进行标注';
        const existing = listEl.querySelector('.inline-warning');
        if (existing) existing.remove();
        listEl.prepend(warn);
        setTimeout(() => warn.remove(), 5000);
      }
      return;
    }
    navigateTo('automl-labels', currentProject);
  }).catch(() => {
    navigateTo('automl-labels', currentProject);
  });
}

function goToAutomlFeatures() {
  if (!currentProject) { navigateTo('projects'); return; }

  // Table mode: only need labels, no segments required
  if (currentProjectMode === 'table') {
    apiGet(`/api/projects/${currentProject}/labels`).then(labels => {
      if (!labels || labels.length === 0) {
        addLog('请先添加标注', 'AutoML');
        return;
      }
      navigateTo('automl-features', currentProject);
    }).catch(() => {
      navigateTo('automl-features', currentProject);
    });
    return;
  }

  // Timeseries mode: need both labels and segments
  Promise.all([
    apiGet(`/api/projects/${currentProject}/labels`),
    apiGet(`/api/projects/${currentProject}/segments`)
  ]).then(([labels, segments]) => {
    if (!labels || labels.length === 0) {
      addLog('请先添加标注', 'AutoML');
      return;
    }
    if (!segments || segments.length === 0) {
      addLog('请先添加分段数据', 'AutoML');
      const listEl = document.getElementById('segment-list');
      if (listEl) {
        const warn = document.createElement('div');
        warn.className = 'inline-warning';
        warn.style.cssText = 'padding:12px;margin:8px 0;background:#fdf6ec;border:1px solid #e6a23c;border-radius:4px;color:#e6a23c;font-size:13px;';
        warn.textContent = '请先添加分段数据，再进行特征工程';
        const existing = listEl.querySelector('.inline-warning');
        if (existing) existing.remove();
        listEl.prepend(warn);
        setTimeout(() => warn.remove(), 5000);
      }
      return;
    }
    navigateTo('automl-features', currentProject);
  }).catch(() => {
    navigateTo('automl-features', currentProject);
  });
}

function goToAutomlTraining() {
  if (!currentProject) { navigateTo('projects'); return; }
  apiGet(`/api/projects/${currentProject}/features/scoring`).then(res => {
    if (!res.ranking || res.ranking.length === 0) {
      addLog('请先计算特征', 'AutoML');
      return;
    }
    navigateTo('automl-training', currentProject);
  }).catch(() => {
    navigateTo('automl-training', currentProject);
  });
}

function goToAutomlExport() {
  if (currentProject) {
    navigateTo('automl-export', currentProject);
  } else {
    navigateTo('projects');
  }
}

// ---------------------------------------------------------------------------
// 串口操作 — 统一走 Python /api/device/serial/*（与协议栈共用同一 UART 句柄）
// 说明: Electron 原生 serial 仅作回退（Python 后端不可用时）；两者不可同时打开
//       同一端口，否则 OS 层会冲突。默认路径一律优先 Python API。
// ---------------------------------------------------------------------------
let pythonSerialOwned = false; // true = 端口由 Python DeviceManager 持有

async function refreshPorts() {
  // 优先: Python 后端枚举（与后续 open 同一服务）
  try {
    const info = await apiGet('/api/device/serial/ports');
    const ports = info?.ports || [];
    serialOpen = !!info?.open;
    if (info?.open && info?.current) {
      serialPortPath = info.current;
      pythonSerialOwned = true;
      const btn = document.getElementById('btn-open-serial');
      if (btn) btn.textContent = '关闭串口';
      const st = document.getElementById('serial-status');
      if (st) {
        st.textContent = `已连接: ${info.current} @ ${info.baudrate}`;
        st.className = 'status-text running';
      }
      updateSerialStatusDisplay();
    }
    const select = document.getElementById('serial-port');
    if (select) {
      select.innerHTML = '';
      ports.forEach(p => {
        const opt = document.createElement('option');
        opt.value = p;
        opt.textContent = p;
        select.appendChild(opt);
      });
      if (serialPortPath && ports.includes(serialPortPath)) {
        select.value = serialPortPath;
      }
    }
    if (ports.length === 0) {
      addLog('未检测到串口', 'Serial');
    } else {
      addLog(`串口列表: ${ports.join(', ')}`, 'Serial');
    }
    return;
  } catch (e) {
    addLog(`Python 串口枚举失败，回退 Electron serial: ${e.message}`, 'WARN');
  }
  // 回退: Electron 原生 serialport
  if (!window.electronAPI) {
    addLog('请在 Electron 桌面应用中使用', 'WARN');
    return;
  }
  const ports = await window.electronAPI.serial.list();
  const select = document.getElementById('serial-port');
  if (!select) return;

  select.innerHTML = '';
  ports.forEach(p => {
    const opt = document.createElement('option');
    opt.value = p.path;
    opt.textContent = p.path + (p.manufacturer ? ` (${p.manufacturer})` : '');
    select.appendChild(opt);
  });
  if (ports.length === 0) {
    addLog('未检测到串口', 'Serial');
  }
}

document.getElementById('btn-refresh-port')?.addEventListener('click', refreshPorts);

document.getElementById('btn-open-serial')?.addEventListener('click', async () => {
  if (serialOpen) {
    // 关闭: 优先走 Python（协议栈占用的句柄）
    if (pythonSerialOwned) {
      try {
        await apiPost('/api/device/serial/close', {});
        serialOpen = false;
        pythonSerialOwned = false;
        document.getElementById('btn-open-serial').textContent = '打开串口';
        document.getElementById('serial-status').textContent = '未连接';
        document.getElementById('serial-status').className = 'status-text';
        updateSerialStatusDisplay();
        addLog('串口已关闭 (Python)', 'Serial');
        return;
      } catch (e) {
        addLog(`Python 关闭串口失败: ${e.message}`, 'WARN');
      }
    }
    const result = await window.electronAPI.serial.close();
    if (result.ok) {
      serialOpen = false;
      pythonSerialOwned = false;
      document.getElementById('btn-open-serial').textContent = '打开串口';
      document.getElementById('serial-status').textContent = '未连接';
      updateSerialStatusDisplay();
      addLog('串口已关闭', 'Serial');
    }
  } else {
    const port = document.getElementById('serial-port').value;
    const baud = parseInt(document.getElementById('serial-baud').value);
    const encrypt = !!document.getElementById('encrypt-mode')?.checked;
    if (!port) {
      showToast('请选择串口', '#e6a23c');
      return;
    }
    // 优先: Python 持有串口（协议测试 / OTA / shell / xmodem 共用）
    try {
      const result = await apiPost('/api/device/serial/open', {
        path: port, baud, encrypt
      });
      serialOpen = true;
      pythonSerialOwned = true;
      serialPortPath = result?.port || port;
      document.getElementById('btn-open-serial').textContent = '关闭串口';
      document.getElementById('serial-status').textContent =
        `已连接: ${serialPortPath} @ ${result?.baudrate || baud}`;
      document.getElementById('serial-status').className = 'status-text running';
      updateSerialStatusDisplay();
      addLog(`串口已打开(Python): ${serialPortPath} @ ${result?.baudrate || baud}` +
        (encrypt ? ' [TEA加密]' : ''), 'Serial');
      return;
    } catch (e) {
      addLog(`Python 打开串口失败，回退 Electron: ${e.message}`, 'WARN');
    }
    // 回退: Electron serial（仅原始收发，不支持 b_protocol）
    const result = await window.electronAPI.serial.open({ path: port, baudRate: baud });
    if (result.ok) {
      serialOpen = true;
      pythonSerialOwned = false;
      serialPortPath = port;
      document.getElementById('btn-open-serial').textContent = '关闭串口';
      document.getElementById('serial-status').textContent = `已连接: ${port}`;
      document.getElementById('serial-status').className = 'status-text running';
      updateSerialStatusDisplay();
      addLog(`串口已打开(Electron原始模式): ${port} @ ${baud} — 协议功能不可用`, 'Serial');
    } else {
      addLog(`打开失败: ${result.error}`, 'Serial');
      showToast(`打开失败: ${result.error}`, '#f56c6c');
    }
  }
});

window.electronAPI?.serial.onData((data) => {
  // 仅当串口由 Electron 持有时才把原始字节打进日志；
  // Python 持有端口时 UART 由 DeviceManager 消费，不会走此通道。
  if (pythonSerialOwned) return;
  const text = new TextDecoder().decode(new Uint8Array(data));
  const logEl = document.getElementById('serial-log');
  if (logEl) {
    logEl.innerHTML += `<div><span style="color:#6a9955">[RX]</span> ${escapeHtml(text)}</div>`;
    logEl.scrollTop = logEl.scrollHeight;
  }
  addLog(`[RX] ${text}`, 'UART');
});

document.getElementById('btn-clear-log')?.addEventListener('click', () => {
  document.getElementById('serial-log').innerHTML = '';
});

function _serialLogLine(text, cls = 'RX') {
  const logEl = document.getElementById('serial-log');
  if (!logEl) return;
  const color = cls === 'TX' ? '#569cd6' : '#6a9955';
  logEl.innerHTML += `<div><span style="color:${color}">[${cls}]</span> ${escapeHtml(text)}</div>`;
  logEl.scrollTop = logEl.scrollHeight;
}

async function _requirePythonSerial() {
  if (!pythonSerialOwned || !serialOpen) {
    showToast('请先通过 Python 打开串口', '#e6a23c');
    addLog('协议功能需要 Python 持有的串口句柄，请先打开串口', 'WARN');
    return false;
  }
  return true;
}

document.getElementById('btn-test')?.addEventListener('click', async () => {
  if (!(await _requirePythonSerial())) return;
  _serialLogLine('>> b_protocol CMD 0x1 test("BabyOS")', 'TX');
  try {
    const r = await apiPost('/api/device/protocol/test', {});
    addLog(`协议测试 OK: device_id=0x${(r.device_id >>> 0).toString(16)} ` +
      `param="${r.param_text}"`, 'Protocol');
    _serialLogLine(`<< ACK cmd=0x${r.cmd.toString(16)} param=${r.param_hex}`);
    showToast('协议测试成功', '#67c23a');
  } catch (e) {
    addLog(`协议测试失败: ${e.message}`, 'Protocol');
    showToast(e.message, '#f56c6c');
  }
});

document.getElementById('btn-set-time')?.addEventListener('click', async () => {
  if (!(await _requirePythonSerial())) return;
  const utc = Math.floor(Date.now() / 1000);
  _serialLogLine(`>> b_protocol CMD 0x2 UTC=${utc}`, 'TX');
  try {
    const r = await apiPost('/api/device/protocol/set_time', { utc });
    addLog(`设置时间 OK: utc=${r.utc}`, 'Protocol');
    showToast('时间已设置', '#67c23a');
  } catch (e) {
    addLog(`设置时间失败: ${e.message}`, 'Protocol');
    showToast(e.message, '#f56c6c');
  }
});

// ---------------------------------------------------------------------------
// OTA 升级 — POST /api/device/ota/start → 轮询 /api/device/ota/status
// ---------------------------------------------------------------------------
document.getElementById('btn-select-firmware')?.addEventListener('click', async () => {
  const path = await window.electronAPI?.dialog.openFile({
    filters: [{ name: 'Bin Files', extensions: ['bin'] }]
  });
  if (path) {
    document.getElementById('firmware-path').value = path;
    document.getElementById('firmware-name').value = path.split(/[/\\]/).pop() || '';
    addLog(`已选择固件: ${path}`);
  }
});

let _otaPollTimer = null;
function _setOtaProgress(pct, text) {
  const fill = document.getElementById('ota-progress');
  const label = document.getElementById('ota-progress-text');
  const p = Math.max(0, Math.min(100, pct | 0));
  if (fill) fill.style.width = p + '%';
  if (label) label.textContent = text || (p + '%');
}

async function _pollOtaStatus(jobId) {
  try {
    const st = await apiGet('/api/device/ota/status' + (jobId ? `?job_id=${encodeURIComponent(jobId)}` : ''));
    const job = st?.job;
    if (job) {
      _setOtaProgress(job.progress || 0,
        `${job.progress || 0}% (${job.state})`);
      if (job.state === 'done') {
        addLog(`OTA 完成: ok=${job.ok} result=${job.result_code}`, 'OTA');
        showToast(job.ok ? 'OTA 升级成功' : 'OTA 失败', job.ok ? '#67c23a' : '#f56c6c');
        clearInterval(_otaPollTimer); _otaPollTimer = null;
        return;
      }
      if (job.state === 'error' || job.state === 'cancelled') {
        addLog(`OTA 结束: ${job.state} ${job.error || ''}`, 'OTA');
        showToast(job.error || `OTA ${job.state}`, '#f56c6c');
        clearInterval(_otaPollTimer); _otaPollTimer = null;
        return;
      }
    } else if (st && st.transfer_active === false && jobId) {
      // job 记录缺失但传输已结束
      clearInterval(_otaPollTimer); _otaPollTimer = null;
    }
  } catch (e) {
    addLog(`OTA 状态轮询失败: ${e.message}`, 'OTA');
  }
}

document.getElementById('btn-start-ota')?.addEventListener('click', async () => {
  if (!(await _requirePythonSerial())) return;
  const path = document.getElementById('firmware-path').value.trim();
  const name = document.getElementById('firmware-name').value.trim();
  if (!path) { showToast('请选择固件文件', '#e6a23c'); return; }
  try {
    const r = await apiPost('/api/device/ota/start', {
      path, name: name || null, timeout: 30.0
    });
    addLog(`OTA 已接受: job=${r.job_id} file=${path}`, 'OTA');
    _setOtaProgress(0, '0% (starting)');
    if (_otaPollTimer) clearInterval(_otaPollTimer);
    _otaPollTimer = setInterval(() => _pollOtaStatus(r.job_id), 400);
    showToast('OTA 已启动', '#409eff');
  } catch (e) {
    addLog(`OTA 启动失败: ${e.message}`, 'OTA');
    showToast(e.message, '#f56c6c');
  }
});

// ---------------------------------------------------------------------------
// Xmodem/Ymodem — POST start → 轮询 /api/device/xmodem/status
// ---------------------------------------------------------------------------
document.getElementById('btn-select-xmodem')?.addEventListener('click', async () => {
  const path = await window.electronAPI?.dialog.openFile();
  if (path) document.getElementById('xmodem-file').value = path;
});

document.getElementById('btn-select-ymodem')?.addEventListener('click', async () => {
  const path = await window.electronAPI?.dialog.openFile();
  if (path) document.getElementById('ymodem-file').value = path;
});

let _xferPollTimer = null;
function _setXferProgress(kind, pct, text) {
  const fill = document.getElementById(kind + '-progress');
  const p = Math.max(0, Math.min(100, pct | 0));
  if (fill) fill.style.width = p + '%';
  if (text) addLog(`${kind} 进度: ${text}`, 'Xfer');
}

async function _pollXferStatus(kind, jobId) {
  try {
    const q = `?kind=${kind}` + (jobId ? `&job_id=${encodeURIComponent(jobId)}` : '');
    const st = await apiGet('/api/device/xmodem/status' + q);
    _setXferProgress(kind, st?.progress || 0,
      `${st?.progress || 0}% state=${st?.xfer_state || ''}`);
    const job = st?.job;
    if (job && (job.state === 'done' || job.state === 'error' || job.state === 'cancelled')) {
      addLog(`${kind} 结束: ${job.state} ${job.error || ''}`, 'Xfer');
      showToast(job.ok ? `${kind} 完成` : (job.error || `${kind} ${job.state}`),
        job.ok ? '#67c23a' : '#f56c6c');
      clearInterval(_xferPollTimer); _xferPollTimer = null;
    } else if (job && !st?.active && job.state === 'running') {
      // sender 已不在，但 job 还没收尾 — 再等一轮
    }
  } catch (e) {
    addLog(`${kind} 状态轮询失败: ${e.message}`, 'Xfer');
  }
}

document.getElementById('btn-xmodem-send')?.addEventListener('click', async () => {
  if (!(await _requirePythonSerial())) return;
  const path = document.getElementById('xmodem-file').value.trim();
  if (!path) { showToast('请选择 Xmodem 文件', '#e6a23c'); return; }
  try {
    const r = await apiPost('/api/device/xmodem/start', { path });
    addLog(`Xmodem 已接受: job=${r.job_id} file=${r.job?.filename || path}`, 'Xfer');
    if (_xferPollTimer) clearInterval(_xferPollTimer);
    _xferPollTimer = setInterval(() => _pollXferStatus('xmodem', r.job_id), 300);
    showToast('Xmodem 发送已启动', '#409eff');
  } catch (e) {
    addLog(`Xmodem 启动失败: ${e.message}`, 'Xfer');
    showToast(e.message, '#f56c6c');
  }
});

document.getElementById('btn-xmodem-cancel')?.addEventListener('click', async () => {
  try {
    await apiPost('/api/device/xmodem/cancel', {});
    addLog('Xmodem 已请求取消', 'Xfer');
  } catch (e) {
    addLog(`Xmodem 取消失败: ${e.message}`, 'Xfer');
    showToast(e.message, '#f56c6c');
  }
});

document.getElementById('btn-ymodem-send')?.addEventListener('click', async () => {
  if (!(await _requirePythonSerial())) return;
  const path = document.getElementById('ymodem-file').value.trim();
  if (!path) { showToast('请选择 Ymodem 文件', '#e6a23c'); return; }
  try {
    const r = await apiPost('/api/device/ymodem/start', { path });
    addLog(`Ymodem 已接受: job=${r.job_id} file=${r.job?.filename || path}`, 'Xfer');
    if (_xferPollTimer) clearInterval(_xferPollTimer);
    _xferPollTimer = setInterval(() => _pollXferStatus('ymodem', r.job_id), 300);
    showToast('Ymodem 发送已启动', '#409eff');
  } catch (e) {
    addLog(`Ymodem 启动失败: ${e.message}`, 'Xfer');
    showToast(e.message, '#f56c6c');
  }
});

document.getElementById('btn-ymodem-cancel')?.addEventListener('click', async () => {
  try {
    await apiPost('/api/device/ymodem/cancel', {});
    addLog('Ymodem 已请求取消', 'Xfer');
  } catch (e) {
    addLog(`Ymodem 取消失败: ${e.message}`, 'Xfer');
    showToast(e.message, '#f56c6c');
  }
});

// ---------------------------------------------------------------------------
// HTTP Mock — 真实本机 HTTP 服务器（记录请求 + 可配置响应）
// ---------------------------------------------------------------------------
let httpServerRunning = false;
let httpMockBaseUrl = '';

async function _syncHttpServerUi() {
  try {
    const st = await apiGet('/api/device/http/status');
    httpServerRunning = !!st?.running;
    httpMockBaseUrl = st?.base_url || '';
    const btn = document.getElementById('btn-http-server');
    const status = document.getElementById('http-server-status');
    if (btn) btn.textContent = httpServerRunning ? '停止服务器' : '启动服务器';
    if (status) {
      status.textContent = httpServerRunning
        ? `运行中 ${st.base_url || ''}`
        : '未运行';
      status.className = httpServerRunning ? 'status-text running' : 'status-text';
    }
  } catch (e) {
    // 后端不可达时保持本地状态
  }
}

document.getElementById('btn-http-server')?.addEventListener('click', async () => {
  try {
    if (httpServerRunning) {
      const r = await apiPost('/api/device/http/stop', {});
      httpServerRunning = false;
      httpMockBaseUrl = '';
      addLog('HTTP Mock 服务器已停止', 'HTTP');
      showToast('Mock 服务器已停止', '#909399');
    } else {
      const port = parseInt(document.getElementById('http-port')?.value || '0') || 0;
      const body = document.getElementById('http-response-body')?.value || '{"ok":true}';
      const content_type = document.getElementById('http-content-type')?.value || 'application/json';
      const status_code = parseInt(document.getElementById('http-status-code')?.value || '200') || 200;
      const https = !!document.getElementById('use-https')?.checked;
      const file_log = !!document.getElementById('http-file-log')?.checked;
      const r = await apiPost('/api/device/http/start', {
        port, body, content_type, status_code, https, file_log
      });
      httpServerRunning = true;
      httpMockBaseUrl = r?.base_url || '';
      const fl = r?.status?.file_log;
      if (fl && fl.enabled && fl.path) {
        const el = document.getElementById('http-filelog-status');
        if (el) el.textContent = `文件日志: ${fl.path}`;
      }
      addLog(`HTTP Mock 服务器已启动: ${httpMockBaseUrl} (https=${https}, file_log=${file_log})`, 'HTTP');
      showToast(`Mock 已启动 ${httpMockBaseUrl}`, '#67c23a');
    }
    await _syncHttpServerUi();
  } catch (e) {
    addLog(`HTTP Mock 启停失败: ${e.message}`, 'HTTP');
    showToast(e.message, '#f56c6c');
    await _syncHttpServerUi();
  }
});

function _appendHttpLog(line, cls = 'HTTP') {
  const logEl = document.getElementById('http-log');
  if (!logEl) return;
  logEl.innerHTML += `<div>${escapeHtml(line)}</div>`;
  logEl.scrollTop = logEl.scrollHeight;
  addLog(line, cls);
}

document.getElementById('btn-http-clear')?.addEventListener('click', () => {
  const logEl = document.getElementById('http-log');
  if (logEl) logEl.innerHTML = '';
});

document.getElementById('btn-http-init')?.addEventListener('click', async () => {
  if (!(await _requirePythonSerial())) return;
  _appendHttpLog('>> CMD 0x52 HTTP_INIT', 'HTTP');
  try {
    const r = await apiPost('/api/device/http/init', {});
    _appendHttpLog(`<< ACK device_id=0x${(r.device_id >>> 0).toString(16)} cmd=0x${r.cmd.toString(16)}`, 'HTTP');
    showToast('设备 HTTP 客户端已初始化', '#67c23a');
  } catch (e) {
    _appendHttpLog(`<< init 失败: ${e.message}`, 'HTTP');
    showToast(e.message, '#f56c6c');
  }
});

document.getElementById('btn-http-deinit')?.addEventListener('click', async () => {
  if (!(await _requirePythonSerial())) return;
  _appendHttpLog('>> CMD 0x53 HTTP_DEINIT', 'HTTP');
  try {
    const r = await apiPost('/api/device/http/deinit', {});
    _appendHttpLog(`<< ACK device_id=0x${(r.device_id >>> 0).toString(16)} cmd=0x${r.cmd.toString(16)}`, 'HTTP');
    showToast('设备 HTTP 客户端已反初始化', '#909399');
  } catch (e) {
    _appendHttpLog(`<< deinit 失败: ${e.message}`, 'HTTP');
    showToast(e.message, '#f56c6c');
  }
});

document.getElementById('btn-http-send')?.addEventListener('click', async () => {
  const url = document.getElementById('http-url')?.value.trim();
  const method = document.getElementById('http-method')?.value || 'GET';
  const body = document.getElementById('http-body')?.value;
  const headersRaw = document.getElementById('http-headers')?.value || '';
  const mode = document.getElementById('http-mode')?.value || 'protocol';
  if (!url) { showToast('请输入 URL', '#e6a23c'); return; }

  const headers = {};
  headersRaw.split(/\r?\n/).forEach(line => {
    const t = line.trim();
    if (!t) return;
    const i = t.indexOf(':');
    if (i > 0) headers[t.slice(0, i).trim()] = t.slice(i + 1).trim();
  });

  if (mode === 'protocol') {
    if (!(await _requirePythonSerial())) return;
    if (httpMockBaseUrl && !url.startsWith(httpMockBaseUrl)) {
      addLog(`提示: Mock 运行于 ${httpMockBaseUrl}，当前 URL 为 ${url}（设备侧需固件自行请求该地址）`, 'HTTP');
    }
    _appendHttpLog(`>> [协议 0x50] ${method} ${url}${body ? ' body=' + body : ''}`, 'HTTP');
    try {
      const r = await apiPost('/api/device/http/request', {
        method, url, headers, body: body || null, timeout: 5.0
      });
      _appendHttpLog(`<< [协议 0x51] status=${r.status} body=${(r.body || '').slice(0, 200)}`, 'HTTP');
      showToast(`设备 HTTP ${r.status}`, r.status < 400 ? '#67c23a' : '#e6a23c');
    } catch (e) {
      _appendHttpLog(`<< 协议请求失败: ${e.message}`, 'HTTP');
      showToast(e.message, '#f56c6c');
    }
    return;
  }

  // 主机侧代理代发（联调 Mock）
  if (httpMockBaseUrl && !url.startsWith(httpMockBaseUrl)) {
    addLog(`提示: Mock 运行于 ${httpMockBaseUrl}，当前 URL 为 ${url}`, 'HTTP');
  }
  _appendHttpLog(`>> [主机代发] ${method} ${url}${body ? ' body=' + body : ''}`, 'HTTP');
  try {
    const r = await apiPost('/api/device/http/proxy', {
      url, method, body: body || null, headers, timeout: 5.0, verify_tls: false
    });
    _appendHttpLog(`<< [主机代发] ${r.status_code} len=${r.body_len} body=${(r.body || '').slice(0, 200)}`, 'HTTP');
    try {
      const reqs = await apiGet('/api/device/http/requests');
      if (reqs?.count) {
        _appendHttpLog(`Mock 已记录 ${reqs.count} 条请求`, 'HTTP');
        (reqs.requests || []).slice(-5).forEach(req => {
          _appendHttpLog(`  mock: ${req.method} ${req.path} from ${req.client}`, 'HTTP');
        });
      }
    } catch (_) { /* mock 未运行时忽略 */ }
    showToast(`代理请求 ${r.status_code}`, r.status_code < 400 ? '#67c23a' : '#e6a23c');
  } catch (e) {
    _appendHttpLog(`<< 代发失败: ${e.message}`, 'HTTP');
    showToast(e.message, '#f56c6c');
  }
});

// 初始化时同步 Mock 状态
_syncHttpServerUi();

// ---------------------------------------------------------------------------
// 参数调节 — 设备 shell 文本命令 "param ..."（非 b_protocol 帧）
// ---------------------------------------------------------------------------
function _paramLog(text) {
  const el = document.getElementById('param-output');
  if (!el) { addLog(text, 'Shell'); return; }
  el.innerHTML += `<div>${escapeHtml(text)}</div>`;
  el.scrollTop = el.scrollHeight;
  addLog(text, 'Shell');
}

function _fillParamDatalist(names) {
  const dl = document.getElementById('param-list');
  if (!dl) return;
  dl.innerHTML = '';
  (names || []).forEach(n => {
    const opt = document.createElement('option');
    opt.value = n;
    dl.appendChild(opt);
  });
}

document.getElementById('btn-param-list')?.addEventListener('click', async () => {
  if (!(await _requirePythonSerial())) return;
  _paramLog('>> param');
  try {
    const r = await apiPost('/api/device/param/list', {});
    _paramLog(`<< (${r.count}) ${r.names.join(', ') || '(空)'}`);
    _fillParamDatalist(r.names);
  } catch (e) {
    _paramLog(`<< 失败: ${e.message}`);
    showToast(e.message, '#f56c6c');
  }
});

document.getElementById('btn-param-get')?.addEventListener('click', async () => {
  if (!(await _requirePythonSerial())) return;
  const name = document.getElementById('param-name').value.trim();
  if (!name) { showToast('请输入参数名称', '#e6a23c'); return; }
  _paramLog(`>> param ${name}`);
  try {
    const r = await apiPost('/api/device/param/get', { name });
    _paramLog(`<< ${r.name}=${r.value}`);
  } catch (e) {
    _paramLog(`<< 失败: ${e.message}`);
    showToast(e.message, '#f56c6c');
  }
});

document.getElementById('btn-param-set')?.addEventListener('click', async () => {
  if (!(await _requirePythonSerial())) return;
  const name = document.getElementById('param-name').value.trim();
  const value = document.getElementById('param-value').value.trim();
  if (!name) { showToast('请输入参数名称', '#e6a23c'); return; }
  if (!value) { showToast('请输入参数值', '#e6a23c'); return; }
  _paramLog(`>> param ${name} ${value}`);
  try {
    const numeric = /^-?\d+$/.test(value) ? parseInt(value, 10) : value;
    const r = await apiPost('/api/device/param/set', { name, value: numeric });
    _paramLog(`<< 设置成功 ${r.name}=${r.value} (verify=${r.verified})`);
  } catch (e) {
    _paramLog(`<< 失败: ${e.message}`);
    showToast(e.message, '#f56c6c');
  }
});

document.getElementById('btn-param-send')?.addEventListener('click', async () => {
  if (!(await _requirePythonSerial())) return;
  const cmd = document.getElementById('param-cmd').value.trim();
  if (!cmd) { showToast('请输入命令', '#e6a23c'); return; }
  _paramLog(`>> ${cmd}`);
  try {
    const r = await apiPost('/api/device/shell/cmd', { cmd });
    _paramLog(`<< ${r.response || '(无输出)'}`);
  } catch (e) {
    _paramLog(`<< 失败: ${e.message}`);
    showToast(e.message, '#f56c6c');
  }
});

function sendShellCmd(cmd) {
  // 兼容旧调用点：转发到 shell/cmd API
  apiPost('/api/device/shell/cmd', { cmd }).then(r => {
    _paramLog(`>> ${cmd}`);
    _paramLog(`<< ${r.response || '(无输出)'}`);
  }).catch(e => {
    _paramLog(`>> ${cmd}`);
    _paramLog(`<< 失败: ${e.message}`);
  });
}

// ---------------------------------------------------------------------------
// 设备信息 — CMD 0x7 UID / 0x8 SN / 0xA DEVINFO
// ---------------------------------------------------------------------------
document.getElementById('btn-get-uid')?.addEventListener('click', async () => {
  if (!(await _requirePythonSerial())) return;
  try {
    const r = await apiPost('/api/device/uid/get', {});
    document.getElementById('device-uid').value = r.uid_hex;
    addLog(`UID: ${r.uid_hex} (${r.uid_len} bytes)`, 'Device');
  } catch (e) {
    addLog(`获取 UID 失败: ${e.message}`, 'Device');
    showToast(e.message, '#f56c6c');
  }
});

document.getElementById('btn-set-sn')?.addEventListener('click', async () => {
  if (!(await _requirePythonSerial())) return;
  const orval = parseInt(document.getElementById('device-orval')?.value || '0', 10) || 0;
  try {
    const r = await apiPost('/api/device/sn/write', { orval });
    document.getElementById('device-sn').value = r.sn_hex;
    addLog(`SN 已写入: orval=${r.orval} sn=${r.sn_hex}`, 'Device');
    showToast('SN 写入成功', '#67c23a');
  } catch (e) {
    addLog(`写入 SN 失败: ${e.message}`, 'Device');
    showToast(e.message, '#f56c6c');
  }
});

document.getElementById('btn-get-device-info')?.addEventListener('click', async () => {
  if (!(await _requirePythonSerial())) return;
  try {
    const r = await apiPost('/api/device/info/get', {});
    document.getElementById('device-version').value = r.version;
    document.getElementById('device-model').value = r.model;
    addLog(`设备信息: version=${r.version} model=${r.model}`, 'Device');
  } catch (e) {
    addLog(`获取设备信息失败: ${e.message}`, 'Device');
    showToast(e.message, '#f56c6c');
  }
});

// ---------------------------------------------------------------------------
// 日志落盘 — POST /api/device/log/start|stop
// ---------------------------------------------------------------------------
function _logfileLog(text) {
  const el = document.getElementById('logfile-status');
  if (el) el.textContent = text;
}

async function _syncLogFileUi() {
  try {
    const st = await apiGet('/api/device/log/status');
    if (st?.enabled) {
      _logfileLog(`落盘中: ${st.path}`);
      const btn = document.getElementById('btn-logfile-start');
      if (btn) btn.textContent = '落盘中';
    } else {
      _logfileLog('未落盘');
      const btn = document.getElementById('btn-logfile-start');
      if (btn) btn.textContent = '开始落盘';
    }
  } catch (_) { /* backend down */ }
}

document.getElementById('btn-logfile-pick')?.addEventListener('click', async () => {
  const p = await window.electronAPI?.dialog.saveFile({
    filters: [{ name: 'Log Files', extensions: ['log', 'txt'] }]
  });
  if (p) document.getElementById('logfile-path').value = p;
});

document.getElementById('btn-logfile-start')?.addEventListener('click', async () => {
  const path = document.getElementById('logfile-path')?.value.trim();
  if (!path) { showToast('请先选择日志文件路径', '#e6a23c'); return; }
  try {
    const r = await apiPost('/api/device/log/start', { path });
    addLog(`日志落盘已启动: ${r.path}`, 'Log');
    await _syncLogFileUi();
  } catch (e) {
    addLog(`日志落盘启动失败: ${e.message}`, 'Log');
    showToast(e.message, '#f56c6c');
  }
});

document.getElementById('btn-logfile-stop')?.addEventListener('click', async () => {
  try {
    const r = await apiPost('/api/device/log/stop', {});
    addLog(`日志落盘已停止 (was=${JSON.stringify(r.was)})`, 'Log');
    await _syncLogFileUi();
  } catch (e) {
    addLog(`日志落盘停止失败: ${e.message}`, 'Log');
    showToast(e.message, '#f56c6c');
  }
});

_syncLogFileUi();

// ---------------------------------------------------------------------------
// 文件传输 CMD 0x6 — POST /api/device/file/start|stop|merge_folder
// ---------------------------------------------------------------------------
function _fileLog(text) {
  const el = document.getElementById('file-merge-log');
  if (!el) { addLog(text, 'File'); return; }
  el.innerHTML += `<div>${escapeHtml(text)}</div>`;
  el.scrollTop = el.scrollHeight;
  addLog(text, 'File');
}

function _setFileProgress(pct, text) {
  const fill = document.getElementById('file-progress');
  const label = document.getElementById('file-progress-text');
  const p = Math.max(0, Math.min(100, pct | 0));
  if (fill) fill.style.width = p + '%';
  if (label) label.textContent = text || (p + '%');
}

document.getElementById('btn-select-file')?.addEventListener('click', async () => {
  const path = await window.electronAPI?.dialog.openFile();
  if (path) document.getElementById('file-path').value = path;
});

document.getElementById('btn-select-folder')?.addEventListener('click', async () => {
  const path = await window.electronAPI?.dialog.openDirectory();
  if (path) document.getElementById('file-folder').value = path;
});

document.getElementById('btn-file-merge')?.addEventListener('click', async () => {
  const folder = document.getElementById('file-folder')?.value.trim();
  const out_name = document.getElementById('file-out-name')?.value.trim() || 'allfile.bin';
  if (!folder) { showToast('请选择目录', '#e6a23c'); return; }
  try {
    const r = await apiPost('/api/device/file/merge_folder', { folder_path: folder, out_name });
    _fileLog(`合并完成: ${r.file_count} 文件 → ${r.path} size=${r.size} crc=0x${(r.crc32 >>> 0).toString(16)}`);
    document.getElementById('file-path').value = r.path;
    showToast(`合并 ${r.file_count} 个文件`, '#67c23a');
  } catch (e) {
    _fileLog(`合并失败: ${e.message}`);
    showToast(e.message, '#f56c6c');
  }
});

let _filePollTimer = null;
async function _pollFileStatus(jobId) {
  try {
    const st = await apiGet('/api/device/file/status' + (jobId ? `?job_id=${encodeURIComponent(jobId)}` : ''));
    const job = st?.job;
    if (job) {
      _setFileProgress(job.progress || 0, `${job.progress || 0}% (${job.state})`);
      if (job.state === 'done') {
        addLog(`文件传输完成: ok=${job.ok} result=${job.result_code} file=${job.path}`, 'File');
        showToast(job.ok ? '文件传输成功' : '文件传输失败', job.ok ? '#67c23a' : '#f56c6c');
        clearInterval(_filePollTimer); _filePollTimer = null;
        return;
      }
      if (job.state === 'error' || job.state === 'cancelled') {
        addLog(`文件传输结束: ${job.state} ${job.error || ''}`, 'File');
        showToast(job.error || `文件传输 ${job.state}`, '#f56c6c');
        clearInterval(_filePollTimer); _filePollTimer = null;
        return;
      }
    }
  } catch (e) {
    addLog(`文件传输状态轮询失败: ${e.message}`, 'File');
  }
}

document.getElementById('btn-file-start')?.addEventListener('click', async () => {
  if (!(await _requirePythonSerial())) return;
  const path = document.getElementById('file-path')?.value.trim();
  const dev_no = parseInt(document.getElementById('file-devno')?.value || '0', 10) || 0;
  const offset = parseInt(document.getElementById('file-offset')?.value || '0', 10) || 0;
  if (!path) { showToast('请选择文件', '#e6a23c'); return; }
  try {
    const r = await apiPost('/api/device/file/start', { path, dev_no, offset, timeout: 30.0 });
    addLog(`文件传输已接受: job=${r.job_id} path=${path} dev_no=${dev_no} offset=${offset}`, 'File');
    _setFileProgress(0, '0% (starting)');
    if (_filePollTimer) clearInterval(_filePollTimer);
    _filePollTimer = setInterval(() => _pollFileStatus(r.job_id), 400);
    showToast('文件传输已启动', '#409eff');
  } catch (e) {
    addLog(`文件传输启动失败: ${e.message}`, 'File');
    showToast(e.message, '#f56c6c');
  }
});

document.getElementById('btn-file-stop')?.addEventListener('click', async () => {
  try {
    const r = await apiPost('/api/device/file/stop', { notify_device: true });
    addLog(`文件传输已停止 (notified=${r.notified_device})`, 'File');
    if (_filePollTimer) {
      clearInterval(_filePollTimer); _filePollTimer = null;
    }
    _setFileProgress(0, 'stopped');
  } catch (e) {
    addLog(`文件传输停止失败: ${e.message}`, 'File');
    showToast(e.message, '#f56c6c');
  }
});

// ---------------------------------------------------------------------------
// 网络 / 语音 / 物模型 — 0x30–0x44 / 0x09
// ---------------------------------------------------------------------------
function _netLog(text) {
  const el = document.getElementById('netvoice-log');
  if (!el) { addLog(text, 'Net'); return; }
  el.innerHTML += `<div>${escapeHtml(text)}</div>`;
  el.scrollTop = el.scrollHeight;
  addLog(text, 'Net');
}

document.getElementById('btn-cfgnet-set')?.addEventListener('click', async () => {
  if (!(await _requirePythonSerial())) return;
  const cfg_type = parseInt(document.getElementById('cfgnet-type')?.value || '0', 10);
  const ssid = document.getElementById('cfgnet-ssid')?.value || '';
  const passwd = document.getElementById('cfgnet-passwd')?.value || '';
  _netLog(`>> CMD 0x30 SETCFGNET type=${cfg_type} ssid=${ssid}`);
  try {
    const r = await apiPost('/api/device/net/set_cfgnet', { cfg_type, ssid, passwd });
    _netLog(`<< ACK device_id=0x${(r.device_id >>> 0).toString(16)} cmd=0x${r.cmd.toString(16)}`);
    showToast('配网模式已设置', '#67c23a');
  } catch (e) {
    _netLog(`<< 失败: ${e.message}`);
    showToast(e.message, '#f56c6c');
  }
});

document.getElementById('btn-netinfo-get')?.addEventListener('click', async () => {
  if (!(await _requirePythonSerial())) return;
  _netLog('>> CMD 0x31 GET_NETINFO');
  try {
    const r = await apiPost('/api/device/net/get_info', {});
    const ssidEl = document.getElementById('netinfo-ssid');
    const ipEl = document.getElementById('netinfo-ip');
    const gwEl = document.getElementById('netinfo-gw');
    const maskEl = document.getElementById('netinfo-mask');
    if (ssidEl) ssidEl.value = r.ssid || '';
    if (ipEl) ipEl.value = r.ip || '';
    if (gwEl) gwEl.value = r.gateway || r.gw || '';
    if (maskEl) maskEl.value = r.netmask || r.mask || '';
    _netLog(`<< ssid=${r.ssid} ip=${r.ip} gw=${r.gateway || r.gw} mask=${r.netmask || r.mask}`);
  } catch (e) {
    _netLog(`<< 失败: ${e.message}`);
    showToast(e.message, '#f56c6c');
  }
});

async function _voiceSwitch(on) {
  if (!(await _requirePythonSerial())) return;
  _netLog(`>> CMD 0x40 SET_VOICE_SWITCH on=${on}`);
  try {
    const r = await apiPost('/api/device/voice/set_switch', { on });
    _netLog(`<< ACK cmd=0x${r.cmd.toString(16)} on=${r.on}`);
    showToast(`语音开关已设置 on=${r.on}`, '#67c23a');
  } catch (e) {
    _netLog(`<< 失败: ${e.message}`);
    showToast(e.message, '#f56c6c');
  }
}
document.getElementById('btn-voice-on')?.addEventListener('click', () => _voiceSwitch(1));
document.getElementById('btn-voice-off')?.addEventListener('click', () => _voiceSwitch(0));

document.getElementById('btn-voice-volume-set')?.addEventListener('click', async () => {
  if (!(await _requirePythonSerial())) return;
  const volume = parseInt(document.getElementById('voice-volume')?.value || '0', 10) || 0;
  _netLog(`>> CMD 0x41 SET_VOICE_VOLUME volume=${volume}`);
  try {
    const r = await apiPost('/api/device/voice/set_volume', { volume });
    _netLog(`<< ACK volume=${r.volume}`);
    showToast(`音量已设置 ${r.volume}`, '#67c23a');
  } catch (e) {
    _netLog(`<< 失败: ${e.message}`);
    showToast(e.message, '#f56c6c');
  }
});

document.getElementById('btn-voice-volume-get')?.addEventListener('click', async () => {
  if (!(await _requirePythonSerial())) return;
  _netLog('>> CMD 0x42 GET_VOICE_VOLUME');
  try {
    const r = await apiPost('/api/device/voice/get_volume', {});
    document.getElementById('voice-volume').value = r.volume;
    _netLog(`<< volume=${r.volume}`);
  } catch (e) {
    _netLog(`<< 失败: ${e.message}`);
    showToast(e.message, '#f56c6c');
  }
});

document.getElementById('btn-voice-stat')?.addEventListener('click', async () => {
  if (!(await _requirePythonSerial())) return;
  _netLog('>> CMD 0x43 GET_VOICE_STAT');
  try {
    const r = await apiPost('/api/device/voice/get_stat', {});
    const names = { 0: 'idle', 1: 'listening', 2: 'playing' };
    const label = names[r.state] || String(r.state);
    const el = document.getElementById('voice-stat-text');
    if (el) el.textContent = `${r.state} (${label})`;
    _netLog(`<< state=${r.state} (${label})`);
  } catch (e) {
    _netLog(`<< 失败: ${e.message}`);
    showToast(e.message, '#f56c6c');
  }
});

document.getElementById('btn-tts-send')?.addEventListener('click', async () => {
  if (!(await _requirePythonSerial())) return;
  const content = document.getElementById('tts-content')?.value || '';
  if (!content.trim()) { showToast('请输入 TTS 内容', '#e6a23c'); return; }
  _netLog(`>> CMD 0x44 TTS "${content}"`);
  try {
    const r = await apiPost('/api/device/voice/tts', { content, timeout: 2.0 });
    _netLog(`<< ACK cmd=0x${r.cmd.toString(16)}`);
    showToast('TTS 内容已发送', '#67c23a');
  } catch (e) {
    _netLog(`<< 失败: ${e.message}`);
    showToast(e.message, '#f56c6c');
  }
});

document.getElementById('btn-tsl-invoke')?.addEventListener('click', async () => {
  if (!(await _requirePythonSerial())) return;
  const content = document.getElementById('tsl-content')?.value || '';
  if (!content.trim()) { showToast('请输入物模型内容', '#e6a23c'); return; }
  _netLog(`>> CMD 0x09 TSL "${content}"`);
  try {
    const r = await apiPost('/api/device/tsl/invoke', { content, timeout: 2.0 });
    _netLog(`<< ACK cmd=0x${r.cmd.toString(16)} param=${r.param_hex || ''}`);
    showToast('物模型调用已发送', '#67c23a');
  } catch (e) {
    _netLog(`<< 失败: ${e.message}`);
    showToast(e.message, '#f56c6c');
  }
});

// ---------------------------------------------------------------------------
// 参数定时轮询
// ---------------------------------------------------------------------------
async function _syncParamPollUi() {
  try {
    const st = await apiGet('/api/device/param/poll/status');
    const el = document.getElementById('param-poll-status');
    if (!el) return;
    if (st?.enabled) {
      el.textContent = `轮询中 ${st.name} @${st.interval_ms}ms last=${st.last_value ?? '-'}`;
      el.className = 'status-text running';
    } else {
      el.textContent = '未轮询';
      el.className = 'status-text';
    }
  } catch (_) { /* backend down */ }
}

document.getElementById('btn-param-poll-start')?.addEventListener('click', async () => {
  if (!(await _requirePythonSerial())) return;
  const name = document.getElementById('param-poll-name')?.value.trim();
  const interval_ms = parseInt(document.getElementById('param-poll-interval')?.value || '1000', 10) || 1000;
  if (!name) { showToast('请输入参数名', '#e6a23c'); return; }
  try {
    await apiPost('/api/device/param/poll/start', { name, interval_ms });
    _paramLog(`>> 开始轮询 param ${name} 每 ${interval_ms}ms`);
    await _syncParamPollUi();
  } catch (e) {
    _paramLog(`<< 轮询启动失败: ${e.message}`);
    showToast(e.message, '#f56c6c');
  }
});

document.getElementById('btn-param-poll-stop')?.addEventListener('click', async () => {
  try {
    await apiPost('/api/device/param/poll/stop', {});
    _paramLog('>> 停止参数轮询');
    await _syncParamPollUi();
  } catch (e) {
    _paramLog(`<< 轮询停止失败: ${e.message}`);
    showToast(e.message, '#f56c6c');
  }
});

// ---------------------------------------------------------------------------
// 配网 Web 调试 — Keil / OpenOCD / 串口日志
// ---------------------------------------------------------------------------
function _wcLog(text) {
  const el = document.getElementById('wc-output');
  if (!el) { addLog(text, 'WebConfig'); return; }
  el.innerHTML += `<div>${escapeHtml(text)}</div>`;
  el.scrollTop = el.scrollHeight;
  addLog(text, 'WebConfig');
}

function _wcFillFromStatus(st) {
  const cfg = st?.cfg || {};
  const set = (id, val) => {
    const el = document.getElementById(id);
    if (el && val != null && val !== '') el.value = val;
  };
  set('wc-project-dir', cfg.project_dir);
  set('wc-keil', cfg.keil_uv4);
  set('wc-openocd', cfg.openocd_dir);
  set('wc-project-file', cfg.project_file_rel);
  set('wc-target', cfg.target_name);
  const lines = [
    `config: ${st.config_path || ''} loaded=${st.config_loaded}`,
    `Keil UV4: ${cfg.keil_uv4 || ''} exists=${st.keil_uv4_exists}`,
    `project: ${st.project_file || ''} exists=${st.project_exists}`,
    `hex: ${st.hex_file || ''}`,
    `OpenOCD: ${st.openocd_exe || ''} exists=${st.openocd_exists}`,
    `Makefile: ${st.makefile || ''} exists=${st.makefile_exists}`,
    `pyserial=${st.pyserial} can_build=${st.can_build} can_flash=${st.can_flash}`,
  ];
  const logEl = document.getElementById('wc-status-log');
  if (logEl) logEl.innerHTML = lines.map(l => `<div>${escapeHtml(l)}</div>`).join('');
}

async function _refreshWebconfigStatus() {
  const st = await apiGet('/api/device/webconfig/status');
  _wcFillFromStatus(st);
  return st;
}

async function _refreshWcPorts() {
  try {
    const info = await apiGet('/api/device/serial/ports');
    const select = document.getElementById('wc-serial-port');
    if (!select) return;
    select.innerHTML = '';
    (info.ports || []).forEach(p => {
      const opt = document.createElement('option');
      opt.value = p;
      opt.textContent = p;
      select.appendChild(opt);
    });
  } catch (e) {
    _wcLog(`刷新串口失败: ${e.message}`);
  }
}

/**
 * Collect webconfig form fields.
 * mode 'save'  → save:true, empty string means CLEAR the field
 * mode 'action'→ build/flash/log only (save:false, do not wipe config)
 */
function _wcCollect(mode) {
  const save = mode !== 'action';
  const s = (id) => {
    const el = document.getElementById(id);
    return el ? (el.value ?? '') : null;
  };
  const n = (id, def) => {
    const v = parseInt(s(id) || String(def), 10);
    return Number.isFinite(v) ? v : def;
  };
  const payload = {
    serial_port: s('wc-serial-port') || null,
    serial_baud: n('wc-serial-baud', 115200),
    log_seconds: n('wc-log-seconds', 30),
    save,
  };
  if (save) {
    // Keep raw values (including '') so the API can clear path fields.
    payload.project_dir = s('wc-project-dir');
    payload.keil_uv4 = s('wc-keil');
    payload.openocd_dir = s('wc-openocd');
    payload.project_file_rel = s('wc-project-file');
    payload.target_name = s('wc-target');
  }
  return payload;
}

document.getElementById('btn-wc-save')?.addEventListener('click', async () => {
  try {
    const body = _wcCollect('save');
    const r = await apiPost('/api/device/webconfig/status', body);
    _wcLog(`配置已保存: ${JSON.stringify(r.updated || {})}`);
    await _refreshWebconfigStatus();
    showToast('配置已保存', '#67c23a');
  } catch (e) {
    _wcLog(`保存配置失败: ${e.message}`);
    showToast(e.message, '#f56c6c');
  }
});

document.getElementById('btn-wc-refresh')?.addEventListener('click', async () => {
  try {
    await _refreshWebconfigStatus();
  } catch (e) {
    _wcLog(`刷新状态失败: ${e.message}`);
  }
});

document.getElementById('btn-wc-refresh-port')?.addEventListener('click', _refreshWcPorts);

document.getElementById('btn-wc-build')?.addEventListener('click', async () => {
  _wcLog('>> 编译开始 ...');
  try {
    const body = Object.assign(_wcCollect('action'), { timeout_sec: 300 });
    const r = await apiPost('/api/device/webconfig/build', body);
    _wcLog(`<< 编译成功 tool=${r.tool || 'keil'} hex=${r.hex || ''}`);
    (r.logs || []).slice(-20).forEach(l => _wcLog(l));
    showToast('编译成功', '#67c23a');
  } catch (e) {
    _wcLog(`<< 编译失败: ${e.message}`);
    const logs = e.logs || e.data?.logs || [];
    logs.slice(-20).forEach(l => _wcLog(l));
    showToast(e.message, '#f56c6c');
  }
});

document.getElementById('btn-wc-flash')?.addEventListener('click', async () => {
  _wcLog('>> 烧录开始 ...');
  try {
    const body = Object.assign(_wcCollect('action'), { timeout_sec: 120 });
    const r = await apiPost('/api/device/webconfig/flash', body);
    _wcLog(`<< 烧录成功 hex=${r.hex || ''}`);
    (r.logs || []).slice(-20).forEach(l => _wcLog(l));
    showToast('烧录成功', '#67c23a');
  } catch (e) {
    _wcLog(`<< 烧录失败: ${e.message}`);
    const logs = e.logs || e.data?.logs || [];
    logs.slice(-20).forEach(l => _wcLog(l));
    showToast(e.message, '#f56c6c');
  }
});

document.getElementById('btn-wc-log-start')?.addEventListener('click', async () => {
  try {
    const body = _wcCollect('action');
    body.serial_port = document.getElementById('wc-serial-port')?.value || null;
    const r = await apiPost('/api/device/webconfig/log/start', body);
    _wcLog(`<< 日志开始: port=${r.port} baud=${r.baud} seconds=${r.seconds} -> ${r.path}`);
    const el = document.getElementById('wc-log-status');
    if (el) el.textContent = `日志中 → ${r.path}`;
  } catch (e) {
    _wcLog(`<< 日志启动失败: ${e.message}`);
    showToast(e.message, '#f56c6c');
  }
});

document.getElementById('btn-wc-log-stop')?.addEventListener('click', async () => {
  try {
    const r = await apiPost('/api/device/webconfig/log/stop', {});
    _wcLog(`<< 日志已停止 path=${r.path || ''} bytes=${r.info?.bytes || 0}`);
    const el = document.getElementById('wc-log-status');
    if (el) el.textContent = '日志已停止';
  } catch (e) {
    _wcLog(`<< 日志停止失败: ${e.message}`);
    showToast(e.message, '#f56c6c');
  }
});

// ---------------------------------------------------------------------------
// data-action event delegation (replaces inline onclick attributes)
// ---------------------------------------------------------------------------
const ACTION_MAP = {
  goToProjects,
  importDataset: () => document.getElementById('btn-import-dataset')?.click(),
  hideDataPreview,
  toggleCollapse: (el) => toggleCollapse(el),
  goToAutomlLabels,
  chartZoomIn,
  chartZoomOut,
  chartFitAll,
  toggleChartMode,
  clearChartSelection,
  goToAutomlFeatures,
  goToAutomlTraining,
  goToAutomlExport,
};
document.addEventListener('click', (e) => {
  const target = e.target.closest('[data-action]');
  if (!target) return;
  const action = target.dataset.action;
  const handler = ACTION_MAP[action];
  if (handler) {
    handler(target, e);
  }
});

// ---------------------------------------------------------------------------
// 初始化
// ---------------------------------------------------------------------------
document.addEventListener('DOMContentLoaded', () => {
  addLog('BabyOS Studio 已启动');
  refreshPorts();
  restoreCollapseStates();

  // Keyboard accessibility for wizard steps (Enter/Space triggers onclick)
  document.querySelectorAll('.wizard-step').forEach(step => {
    step.addEventListener('keydown', (e) => {
      if (e.key === 'Enter' || e.key === ' ') {
        e.preventDefault();
        step.click();
      }
    });
  });

  // Fetch version from backend
  const versionEl = document.getElementById('version-text');
  if (versionEl) {
    apiGet('/api/version').then(info => {
      versionEl.textContent = info.version || 'v' + (window.APP_VERSION || '1.0.0');
    }).catch(() => {
      versionEl.textContent = 'v1.0.0';
    });
  }

  // 检查 Python 后端状态（带自动重试）
  let _pythonCheckRetries = 0;
  const _maxPythonRetries = 6; // 6 retries x 5s = 30s max
  const _retryInterval = 5000;
  function _checkPythonStatus() {
    window.electronAPI?.python.status().then(status => {
      pythonRunning = status.running;

      const el = document.getElementById('python-status');
      if (el) {
        el.textContent = status.running ? '运行中' : (_pythonCheckRetries < _maxPythonRetries ? '检测中...' : '未启动');
        el.className = status.running ? 'status-text running' : 'status-text';
      }

      if (status.running) {
        loadProjects();
        return; // Stop retrying
      }

      // Auto-retry if backend not yet started
      _pythonCheckRetries++;
      if (_pythonCheckRetries < _maxPythonRetries) {
        setTimeout(_checkPythonStatus, _retryInterval);
      }
    }).catch(err => {
      debug(`检查 Python 状态失败: ${err.message}`);
      _pythonCheckRetries++;
      if (_pythonCheckRetries < _maxPythonRetries) {
        setTimeout(_checkPythonStatus, _retryInterval);
      }
    });
  }
  _checkPythonStatus();

  // 监听 Python 后端日志
  window.electronAPI?.python.onLog((msg) => {
    addLog(msg, 'Python');
  });

  // 绑定 AutoML 按钮事件
  const bindBtn = (id, handler) => {
    const el = document.getElementById(id);
    if (el) {
      el.addEventListener('click', handler);
    }
  };

  bindBtn('btn-create-project', showCreateProjectForm);
  bindBtn('btn-confirm-create', createProject);
  bindBtn('btn-cancel-create', hideCreateProjectForm);
  bindBtn('btn-clear-all-projects', clearAllProjects);

  // 项目名称输入框支持 Enter 键创建
  document.getElementById('project-name')?.addEventListener('keydown', e => {
    if (e.key === 'Enter') createProject();
  });
  bindBtn('btn-import-project', importProject);
  bindBtn('btn-import-dataset', importDataset);
  bindBtn('btn-download-sample', downloadSampleCSV);
  bindBtn('btn-add-label', addLabel);
  bindBtn('btn-add-segment', addSegmentFromForm);
  bindBtn('btn-run-features', runFeatureEngine);
  bindBtn('btn-start-training', startTraining);
  bindBtn('btn-stop-training', stopTraining);
  bindBtn('btn-export-project', exportProject);

  // 波形图 / 频域控件绑定 (原 inline onchange，现统一用 addEventListener)
  document.getElementById('chart-file-select')?.addEventListener('change', onChartFileChange);
  document.getElementById('chart-start')?.addEventListener('change', onChartRangeChange);
  document.getElementById('chart-end')?.addEventListener('change', onChartRangeChange);
  document.getElementById('freq-enabled')?.addEventListener('change', toggleFreqFeatures);

  // 特征参数实时联动
  const srEl = document.getElementById('feat-sampling-rate');
  const wsEl = document.getElementById('feat-window-s');
  const stepEl = document.getElementById('feat-step');
  if (srEl) srEl.addEventListener('input', updateWindowInfo);
  if (wsEl) wsEl.addEventListener('input', updateWindowInfo);
  // 步进校验：用户手动设步进 < 窗口点数/10 时提示
  // _stepAutoAdjusting 标记防止 updateWindowInfo 自动调整时触发重复 toast
  window._stepAutoAdjusting = false;
  if (stepEl) {
    stepEl.addEventListener('change', () => {
      if (window._stepAutoAdjusting) return;
      const sr = parseFloat(document.getElementById('feat-sampling-rate')?.value) || _projectSamplingRate;
      const ws = parseFloat(document.getElementById('feat-window-s')?.value) || 2.0;
      let n = Math.max(2, Math.round(sr * ws));
      if (n % 2 !== 0) n += 1;  // 镜像 effective_n() 偶数对齐
      const minStep = Math.max(1, Math.floor(n / 10));
      const curStep = parseInt(stepEl.value);
      if (isNaN(curStep) || curStep < 1) {
        showToast('步进最小为 1', '#e6a23c');
      } else if (curStep < minStep) {
        const minStepSec = (minStep / sr).toFixed(2);
        showToast(`步进不允许，最小为窗口点数的 1/10（即 ${minStep} 点 ≈ ${minStepSec}s）`, '#e6a23c');
      }
    });
  }

  // 特征复选框变化时自动保存配置 (debounced to avoid rapid-fire API calls)
  let _saveFeatureDebounce = null;
  const debouncedSaveFeatureConfig = () => {
    if (_saveFeatureDebounce) clearTimeout(_saveFeatureDebounce);
    _saveFeatureDebounce = setTimeout(async () => {
      try {
        await saveFeatureConfig();
        // Use a less intrusive indicator for auto-save success
        const resultEl = document.getElementById('feature-result');
        if (resultEl) {
          resultEl.innerHTML = '<span style="color:#67c23a;font-size:12px;">配置已自动保存</span>';
          setTimeout(() => { resultEl.innerHTML = ''; }, 2000);
        }
      } catch (e) {
        showToast('配置保存失败: ' + e.message, '#f56c6c');
      }
    }, 300);
  };
  document.querySelectorAll('#time-features input[type="checkbox"], #freq-features input[type="checkbox"]').forEach(cb => {
    cb.addEventListener('change', debouncedSaveFeatureConfig);
  });
  // Categorized feature panel checkboxes (delegated since dynamically created)
  document.getElementById('feature-categories-container')?.addEventListener('change', (e) => {
    if (e.target.classList.contains('feat-cb') || e.target.classList.contains('feat-ch-cb') || e.target.classList.contains('feat-ch-select-all')) {
      debouncedSaveFeatureConfig();
    }
  });
  // Table mode column checkboxes (delegated)
  document.getElementById('table-feature-columns')?.addEventListener('change', (e) => {
    if (e.target.classList.contains('table-col-cb')) debouncedSaveFeatureConfig();
  });
  document.getElementById('table-select-all')?.addEventListener('change', debouncedSaveFeatureConfig);

  // 标注名称输入框支持 Enter 键添加
  document.getElementById('label-name-input')?.addEventListener('keydown', e => {
    if (e.key === 'Enter') addLabel();
  });

  // 分段结束行输入框支持 Enter 键添加
  document.getElementById('seg-end')?.addEventListener('keydown', e => {
    if (e.key === 'Enter') addSegmentFromForm();
  });

  // 全局 Escape 键：关闭预览/清除选区/关闭创建表单
  document.addEventListener('keydown', e => {
    if (e.key === 'Escape') {
      // 关闭数据预览
      const preview = document.getElementById('data-preview-section');
      if (preview && preview.style.display !== 'none') {
        hideDataPreview();
        return;
      }
      // 清除波形图选区
      if (Waveform.selectStart >= 0) {
        clearChartSelection();
        return;
      }
      // 关闭创建项目表单
      const form = document.getElementById('create-project-form');
      if (form && form.classList.contains('open')) {
        const nameInput = document.getElementById('project-name');
        if (nameInput && nameInput.value.trim()) {
          showConfirm('项目名称已输入，确定要放弃吗？').then(ok => {
            if (ok) hideCreateProjectForm();
          });
        } else {
          hideCreateProjectForm();
        }
        return;
      }
    }
  });

  // 刷新系统状态按钮
  document.getElementById('btn-refresh-status')?.addEventListener('click', () => {
    const el = document.getElementById('python-status');
    if (el) el.textContent = '检测中...';
    window.electronAPI?.python.status().then(status => {
      pythonRunning = status.running;
      if (el) {
        el.textContent = status.running ? '运行中' : '未启动';
        el.className = status.running ? 'status-text running' : 'status-text';
      }
      if (status.running) loadProjects();
    }).catch(err => {
      if (el) {
        el.textContent = '检测失败';
        el.className = 'status-text';
      }
    });
    // Refresh version display
    const versionEl = document.getElementById('version-text');
    if (versionEl) {
      apiGet('/api/version').then(info => {
        versionEl.textContent = info.version || 'v1.0.0';
      }).catch(() => {});
    }
    refreshPorts();
    addLog('已刷新系统状态', 'System');
  });

  // 日志面板折叠/展开
  const logHeader = document.getElementById('log-header');
  const logPanel = document.getElementById('log-panel');
  const logToggleIcon = document.getElementById('log-toggle-icon');
  if (logHeader && logPanel) {
    // 恢复折叠状态
    try {
      if (localStorage.getItem('log-panel-collapsed') === '1') {
        logPanel.classList.add('collapsed');
        if (logToggleIcon) logToggleIcon.textContent = '▸';
      }
    } catch (e) {}
    logHeader.addEventListener('click', () => {
      const isCollapsed = logPanel.classList.toggle('collapsed');
      if (logToggleIcon) logToggleIcon.textContent = isCollapsed ? '▸' : '▾';
      try { localStorage.setItem('log-panel-collapsed', isCollapsed ? '1' : '0'); } catch (e) {}
    });
  }
});
