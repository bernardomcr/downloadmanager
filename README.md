# Download Manager

Gerenciador de downloads para Windows 10/11: leve, simples e rápido.

O plano completo do projeto está em [PLAN.md](PLAN.md).

## Instalar

Baixe o `DownloadManager-Setup.exe` do [último release](https://github.com/bernardomcr/downloadmanager/releases/latest) e abra. Não precisa de administrador: instala só para o seu usuário. Depois disso o app se atualiza sozinho (dá para desligar em Configurações).

A cada push na `main`, o GitHub Actions também gera os executáveis de teste (aba **Actions** → última execução de **Build** → *Artifacts*).

## Lançar uma versão

1. Mude a versão em `CMakeLists.txt` (`project(... VERSION X.Y.Z)`) e em `extension/manifest.json`, e faça o commit na `main`.
2. No GitHub: aba **Actions** → **Build** → **Run workflow**, marque **publicar** e confirme. (Ou faça push de uma tag `vX.Y.Z`.)
3. O CI confere as versões, compila, testa e publica o release; os apps instalados pegam a versão nova em até um dia.

Para o Firefox instalar a extensão sem modo de desenvolvedor, ela precisa ser assinada pela Mozilla (grátis): crie as chaves em addons.mozilla.org → *Ferramentas* → *Gerenciar chaves de API* e cadastre no GitHub (repositório → *Settings* → *Secrets and variables* → *Actions*) os segredos `AMO_JWT_ISSUER` e `AMO_JWT_SECRET`. Os próximos releases já saem com o `.xpi` assinado dentro do instalador.

## Extensão do navegador

A extensão manda para o app os downloads que você inicia no navegador e organiza até o que o navegador baixar sozinho. Ela conversa com o app pelo `dm-host.exe`, que precisa estar na mesma pasta do `DownloadManager.exe` (o app se registra nos navegadores ao abrir).

- **Chrome / Edge / Brave**: `chrome://extensions` → "Modo do desenvolvedor" → "Carregar sem compactação" → pasta `extension` dentro da pasta do app (`%LOCALAPPDATA%\Programs\Download Manager\extension`).
- **Firefox**: com a extensão assinada no release, o Firefox oferece ativá-la ao abrir depois de instalar o app. Sem assinatura: `about:debugging` → "Este Firefox" → "Carregar extensão temporária" → `extension/manifest.json`.

## Linha de comando

```bat
dm-cli <link> [pasta] [--conexoes N] [--nome arquivo]
```

Regras: `dm-cli --organizar <arquivo> <pasta-base> [--extrair]` aplica as regras padrão a um arquivo (usado no CI).

Vídeos (yt-dlp): `dm-cli --video <link> [pasta] [--qualidade best|1080|720|mp3|audio]`. `dm-cli --preparar-videos <pasta-de-dados>` baixa o yt-dlp e o ffmpeg (o app faz isso sozinho na primeira vez).

Ctrl+C pausa e salva o progresso; rodar o mesmo comando de novo continua de onde parou (também depois de travamento ou reinício do PC).

## Claude Code: mod "terminal amigo"

`claude-mods/terminal-amigo` deixa o Claude Code no terminal (ou no app desktop, aba Code) mais fácil de usar neste projeto: acima da caixa de mensagem aparecem botões para os pedidos mais comuns (puxar novidades, compilar e abrir o app, enviar pro GitHub, publicar versão) e, enquanto o Claude trabalha, um jogo da cobrinha.

Ao abrir esta pasta no Claude Code e confiar nela, ele oferece instalar o mod. Ou instale à mão, na caixa de mensagem do Claude Code:

```
/plugin install terminal-amigo --marketplace bernardomcr/downloadmanager
```

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
src/app/                lista de downloads (DownloadManager), organizador das regras e integrações com o Windows
src/ui/                 janela principal, listas, diálogos, Regras, Configurações, bandeja (Win32 puro)
src/cli/                dm-cli
src/setup/              instalador e desinstalador (DownloadManager-Setup.exe)
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
