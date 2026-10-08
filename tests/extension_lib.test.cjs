// Testes das funções puras da extensão: node --test tests/
const test = require("node:test");
const assert = require("node:assert");
const lib = require("../extension/lib.js");

const headers = (map) => Object.entries(map).map(([name, value]) => ({ name, value }));

test("captura só downloads http/https comuns", () => {
  assert.ok(lib.shouldCapture({ url: "https://a.com/x.zip", state: "in_progress" }));
  assert.ok(!lib.shouldCapture({ url: "blob:https://a.com/123" }));
  assert.ok(!lib.shouldCapture({ url: "data:text/plain,oi" }));
  assert.ok(!lib.shouldCapture({ url: "https://a.com/x", incognito: true }));
  assert.ok(!lib.shouldCapture({ url: "https://a.com/x", byExtensionId: "outra" }));
  assert.ok(!lib.shouldCapture({ url: "https://a.com/x", state: "complete" }));
  assert.ok(lib.shouldCapture({ url: "blob:x", finalUrl: "https://a.com/real.bin" }));
});

test("nome do arquivo a partir do caminho do navegador", () => {
  assert.strictEqual(lib.basename("C:\\Users\\a\\Downloads\\x.zip"), "x.zip");
  assert.strictEqual(lib.basename("/home/a/x.zip"), "x.zip");
  assert.strictEqual(lib.basename(""), "");
});

test("cabeçalho Cookie", () => {
  assert.strictEqual(lib.cookieHeader([{ name: "a", value: "1" }, { name: "b", value: "2" }]), "a=1; b=2");
  assert.strictEqual(lib.cookieHeader([]), "");
});

test("classifica mídia", () => {
  const big = { "content-length": "5000000" };
  assert.strictEqual(lib.classifyMedia({ url: "https://a.com/v.mp4", responseHeaders: headers(big) }), "file");
  assert.strictEqual(
    lib.classifyMedia({ url: "https://a.com/play", responseHeaders: headers({ "Content-Type": "video/webm", ...big }) }),
    "file",
  );
  assert.strictEqual(lib.classifyMedia({ url: "https://a.com/master.m3u8", responseHeaders: [] }), "stream");
  assert.strictEqual(
    lib.classifyMedia({ url: "https://a.com/x", responseHeaders: headers({ "content-type": "application/dash+xml" }) }),
    "stream",
  );
  assert.strictEqual(lib.classifyMedia({ url: "https://a.com/seg1.ts", responseHeaders: headers(big) }), null);
  assert.strictEqual(lib.classifyMedia({ url: "https://a.com/click.mp3", responseHeaders: headers({ "content-length": "2000" }) }), null);
  assert.strictEqual(lib.classifyMedia({ url: "https://a.com/page.html", responseHeaders: headers({ "content-type": "text/html" }) }), null);
});

test("tamanho pelo Content-Range e agrupamento de pedaços", () => {
  assert.strictEqual(lib.mediaSize({ responseHeaders: headers({ "content-range": "bytes 0-99/123456" }) }), 123456);
  assert.strictEqual(lib.mediaSize({ responseHeaders: headers({ "content-length": "42" }) }), 42);
  assert.strictEqual(lib.mediaSize({ responseHeaders: [] }), -1);
  assert.strictEqual(lib.mediaKey("https://a.com/v.mp4?range=0-100&id=1"), lib.mediaKey("https://a.com/v.mp4?id=1&range=200-300"));
});

test("formatação de tamanho", () => {
  assert.strictEqual(lib.formatSize(512), "512 B");
  assert.strictEqual(lib.formatSize(1536, ","), "1,5 KB");
  assert.strictEqual(lib.formatSize(-1), "");
});

test("repassa os cabeçalhos do navegador sem os de controle", () => {
  const forwarded = lib.forwardableHeaders(
    headers({
      Host: "a.com",
      Cookie: "sid=1",
      Authorization: "Bearer x",
      Range: "bytes=0-",
      "Accept-Encoding": "gzip",
      "X-Token": "abc",
      "Bad Header": "x",
      "X-Inject": "a\r\nEvil: 1",
    }),
  );
  assert.deepStrictEqual(
    forwarded.map((h) => h.name),
    ["Cookie", "Authorization", "X-Token"],
  );
});

test("lembra pedidos recentes por pouco tempo", () => {
  const recent = new lib.RecentRequests(2, 1000);
  recent.remember("https://a.com/1", { method: "GET" }, 0);
  recent.remember("https://a.com/2", { method: "POST" }, 0);
  recent.remember("https://a.com/3", { method: "GET" }, 0);
  assert.strictEqual(recent.find(["https://a.com/1"], 10), null);  // saiu pelo limite
  assert.strictEqual(recent.find([undefined, "https://a.com/2"], 10).method, "POST");
  assert.strictEqual(recent.find(["https://a.com/3"], 5000), null);  // expirou
});

test("reconhece magnets de torrent", () => {
  assert.ok(lib.isMagnet("magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&dn=x"));
  assert.ok(lib.isMagnet("magnet:?dn=x&xt=urn:btih:abcdefghijklmnopqrstuvwxyz234567"));
  assert.ok(!lib.isMagnet("magnet:?xt=urn:btih:123"));
  assert.ok(!lib.isMagnet("https://a.com/x.torrent"));
  assert.ok(!lib.isMagnet(undefined));
});
