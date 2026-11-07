(function () {
  'use strict';

  var TOKEN = new URLSearchParams(location.search).get('k') || '';
  var state = { path: '', parent: '', roots: [], items: [], busy: false };
  var queue = [];

  function $(id) { return document.getElementById(id); }

  function api(path, params) {
    var url = path + '?';
    var parts = [];
    if (params) {
      Object.keys(params).forEach(function (key) {
        var value = params[key];
        if (value !== undefined && value !== null) {
          parts.push(encodeURIComponent(key) + '=' + encodeURIComponent(value));
        }
      });
    }
    if (TOKEN) parts.push('k=' + encodeURIComponent(TOKEN));
    return url + parts.join('&');
  }

  function requestJson(path, params, options) {
    return fetch(api(path, params), options || { cache: 'no-store' }).then(function (res) {
      return res.json().catch(function () {
        return { ok: false, error: 'HTTP ' + res.status };
      }).then(function (data) {
        if (!res.ok || !data.ok) throw new Error(data.error || ('HTTP ' + res.status));
        return data;
      });
    });
  }

  function postJson(path, body) {
    return requestJson(path, null, {
      method: 'POST',
      cache: 'no-store',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body || {})
    });
  }

  function toast(message) {
    var box = $('toast');
    box.textContent = message;
    box.hidden = false;
    clearTimeout(toast.timer);
    toast.timer = setTimeout(function () { box.hidden = true; }, 2600);
  }

  function formatSize(bytes) {
    if (!bytes) return '0 B';
    var units = ['B', 'KB', 'MB', 'GB', 'TB'];
    var index = 0;
    var value = bytes;
    while (value >= 1024 && index < units.length - 1) { value /= 1024; index += 1; }
    return (index === 0 ? value : value.toFixed(value >= 10 ? 0 : 1)) + ' ' + units[index];
  }

  function formatTime(seconds) {
    if (!seconds) return '';
    var date = new Date(seconds * 1000);
    var pad = function (n) { return n < 10 ? '0' + n : '' + n; };
    return date.getFullYear() + '-' + pad(date.getMonth() + 1) + '-' + pad(date.getDate()) +
           ' ' + pad(date.getHours()) + ':' + pad(date.getMinutes());
  }

  function renderChips() {
    var box = $('roots');
    box.innerHTML = '';
    state.roots.forEach(function (root) {
      var chip = document.createElement('button');
      chip.type = 'button';
      chip.className = 'chip' + (state.path === root.path ? ' active' : '');
      chip.textContent = root.name || root.path;
      chip.addEventListener('click', function () { load(root.path); });
      box.appendChild(chip);
    });
  }

  function renderList() {
    var list = $('list');
    list.innerHTML = '';
    $('empty').hidden = state.items.length > 0;
    $('crumb').textContent = state.path || '';

    state.items.forEach(function (item) {
      var row = document.createElement('li');
      row.className = 'item';

      var icon = document.createElement('div');
      icon.className = 'item-icon';
      icon.textContent = item.dir ? '目录' : '文件';
      if (!item.dir) icon.style.background = '#f1efe8';

      var main = document.createElement('div');
      main.className = 'item-main';

      var name = document.createElement('div');
      name.className = 'item-name';
      name.textContent = item.name;

      var meta = document.createElement('div');
      meta.className = 'item-meta';
      meta.textContent = item.dir ? formatTime(item.mtime) || '文件夹'
                                  : formatSize(item.size) + ' · ' + formatTime(item.mtime);

      main.appendChild(name);
      main.appendChild(meta);

      var actions = document.createElement('div');
      actions.className = 'item-actions';

      if (!item.dir) {
        var download = document.createElement('button');
        download.type = 'button';
        download.className = 'icon-btn';
        download.textContent = '下载';
        download.addEventListener('click', function (event) {
          event.stopPropagation();
          location.href = api('/api/download', { path: item.path });
        });
        actions.appendChild(download);
      }

      var rename = document.createElement('button');
      rename.type = 'button';
      rename.className = 'icon-btn';
      rename.textContent = '改名';
      rename.addEventListener('click', function (event) {
        event.stopPropagation();
        var next = prompt('新的名称', item.name);
        if (!next || next === item.name) return;
        postJson('/api/rename', { path: item.path, name: next })
          .then(loadCurrent)
          .catch(function (err) { toast(err.message); });
      });
      actions.appendChild(rename);

      var remove = document.createElement('button');
      remove.type = 'button';
      remove.className = 'icon-btn btn danger';
      remove.textContent = '删除';
      remove.addEventListener('click', function (event) {
        event.stopPropagation();
        var question = item.dir
          ? '删除目录「' + item.name + '」及其全部内容？'
          : '删除文件「' + item.name + '」？';
        if (!confirm(question)) return;
        postJson('/api/delete', { path: item.path, recursive: true })
          .then(function () { toast('已删除'); loadCurrent(); })
          .catch(function (err) { toast(err.message); });
      });
      actions.appendChild(remove);

      row.appendChild(icon);
      row.appendChild(main);
      row.appendChild(actions);

      row.addEventListener('click', function () {
        if (item.dir) load(item.path);
        else location.href = api('/api/download', { path: item.path });
      });

      list.appendChild(row);
    });
  }

  function load(path) {
    $('loading').hidden = false;
    return requestJson('/api/list', { path: path })
      .then(function (data) {
        state.path = data.path;
        state.parent = data.parent || '';
        state.items = data.items || [];
        $('usage').textContent = data.total
          ? '可用 ' + formatSize(data.free) + ' / ' + formatSize(data.total)
          : '';
        renderChips();
        renderList();
      })
      .catch(function (err) {
        toast(err.message);
      })
      .finally(function () {
        $('loading').hidden = true;
      });
  }

  function loadCurrent() {
    return load(state.path);
  }

  function loadRoots() {
    return requestJson('/api/roots')
      .then(function (data) {
        state.roots = data.roots || [];
        if (!state.roots.length) {
          toast('设备上没有可访问的目录');
          return;
        }
        var params = new URLSearchParams(location.search);
        var start = params.get('path');
        var target = state.roots[0].path;
        if (start) {
          target = start;
        } else if (params.get('tab')) {
          var match = state.roots.filter(function (item) { return item.name === params.get('tab'); });
          if (match.length) target = match[0].path;
        }
        return load(target);
      })
      .catch(function (err) { toast(err.message); });
  }

  function uploadNext() {
    if (!queue.length) {
      $('progress').hidden = true;
      $('progress-fill').style.width = '0%';
      loadCurrent();
      return;
    }

    var file = queue.shift();
    var sent = 0;

    var xhr = new XMLHttpRequest();
    xhr.open('POST', api('/api/upload', { path: state.path, name: file.name }));
    xhr.upload.onprogress = function (event) {
      if (!event.lengthComputable) return;
      var percent = Math.round(event.loaded * 100 / event.total);
      $('progress-fill').style.width = percent + '%';
      $('progress-text').textContent = '上传 ' + file.name + ' ' + percent + '%';
      sent = event.loaded;
    };
    xhr.onerror = function () {
      toast('上传失败：' + file.name);
      uploadNext();
    };
    xhr.onload = function () {
      var data = null;
      try { data = JSON.parse(xhr.responseText); } catch (e) { data = null; }
      if (!data || !data.ok) {
        toast('上传失败：' + (data && data.error ? data.error : 'HTTP ' + xhr.status));
      } else {
        toast('已上传 ' + file.name + '（' + formatSize(sent || file.size) + '）');
      }
      queue.done += 1;
      uploadNext();
    };

    $('progress').hidden = false;
    $('progress-text').textContent = '准备上传 ' + file.name;
    xhr.send(file);
  }

  function startUpload(files) {
    if (!files || !files.length) return;
    if (!state.path) { toast('请先选择一个目录'); return; }
    queue = Array.from(files);
    queue.done = 0;
    uploadNext();
  }

  function bind() {
    $('btn-up').addEventListener('click', function () {
      if (state.parent) load(state.parent);
    });
    $('btn-refresh').addEventListener('click', loadCurrent);

    $('btn-upload').addEventListener('click', function () { $('file-input').click(); });
    $('file-input').addEventListener('change', function (event) {
      startUpload(event.target.files);
      event.target.value = '';
    });

    $('btn-mkdir').addEventListener('click', function () {
      var name = prompt('新文件夹名称');
      if (!name) return;
      postJson('/api/mkdir', { path: state.path, name: name })
        .then(function () { toast('已创建'); loadCurrent(); })
        .catch(function (err) { toast(err.message); });
    });
  }

  document.addEventListener('DOMContentLoaded', function () {
    bind();
    loadRoots();
  });
})();
