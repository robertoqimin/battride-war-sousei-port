# Fase 1 — Áudio: mixer Ngs2 + ATRAC9

**Data:** 2026-11-12
**Passo:** tirar o jogo do mudo. Antes, o Ngs2 e o Audiodec eram stubs silenciosos.

---

## Resumo

**O jogo tem som.** Nas medições do áudio de saída:

| Saída | O que é | Resultado |
|---|---|---|
| porta 8 (BGM) | música | contínua do início ao fim, sem cortes nem cliques, em estéreo de verdade (correlação L/R 0.39) |
| porta 0 (efeitos e vozes) | efeitos e vozes | som nos diálogos iniciais e durante o combate (golpes, efeitos, falas) |
| porta 1 (vídeos) | áudio dos vídeos (AvPlayer) | já funcionava pelo bbport; a abertura tem som |

O desempenho não mudou: 30 FPS no jogo e 60 nos menus.

**Amostra para ouvir:** `audio/2026-11-12_fase1-audio_amostra-menu-e-combate.opus` (55 s: menu, Story Mode e combate; BGM e efeitos juntos).

> Não consigo ouvir daqui. A validação foi feita medindo o sinal: nível, picos, saltos entre amostras e correlação estéreo. **Vale você ouvir a amostra** para confirmar o timbre, o volume e o pitch.

---

## Como o jogo usa o áudio (descoberto neste passo)

- Os sons ficam em **11.236 arquivos ATRAC9 (`.at9`)**: BGM, vozes e efeitos. Os efeitos vêm empacotados em `.atp`.
- **Tudo passa pelo Ngs2** (sampler). O jogo **não usa o Audiodec**: nenhuma chamada de decodificação foi registrada.
- Há dois modos de tocar:
  1. **Efeitos e vozes**: o arquivo `.at9` inteiro fica na memória. `sceNgs2ParseWaveformData` gera os blocos, e o jogo os passa para a voz.
  2. **BGM em streaming**: o *play* chega **sem dados**, e o jogo vai acrescentando pedaços de **8 KB** de um buffer circular de 3 posições. Ele sabe quanto foi consumido lendo `decodedDataSize` no estado da voz, e o loop da música é ele mesmo quem refaz.
- IDs dos parâmetros de voz: seguem a ordem das structs do shadPS4 e foram confirmados pelos tamanhos que o jogo envia.
  - Voz: `0x1` níveis da matriz, `0x2` matriz da porta, `0x3` volume da porta, `0x5` patch, `0x6` evento.
  - Sampler: `0x10000000` setup, `0x10000001` blocos, `0x10000004` sair do loop, `0x10000005` pitch.
- No código do jogo (eboot `0x2d6ff1` e `0x2d7d41`):
  - evento 0 é **play**;
  - *state flags* ≠ 0 significa "ainda tocando";
  - o bit 0x2 marca "tocando";
  - o bit 0x10 é tratado como erro.
- Buffer de render: float, 8 canais, 256 quadros. A matriz de níveis é organizada por saída (`level[saída × canais_entrada + entrada]`).

---

## O que foi implementado

`port/src/runtime_ngs2.c` foi reescrito: deixou de ser o mixer silencioso e virou um mixer real.

- **ParseWaveformData**: lê os chunks RIFF `fmt` (ATRAC9 extensible e configuração), `fact` (total de amostras e atraso do encoder) e `smpl` (loop). Devolve 1 bloco ou, quando há loop, introdução + loop repetido até "sair do loop". Também aceita PCM 16-bit.
- **Vozes do sampler**: setup, blocos (substituir ou acrescentar), relocação de endereço, pitch e saída do loop.
- **Parâmetros de voz**: matriz de níveis, matriz de porta e os eventos play, stop, kill, pause e resume.
- **Decodificação ATRAC9** com a LibAtrac9, que já vinha no bbport.
- **Streaming** com FIFO própria por voz. Os pedaços de 8 KB não são múltiplos do superquadro de 384 bytes, então os bytes são copiados ao chegar e decodificados um superquadro inteiro de cada vez. O `skip` e o total de amostras do primeiro pedaço valem para o stream todo.
- **Reamostragem linear**: pitch × taxa do arquivo / taxa do sistema.
- **Mixagem** pela matriz de cada voz no buffer float de 8 canais.
- **Limitador suave** acima de 0.8. Em combate, com muitos sons juntos, o pico chegava a 1.44; agora fica em ≤ 1.0. No PS4 quem faz isso é o rack de mastering.
- **Estado da voz**: flags (em uso / tocando / pausado), amostras decodificadas e `decodedDataSize`.
- **Diagnóstico**: `KR_TRACE_NGS2=1` mostra parâmetros, plays, blocos e fim de voz. `KR_NGS2_GAIN=x` aplica um ganho geral.

Ferramenta usada: `BB_AUDIO_DUMP=<prefixo>` (do bbport) grava o áudio de cada porta em `.f32`, que converti com ffmpeg.

---

## Execuções

| Run | Resultado |
|---|---|
| run20–21 | trace: o jogo usa só Ngs2; IDs, matriz e formato do buffer mapeados |
| run22 | primeiro som (efeitos curtos); BGM ainda muda |
| run23–24 | descoberto o streaming (play sem dados + pedaços de 8 KB) |
| run25 | **BGM contínua + efeitos no combate**; pico 1.44 (clipping) |
| run26 | com limitador: pico 1.0, FPS sem mudança |

---

## Limitações conhecidas

- **Mastering, patches e efeitos** (reverb, EQ, envelope e filtro do sampler) não são aplicados. As vozes vão direto para a saída com a matriz de níveis. O som deve ficar "seco" e talvez com volumes um pouco diferentes do PS4.
- O **volume da porta** (`0x3`) é ignorado: o jogo manda 0 em todo setup e nunca outro valor, então não pode ser um ganho.
- A **interpolação linear** no pitch é simples. Pode soar um pouco áspera quando o pitch varia muito.
- A posição exata do loop (antes ou depois do atraso do encoder) não foi confirmada; o erro máximo seria de 256 amostras (~5 ms).
- O Audiodec continua silencioso. O jogo não o usa nos trechos testados.

---

## Próximos passos sugeridos

1. **Você ouvir a amostra** e me dizer se o volume e o timbre estão certos. Se precisar, ajusto o ganho (`KR_NGS2_GAIN`).
2. **Teclado próprio do jogo** (○ confirmar, □/△ ataques, OPTIONS).
3. Envelope e filtro do sampler, se aparecerem sons "duros" (sem fade).
4. Tirar o resto da camada do Bloodborne (overlay, upscaler, ganchos).

---

## Arquivos alterados neste passo

- `port/src/runtime_ngs2.c`: mixer real (Parse, sampler ATRAC9, streaming, matriz, limitador, estado)
- `port/src/runtime_audiodec.c`: trace opcional (`KR_TRACE_NGS2`)
- `docs/relatorios/audio/2026-11-12_fase1-audio_amostra-menu-e-combate.opus`: amostra (só local, não distribuir)
- Logs: `out/run20.log` … `out/run26.log`; dumps em `out/audio22` … `out/audio26`
