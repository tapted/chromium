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

class FilesIndex extends HTMLUListElement {
  constructor() {
    super();
    fetchJson('/files').then((files) => {
      console.log(files);
    });
  }
}

customElements.define('files-index', FilesIndex, {'extends': 'ul'});
let rootFolder = new FilesIndex();

document.addEventListener('DOMContentLoaded', (event) => {
  document.querySelector('body').appendChild(rootFolder);
});
