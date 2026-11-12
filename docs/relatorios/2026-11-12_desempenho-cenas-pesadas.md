# Desempenho 2: cenas pesadas

**Data:** 2026-11-12

## Resposta curta

- **A GPU não é o gargalo.** No pior trecho (~1.400–1.700 draws por quadro), a placa trabalha **~7,5 ms por quadro** e fica **~11 ms ociosa** esperando comandos. Há folga para aumentar a resolução.
- **Achei e corrigi um desperdício grande de CPU.** O uso total de CPU do jogo caiu de **360% para 118%** (média em combate). O áudio continua íntegro: 0 underruns e 0 erros.
- **O que ainda segura as cenas pesadas é a thread principal do jogo** (a lógica do próprio jogo na CPU), com picos de 84–90% de um núcleo. Os quadros lentos são raros: 3 em 13 mil quadros na última rota.

## 1. GPU: medida com timestamps

O profiler de GPU herdado do bbport (`BB_GPU_PROFILE=1`) não funcionava neste jogo. Ele marca o início do quadro num ponto exclusivo do Bloodborne. Agora ele também marca o quadro no primeiro envio de comandos depois de cada flip, o que vale para qualquer jogo.

| Janela (5 s) | Quadro | GPU ociosa | GPU trabalhando |
|---|---|---|---|
| Combate normal | 16,7 ms | ~12,3 ms | ~4,4 ms |
| Cena pesada (130 s, 1.404 draws/quadro, 53,6 FPS) | 18,9 ms | ~11,4 ms | ~7,5 ms |

A thread de GPU do port fica **82% ociosa** mesmo na cena pesada (2,3 µs por draw). O envio também não é o gargalo.

## 2. O desperdício: threads de stream de áudio em espera ativa

Amostrei o uso de CPU de cada thread pelo `/proc` (não há `perf` instalado) e apareceu isto:

| Thread | CPU média | Pico |
|---|---|---|
| `update ngs2 stream sound` (várias ao mesmo tempo) | **41% + 16% + 15% + 12% + …** | **100% cada** |
| thread principal do jogo | 42% | 90% |

O jogo cria uma thread para cada som em stream (BGM, vozes e alguns efeitos; ~300 em 4 minutos). Usei o `KR_STALL_PROFILE` e o disassembly para ver o que ela faz:

```
0x2d74c0: while (!stream->done()) {   // vfunc +0x60
              stream->update();       // vfunc +0x30 → sceNgs2VoiceGetState(Flags)
              scePthreadYield();      // 0x26d890
          }
```

**No PS4**, o `yield` passa o núcleo para outras threads da mesma prioridade. **No Linux, com núcleos livres**, o `sched_yield` volta na hora e cada stream ocupa um núcleo inteiro. Em combate havia até ~7 ao mesmo tempo.

**Correção (`port/src/runtime_thread.c`):** só nas threads chamadas `update ngs2 stream…`, o yield virou uma pausa de **1 ms** (`KR_STREAM_YIELD_US`, padrão 1000; `0` volta ao yield puro). As outras threads continuam com o yield normal, porque podem usá-lo em esperas sensíveis à latência.

### Resultado (mesma rota, 240 s)

| | Antes | Depois |
|---|---|---|
| CPU total do processo (média) | **360%** | **118%** |
| Threads de stream no topo | 4+ a 100% | nenhuma |
| Erros de decodificação / *host faults* | 0 / 0 | 0 / 0 |
| Quadros >17,5 ms (após 20 s) | 4 | 3 |

### O áudio continua alimentado

Criei um contador de **underruns**: grãos de áudio em que um stream no meio da reprodução ficou sem dados. Ele aparece no `KR_NGS2_PROF=1`.

| Rodada (150 s, até 23 vozes) | Underruns |
|---|---|
| Correção (1 ms) | **0** |
| Sem correção (`KR_STREAM_YIELD_US=0`) | 0 |
| Controle: pausa forçada de 200 ms | 7.283 (prova que o contador funciona) |

### Por que importa

Nesta máquina (Ryzen 7 5700X, 16 threads) sobravam núcleos, então os FPS quase não mudaram. **Em CPUs de 4–6 núcleos**, como notebooks e o Steam Deck, essas threads disputavam núcleo com a thread principal do jogo e com as threads do port. Também é menos consumo e menos calor.

## 3. O que ainda segura as cenas pesadas

Com GPU e envio folgados, o limite que resta é a **thread principal do jogo** (lógica, IA e montagem dos comandos de desenho), com picos de 84–90% de um núcleo quando há muitos inimigos. Quando um quadro passa de 16,7 ms de CPU, ele sai em 33 ms. Para ir além, preciso de um perfil por função dessa thread na cena pesada, para separar o que é código do jogo do que são chamadas ao nosso runtime (Gnm, memória, locks).

## Outras observações

- **`Update AvPlayer`** fica em 100% durante os vídeos (abertura e prólogo), não no jogo. Provavelmente é o mesmo padrão de espera ativa, ou a própria decodificação. Prioridade menor.

## Arquivos alterados

- `port/src/runtime_thread.c`: pausa de 1 ms no yield das threads de stream (`KR_STREAM_YIELD_US`).
- `port/src/runtime_ngs2.c`: contador de underruns de stream no `KR_NGS2_PROF`.
- `port/gpu/render/rasterizer.cpp|.h`, `port/gpu/platform/videoout/display.cpp`, `port/gpu/shim/bbport_toggles.h`: o `BB_GPU_PROFILE` passa a funcionar neste jogo (quadro marcado pelo flip).

**Ainda não foram enviadas ao GitHub.**

## Próximos passos sugeridos

1. **Resolução maior** (via `sysConf`): a GPU tem ~55% de folga mesmo na cena pesada.
2. **Perfil por função da thread principal** nas cenas pesadas, para ver se parte do tempo está no runtime (que dá para otimizar) ou só no código do jogo.
3. Ver a thread do `AvPlayer` nos vídeos.
