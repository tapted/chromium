'use strict';

function fetchJson(url, data = {}) {
  return fetch(url, {
           method: 'POST',
           headers: {
             'Content-Type': 'application/json; charset=utf-8',
           },
           body: JSON.stringify(data),
         })
      .then(response => response.json());
}

function fileSizeIEC(a, b, c, d, e) {
  return (b = Math, c = b.log, d = 1024, e = c(a) / c(d) | 0, a / b.pow(d, e))
             .toFixed(2) +
      ' ' + (e ? 'KMGTPEZY'[--e] + 'iB' : 'Bytes')
}

class Entry extends HTMLElement {
  constructor(entry, templateName) {
    super();
    this.path = entry.path;
    this.mtime = entry.mtime;
    this.size = entry.size;
    this.isDir = entry.isDir;

    let template = document.getElementById(templateName).content;
    const shadowRoot =
        this.attachShadow({mode: 'open'}).appendChild(template.cloneNode(true));
  }

  appendAnchor(suffix) {
    let anchor = document.createElement('a');
    anchor.slot = 'path';
    anchor.href = 'javascript:void(0)';
    anchor.innerHTML = this.path.split('/').pop() + suffix;
    this.appendChild(anchor);
    return anchor;
  }

  makeReq() {
    return {'path': this.path};
  }
}

class FileEntry extends Entry {
  constructor(entry) {
    super(entry, 'file-entry-template');
    this.index = null;
    this.appendAnchor('').addEventListener('click', (evt) => {
      this.openFile(evt);
    });

    const prefix = '<br>&middot; ';
    let sizeElement = document.createElement('span');
    sizeElement.slot = 'size';
    sizeElement.innerHTML = prefix + fileSizeIEC(this.size);
    this.appendChild(sizeElement);

    let timeElement = document.createElement('span');
    timeElement.slot = 'mtime';
    timeElement.innerHTML = prefix + (new Date(this.mtime)).toISOString();
    this.appendChild(timeElement);
  }

  openFile(evt) {
    fetchJson('/open', this.makeReq()).then((result) => {
      console.log(result);
    });
  }
}

class DirEntry extends Entry {
  constructor(entry) {
    super(entry, 'dir-entry-template');
    this.index = null;
    this.appendAnchor('/').addEventListener('click', (evt) => {
      this.toggleDir(evt);
    });
  }

  toggleDir(evt) {
    if (this.index) {
      this.removeChild(this.index);
      this.index = null;
    } else {
      this.index = new FilesIndex(this.makeReq());
      this.appendChild(this.index);
    }
  }
};

class FilesIndex extends HTMLUListElement {
  constructor(req = {}) {
    super();
    this.slot = 'index';
    fetchJson('/files', req).then((files) => {
      console.log(files);
      for (let dir in files.folders)
        this.appendChild(new DirEntry(files.folders[dir], true));
      for (let file in files.entries)
        this.appendChild(new FileEntry(files.entries[file]));
    });
  }
}

customElements.define('files-index', FilesIndex, {'extends': 'ul'});
customElements.define('dir-entry', DirEntry);
customElements.define('file-entry', FileEntry);
let rootFolder = new FilesIndex();

document.addEventListener('DOMContentLoaded', (event) => {
  document.querySelector('body').appendChild(rootFolder);
});
