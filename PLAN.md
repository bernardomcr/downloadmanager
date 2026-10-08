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
| Captura | Só o que o usuário clicou para baixar no navegador. Sem monitorar a área de transferência, sem botão/barra flutuante sobre vídeos, sem pop-up "inteligente". Vídeos: pelo botão da extensão na barra do navegador |
| Instalador | Só instalador (`DownloadManager-Setup.exe`, próprio, Win32 puro). Instala só para o usuário (sem administrador). Opção "Iniciar com o Windows" marcada por padrão, desmarcável |
| Iniciar com o Windows | Sim, minimizado na bandeja |
| Abas | Downloads · Concluídos · Regras · Configurações + botão único **Adicionar** que detecta o tipo do link |
| Linguagem | C++20 (mesma família do IDM) |
| Interface | Win32 nativo (sem Electron/WebView/.NET) — janela branca, pequena, abas com nomes claros |
| Vídeos | Sim — yt-dlp (YouTube + ~1800 sites) + farejador de streams HLS/DASH na extensão; login via cookies do navegador. Sem burlar DRM. Referência: katomart (só como inspiração de funcionalidades; código não reaproveitado) |
| Torrent | Sem cliente de torrent próprio (libtorrent não vale o peso). Torrents e links magnet vão para um serviço de debrid (Real-Debrid primeiro) e voltam como download direto: ver Fase 9 |
| Regras automáticas | Sim |
| Sincronizar filas | Não |

## Stack técnica
- **Build**: CMake + vcpkg, MSVC. CI no GitHub Actions (`windows-latest`) gerando o `.exe`.
- **HTTP**: WinHTTP (já vem no Windows: zero dependências, proxy do sistema, TLS do Windows). HTTP/1.1 com conexões TCP separadas, que é o que dá o ganho de velocidade. FTP fica fora por enquanto.
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
4. **Navegador** ✅ — extensão (Firefox, Chrome, Edge, Brave) + `dm-host.exe` (Native Messaging). **Nenhum download fica fora da organização**:
   - captura na origem repetindo exatamente os cabeçalhos que o navegador mandou (cookies, referer, Authorization, tokens do site, user agent);
   - o navegador só larga o download depois que o app confirma que começou a receber; se o app falhar, o navegador continua;
   - formulários (POST) e arquivos `blob:`/`data:` ficam com o navegador;
   - tudo que o navegador terminar é **adotado**: movido para a pasta do app e listado em Concluídos;
   - clique direito em links/vídeos/imagens: "Baixar com o Download Manager"; popup lista vídeos/áudios da página (badge com a contagem), sem nada flutuando na página;
   - janela anônima fica com o navegador (privacidade).
5. **Vídeos** ✅ — yt-dlp + ffmpeg baixados pelo próprio app na primeira vez (~200 MB, em `%LOCALAPPDATA%\DownloadManager\tools`), yt-dlp atualizado a cada 7 dias. Link de site conhecido (YouTube, Vimeo, X, Instagram, TikTok...) ou qualquer link colado que abra uma página → janela de vídeo: título, duração, qualidade (melhor / até Np / MP3 / áudio original), legendas pt/en, playlist com caixas de seleção. Na lista: pausar/continuar (o yt-dlp aproveita o que já baixou), fila, limite. Streams HLS/DASH com 8 pedaços em paralelo. Extensão: "Baixar o vídeo desta página" (leva os cookies: evita o "confirme que não é um robô" do YouTube) e streams no popup. DRM: detectado e recusado.
6. ~~**Torrent**~~ — cancelado (decisão do usuário).
7. **Regras automáticas** ✅ — aba Regras (ver abaixo): ao concluir, o arquivo vai para a subpasta da primeira regra que servir; extrair (NanaZip/7-Zip/WinRAR do usuário; tar só se não houver), apagar o compactado, abrir arquivo/pasta. O que o navegador baixa sozinho não é movido depois: no Chrome/Edge já é salvo na pasta da regra (ver Fase 9).
8. **Distribuição** ✅ — ver "Distribuição" abaixo. Pendente do lado do usuário: chaves da Mozilla (segredos no GitHub) para assinar a extensão do Firefox; publicação na loja do Edge/Chrome é manual e opcional.

