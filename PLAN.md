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
| Torrent | Continua semeando ao concluir (etiqueta). Configurações: "Parar de semear automaticamente" (por razão/tempo), desligado por padrão |
| Captura | Só o que o usuário clicou para baixar no navegador. Sem monitorar a área de transferência, sem botão/barra flutuante sobre vídeos, sem pop-up "inteligente". Vídeos: pelo botão da extensão na barra do navegador |
| Instalador | Só instalador (setup.exe). Opção "Iniciar com o Windows" marcada por padrão, desmarcável |
| Iniciar com o Windows | Sim, minimizado na bandeja |
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

## Cursos (inspirado no katomart, versão simplificada)
Referência analisada: katomart (Python, ~34 mil linhas, 61 adaptadores de plataforma, login por e-mail/senha/token, Playwright para capturar token, fluxo Plataforma → Login → Cursos → Módulos → Download).
Não reaproveitamos código (repo sem arquivo de licença). Aproveitamos as ideias, simplificando:
- **Sem tela de login**: o usuário abre o curso no navegador onde já está logado e clica no botão da extensão → "Baixar este curso".
- **Adaptadores na extensão**: cada plataforma é um pequeno script JS que roda com a sessão do próprio navegador e devolve a árvore Curso → Módulos → Aulas (vídeo, anexos, legendas, descrição). Nada de Playwright, token ou senha salva.
- **No app**: aparece uma lista com caixas de seleção (módulos/aulas, tudo marcado) → Baixar. Estrutura em disco: `Curso/01. Módulo/01. Aula.mp4` + anexos.
- **Genérico**: em sites sem adaptador, a extensão lista os vídeos/streams (HLS/DASH/MP4) encontrados na página aberta.
- **Sem DRM**: vídeo com Widevine é detectado e marcado como "protegido — não é possível baixar". Sem CDM, sem mp4decrypt.
- **Cuidado com a conta**: velocidade conservadora por padrão para não acionar bloqueio da plataforma.

## Em aberto
- Ver rodada 4 no chat.
