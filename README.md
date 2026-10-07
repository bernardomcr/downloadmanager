# Download Manager

Gerenciador de downloads para Windows 10/11: leve, simples e rápido.

O plano completo do projeto está em [PLAN.md](PLAN.md).

## Baixar

A cada push na `main`, o GitHub Actions gera o `DownloadManager.exe`. Ele fica na aba **Actions**, na última execução de **Build**, em *Artifacts*.

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
src/main.cpp            entrada, instância única
src/ui/                 janela principal (Win32 puro)
src/i18n/               textos em português e inglês
res/                    manifesto (DPI, estilos visuais) e versão do .exe
```
