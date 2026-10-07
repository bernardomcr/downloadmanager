# Download Manager

Gerenciador de downloads para Windows 10/11: leve, simples e rápido.

O plano completo do projeto está em [PLAN.md](PLAN.md).

## Baixar

A cada push na `main`, o GitHub Actions gera o `DownloadManager.exe` (app) e o `dm-cli.exe` (linha de comando). Eles ficam na aba **Actions**, na última execução de **Build**, em *Artifacts*.

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
src/main.cpp            entrada do app, instância única
src/ui/                 janela principal (Win32 puro)
src/cli/                dm-cli
src/core/               núcleo portátil: divisão de segmentos, cabeçalhos HTTP, estado de retomada
src/engine/             motor de download (WinHTTP, gravação em disco, tarefa de download)
src/i18n/               textos em português e inglês
src/util/               conversões UTF-8/UTF-16
res/                    manifesto (DPI, estilos visuais) e versão do .exe
tests/                  testes do núcleo e servidor HTTP de teste
```

## Testes

```sh
cmake -S . -B build-linux -G Ninja && cmake --build build-linux && ctest --test-dir build-linux
```

Teste ponta a ponta do motor: `tests/range_server.py` serve um arquivo com suporte a Range, limite de velocidade por conexão (`--rate`), quedas aleatórias (`--drop`) e modo sem Range (`--no-range`); `/redirect` redireciona e `/expired` responde 403.
