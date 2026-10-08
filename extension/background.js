// Download Manager: manda para o app os downloads que você inicia no navegador.
//
// Nenhum download fica de fora da organização:
//  1. Download http/https: o navegador pausa, o app recebe os mesmos cabeçalhos que o navegador mandou
//     (cookies, referer, autorização, tokens do site) e só quando o app
//     confirma que começou a receber dados o download do navegador é cancelado.
//  2. Se o app não consegue (link de uso único, app fechado...), o navegador continua. Formulários (POST)
//     ficam direto com o navegador.
//  3. Todo download que terminar pelo navegador é "adotado": o app move o arquivo para a pasta
//     organizada e mostra em Concluídos.
"use strict";

if (typeof importScripts === "function" && typeof DMLib === "undefined") importScripts("lib.js");

const api = globalThis.browser ?? globalThis.chrome;
const HOST = "com.bernardomcr.downloadmanager";
const STATUS_INTERVAL_MS = 1000;
const STATUS_TIMEOUT_MS = 10 * 60 * 1000;  // diálogo "Adicionar" esquecido aberto: devolve ao navegador

// Últimos eventos, para diagnosticar um download que não foi capturado (fica só na memória da sessão).
async function logEvent(text) {
  try {
    const { log = [] } = await api.storage.session.get("log");
    log.push(`${new Date().toISOString()} ${text}`);
    await api.storage.session.set({ log: log.slice(-30) });
  } catch {}
}

function sendNative(message) {
  return new Promise((resolve) => {
    try {
      const result = api.runtime.sendNativeMessage(HOST, message, (response) => {
        if (api.runtime.lastError) resolve(null);
        else resolve(response || null);
      });
      if (result && typeof result.then === "function") result.then(resolve, () => resolve(null));
    } catch {
      resolve(null);
    }
  });
}

async function isCaptureEnabled() {
  const { captureEnabled } = await api.storage.local.get("captureEnabled");
  return captureEnabled !== false;
}

async function cookiesFor(url) {
  try {
    return DMLib.cookieHeader(await api.cookies.getAll({ url }));
  } catch {
    return "";
  }
}

// --- Pedidos recentes: o app repete exatamente os cabeçalhos que o navegador mandou ---

const recentRequests = new DMLib.RecentRequests();

function rememberRequest(details) {
  if (details.tabId === undefined || !DMLib.isWebUrl(details.url)) return;
  recentRequests.remember(details.url, { method: details.method, headers: details.requestHeaders || [] });
}

const requestFilter = { urls: ["<all_urls>"], types: ["main_frame", "sub_frame", "other", "xmlhttprequest", "media", "object"] };
try {
  // Chrome/Edge só mostram Cookie e Referer com "extraHeaders".
  api.webRequest.onSendHeaders.addListener(rememberRequest, requestFilter, ["requestHeaders", "extraHeaders"]);
} catch {
  api.webRequest.onSendHeaders.addListener(rememberRequest, requestFilter, ["requestHeaders"]);
}

async function buildRequest(url, { fileName = "", referrer = "", source = "capture", sent = null } = {}) {
  const headers = sent ? DMLib.forwardableHeaders(sent.headers) : [];
  const has = (name) => headers.some((h) => h.name.toLowerCase() === name);
  const userAgent = (headers.find((h) => h.name.toLowerCase() === "user-agent") || {}).value || navigator.userAgent;
  return {
    type: "add",
    token: crypto.randomUUID(),
    url,
    fileName,
    referrer: has("referer") || !DMLib.isWebUrl(referrer) ? "" : referrer,
    cookies: has("cookie") ? "" : await cookiesFor(url),
    userAgent,
    headers: headers.filter((h) => h.name.toLowerCase() !== "user-agent"),
    source,
  };
}

// --- Downloads que o navegador termina: o app adota o arquivo ---

async function adopt(item) {
  if (!item || !item.filename) return;
  const reply = await sendNative({ type: "adopt", path: item.filename, url: item.finalUrl || item.url || "" });
  await logEvent(`adotar ${item.id}: ${reply && reply.ok ? "ok" : (reply && reply.error) || "sem resposta"}`);
}

async function stopWatching(downloadId) {
  const { adopt: watched = [] } = await api.storage.session.get("adopt");
  if (!watched.includes(downloadId)) return false;
  await api.storage.session.set({ adopt: watched.filter((id) => id !== downloadId) });
  return true;
}

async function watchForAdoption(downloadId) {
  const { adopt: watched = [] } = await api.storage.session.get("adopt");
  if (!watched.includes(downloadId)) await api.storage.session.set({ adopt: [...watched, downloadId] });
  // Arquivos pequenos podem terminar antes de começarmos a vigiar.
  const [item] = await api.downloads.search({ id: downloadId });
  if (item && item.state !== "in_progress" && (await stopWatching(downloadId)) && item.state === "complete") {
    await adopt(item);
  }
}

api.downloads.onChanged.addListener(async (delta) => {
  if (!delta.state || delta.state.current === "in_progress") return;
  if (!(await stopWatching(delta.id)) || delta.state.current !== "complete") return;
  const [item] = await api.downloads.search({ id: delta.id });
  await adopt(item);
});

// --- Captura ---

