# Roadmap geral — port de Kamen Rider: Battride War Genesis para PC

**Data:** 2026-11-12
**Prioridade definida:** desempenho primeiro.
**Ponto de partida:** o jogo roda do boot ao combate, com imagem, vídeos, save, controle e áudio (BGM, efeitos e vozes).

---

## 1. Onde estamos em desempenho (medido hoje)

Medição: run `perf_default`, 110 s com `BB_FRAME_STATS=1`, menus → Story Mode → combate, RX 6750 XT.

| Métrica | Menus | Combate |
|---|---|---|
| FPS | 60.0 | 30.1 (travado pelo próprio jogo) |
| Mediana do quadro | 16.7 ms | 33.33 ms |
| Desvio padrão | — | 2.3–2.7 ms |
| **Pior quadro a cada 5 s** | 16.7 ms | **50 ms** (todas as janelas); um pico de **133 ms** |
| Thread de GPU ociosa | 96% | **97%** |
| Draws por quadro | — | ~270 |
| Tempo do jogo dormindo por quadro | — | **~30 ms** (ele mesmo limita a 30 FPS) |
| Draws esperando pipeline (não prontos) | — | ~7–8 mil a cada 5 s |

**O que isso quer dizer:**
1. **Sobra muita capacidade.** O jogo monta um quadro em ~3 ms e dorme o resto. Renderizar mais rápido não muda nada no gameplay.
2. Os ganhos de verdade estão em: **(a) 60 FPS no gameplay**, **(b) acabar com os picos** de 50/133 ms (engasgos visíveis) e **(c) tirar custos herdados do Bloodborne** que não servem para este jogo. Exemplos: *object motion* com 4 milhões de vértices por quadro e o FSR, que procura o "scene color" do Bloodborne em todo quadro.
3. Toda otimização será medida antes e depois com uma **rota fixa automatizada**, sempre a mesma.

---

## 2. Visão geral das fases

| Fase | Tema | Prioridade | Tamanho |
|---|---|---|---|
| **2** | **Desempenho e fluidez** | 🔴 máxima | G |
| 3 | Completar o que falta (funções, entrada, áudio) | 🟠 alta | M |
| 4 | Limpar a herança do Bloodborne + configuração própria | 🟡 média | M |
| 5 | Validação completa do jogo | 🟡 média | M |
| 6 | Distribuição (sem arquivos do jogo) | 🟢 depois | M |

Tamanhos: P = 1 sessão, M = algumas sessões, G = várias sessões. As fases 3 e 4 podem andar em paralelo com a 2 quando um item da 2 estiver esperando teste.

---

## Fase 2 — Desempenho e fluidez 🔴

### 2.1 Benchmark reproduzível *(P, primeiro de tudo)*
- Script `tools/bench.sh`: boot → menu → Story → 60 s de combate, com entrada por `BB_PAD_FILE` e saída em JSON (FPS, mediana, p99, pior quadro, picos > 1.5× mediana, % da GPU ociosa, tempo de load).
- A *baseline* é a tabela da seção 1. Todo item abaixo vira um par **antes/depois** no relatório.
- **Pronto quando:** duas execuções seguidas dão os mesmos números (±2%).

### 2.2 Acabar com os picos de 50–133 ms *(M, maior impacto percebido)*
- Investigar cada `Stall:` do log: compilação de pipeline, upload de textura (~8 MB por pico), espera de evento (`RegisterRenderTargetForDisplay` ~7–9 ms).
- **Cache de shaders/pipelines persistente**: usar o fluxo do bbport (`--export-shader-recipes` / `--import-shaders` / `--rebuild-shader-cache`). O cache é gerado na máquina do jogador, a partir do jogo dele.
- **Pré-aquecimento** no boot/loading com os pipelines já vistos, para os "not ready" (~8 mil a cada 5 s) chegarem a zero.
- **Pronto quando:** pior quadro ≤ 1.5× a mediana na rota de benchmark, depois do primeiro uso.

### 2.3 Desligar custos herdados do Bloodborne *(P–M, A/B um por um)*
| Recurso | Hoje | Ação |
|---|---|---|
| Object motion (vetores de movimento) | **on**, 4 M vértices/quadro | desligar por padrão; só serve para upscaler temporal ou geração de quadros |
| Upscaler FSR 3.1 | on, sem efeito ("frame without upscaler" em todos os quadros) | desligar a busca por quadro; religar só quando for configurado para este jogo (2.7) |
| Geração de quadros (FSR3) | disponível | off por padrão |
| Write traps em imagens / layer watch | on | medir A/B; manter só o que for preciso para a imagem ficar certa |
| Ganchos de código do Bloodborne (câmera, HUD, mouse, engine fixes) | conferem bytes antes de aplicar, então falham sem efeito | remover (Fase 4), mas medir já |
- **Pronto quando:** cada item tiver o ganho ou a perda medidos e um padrão decidido.

### 2.4 60 FPS no gameplay *(G, maior ganho visível; tem risco)*
- O jogo dorme ~30 ms por quadro. É preciso achar o **limitador de quadros** e o **passo de tempo** da engine no Ghidra: o laço principal está na vtable `+0x80` → `0x34a550`; procurar `sceKernelUsleep`, `sceVideoOutSetFlipRate` e constantes 1/30.
- Verificar se física, animação, câmera e lógica usam passo variável ou fixo:
  - **passo variável**: basta liberar o limitador;
  - **passo fixo**: hook no estilo do `BbFrameRate` do bbport (interpolação ou passo dividido).
- Opção do usuário: 30 (original) / 60 / sem limite, com vsync.
- **Pronto quando:** 60 FPS estáveis, com velocidade de jogo, cutscenes, áudio e combos iguais aos de 30 FPS (comparação por vídeo/quadros na rota fixa).

