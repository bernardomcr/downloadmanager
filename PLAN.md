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
- **HTTP**: WinHTTP (já vem no Windows: zero dependências, proxy do sistema, TLS do Windows). HTTP/1.1 com conexões TCP separadas, que é o que dá o ganho de velocidade. FTP fica fora por enquanto.
- **Torrent**: libtorrent-rasterbar (C++, mesma linguagem, padrão da indústria — usado por qBittorrent).
- **Vídeo**: yt-dlp.exe empacotado + ffmpeg (para juntar áudio/vídeo), atualizado automaticamente.
- **Persistência**: arquivos de texto simples em `%LOCALAPPDATA%\DownloadManager` (`downloads.dat`, `settings.ini`), gravados de forma atômica; o progresso de cada download fica num `.dmstate` ao lado do arquivo. SQLite só se a lista crescer a ponto de precisar.
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
0. **Fundação** ✅ — repo, CMake/vcpkg, CI Windows, esqueleto da janela.
1. **Engine HTTP** ✅ — multi-conexão com divisão dinâmica de segmentos, pausar/retomar, retomada após travamento, validação por tamanho/ETag, troca de link expirado (no motor; a UI vem na fase 2), `dm-cli`.
2. **Interface** ✅ — janela principal com abas (com contadores), diálogo "Adicionar", barra de progresso/velocidade/tempo restante, menu de clique direito (pausar, continuar, trocar link, copiar link, abrir pasta, remover, apagar para a Lixeira), bandeja, aviso de concluído, Configurações (pasta, conexões, idioma ao vivo, bandeja, iniciar com o Windows, avisos), retomada automática ao reabrir.
3. **Fila e agendador** ✅ — limite de downloads ao mesmo tempo (fila), "Começar agora", agendador por horário (atravessa a meia-noite), limite de velocidade total e por download, não deixar o PC dormir enquanto baixa, suspender/desligar quando tudo terminar (com contagem regressiva de 60 s e Cancelar; vale uma vez).
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

## Cursos (estrutura do katomart, otimizada)
Referência: katomart (Python, 61 adaptadores de plataforma). Reescrita própria — o repo não tem arquivo de licença, então nada é copiado literalmente.

### Fluxo (mantido do katomart)
Plataforma → Login/Token → Cursos (lista/busca) → Módulos/Aulas → Seleção → Download.

### Adaptadores de plataforma
- **Todas as 61 plataformas** do katomart.
- Escritos em **JavaScript**, rodando num interpretador embutido no app (QuickJS, ~1 MB). Assim um adaptador quebrado é corrigido sem lançar versão nova do app: o app baixa o pacote de adaptadores atualizado.
- Interface de cada adaptador (equivalente ao `BasePlatform` do katomart):
  `authFields()`, `authenticate(credenciais)`, `refreshAuth()`, `fetchCourses()`, `searchCourses(q)`, `fetchCourseContent(cursos)`, `fetchLessonDetails(aula)`, `downloadAttachment(anexo)`.
- Downloaders de vídeo por hospedagem (Panda, Bunny, Gumlet, ScaleUp, Spalla, SafeVideo, Vimeo/YouTube via yt-dlp etc.) separados dos adaptadores, como no katomart.

### Login e token automáticos (otimização)
Ordem de tentativa, sem o usuário precisar caçar token:
1. **Sessão do navegador**: a extensão lê o token/cookies da plataforma onde o usuário já está logado e envia ao app.
2. **Login embutido**: o app abre uma janelinha WebView2 (já vem no Windows) na página de login da plataforma; o usuário entra normalmente e o app captura o token sozinho (substitui o Playwright do katomart, sem baixar navegador).
3. **E-mail e senha** direto pela API da plataforma, quando ela permitir.
4. **Token manual** — último recurso.
- Credenciais e tokens salvos **criptografados com o Windows (DPAPI)**, nunca em texto puro (o katomart salva em texto puro).
- Re-autenticação automática ao receber 401/403.

### Seleção do que baixar
Árvore com caixas de seleção mostrando tudo que existe em cada aula: vídeo, anexos, legendas, descrição, links extras, vídeos linkados na descrição (YouTube/Vimeo), áudio/podcast. O usuário marca o que quer. Atalhos: "marcar tudo", "só vídeos", "só anexos".

### Fora do escopo
- Marcar aula como assistida — removido.
- Transcrição (Whisper) — removido.
- **DRM (Widevine etc.)**: o app detecta conteúdo protegido e mostra a aula como "protegida — não baixada". Nenhuma parte de quebra de DRM faz parte deste projeto.

### Padrões de velocidade (iguais ao katomart)
| Configuração | Padrão |
|---|---|
| Segmentos simultâneos por vídeo | 1 (recomendado até 8, aviso acima de 20) |
| Tentativas extras | 0 |
| Espera entre tentativas | 60 s |
| Espera entre aulas | 0 s |
| Timeout de requisição | 30 s |
| Limites de nome (curso/módulo/aula/arquivo) | 40 / 60 / 60 / 30 caracteres |
| Pausar após X erros / X aulas parciais | 0 (desligado) |
| Pular arquivos já baixados | Sim |

## Extensão do navegador
- Firefox: assinatura "não listada" na Mozilla (grátis), `.xpi` distribuído pelo instalador.
- Edge: publicação não listada (grátis).
- Chrome: pacote pronto para publicar, publicação opcional (taxa única de US$ 5). Até lá, instala em modo desenvolvedor.

## Em aberto
- Nada. Próximo passo: Fase 0.
