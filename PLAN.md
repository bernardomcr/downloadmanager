# Download Manager — Plano (v1)

> Repo público (bernardomcr/downloadmanager), para uso próprio e de amigos. Somente Windows 10/11 (x64).
> Prioridades, nesta ordem: **velocidade de download**, **leveza**, **interface simples**.

## Decisões fechadas
| Tema | Decisão |
|---|---|
| Nome | Download Manager (repo `downloadmanager`) |
| Plataforma | Só Windows 10/11 x64 |
| Distribuição | Repo público; releases no GitHub com atualização automática |
| Idiomas | Português (BR) e inglês, troca nas Configurações |
| Navegadores | Firefox, Chrome e Edge (uma extensão WebExtension para os três) |
| Espelhos múltiplos | Adiado (pós-v1) |
| Abas | Downloads · Concluídos · Regras · Configurações + botão único **Adicionar** que detecta o tipo do link |
| Linguagem | C++20 (mesma família do IDM) |
| Interface | Win32 nativo (sem Electron/WebView/.NET) — janela branca, pequena, abas com nomes claros |
| Vídeos | Sim — yt-dlp (YouTube + ~1800 sites) + farejador de streams HLS/DASH na extensão; login via cookies do navegador. Sem burlar DRM. Referência: katomart (só como inspiração de funcionalidades; código não reaproveitado) |
| Torrent | Sim |
| Regras automáticas | Sim |
| Sincronizar filas | Não |

## Stack técnica
- **Build**: CMake + vcpkg, MSVC. CI no GitHub Actions (`windows-latest`) gerando o `.exe`.
- **HTTP/FTP**: libcurl (multi interface) — HTTP/1.1, HTTP/2, proxies, cookies, FTP.
- **Torrent**: libtorrent-rasterbar (C++, mesma linguagem, padrão da indústria — usado por qBittorrent).
- **Vídeo**: yt-dlp.exe empacotado + ffmpeg (para juntar áudio/vídeo), atualizado automaticamente.
- **Banco**: SQLite (fila, histórico, estado de retomada, regras, configurações).
- **Integração com navegador**: extensão MV3 (Chrome/Edge/Firefox) + Native Messaging host.
- **Meta de tamanho**: executável principal < 5 MB (sem contar yt-dlp/ffmpeg), < 30 MB de RAM ocioso.

## Arquitetura
```
app.exe (um único processo)
 ├─ UI Win32 (thread principal)
 ├─ Engine (thread pool)
 │   ├─ HttpDownloader  (segmentação dinâmica, multi-conexão)
 │   ├─ TorrentSession  (libtorrent)
 │   └─ MediaDownloader (processo yt-dlp)
 ├─ Scheduler / Fila / Limite de banda
 ├─ RulesEngine (pós-download)
 ├─ Storage (SQLite)
 └─ NativeMessaging bridge (extensão do navegador)
```

## Fases
0. **Fundação** — repo, CMake/vcpkg, CI Windows, esqueleto da janela.
1. **Engine HTTP** — multi-conexão com divisão dinâmica de segmentos, pausar/retomar, retomada após crash, verificação de integridade, atualizar link expirado.
2. **Interface** — janela principal com abas, diálogo "Adicionar", progresso/velocidade/ETA, bandeja, notificações.
3. **Fila e agendador** — downloads simultâneos, agendamento, limite de banda, desligar ao concluir.
4. **Navegador** — extensão capturando downloads (cookies/referer) e vídeos.
5. **Vídeos** — yt-dlp integrado, escolha de qualidade, playlists.
6. **Torrent** — magnet/.torrent, seleção de arquivos, seed configurável.
7. **Regras automáticas** — por extensão/site/tamanho: pasta destino, extrair, abrir, rodar comando.
8. **Distribuição** — instalador, auto-update, assinatura.

## Aba Regras (proposta)
Lista de regras "SE → ENTÃO", avaliadas na ordem, a primeira que casa vence.
- **SE**: extensão do arquivo, site de origem (domínio), tamanho, tipo (arquivo/vídeo/torrent), palavra no nome.
- **ENTÃO**: salvar na pasta X, renomear com padrão, limite de velocidade/conexões, iniciar agora ou agendar, depois de concluir: extrair, abrir, abrir pasta, apagar o compactado, rodar comando.
- Vem com regras padrão (equivalente às categorias do IDM): Compactados, Documentos, Músicas, Programas, Vídeos, Torrents.

## Em aberto
- Ver rodada 3 no chat.