### 2.5 Tempo de loading *(M)*
- Medir: boot até o menu, menu até a missão, entre fases. Os `.fpk` são montados inteiros na RAM.
- Caminhos: leitura sequencial grande ou `mmap`, cache de páginas, zlib-ng (já em uso), verificar se as threads do Fios2 trabalham em paralelo.
- **Pronto quando:** loads iguais ou mais rápidos que no PS4, e documentados.

### 2.6 Custo do áudio *(P)*
- O mixer decodifica e mixa sob um lock global. Medir o tempo por grão (256 amostras, 5.3 ms) com muitos sons juntos.
- Se precisar: decodificar fora do lock, um lock por sistema, mixagem vetorizada (SIMD).
- **Pronto quando:** < 0.5 ms por grão no pior combate e zero underrun.

### 2.7 Resolução e qualidade de imagem *(M, depois de 2.2–2.4)*
- Resolução interna maior (1440p/4K) e telas ultrawide (HUD em 16:9 no centro).
- Upscaler (FSR) só depois de identificar o "scene color" deste jogo.
- **Pronto quando:** 4K a 60 FPS na RX 6750 XT sem picos.

### 2.8 Threads da CPU *(P)*
- Mapear as prioridades PS4 das 9 threads do jogo, além das do áudio e da GPU, para o Linux (nice/affinity). Evitar que o render de áudio dispute com a gravação da GPU.

---

## Fase 3 — Completar o que falta 🟠

### 3.1 Zerar as funções que faltam (137 hoje) *(P)*
| Grupo | Funções | De onde vem |
|---|---|---|
| Rtc (ler data/hora, somar minutos) | 3 | **shadPS4** `rtc.cpp` |
| Troféus (ícones, grupos, fechar contexto) | 6 | **shadPS4** `np/np_trophy` |
| DiscMap | 1 | **shadPS4** (stub) |
| Online: Json2 (62), Score (9), TUS (17), Matching2 (9), Utility (13), WebApi (7), Auth/Manager/Commerce/Http/Facebook | ~120 | stubs nossos de "desconectado" (os ports só têm stubs) |
| Unwind para exceções C++ (kernel + libc) | 5 | **shadPS4** (só se o jogo lançar uma exceção) |
- **Pronto quando:** `tools/missing_imports.py` mostra 0.

### 3.2 Entrada *(M)*
- **Teclado/mouse próprios do jogo** (hoje é o layout do Bloodborne): ○ confirmar, ✕ voltar, □/△ ataques, OPTIONS, câmera no mouse.
- Arquivo de configuração para remapear.
- Controles Xbox/DualShock/DualSense: o SDL já cuida; validar vibração e light bar.
- Ícones de botão Xbox/teclado: substituir texturas (avaliar o custo; opcional).

### 3.3 Áudio completo *(M)*
- Envelope e filtro do sampler (sons sem "fade" abrupto), reverb, mastering com compressor no lugar do limitador simples.
- Reamostragem melhor (cúbica) se o pitch variar muito.
- Validar os volumes **ouvindo**, comparando com o PS4.
- Modo "Custom Soundtrack" do jogo: conferir o que ele espera do sistema.

### 3.4 Vídeos *(P)*
- Tocar os 61 `.mp4` (aberturas, cutscenes) e conferir sincronia de áudio e vídeo.

---

## Fase 4 — Limpeza da herança do Bloodborne 🟡

- Remover ou isolar: menu do jogo (`BbGameMenu`), limitador do Bloodborne (`BbFrameRate`, que dá lugar ao nosso da 2.4), ganchos Gnm, ganchos de câmera/HUD, `engine_fixes`, ícones `common.tpf.dcx`, perfil de conteúdo.
- Overlay próprio (Insert): resolução, FPS, volume, controles.
- **Configuração em arquivo** (`kr-port.toml`) no lugar de dezenas de variáveis `BB_*`.
- Renomear binário e logs (`kr-probe` → `kamen-rider-pc`).
- Manter GPL-2.0+ e créditos (bbport, shadPS4, LibAtrac9) no NOTICE.

---

## Fase 5 — Validação completa 🟡

- Rodar todos os modos: Story (todas as missões), Time Survival, Free Mission, Custom Soundtrack, Contents, Options.
- Todos os riders, trocas de forma, TAG, chefes e cutscenes.
- **Sessões longas** (1–2 h): vazamento de memória (GPU e RAM), estabilidade do áudio.
- Save/load no meio do jogo, autosave, troféus offline.
- **Regressão automatizada:** rotas por `BB_PAD_FILE` + captura de quadros + dump de áudio, comparadas entre versões.

---

## Fase 6 — Distribuição 🟢

- **Nunca distribuir arquivos do jogo.** O jogador usa o próprio dump.
- Assistente de instalação: SELF→ELF, mesclar patch, `prepare/link`, gerar cache de shaders, tudo a partir do dump do usuário.
- Pacote Linux (AppImage ou Flatpak), Steam Deck, e avaliar Windows (o CMake do bbport já tem o caminho Win32).
- README, licença, créditos, como reportar bugs.

---

## Ordem sugerida das próximas sessões

1. **2.1** benchmark reproduzível
2. **2.3** desligar o que é do Bloodborne (ganho rápido, medido)
3. **2.2** picos / cache de shaders
4. **3.1** zerar funções que faltam (rápido; evita surpresas nos testes longos)
5. **2.4** 60 FPS (investigação no Ghidra, depois implementação)
6. **3.2** teclado próprio
7. **2.5 / 2.6 / 2.8** loading, áudio, threads
8. **2.7** resolução/4K → Fase 4 → Fase 5 → Fase 6

Cada passo termina com um relatório `docs/relatorios/<data>_<passo>.md`.
