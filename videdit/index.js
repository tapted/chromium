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

class FilesEntry extends HTMLElement {
  constructor(path, isDir = false) {
    super();
    this.path = path;
    this.isDir = isDir;
    this.index = null;

    let template = document.getElementById('files-entry-template').content;
    const shadowRoot =
        this.attachShadow({mode: 'open'}).appendChild(template.cloneNode(true));

    const suffix = isDir ? '/' : '';
    let anchor = document.createElement('a');
    anchor.slot = 'path';
    anchor.href = 'javascript:void(0)';
    anchor.innerHTML = path.split('/').pop() + suffix;
    this.appendChild(anchor);
    if (this.isDir) {
      anchor.addEventListener('click', (evt) => {
        this.toggleDir(evt);
      });
    } else {
      anchor.addEventListener('click', (evt) => {
        this.openFile(evt);
      })
    }
  }

  makeReq() {
    return {'path': this.path};
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
  openFile(evt) {
    fetchJson('/open', this.makeReq()).then((result) => {
      console.log(result);
    });
  }
}

class FilesIndex extends HTMLUListElement {
  constructor(req = {}) {
    super();
    this.slot = 'index';
    fetchJson('/files', req).then((files) => {
      console.log(files);
      for (let dir in files.folders)
        this.appendChild(new FilesEntry(files.folders[dir], true));
      for (let file in files.entries)
        this.appendChild(new FilesEntry(files.entries[file]));
    });
  }
}

customElements.define('files-index', FilesIndex, {'extends': 'ul'});
customElements.define('files-entry', FilesEntry);
let rootFolder = new FilesIndex();

document.addEventListener('DOMContentLoaded', (event) => {
  document.querySelector('body').appendChild(rootFolder);
});
