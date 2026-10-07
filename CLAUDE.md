# Notas para agentes

- Produto: download manager só para Windows 10/11 x64. Prioridades: velocidade, leveza, interface simples. Decisões de produto ficam em `PLAN.md`; atualize-o quando uma decisão mudar.
- C++20 + Win32 puro (sem MFC, Qt, .NET, Electron ou WebView na janela principal). Build oficial: MSVC via CMake no GitHub Actions. Verificação local no Linux: toolchain `cmake/mingw-w64.cmake`.
- Todo texto visível passa por `i18n::tr(Str::...)`; toda frase nova precisa existir em português e inglês em `src/i18n/strings.cpp`, na mesma ordem do enum.
- Medidas de UI em pixels lógicos (96 DPI), convertidas com `scale()`; a janela é PerMonitorV2.
- Interface sem nada invasivo: sem monitorar área de transferência, sem botões/barras flutuantes sobre páginas, sem pop-ups não solicitados.
- Fora do escopo: qualquer código de contorno de DRM (Widevine, CDM, mp4decrypt etc.). Conteúdo protegido é detectado e marcado como não baixável.
- Mensagens de commit em português.
