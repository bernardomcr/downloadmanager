// Funções puras da extensão (sem APIs do navegador): usadas pelo background e testadas no Node.
"use strict";

(function (root) {
  const MEDIA_EXTENSIONS = ["mp4", "webm", "mkv", "mov", "m4v", "mp3", "m4a", "aac", "ogg", "oga", "opus", "flac", "wav"];
  const STREAM_EXTENSIONS = ["m3u8", "mpd"];
  // Pedaços de streaming: aparecem às centenas e não fazem sentido sozinhos.
  const SEGMENT_EXTENSIONS = ["ts", "m4s", "aac", "vtt"];
  const STREAM_TYPES = ["application/vnd.apple.mpegurl", "application/x-mpegurl", "audio/mpegurl", "application/dash+xml"];
  const MIN_MEDIA_BYTES = 512 * 1024;

  function isWebUrl(url) {
    return /^https?:\/\//i.test(url || "");
  }

  // Magnet de torrent (o app manda para o Real-Debrid).
  function isMagnet(url) {
    return /^magnet:\?.*xt=urn:btih:([0-9a-f]{40}|[a-z2-7]{32})(&|$)/i.test(url || "");
  }

  function extensionOf(url) {
    try {
      const path = new URL(url).pathname;
      const dot = path.lastIndexOf(".");
      const slash = path.lastIndexOf("/");
      return dot > slash ? path.slice(dot + 1).toLowerCase() : "";
    } catch {
      return "";
    }
  }

  function basename(path) {
    if (!path) return "";
    const parts = String(path).split(/[\\/]/);
    return parts[parts.length - 1] || "";
  }

  // Download iniciado no navegador deve ir para o app?
  function shouldCapture(item) {
    if (!item || item.incognito) return false;  // janela anônima: deixa com o navegador
    if (item.byExtensionId) return false;       // criado por uma extensão (inclusive esta)
    const url = item.finalUrl || item.url;
    if (!isWebUrl(url)) return false;            // blob:, data:, file: ficam com o navegador
    if (item.state && item.state !== "in_progress") return false;
    return true;
  }

  function cookieHeader(cookies) {
    return (cookies || []).map((cookie) => `${cookie.name}=${cookie.value}`).join("; ");
  }

  function header(headers, name) {
    const wanted = name.toLowerCase();
    const found = (headers || []).find((h) => (h.name || "").toLowerCase() === wanted);
    return found ? found.value || "" : "";
  }

  // Classifica uma resposta de rede: "file" (dá para baixar direto), "stream" (HLS/DASH) ou null.
  function classifyMedia(response) {
    const url = response.url || "";
    if (!isWebUrl(url)) return null;
    const contentType = (header(response.responseHeaders, "content-type") || "").split(";")[0].trim().toLowerCase();
    const extension = extensionOf(url);

    if (STREAM_TYPES.includes(contentType) || STREAM_EXTENSIONS.includes(extension)) return "stream";
    if (SEGMENT_EXTENSIONS.includes(extension) || contentType === "video/mp2t") return null;

    const isMedia =
      contentType.startsWith("video/") || contentType.startsWith("audio/") || MEDIA_EXTENSIONS.includes(extension);
    if (!isMedia) return null;
    const size = mediaSize(response);
    if (size >= 0 && size < MIN_MEDIA_BYTES) return null;  // sons de interface, prévias
    return "file";
  }

  // Tamanho total: do Content-Range (respostas 206) ou do Content-Length. -1 se desconhecido.
  function mediaSize(response) {
    const range = header(response.responseHeaders, "content-range");
    const total = /\/(\d+)\s*$/.exec(range);
    if (total) return Number(total[1]);
    const length = header(response.responseHeaders, "content-length");
    return length && /^\d+$/.test(length) ? Number(length) : -1;
  }

  // Mesma mídia pedida em pedaços (range=..., &bytes=...) vira uma entrada só.
  function mediaKey(url) {
    try {
      const parsed = new URL(url);
      for (const param of ["range", "bytes", "rn", "rbuf"]) parsed.searchParams.delete(param);
      parsed.hash = "";
      return parsed.toString();
    } catch {
      return url;
    }
  }

  function mediaName(url) {
    try {
      return decodeURIComponent(basename(new URL(url).pathname)) || new URL(url).hostname;
    } catch {
      return url;
    }
  }

  function formatSize(bytes, decimalSeparator) {
    if (!(bytes >= 0)) return "";
    const units = ["B", "KB", "MB", "GB", "TB"];
    let value = bytes;
    let unit = 0;
    while (value >= 1024 && unit < units.length - 1) {
      value /= 1024;
      unit += 1;
    }
    const text = unit === 0 ? String(value) : value.toFixed(1);
    return `${text.replace(".", decimalSeparator || ".")} ${units[unit]}`;
  }

  // Cabeçalhos que não fazem sentido repetir (o app/WinHTTP cuida deles) ou que quebrariam o download.
  const SKIPPED_HEADERS = new Set([
    "host", "connection", "content-length", "range", "if-range", "accept-encoding", "transfer-encoding",
    "upgrade", "te", "trailer", "keep-alive", "expect", "proxy-authorization", "proxy-connection",
    "if-none-match", "if-modified-since",
  ]);
  const MAX_HEADERS = 40;

  // Cabeçalhos que o navegador mandou no pedido do download, prontos para o app repetir.
  function forwardableHeaders(requestHeaders) {
    const result = [];
    for (const h of requestHeaders || []) {
      const name = (h.name || "").trim();
      const value = h.value ?? "";
      if (!name || SKIPPED_HEADERS.has(name.toLowerCase())) continue;
      if (/[\r\n]/.test(name + value) || !/^[A-Za-z0-9!#$%&'*+.^_`|~-]+$/.test(name)) continue;
      result.push({ name, value });
      if (result.length >= MAX_HEADERS) break;
    }
    return result;
  }

  // Guarda por pouco tempo os pedidos recentes, para achar os cabeçalhos quando o download aparecer.
  class RecentRequests {
    constructor(limit = 300, ttlMs = 2 * 60 * 1000) {
      this.limit = limit;
      this.ttlMs = ttlMs;
      this.entries = new Map();
    }
    remember(url, info, now = Date.now()) {
      this.entries.delete(url);
      this.entries.set(url, { ...info, at: now });
      while (this.entries.size > this.limit) this.entries.delete(this.entries.keys().next().value);
    }
    find(urls, now = Date.now()) {
      for (const url of urls) {
        const entry = url && this.entries.get(url);
        if (entry && now - entry.at <= this.ttlMs) return entry;
      }
      return null;
    }
  }

  const DMLib = { isWebUrl, isMagnet, extensionOf, basename, shouldCapture, cookieHeader, classifyMedia, mediaSize, mediaKey, mediaName, formatSize, forwardableHeaders, RecentRequests };
  root.DMLib = DMLib;
  if (typeof module !== "undefined" && module.exports) module.exports = DMLib;
})(typeof globalThis !== "undefined" ? globalThis : this);
