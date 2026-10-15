'use strict';

(function () {
  const MARKER = '.import-complete.json';
  const DATA_ROOT = '/hp2data';
  const USER_ROOT = '/hp2home/harry-potter-2/User';
  const HOME_PREFIX = '/hp2home';
  const LOG_LINES = 20000;
  const params = new URLSearchParams(location.search);
  const devMode = params.has('devdata');
  const smokeMode = ['shell', 'boot', 'launcher'].indexOf(params.get('smoke')) >= 0
    ? params.get('smoke') : null;
  const androidSingleThreaded = params.get('hp2AndroidSingleThreaded') === '1';
  const canvas = document.getElementById('canvas');
  const statusEl = document.getElementById('status');
  const logEl = document.getElementById('log');
  const logToggle = document.getElementById('log-toggle');
  const stageEl = document.getElementById('stage');
  const fullscreenToggle = document.getElementById('fullscreen-toggle');
  const COOP_STATUS =
    'This page must be served with COOP/COEP headers (python3 Build/serve_web.py).';
  let currentModule = null;
  let started = false;
  let syncInFlight = false;
  let smokeReported = false;
  let nextRequestId = 1;
  const pendingPickers = new Map();
  let folderOverlay = null;

  function setStatus(text) { statusEl.textContent = text; }

  function logLine(text) {
    const atBottom = logEl.scrollTop + logEl.clientHeight >= logEl.scrollHeight - 4;
    logEl.textContent += text + '\n';
    const lines = logEl.textContent.split('\n');
    if (lines.length > LOG_LINES) logEl.textContent = lines.slice(-LOG_LINES).join('\n');
    if (atBottom) logEl.scrollTop = logEl.scrollHeight;
  }

  function logToBoth(text) {
    logLine(text);
    console.log(text);
    if (smokeMode === 'launcher' && String(text).indexOf('HP2Launcher: ready') >= 0) {
      reportSmoke('ready', 'in-engine launcher rendered its first frame');
    }
  }

  function reportSmoke(stage, detail) {
    if (!smokeMode || smokeReported) return;
    smokeReported = true;
    const report = { mode: smokeMode, stage: stage, detail: detail,
      frames: 0, log: logEl.textContent.split('\n').slice(-400).join('\n') };
    if (smokeMode === 'launcher' && stage === 'ready') {
      try { report.screenshot = canvas.toDataURL('image/png'); }
      catch (err) { report.screenshotError = String(err); }
    }
    fetch('/__hp2_smoke', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(report),
    }).catch(function (err) { console.error('smoke report failed: ' + err); });
  }

  const CANONICAL_ROOTS = {
    system: 'System', maps: 'Maps', textures: 'Textures', sounds: 'Sounds',
    music: 'music', help: 'Help',
  };
  const SUFFIX = 'System/Default.ini';

  function normalizeDataPath(rel) {
    const parts = String(rel).replace(/\\/g, '/').split('/').filter(Boolean);
    if (!parts.length) return null;
    if (parts.length === 1) {
      return parts[0] === 'overlay-manifest.json' || parts[0] === 'overlay-checksums.txt'
        ? parts[0] : null;
    }
    const root = parts[0].toLowerCase();
    if (parts.length === 2) {
      const canonical = CANONICAL_ROOTS[root];
      if (!canonical || parts[1].toLowerCase().endsWith('.scc')) return null;
      return canonical + '/' + parts[1];
    }
    if (root === 'system' && parts[1].toLowerCase() === 'cutscenes') {
      return ['System', 'Cutscenes'].concat(parts.slice(2)).join('/');
    }
    return null;
  }

  function findDataRoot(paths) {
    const normalized = paths.map((path) => String(path).replace(/\\/g, '/'));
    const suffix = SUFFIX.toLowerCase();
    const matches = normalized.filter((path) => {
      const lower = path.toLowerCase();
      return lower === suffix || lower.endsWith('/' + suffix);
    }).sort((a, b) => a.length - b.length);
    if (!matches.length) throw new Error('No System/Default.ini found in the selected folder.');
    const shortest = matches[0];
    const root = shortest.length === suffix.length
      ? '' : shortest.slice(0, shortest.length - suffix.length - 1);
    return { root: root, relativize: (path) =>
      root && path.startsWith(root + '/') ? path.slice(root.length + 1) : path };
  }

  async function writeToOpfs(rootDir, relative, data) {
    const parts = relative.split('/');
    let dir = rootDir;
    for (let i = 0; i < parts.length - 1; ++i) {
      dir = await dir.getDirectoryHandle(parts[i], { create: true });
    }
    const handle = await dir.getFileHandle(parts[parts.length - 1], { create: true });
    const stream = await handle.createWritable();
    await stream.write(data);
    await stream.close();
  }

  async function clearOpfsDirectory(parent, name) {
    try { await parent.removeEntry(name, { recursive: true }); }
    catch (err) { if (!err || err.name !== 'NotFoundError') throw err; }
  }

  async function markDirectory(dir, files, bytes) {
    const marker = { files: files, bytes: bytes, importedAt: new Date().toISOString() };
    await writeToOpfs(dir, MARKER, new Blob([JSON.stringify(marker)], {
      type: 'application/json',
    }));
  }

  async function importEntries(entries) {
    const dataRoot = findDataRoot(entries.map((entry) => entry.path));
    const opfs = await navigator.storage.getDirectory();
    await clearOpfsDirectory(opfs, 'prototype');
    const target = await opfs.getDirectoryHandle('prototype', { create: true });
    let written = 0;
    let bytes = 0;
    for (const entry of entries) {
      const path = normalizeDataPath(dataRoot.relativize(entry.path));
      if (!path) continue;
      let data;
      try { data = await entry.read(); }
      catch (err) { throw new Error(entry.path + ': ' + err); }
      await writeToOpfs(target, path, data);
      ++written;
      bytes += data.byteLength;
      if ((written & 63) === 0) await new Promise((resolve) => setTimeout(resolve, 0));
    }
    await markDirectory(target, written, bytes);
    setStatus('Prototype data imported (' + written + ' files).');
    logToBoth('Imported prototype data: ' + written + ' files.');
  }

  async function hasMarker(opfs, kind) {
    try {
      const dir = await opfs.getDirectoryHandle(kind);
      await dir.getFileHandle(MARKER);
      return true;
    } catch (err) { return false; }
  }

  async function migrateLegacyPrototype(opfs) {
    if (await hasMarker(opfs, 'prototype')) return;
    let legacy;
    try { legacy = await opfs.getDirectoryHandle('hp2-data'); }
    catch (err) { if (err && err.name === 'NotFoundError') return; throw err; }
    let hasLegacyMarker = false;
    try { await legacy.getFileHandle(MARKER); hasLegacyMarker = true; }
    catch (err) { if (!err || err.name !== 'NotFoundError') throw err; }
    if (!hasLegacyMarker) return;
    await clearOpfsDirectory(opfs, 'prototype');
    const prototype = await opfs.getDirectoryHandle('prototype', { create: true });
    const files = await listOpfsDataFiles(legacy, '');
    let bytes = 0;
    for (const file of files) {
      const data = await file.handle.getFile().then((f) => f.arrayBuffer());
      await writeToOpfs(prototype, file.path, data);
      bytes += data.byteLength;
    }
    await markDirectory(prototype, files.length, bytes);
    await clearOpfsDirectory(opfs, 'hp2-data');
    logToBoth('Migrated existing browser data into the prototype source.');
  }

  async function listOpfsDataFiles(dir, prefix) {
    const files = [];
    for await (const [name, handle] of dir.entries()) {
      if (name === MARKER && !prefix) continue;
      if (handle.kind === 'directory') {
        files.push(...await listOpfsDataFiles(await dir.getDirectoryHandle(name), prefix + name + '/'));
      } else {
        files.push({ handle: handle, path: prefix + name });
      }
    }
    return files;
  }

  function mkdirTree(fs, path) {
    let current = '';
    for (const part of path.split('/').filter(Boolean)) {
      current += '/' + part;
      try { fs.mkdir(current); } catch (err) { if (!fs.analyzePath(current).exists) throw err; }
    }
  }

  function removeFsTree(fs, path) {
    let stat;
    try { stat = fs.stat(path); } catch (err) { return; }
    if (fs.isDir(stat.mode)) {
      for (const name of fs.readdir(path)) {
        if (name !== '.' && name !== '..') removeFsTree(fs, path + '/' + name);
      }
      fs.rmdir(path);
    } else fs.unlink(path);
  }

  async function copyOpfsTreeToFs(kind, m) {
    const opfs = await navigator.storage.getDirectory();
    const source = await opfs.getDirectoryHandle(kind);
    const files = await listOpfsDataFiles(source, '');
    const destination = DATA_ROOT + '/' + kind;
    removeFsTree(m.FS, destination);
    mkdirTree(m.FS, destination);
    for (let i = 0; i < files.length; ++i) {
      const file = files[i];
      const data = await file.handle.getFile().then((f) => f.arrayBuffer());
      const path = destination + '/' + file.path;
      mkdirTree(m.FS, path.slice(0, path.lastIndexOf('/')));
      m.FS.writeFile(path, new Uint8Array(data));
      if ((i & 63) === 63) await new Promise((resolve) => setTimeout(resolve, 0));
    }
    return files.length;
  }

  async function copyFsTreeToOpfs(m, sourcePath, kind) {
    const fs = m.FS;
    const files = [];
    function visit(directory, relative) {
      for (const name of fs.readdir(directory)) {
        if (name === '.' || name === '..') continue;
        const path = directory + '/' + name;
        const rel = relative ? relative + '/' + name : name;
        const stat = fs.stat(path);
        if (fs.isDir(stat.mode)) visit(path, rel);
        else files.push({ path: rel, data: fs.readFile(path) });
      }
    }
    visit(sourcePath, '');
    const opfs = await navigator.storage.getDirectory();
    await clearOpfsDirectory(opfs, kind);
    const destination = await opfs.getDirectoryHandle(kind, { create: true });
    let bytes = 0;
    for (let i = 0; i < files.length; ++i) {
      const file = files[i];
      await writeToOpfs(destination, file.path, file.data);
      bytes += file.data.byteLength;
      if ((i & 31) === 31) await new Promise((resolve) => setTimeout(resolve, 0));
    }
    await markDirectory(destination, files.length, bytes);
  }

  function bridgeComplete(id, ok, payload) {
    const m = currentModule;
    if (!m || typeof m._hp2_web_bridge_done !== 'function') {
      logToBoth('Web bridge callback unavailable for request ' + id);
      return;
    }
    let pointer = 0;
    try {
      const bytes = new TextEncoder().encode(payload == null ? '' : String(payload));
      pointer = m._malloc(bytes.length + 1);
      if (!pointer) throw new Error('Could not allocate web bridge payload.');
      const offset = typeof pointer === 'bigint' ? Number(pointer) : pointer;
      if (!Number.isSafeInteger(offset) || offset < 0) {
        throw new Error('Web bridge pointer is outside JavaScript typed-array range.');
      }
      m.HEAPU8.set(bytes, offset);
      m.HEAPU8[offset + bytes.length] = 0;
      m._hp2_web_bridge_done(id, ok ? 1 : 0, pointer);
    } finally {
      if (pointer && typeof m._free === 'function') m._free(pointer);
    }
  }

  function nextId() { return nextRequestId++; }

  async function processFolderSelection(request, entries) {
    try {
      if (!entries || entries.length === 0) throw new Error('No files were found in the selected folder.');
      if (request.kind === 'prototype') {
        await importEntries(entries);
        bridgeComplete(request.id, true, '');
      } else {
        const m = currentModule;
        const source = '/hp2import/source';
        removeFsTree(m.FS, source);
        mkdirTree(m.FS, source);
        for (let i = 0; i < entries.length; ++i) {
          const entry = entries[i];
          const rel = String(entry.path).replace(/\\/g, '/');
          if (rel.split('/').some((part) => !part || part === '.' || part === '..')) {
            throw new Error('Unsafe file path: ' + rel);
          }
          const path = source + '/' + rel;
          mkdirTree(m.FS, path.slice(0, path.lastIndexOf('/')));
          m.FS.writeFile(path, new Uint8Array(await entry.read()));
          if ((i & 31) === 31) await new Promise((resolve) => setTimeout(resolve, 0));
        }
        bridgeComplete(request.id, true, source);
      }
    } catch (err) {
      bridgeComplete(request.id, false, String(err));
    }
  }

  function makeFolderOverlay() {
    if (folderOverlay) return folderOverlay;
    const overlay = document.createElement('div');
    overlay.id = 'hp2-folder-overlay';
    overlay.hidden = true;
    const message = document.createElement('p');
    const button = document.createElement('button');
    button.type = 'button';
    button.textContent = 'Choose folder…';
    const input = document.createElement('input');
    input.type = 'file';
    input.multiple = true;
    input.setAttribute('webkitdirectory', '');
    input.hidden = true;
    overlay.append(message, button, input);
    document.body.appendChild(overlay);
    button.addEventListener('click', function () { input.click(); });
    input.addEventListener('change', async function () {
      const request = overlayRequest;
      overlayRequest = null;
      overlay.hidden = true;
      if (!request) return;
      const selected = Array.from(input.files || []);
      await processFolderSelection(request, selected.map((file) => ({
        path: file.webkitRelativePath || file.name,
        read: () => file.arrayBuffer(),
      })));
      input.value = '';
    });
    input.addEventListener('cancel', function () {
      const request = overlayRequest;
      overlayRequest = null;
      overlay.hidden = true;
      input.value = '';
      if (request) bridgeComplete(request.id, false, 'Folder selection cancelled.');
    });
    let overlayRequest = null;
    folderOverlay = {
      show: function (request) {
        overlayRequest = request;
        message.textContent = request.kind === 'prototype'
          ? 'Choose the folder containing your prototype game data.'
          : 'Choose the folder containing your retail game files.';
        if (window.Hp2Android && typeof window.Hp2Android.pickFolder === 'function') {
          const key = String(request.id);
          pendingPickers.set(key, request);
          overlayRequest = null;
          overlay.hidden = true;
          try {
            window.Hp2Android.pickFolder(key, request.kind);
          } catch (err) {
            pendingPickers.delete(key);
            bridgeComplete(request.id, false, String(err));
          }
          return;
        }
        overlay.hidden = false;
        button.focus();
      },
    };
    return folderOverlay;
  }

  window.hp2AndroidFolderResult = async function (requestId, kind, files, error, importToken) {
    const key = String(requestId);
    const request = pendingPickers.get(key);
    if (!request) return;
    pendingPickers.delete(key);
    try {
      if (request.kind !== kind) throw new Error('Folder picker returned the wrong data type.');
      if (error) throw new Error(String(error));
      if (!/^[0-9a-f]{32}$/i.test(String(importToken || ''))) {
        throw new Error('Android returned an invalid folder token.');
      }
      const token = encodeURIComponent(String(importToken));
      let manifest = files;
      if (!Array.isArray(manifest)) {
        const response = await fetch('/__hp2_android_import/' + token + '/manifest',
          { cache: 'no-store' });
        if (!response.ok) throw new Error('Could not list the selected folder (HTTP ' +
          response.status + ').');
        manifest = await response.json();
      }
      const entries = manifest.map((file) => {
        const index = Number(file.index);
        if (!Number.isSafeInteger(index) || index < 0) {
          throw new Error('Invalid file index from Android folder picker.');
        }
        return {
          path: String(file.path || ''),
          read: async function () {
            const response = await fetch('/__hp2_android_import/' + token + '/' + index,
              { cache: 'no-store' });
            if (!response.ok) throw new Error('Could not read imported file ' + file.path +
              ' (HTTP ' + response.status + ').');
            return response.arrayBuffer();
          },
        };
      });
      await processFolderSelection(request, entries);
    } catch (err) {
      bridgeComplete(request.id, false, String(err));
    } finally {
      if (window.Hp2Android && typeof window.Hp2Android.releaseFolder === 'function') {
        window.Hp2Android.releaseFolder(key);
      }
    }
  };

  function exposeBridge(m) {
    m.hp2Web = {
      dataSourceStatus: function (requestId) {
        const id = requestId === undefined ? nextId() : requestId;
        Promise.resolve().then(async function () {
          const opfs = await navigator.storage.getDirectory();
          await migrateLegacyPrototype(opfs);
          const result = {
            retail: await hasMarker(opfs, 'retail'),
            prototype: await hasMarker(opfs, 'prototype'),
          };
          bridgeComplete(id, true, JSON.stringify(result));
        }).catch((err) => bridgeComplete(id, false, String(err)));
      },
      pickFolder: function (requestId, kind) {
        if (typeof requestId === 'string') { kind = requestId; requestId = nextId(); }
        const id = requestId === undefined ? nextId() : requestId;
        if (kind !== 'retail' && kind !== 'prototype') {
          bridgeComplete(id, false, 'Unknown data source: ' + kind);
          return;
        }
        makeFolderOverlay().show({ id: id, kind: kind });
      },
      persistImport: function (requestId, sourcePath, destinationKind) {
        if (typeof requestId === 'string') {
          destinationKind = sourcePath;
          sourcePath = requestId;
          requestId = nextId();
        }
        const id = requestId === undefined ? nextId() : requestId;
        Promise.resolve().then(async function () {
          if (!currentModule || (destinationKind !== 'retail' && destinationKind !== 'prototype')) {
            throw new Error('Invalid import destination.');
          }
          await copyFsTreeToOpfs(currentModule, sourcePath, destinationKind);
          removeFsTree(currentModule.FS, sourcePath);
          removeFsTree(currentModule.FS, '/hp2import/source');
          bridgeComplete(id, true, '');
        }).catch((err) => bridgeComplete(id, false, String(err)));
      },
      stageDataSource: function (requestId, kind) {
        if (typeof requestId === 'string') { kind = requestId; requestId = nextId(); }
        const id = requestId === undefined ? nextId() : requestId;
        Promise.resolve().then(async function () {
          if (kind !== 'retail' && kind !== 'prototype') throw new Error('Unknown data source: ' + kind);
          const copied = await copyOpfsTreeToFs(kind, currentModule);
          if (!copied) throw new Error('The ' + kind + ' data source is empty.');
          bridgeComplete(id, true, DATA_ROOT + '/' + kind);
        }).catch((err) => bridgeComplete(id, false, String(err)));
      },
      syncUser: function (requestId) {
        const shouldReport = requestId !== undefined;
        const id = shouldReport ? requestId : 0;
        if (!currentModule || !currentModule.FS) {
          if (shouldReport) bridgeComplete(id, false, 'User filesystem is unavailable.');
          else logToBoth('IDBFS sync failed: user filesystem is unavailable.');
          return;
        }
        currentModule.FS.syncfs(false, function (err) {
          if (shouldReport) bridgeComplete(id, !err, err ? String(err) : '');
          else if (err) logToBoth('IDBFS sync failed: ' + err);
        });
      },
    };
  }

  function syncFilesystem() {
    if (!currentModule || !currentModule.FS) return;
    currentModule.FS.syncfs(false, (err) => { if (err) logToBoth('IDBFS sync failed: ' + err); });
  }

  function startPersistence(instance) {
    currentModule = instance;
    setInterval(function () {
      if (syncInFlight) return;
      syncInFlight = true;
      instance.FS.syncfs(false, function (err) {
        syncInFlight = false;
        if (err) logToBoth('IDBFS sync failed: ' + err);
      });
    }, 10000);
    document.addEventListener('visibilitychange', function () {
      if (document.visibilityState === 'hidden') syncFilesystem();
    });
  }

  function resumeAudioContexts() {
    const al = currentModule && currentModule.AL;
    if (!al) return;
    const contexts = [];
    if (al.currentCtx) contexts.push(al.currentCtx);
    for (const context of Object.values(al.contexts || {})) {
      if (contexts.indexOf(context) < 0) contexts.push(context);
    }
    for (const context of contexts) {
      if (context && context.audioCtx && context.audioCtx.state === 'suspended') {
        context.audioCtx.resume();
      }
    }
  }

  function finish(text) {
    syncFilesystem();
    document.body.classList.remove('running');
    setStatus(text);
    reportSmoke('failed', text);
  }

  async function entriesFromDevServer() {
    const response = await fetch('/dev-data/manifest.json', { cache: 'no-store' });
    if (!response.ok) throw new Error('dev data manifest unavailable (HTTP ' + response.status + ')');
    return (await response.json()).map((path) => ({
      path: path,
      read: async function () {
        const encoded = path.split('/').map(encodeURIComponent).join('/');
        const result = await fetch('/dev-data/' + encoded, { cache: 'no-store' });
        if (!result.ok) throw new Error('dev data fetch failed for ' + path + ' (HTTP ' + result.status + ')');
        return result.arrayBuffer();
      },
    }));
  }

  async function requestDirectBoot() {
    if (!params.has('map')) return;
    const map = params.get('map');
    if (!map) return;
    const opfs = await navigator.storage.getDirectory();
    const kind = await hasMarker(opfs, 'prototype') ? 'prototype' : 'retail';
    if (!await hasMarker(opfs, kind)) {
      throw new Error('No imported retail or prototype data is available for direct map boot.');
    }
    await copyOpfsTreeToFs(kind, currentModule);
    currentModule.FS.chdir(DATA_ROOT + '/' + kind + '/System');
    started = true;
    document.body.classList.add('running');
    setStatus('Starting ' + map + '…');
    resumeAudioContexts();
    canvas.addEventListener('pointerdown', resumeAudioContexts);
    canvas.getContext('webgl2', { preserveDrawingBuffer: true, alpha: false, depth: true, stencil: false, antialias: false });
    const args = [map, '-datadir=' + DATA_ROOT + '/' + kind, '-LOG'];
    logToBoth('Launching: ' + args.join(' '));
    currentModule.callMain(args);
  }

  // The browser keeps its view controls, but all data selection and launch
  // policy now belongs to the in-engine SDL/ImGui launcher.
  function syncFullscreenButton() {
    fullscreenToggle.textContent = document.fullscreenElement ? '⤡' : '⤢';
    fullscreenToggle.title = document.fullscreenElement ? 'Leave full screen (F)' : 'Full screen (F)';
  }
  fullscreenToggle.addEventListener('click', async function () {
    try {
      if (document.fullscreenElement) await document.exitFullscreen();
      else await document.documentElement.requestFullscreen();
    } catch (err) { logToBoth('Full screen was refused by the browser: ' + err); }
  });
  document.addEventListener('fullscreenchange', syncFullscreenButton);
  syncFullscreenButton();
  document.addEventListener('keydown', function (event) {
    if (event.key === 'f' || event.key === 'F') fullscreenToggle.click();
  });
  canvas.addEventListener('pointerdown', resumeAudioContexts);
  canvas.addEventListener('keydown', resumeAudioContexts);
  logToggle.addEventListener('click', function () { logEl.classList.toggle('open'); });

  const FONT_DIR = '/hp2fonts';
  const FONT_FILES = ['LiberationSans-Regular.ttf', 'LiberationSans-Bold.ttf',
    'LiberationMono-Regular.ttf', 'NotoSansSC-Regular.otf'];
  async function stageFonts(m) {
    mkdirTree(m.FS, FONT_DIR);
    for (const name of FONT_FILES) {
      try {
        const response = await fetch('fonts/' + name, { cache: 'force-cache' });
        if (!response.ok) throw new Error('HTTP ' + response.status);
        m.FS.writeFile(FONT_DIR + '/' + name, new Uint8Array(await response.arrayBuffer()));
      } catch (err) { logToBoth('Font not staged: ' + name + ' (' + err + ')'); }
    }
  }

  function createModule() {
    return createHP2Module({
      canvas: canvas,
      thisProgram: 'HarryPotter2',
      noExitRuntime: true,
      print: logToBoth,
      printErr: logToBoth,
      onHP2Exit: (code) => finish('Game exited (code ' + code + ').'),
      onExit: (code) => finish('Game exited (code ' + code + ').'),
      onAbort: (reason) => finish(reason ? 'Game crashed: ' + reason : 'Game exited.'),
      preRun: [function (m) {
        exposeBridge(m);
        m.addRunDependency('hp2-font');
        stageFonts(m).then(function () { m.removeRunDependency('hp2-font'); },
          function (err) {
            logToBoth('Font staging failed: ' + err);
            m.removeRunDependency('hp2-font');
          });
        m.ENV.XDG_DATA_HOME = HOME_PREFIX;
        mkdirTree(m.FS, USER_ROOT);
        m.FS.mount(m.FS.filesystems.IDBFS, {}, USER_ROOT);
        m.addRunDependency('hp2-idbfs');
        m.FS.syncfs(true, function (err) {
          if (err) logToBoth('IDBFS load failed: ' + err);
          m.removeRunDependency('hp2-idbfs');
        });
      }],
    });
  }

  (async function init() {
    if (!androidSingleThreaded && !self.crossOriginIsolated) {
      setStatus(COOP_STATUS);
      logToBoth('Web startup failed: ' + COOP_STATUS);
      reportSmoke('failed', COOP_STATUS);
      return;
    }
    if (!navigator.storage || !('getDirectory' in navigator.storage)) {
      const message = 'This browser has no private file storage, so the game cannot be installed here.';
      setStatus(message);
      logToBoth('Web startup failed: ' + message);
      reportSmoke('failed', 'no origin-private file system (OPFS)');
      return;
    }
    try {
      // Keep the most recently presented canvas pixels available to the
      // browser launcher smoke screenshot.
      if (!canvas.getContext('webgl2', {
        preserveDrawingBuffer: true, alpha: false, depth: true, stencil: false, antialias: false,
      })) throw new Error('WebGL2 is unavailable.');
      const instance = await createModule();
      currentModule = instance;
      exposeBridge(instance);
      startPersistence(instance);
      const opfs = await navigator.storage.getDirectory();
      await migrateLegacyPrototype(opfs);
      if (devMode && !await hasMarker(opfs, 'prototype')) {
        try { await importEntries(await entriesFromDevServer()); }
        catch (err) { setStatus('Dev data import failed: ' + err); }
      }
      if (smokeMode === 'shell') reportSmoke('ready', 'module instantiated');
      if (params.has('map')) {
        await requestDirectBoot();
      } else {
        setStatus('Starting launcher…');
        currentModule.callMain(['-LOG']);
      }
    } catch (err) {
      const message = err && err.message ? err.message : String(err);
      const details = err && err.stack ? String(err.stack) : message;
      setStatus('Loading the game failed: ' + message);
      logToBoth('Web startup failed: ' + details);
      reportSmoke('failed', 'startup failed: ' + message);
    }
  })();
})();