function wait(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function giveBack(downloadId) {
  try {
    await api.downloads.resume(downloadId);
  } catch {
    // Já estava baixando (ou terminou): segue normal.
  }
  await watchForAdoption(downloadId);
}

async function dropBrowserCopy(downloadId) {
  try {
    await api.downloads.cancel(downloadId);
  } catch {}
  try {
    await api.downloads.erase({ id: downloadId });
  } catch {}
}

api.downloads.onCreated.addListener(async (item) => {
  if (!(await isCaptureEnabled()) || item.incognito) return;

  if (!DMLib.shouldCapture(item)) {
    await logEvent(`não capturado (${item.url.slice(0, 40)}): fica com o navegador`);
    // blob:, data: e afins só o navegador consegue baixar; o app organiza quando terminar.
    if (!item.byExtensionId) await watchForAdoption(item.id);
    return;
  }

  try {
    await api.downloads.pause(item.id);
  } catch (error) {
    // Arquivo pequeno que já terminou: fica com o navegador e é adotado.
    await logEvent(`não deu para pausar ${item.id}: ${error && error.message}`);
    await watchForAdoption(item.id);
    return;
  }

  const url = item.finalUrl || item.url;
  const sent = recentRequests.find([item.finalUrl, item.url]);
  if (sent && sent.method && sent.method !== "GET") {
    // Formulário (POST): só o navegador consegue repetir. Ele baixa e o app organiza no fim.
    await giveBack(item.id);
    return;
  }
  const request = await buildRequest(url, {
    fileName: DMLib.basename(item.filename),
    referrer: item.referrer || "",
    sent,
  });
  const added = await sendNative(request);
  if (!added || !added.ok) {
    await logEvent(`app não aceitou ${item.id}: ${added ? added.error : "sem resposta"}`);
    await giveBack(item.id);
    return;
  }

  // Espera o app confirmar que o download dele começou de verdade.
  const started = Date.now();
  while (Date.now() - started < STATUS_TIMEOUT_MS) {
    await wait(STATUS_INTERVAL_MS);
    const reply = await sendNative({ type: "status", token: request.token });
    const status = reply && reply.ok ? reply.status : "unknown";
    if (status === "started" || status === "declined") {
      await dropBrowserCopy(item.id);
      return;
    }
    if (status !== "waiting") break;  // failed / unknown: o navegador assume
  }
  await logEvent(`app não conseguiu ${item.id}: o navegador continua`);
  await giveBack(item.id);
});

// --- Clique direito em links e mídia ---

api.runtime.onInstalled.addListener(() => {
  api.contextMenus.create({
    id: "download-link",
    title: api.i18n.getMessage("menuDownloadLink"),
    contexts: ["link", "video", "audio", "image"],
  });
});

api.contextMenus.onClicked.addListener(async (info) => {
  if (info.menuItemId !== "download-link") return;
  const url = info.linkUrl || info.srcUrl;
  if (DMLib.isMagnet(url)) {
    // Torrent: o app manda para o Real-Debrid; sem cookies nem cabeçalhos.
    await sendNative({ type: "add", token: crypto.randomUUID(), url, source: "link" });
    return;
  }
  if (!DMLib.isWebUrl(url)) return;
  await sendNative(await buildRequest(url, { referrer: info.pageUrl || "", source: "link" }));
});

// --- Mídia encontrada nas páginas (para o popup) ---

async function mediaFor(tabId) {
  const key = `media:${tabId}`;
  const stored = await api.storage.session.get(key);
  return stored[key] || [];
}

async function setMedia(tabId, list) {
  await api.storage.session.set({ [`media:${tabId}`]: list });
  try {
    await api.action.setBadgeText({ tabId, text: list.length ? String(list.length) : "" });
    await api.action.setBadgeBackgroundColor({ tabId, color: "#2563eb" });
  } catch {}
}

api.webRequest.onHeadersReceived.addListener(
  (details) => {
    if (details.tabId < 0 || details.statusCode >= 400) return;
    const kind = DMLib.classifyMedia(details);
    if (!kind) return;
    (async () => {
      const list = await mediaFor(details.tabId);
      const key = DMLib.mediaKey(details.url);
      if (list.some((media) => media.key === key)) return;
      list.push({ key, url: details.url, kind, size: DMLib.mediaSize(details), name: DMLib.mediaName(details.url) });
      await setMedia(details.tabId, list.slice(-50));
    })();
  },
  { urls: ["<all_urls>"], types: ["media", "xmlhttprequest", "other", "object"] },
  ["responseHeaders"],
);

api.tabs.onUpdated.addListener((tabId, changeInfo) => {
  if (changeInfo.url) setMedia(tabId, []);
});

api.tabs.onRemoved.addListener((tabId) => {
  api.storage.session.remove(`media:${tabId}`);
});

// Mensagens do popup.
api.runtime.onMessage.addListener((message, _sender, sendResponse) => {
  (async () => {
    if (message.type === "media") {
      sendResponse(await mediaFor(message.tabId));
    } else if (message.type === "ping") {
      sendResponse(await sendNative({ type: "ping" }));
    } else if (message.type === "download") {
      const source = message.source === "page" ? "page" : "media";
      const reply = await sendNative(
        await buildRequest(message.url, { referrer: message.pageUrl || "", source, fileName: message.title || "" }),
      );
      sendResponse(reply);
    }
  })();
  return true;  // resposta assíncrona
});