## Aba Regras
Lista de regras "SE → ENTÃO", avaliadas na ordem, a primeira que casa vence. Uma caixa liga/desliga tudo; cada regra tem a sua.
- **SE**: tipo (qualquer / arquivo / vídeo de site), extensões, sites (domínio e subdomínios), palavra no nome, maior/menor que N MB.
- **ENTÃO**: pasta (nome simples = dentro da pasta padrão; ou caminho completo), extrair compactados (pasta com o nome do arquivo), apagar o compactado depois de extrair, abrir o arquivo, abrir a pasta.
- Aplicada ao **concluir**, em segundo plano (Concluídos mostra "Organizando…"). Só organiza o que foi para a pasta padrão: se a pessoa escolheu outra pasta no Adicionar, a escolha dela vale.
- Regras padrão (categorias do IDM): Vídeos (de site), Compactados, Programas, Imagens de disco, Documentos, Músicas, Vídeos (por extensão), Imagens. "Restaurar padrão" volta a elas.
- Salvas em `%LOCALAPPDATA%\DownloadManager\rules.ini`.
- Ficaram de fora por enquanto (dá para adicionar se fizer falta): renomear com padrão, limite de velocidade/conexões por regra, rodar comando.

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

## Distribuição
- **Instalador próprio** (`src/setup/`, Win32 puro, ~3 MB) em vez de Inno Setup: o Inno 6.4+ mudou a licença para uso comercial e roda 32 bits; o nosso é x64, testável no Wine e serve também para a atualização silenciosa.
  - Instala em `%LOCALAPPDATA%\Programs\Download Manager` (sem UAC). Atalho no Menu Iniciar; área de trabalho e "Iniciar com o Windows" marcados por padrão, desmarcáveis.
  - Fecha o app aberto pedindo com educação (mensagem `kMessageQuit`: salva e sai); um `dm-host.exe` em uso pelo navegador é renomeado para `.old` e trocado.
  - Entrada em "Apps e recursos"; o desinstalador pergunta se apaga também configurações, lista e ferramentas de vídeo (os arquivos baixados nunca são apagados).
- **Atualização automática** (pode ser desligada em Configurações): uma vez por dia o app lê o último release do GitHub; se for mais novo, baixa o `DownloadManager-Setup.exe` e confere o SHA-256 publicado junto. Instala sem perguntar só quando ninguém nota: com a janela na bandeja e nada baixando, ao fechar o app ou ao abrir de novo. Um instalador que falhou não é tentado de novo sozinho. Cópia do app que não foi instalada pelo setup não se atualiza sozinha (o "Atualizar agora" abre o instalador normal).
- **Release**: mudar a versão em `CMakeLists.txt` e `extension/manifest.json`, commit e, nas Actions, "Run workflow" com "publicar" marcado (ou push de uma tag `vX.Y.Z`). O CI confere as versões, compila, testa (inclusive instalar e desinstalar) e publica o release com o instalador, o `.sha256`, o zip da extensão e o `.xpi` assinado (se houver as chaves).

## Extensão do navegador
- Firefox: assinatura "não listada" na Mozilla (grátis), feita pelo CI nos releases com os segredos `AMO_JWT_ISSUER`/`AMO_JWT_SECRET`. O instalador registra o `.xpi` e o Firefox oferece ativá-la ao abrir.
- Edge: publicação não listada (grátis).
- Chrome: pacote pronto para publicar, publicação opcional (taxa única de US$ 5). Até lá, instala em modo desenvolvedor.

