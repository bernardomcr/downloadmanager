# Notas para agentes

- Produto: download manager só para Windows 10/11 x64. Prioridades: velocidade, leveza, interface simples. Decisões de produto ficam em `PLAN.md`; atualize-o quando uma decisão mudar.
- C++20 + Win32 puro (sem MFC, Qt, .NET, Electron ou WebView na janela principal). Build oficial: MSVC via CMake no GitHub Actions. Verificação local no Linux: toolchain `cmake/mingw-w64.cmake`.
- Todo texto visível passa por `i18n::tr(Str::...)`; toda frase nova precisa existir em português e inglês em `src/i18n/strings.cpp`, na mesma ordem do enum.
- Medidas de UI em pixels lógicos (96 DPI), convertidas com `scale()`; a janela é PerMonitorV2.
- Interface sem nada invasivo: sem monitorar área de transferência, sem botões/barras flutuantes sobre páginas, sem pop-ups não solicitados.
- Fora do escopo: qualquer código de contorno de DRM (Widevine, CDM, mp4decrypt etc.). Conteúdo protegido é detectado e marcado como não baixável.
- Motor: `src/core/` é portátil e testado em `tests/core_tests.cpp` (roda no Linux e no CI). `src/engine/` usa WinHTTP; erros saem como `DownloadError` + detalhe e só a UI/CLI traduz (`i18n::describeError`).
- Teste ponta a ponta no Linux: compile com mingw, rode `dm-cli.exe` no Wine contra `tests/range_server.py`. Use `LANG=C.UTF-8` (senão o Wine falha em nomes com acento), `wine taskkill /F /IM dm-cli.exe` para simular travamento e `kill -INT` no processo `dm-cli.exe` para Ctrl+C. Cuidado: `pkill -f` com um padrão que aparece na própria linha de comando mata o shell.
- App: `src/app/` (DownloadManager dono da lista, integrações com o Windows) e `src/ui/` (janela, listas virtuais, diálogos de `res/app.rc`, página de Configurações, bandeja). Dados em `%LOCALAPPDATA%\DownloadManager`.
- Teste da interface no Linux: Xvfb em `:99` + Wine + `xdotool` (cliques/teclas) + `import -window root` (print). Sem gerenciador de janelas, as janelas aparecem sem barra de título.
- Extensão (`extension/`): JS puro em `lib.js` (testado com `node --test`), APIs do navegador em `background.js`. Regra central: nenhum download pode escapar da organização (captura → confirmação "started" antes de largar a cópia do navegador → adoção do que o navegador terminar). Não quebrar esse fluxo.
- Contrato app ↔ `dm-host.exe` em `src/app/ipc.h` (WM_COPYDATA); mensagens validadas em `src/core/browser_request.cpp` (vêm da internet: só http/https, sem quebras de linha em cabeçalhos, sem `..` em caminhos).
- Teste real da extensão: Chromium do Playwright com `--load-extension` sob Xvfb; com `--user-data-dir`, o manifesto da ponte vai em `<perfil>/NativeMessagingHosts/` (script que chama `wine dm-host.exe`).
- Mensagens de commit em português.
