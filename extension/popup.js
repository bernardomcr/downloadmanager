"use strict";

const api = globalThis.browser ?? globalThis.chrome;
const message = (name) => api.i18n.getMessage(name);
const decimal = (1.5).toLocaleString(api.i18n.getUILanguage()).charAt(1);

for (const element of document.querySelectorAll("[data-i18n]")) {
  element.textContent = message(element.dataset.i18n);
}

async function init() {
  const capture = document.getElementById("capture");
  const { captureEnabled } = await api.storage.local.get("captureEnabled");
  capture.checked = captureEnabled !== false;
  capture.addEventListener("change", () => api.storage.local.set({ captureEnabled: capture.checked }));

  api.runtime.sendMessage({ type: "ping" }).then((reply) => {
    document.getElementById("app-missing").hidden = Boolean(reply && reply.ok);
  });

  const [tab] = await api.tabs.query({ active: true, currentWindow: true });
  const media = tab ? await api.runtime.sendMessage({ type: "media", tabId: tab.id }) : [];
  const files = media.filter((item) => item.kind === "file");
  const list = document.getElementById("media");

  for (const item of files) {
    const row = document.createElement("li");
    const name = document.createElement("span");
    name.className = "name";
    name.textContent = item.name;
    name.title = item.url;
    const size = document.createElement("span");
    size.className = "size";
    size.textContent = DMLib.formatSize(item.size, decimal);
    const button = document.createElement("button");
    button.textContent = message("popupDownload");
    button.addEventListener("click", async () => {
      button.disabled = true;
      const reply = await api.runtime.sendMessage({ type: "download", url: item.url, pageUrl: tab.url || "" });
      button.textContent = message(reply && reply.ok ? "popupSent" : "popupFailed");
    });
    row.append(name, size, button);
    list.append(row);
  }

  document.getElementById("empty").hidden = files.length > 0;
  document.getElementById("streams").hidden = !media.some((item) => item.kind === "stream");
}

init();
