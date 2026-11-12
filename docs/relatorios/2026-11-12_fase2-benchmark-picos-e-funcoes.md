# Fase 2/3 — Benchmark, custos do Bloodborne, picos e funções faltantes

**Data:** 2026-11-12
**Itens do roadmap:** 2.1 (benchmark), 2.3 (custos herdados), 2.2 (picos), 3.1 (funções faltantes) + vazamento de pilhas de thread.

---

## Resumo

| Item | Resultado |
|---|---|
| **2.1 Benchmark** | `tools/bench/` pronto: rota fixa e resumo em JSON. **Repete** entre execuções. |
| **2.3 Custos do Bloodborne** | Nenhum pesa de forma mensurável (GPU ~92% ociosa em todos). Os inúteis para este jogo foram **desligados por padrão**. |
| **2.2 Picos** | **No combate, 99,8% dos quadros saem em 16,7 ms.** Só 4 quadros passam de 40 ms em ~3 min, todos em **transições de carregamento**; nenhum no combate. |
| **3.1 Funções faltantes** | **137 → 5.** Sobram só as do unwind de exceções C++, que o jogo não usou até agora. |
| Vazamento | As pilhas de thread nunca eram liberadas. Agora existe um **pool de reuso**. |

![Combate a 60 FPS com os novos padrões](img/2026-11-12_fase2-desempenho_combate-60fps.png)

---

## 2.1 Benchmark reproduzível

- `tools/bench/route_story.sh`: rota fixa (boot → menus com ○ → Story → tutorial com OPTIONS → ~65 s de combate com movimento, □□△ e virada de câmera).
- `tools/bench/bench.sh <nome> [VAR=valor…]`: roda a rota com `BB_FRAME_STATS` e `BB_FPS_PRINT`.
- `tools/bench/summarize.py`: produz um JSON com FPS, pior quadro, janelas com pico, % da GPU ociosa, relatórios de *stall* e tempo até o combate.
- **Repetibilidade:** `base_a` e `base_b` deram 59.95 e 59.96 FPS no combate, mesmo pior quadro e mesmo tempo até o combate (90.8 s).

---

## 2.3 A/B dos recursos herdados do Bloodborne

| Variante | FPS combate | Pior quadro (combate) | Janelas com pico (combate) | GPU ociosa |
|---|---|---|---|---|
| base_a | 59.95 | 33.3 ms | 2/11 | 91.9% |
| base_b | 59.96 | 33.3 ms | 1/11 | 92.0% |
| object motion off | 59.98 | 33.3 ms | 1/11 | 92.7% |
| upscaler off | 59.96 | 33.4 ms | 2/11 | 91.9% |
| image write traps off | 60.00 | 16.7 ms | 0/11 | 92.9% |
| layer watch off | 60.00 | 17.0 ms | 0/11 | 93.0% |
| guest hooks off | 60.00 | 16.9 ms | 0/11 | 92.8% |

**Leitura:** as diferenças estão dentro do ruído (uma janela a mais ou a menos). Nada disso explica os picos grandes, que continuam em todas as variantes.

**Decisão (`port/src/probe.c`, padrões `kr-port`):**
- `BB_OBJECT_MOTION=0`: vetores de movimento só servem a upscaler temporal ou geração de quadros.
- `BB_UPSCALER=none`: o FSR nunca achava o "scene color" deste jogo e só gastava processamento.
- `BB_GUEST_HOOKS=0`: ganchos de código feitos para endereços do Bloodborne.
- *Write traps* e *layer watch* **continuam ligados**. Eles garantem que texturas escritas pela CPU apareçam certas; desligar pode causar erros de imagem.
- Qualquer um pode ser religado pela variável de ambiente.

---

## 2.2 Investigação dos picos

### Ferramentas novas
- **`KR_STALL_PROFILE=1`** (`runtime_thread.c`): depois de cada criação de thread pelo jogo, amostra **todas** as threads a cada 1 ms durante 200 ms (RIP e endereços de retorno do eboot) e imprime os pontos mais frequentes de cada uma.
- **`BB_FRAME_LOG=<arquivo>`** (do bbport): uma linha por quadro, com tempo, uploads, draws, compilações e alocações.

### O caminho da investigação
1. Em todos os runs a 60 FPS, **todo pico vinha logo depois de o jogo criar uma thread**: `Object_AsyncLoad`, `Enemy_AsyncSetup`, `update ngs2 stream sound` ou as do AvPlayer.
2. O amostrador mostrou o que cada thread fazia:
   - **`runTaskMain`** (laço do jogo, `0x24a740`): roda update/draw, sinaliza um *event flag* e espera outro (`0x24a76c` → `sceKernelWaitEventFlag`);
   - **`main`**: faz o flip e espera a GPU terminar o quadro (`0x62d650`, laço com `scePthreadYield`);
   - **threads de job**: ociosas.
3. O log por quadro fechou a questão. Em ~170 s (10.114 quadros), **só 4 passaram de 40 ms**:

| Tempo | Duração | Momento | Draws | Compilações |
|---|---|---|---|---|
| 14.4 s | 66.7 ms | início do vídeo de abertura | 5 | 0 |
| 26.6 s | 50.0 ms | título → menu | 32 | 0 |
| 28.9 s | 66.7 ms | menu (13 texturas novas, 14.8 MB) | 116 | 0 |
| 91.1 s | 116.7 ms | **carregamento do início da missão** | 281 | 0 |