## Fase 9 (pedidos de 08/10/2026) ✅ — feita no Windows de verdade (MSVC local)
1. **Visual** ✅ — o usuário preferiu fundo branco e abas/botão Adicionar nativos (testou abas desenhadas e não gostou). Ficou: lista toda desenhada pelo app (linhas altas, ícone do tipo de arquivo, barra fina arredondada com gradiente e cor por estado: azul baixando, roxo no Real-Debrid, cinza pausado/fila, vermelho erro; porcentagem ao lado), cabeçalho da lista discreto, rodapé com "N baixando · N na fila" e a velocidade total, barra de título branca no Windows 11, Configurações com rolagem.
2. **Diálogo de download** ✅ — vindo do navegador: ícone + nome do arquivo em destaque, "de site.com", link escondido ("Mostrar link"). Manual: link (http/https/magnet) + "Abrir .torrent…". Nos dois, "Salvar em" já mostra a pasta da regra ("Pela regra Compactados"); o download vai direto para lá. Se o usuário trocar a pasta, a escolha dele vale.
3. **Real-Debrid** ✅ — token em Configurações (DPAPI, conta e validade mostradas, Desconectar). Entrada: magnet no Adicionar, .torrent (botão ou arrastar para a janela), magnet pelo clique direito da extensão. Todos os arquivos do torrent são selecionados; a lista mostra a etapa no Real-Debrid e, pronto, cada arquivo vira download direto com as regras valendo. Testado com a conta do usuário. Pendente: escolher arquivos quando o torrent tem vários; Torbox e outros.
4. **Navegador sem mover arquivos** ✅ — mover depois de pronto quebrava o "Mostrar na pasta" do navegador. Agora: Chrome/Edge salvam já na subpasta da regra (`onDeterminingFilename` + `route` no dm-host); Firefox (sem essa API) fica onde salvou. O app só lista em Concluídos. Downloads do claude.ai e afins (blob:) só o navegador consegue baixar.
5. **Extração pelo programa do usuário** ✅ — NanaZip, 7-Zip ou WinRAR (o associado à extensão primeiro), com a janela de progresso deles; `tar` só se não houver nenhum.
6. **Rede** ✅ — fallback rápido para IPv4 no WinHTTP (a rede do usuário tem IPv6 quebrado; conexões ficavam esperando estourar o tempo).

## Fase 10 (pedidos de 08/10/2026, depois da 0.2.0) ✅
1. **Janela ao concluir** ✅ — Abrir, Abrir pasta, Extrair (só compactados) e Fechar; no canto da tela, sem roubar o foco, empilhando. Pode ser desligada nas Configurações (aí volta o aviso da bandeja). "Extrair" também no menu da aba Concluídos.
2. **Extrair inteligente no lugar da extração automática** ✅ — como o "Smart Extraction" do NanaZip: um item na raiz vai ao lado do compactado, vários vão para uma pasta com o nome; nunca sobrescreve. Feito pelo NanaZip/7-Zip/WinRAR do usuário (janela de progresso deles). A opção "extrair" das regras continua existindo e usa o mesmo método.
3. **Magnet automático** ✅ — clique em link magnet em qualquer página vai para o app → Real-Debrid → download direto. Sem o app, o link segue para o programa padrão.
4. **Pasta Navegador** ✅ — o que o navegador baixa sozinho vai para `Downloads\Navegador` (Chrome/Edge pela extensão; Firefox pela preferência do perfil, a partir da próxima abertura). Opção nas Configurações.
5. **Velocidade** ✅ — 16 conexões adaptativas, User-Agent honesto com segunda tentativa, TCP Fast Open/TLS False Start, IPv4 rápido. Contra servidor que limita por conexão: 15x a velocidade de uma conexão (o que o navegador faz).
6. **MB/s ou Mb/s** ✅ — nas Configurações.

## Em aberto
- Escolher os arquivos de um torrent com vários arquivos; outros serviços de debrid (Torbox).
- Registrar o app como opção de "abrir magnet" do Windows (fora do navegador).
