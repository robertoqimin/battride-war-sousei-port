# Desempenho 5: perfil da lógica do jogo

**Data:** 2026-11-12

## Resposta curta

**A lógica do jogo não é gargalo.** Em combate na 1ª missão (4 min de rota, ~10 mil quadros):

| Thread do jogo | Trabalho real | Esperando |
|---|---|---|
| `main` (laço principal e apresentação) | **~6%** (3,3% código do jogo + 2,2% runtime) | 94%: vsync e GPU |
| `runTaskMain` | ~11% | 89% |
| `TaskJobThread_1..4` | <1% cada | 99% |

- Trabalho da thread principal **por quadro**: mediana **1,1 ms**, p99 **8 ms**, p99,9 **10 ms**, contra um orçamento de 16,7 ms. Só passou de 16,7 ms em **telas de carregamento**.
- Os poucos quadros lentos que sobram em combate (2 em 4 minutos nesta rodada) são **eventos isolados**, não CPU sobrecarregada:
  - **troca de cena**: de 265 para 2.028 draws num quadro, com **58 imagens novas e 55 alocações Vulkan** (uma leva nova de inimigos ou efeitos aparecendo pela primeira vez);
  - um pico de **2,4 ms no kernel** num quadro normal fora isso.
- Somando os passos anteriores (GPU com ~55% de folga, envio de comandos 82% ocioso, lógica ~6%), **o jogo tem folga em tudo** nesta máquina a 60 FPS.

## A ferramenta: `KR_GAME_PROFILE`

O `perf` não está instalado, e o `ptrace` (gdb, eu-stack) é bloqueado pela política do kernel sem sudo. Por isso fiz um amostrador dentro do processo:

- **`KR_GAME_PROFILE=<arquivo>`** (`port/src/runtime_thread.c`): a cada 1 ms, um sinal para cada thread da lógica (`KR_GAME_PROFILE_THREADS`, padrão `main,runTaskMain,TaskJobThread`). O handler grava:
  - o instante e o RIP (função do jogo, ou símbolo do port via `dladdr`);
  - se a thread está **bloqueada numa syscall**;
  - os endereços de retorno do jogo e do port encontrados na pilha.
- **`tools/game_profile.py`**: agrupa por thread, separa **código do jogo** (por função), **runtime** (pelo import chamado, via o endereço de retorno e a tabela PLT) e **bloqueado**, e lista os segundos em que a thread principal ficou ocupada.

### Erros de medição que corrigi no caminho

1. **Detectar "bloqueado".** Quando um sinal interrompe uma syscall que pode ser reiniciada, o kernel volta o RIP **para cima** da instrução `syscall`, e não para depois dela. A primeira versão só olhava depois, e as threads de job pareciam 99% ocupadas, quando na verdade esperavam.
2. **Pilha da thread principal.** Ela roda perto do topo da pilha que o port aloca. A leitura de 4 KB a partir do `sp` passava do fim, voltava parcial e era descartada inteira, e a thread principal ficava sem nenhuma função identificada. Agora a leitura parcial é aproveitada.

## O que a thread principal faz enquanto espera

A função `0x62d5f0` do jogo é o laço de apresentação:

```
while (*label_do_buffer != 1) scePthreadYield();  // espera a GPU liberar o buffer de tela
sceVideoOutSubmitFlip(...);
sceKernelWaitEqueue(...);                          // espera o flip (vsync)
```

- **59%** do tempo em `sceKernelWaitEqueue`: espera normal pelo vsync, sem custo de CPU.
- **34%** girando no `scePthreadYield` à espera do buffer. Isso **gasta CPU** (é o que aparecia como 42% de um núcleo no `/proc`), mas fica fora do caminho crítico.

**Possível otimização (não aplicada):** trocar esse yield por uma pausa curta, como fiz nas threads de áudio, liberaria quase meio núcleo. Mas essa espera fica bem no caminho do ritmo dos quadros: uma pausa de 1 ms pode atrasar um flip. Só vale a pena com uma espera mais esperta, que acorde quando a GPU escrever o label. Fica para quando o alvo for hardware fraco.

## Arquivos alterados

- `port/src/runtime_thread.c`: `KR_GAME_PROFILE` (amostrador contínuo).
- `port/src/runtime.h`, `port/src/probe.c`: inicialização.
- `tools/game_profile.py` (novo, MIT): análise.

**Ainda não foram enviadas ao GitHub.**

## Próximos passos sugeridos

1. **Picos de troca de cena:** pré-criar ou reaproveitar as imagens e alocações que aparecem quando uma leva nova de inimigos surge (58 imagens num quadro).
2. **Espera do label sem girar** (para hardware fraco).
3. Fora de desempenho, o que ficou pendente: C++ unwind (5 funções), a thread do `AvPlayer` nos vídeos, e a validação jogando até o fim da 1ª missão.
