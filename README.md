# Download Manager

Gerenciador de downloads para Windows 10/11: leve, simples e rápido.

O plano completo do projeto está em [PLAN.md](PLAN.md).

## Baixar

A cada push na `main`, o GitHub Actions gera o `DownloadManager.exe` (app) e o `dm-cli.exe` (linha de comando). Eles ficam na aba **Actions**, na última execução de **Build**, em *Artifacts*.

## Extensão do navegador

A extensão manda para o app os downloads que você inicia no navegador e organiza até o que o navegador baixar sozinho. Ela conversa com o app pelo `dm-host.exe`, que precisa estar na mesma pasta do `DownloadManager.exe` (o app se registra nos navegadores ao abrir).

- **Chrome / Edge / Brave**: `chrome://extensions` → "Modo do desenvolvedor" → "Carregar sem compactação" → pasta `extension/` (ou o zip extraído).
- **Firefox**: enquanto a extensão não é assinada pela Mozilla, `about:debugging` → "Este Firefox" → "Carregar extensão temporária" → `extension/manifest.json`.

## Linha de comando

```bat
dm-cli <link> [pasta] [--conexoes N] [--nome arquivo]
```

Ctrl+C pausa e salva o progresso; rodar o mesmo comando de novo continua de onde parou (também depois de travamento ou reinício do PC).

## Compilar

Requisitos: Visual Studio 2022 ou mais novo (C++), CMake 3.21+.

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

No Linux dá para verificar a compilação com mingw-w64:

```sh
cmake -S . -B build-mingw -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-mingw
```

## Estrutura

```
src/main.cpp            entrada do app, instância única, --tray
src/app/                lista de downloads (DownloadManager) e integrações com o Windows
src/ui/                 janela principal, listas, diálogos, Configurações, bandeja (Win32 puro)
src/cli/                dm-cli
src/host/               dm-host.exe: ponte com a extensão (Native Messaging)
extension/              extensão do navegador (WebExtension MV3)
src/core/               núcleo portátil: divisão de segmentos, cabeçalhos HTTP, estado de retomada
src/engine/             motor de download (WinHTTP, gravação em disco, tarefa de download)
src/i18n/               textos em português e inglês
src/util/               conversões UTF-8/UTF-16
res/                    ícone, diálogos, manifesto (DPI, estilos visuais) e versão do .exe
tools/                  gerador do ícone
tests/                  testes do núcleo e servidor HTTP de teste
```

## Testes

```sh
cmake -S . -B build-linux -G Ninja && cmake --build build-linux && ctest --test-dir build-linux
```

Testes da extensão: `node --test tests/extension_lib.test.cjs`. `tests/native_host_client.py` finge ser o navegador falando com o `dm-host.exe`.

Teste ponta a ponta do motor: `tests/range_server.py` serve um arquivo com suporte a Range, limite de velocidade por conexão (`--rate`), quedas aleatórias (`--drop`) e modo sem Range (`--no-range`); `/redirect` redireciona e `/expired` responde 403.
