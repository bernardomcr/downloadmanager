// Clique em link magnet em qualquer página: vai para o Download Manager (Real-Debrid) em vez do
// programa de torrent. Só intercepta o clique comum (botão esquerdo, sem Ctrl/Shift).
"use strict";

(function () {
  const api = globalThis.browser ?? globalThis.chrome;
  document.addEventListener(
    "click",
    (event) => {
      if (event.defaultPrevented || event.button !== 0 || event.ctrlKey || event.shiftKey || event.metaKey || event.altKey) {
        return;
      }
      const link = event.target && event.target.closest ? event.target.closest("a[href]") : null;
      if (!link || !/^magnet:\?/i.test(link.href)) return;
      event.preventDefault();
      event.stopPropagation();
      try {
        api.runtime.sendMessage({ type: "magnet", url: link.href });
      } catch {}
    },
    true,
  );
})();