4. **No combate (92–160 s): 4.065 de 4.072 quadros em 16,7 ms (99,8%)**. Só 7 quadros de 33 ms, exatamente quando os draws passam de 1.000 (ondas de inimigos surgindo).

### Conclusão
- Os picos são **engasgos de carregamento** nas transições de cena: carga de objetos, inimigos e stream de som. Nesses quadros a GPU e a camada gráfica quase não trabalham (sem compilação de shader, sem alocação); a espera é do lado do jogo. É provável que o PS4 também tenha esses engasgos.
- **No gameplay o port está essencialmente perfeito a 60 FPS.**
- Se quiser atacar as transições depois, o caminho é medir a **leitura de arquivos** nesses momentos (o loader faz I/O síncrono) e as cópias de textura da CPU. Os 7 quadros de 33 ms com mais de 1.000 draws podem melhorar com menos custo por draw.

---

## 3.1 Funções faltantes: 137 → 5

| Grupo | Implementação |
|---|---|
| **Rtc** (`sceRtcParseRFC3339`, `sceRtcParseDateTime`, `sceRtcTickAddMinutes`) | `runtime_rtc.c`: aceita RFC 3339 (com fração e fuso), RFC 2822 e asctime, convertidos para UTC, com validação de campos (o shadPS4 não valida) |
| **Troféus** (destruir contexto e handle, informações de grupo, 3 ícones) | `runtime_services.c`: os ícones respondem "ícone não encontrado" (`0x80551614`) |
| **Online** (Score, TUS/TSS, Matching2, WebApi, word filter, bandwidth, lookup, auth, request) | `runtime_kroffline.c`: "desconectado" (`0x80550006` / Matching2 `0x80550C26`); deletes e shutdowns retornam OK |
| **Json2** (62 métodos C++ de `sce::Json`) | `runtime_kroffline.c`: stubs que respeitam o ABI (construtores devolvem `this`, getters 0, consultas devolvem um valor nulo, iteradores vazios, `parse` falha). Offline, nunca recebe dados. |
| `sceHttpUriEscape` | percent-encoding real (RFC 3986) |
| Commerce dialog, Facebook dialog, DiscMap | cancelado / status / "sem bitmap" (como no shadPS4) |

**Restam 5** (`sceKernelGetModuleInfoForUnwind/FromAddr`, `__elf_phdr_match_addr`, `_is_signal_return`, `sigreturn`). São usadas **só quando o jogo lança uma exceção C++**, o que não aconteceu em nenhum teste. Implementá-las exige levar o endereço do `.eh_frame` de cada módulo para o formato de boot do bbport. Fica como pendência de baixa prioridade.

No benchmark final, nenhum stub offline foi chamado. Os stubs existem para quando o jogo tentar recursos online (rankings, por exemplo).

---

## Vazamento de pilhas de thread (corrigido)

- O bbport **nunca liberava** a pilha de uma thread do jogo. Este jogo cria uma thread por som em streaming (31 em 3 minutos, cerca de 320 KB cada).
- Agora existe um **pool de reuso** (`runtime_thread.c`):
  - depois de um `join`, a pilha volta ao pool na hora;
  - em thread *detached*, volta 2 s depois de ela terminar, porque a glibc mantém o descritor da thread na própria pilha até o fim;
  - `create` reaproveita uma pilha do mesmo tamanho antes de mapear outra.
- No benchmark de 170 s, com dezenas de threads de stream, não houve erro. O contador `stacks reused` só sai no relatório de fim normal, e o benchmark termina pelo watchdog, então ele ainda não foi visto rodando; a confirmação fica para uma sessão longa (Fase 5).

---

## Benchmark final (todas as mudanças)

| | FPS combate | Pior quadro combate | GPU ociosa | Pior quadro geral | Erros |
|---|---|---|---|---|---|
| final | 59.95 | 33.3 ms | 92.5% | 100 ms (transição) | 0 |

---

## Próximos passos sugeridos

1. **3.2 Teclado próprio** do jogo (○ confirmar, □/△ ataques, OPTIONS). Hoje é o layout do Bloodborne.
2. **2.5 Loading**: medir a leitura de arquivos nas transições (onde estão os 4 picos restantes).
3. **2.7 Resolução**: 1440p/4K, já que sobra 92% de GPU.
4. Unwind de exceções C++ (as 5 funções), antes da validação longa da Fase 5.

---

## Arquivos alterados neste passo

- `tools/bench/route_story.sh`, `bench.sh`, `summarize.py`, `series1.sh` (novos)
- `port/src/probe.c`: padrões kr-port + base do eboot para o amostrador
- `port/src/runtime_thread.c`: `KR_STALL_PROFILE`, pool de pilhas, `tid` por thread
- `port/src/runtime_kroffline.c` (novo), `runtime.c` e `runtime.h`: registro
- `port/src/runtime_rtc.c`, `runtime_services.c`, `runtime_krstubs.c`: Rtc, troféus, Facebook
- Logs e JSON: `out/bench/*.log|json`; log por quadro: `out/bench/flog.frames`
