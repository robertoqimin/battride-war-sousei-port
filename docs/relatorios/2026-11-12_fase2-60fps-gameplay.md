# Fase 2.4 — 60 FPS no gameplay

**Data:** 2026-11-12
**Pergunta:** dá para ter 60 FPS no gameplay?
**Resposta:** **sim, e já está implementado.** Por padrão o port roda o gameplay a **60 FPS**, com a velocidade do jogo correta.

---

## Resultado

| | Antes (original) | Agora (padrão) |
|---|---|---|
| FPS no combate | 30.1 | **60.0** |
| Velocidade do jogo (cronômetro da missão) | 16 s em 5 ciclos | **16 s em 5 ciclos** (idêntica) |
| Pior quadro na maioria das janelas de 5 s | 50 ms | **16.7 ms** |
| Chegada à missão no roteiro de teste | referência | **~4 s mais cedo** (os menus e o loading andam junto com os quadros) |

![Cronômetro da missão: 60 FPS (esquerda) × 30 FPS (direita), mesmo roteiro](img/2026-11-12_fase2-60fps_cronometro-60-vs-30.png)

Os dois lados avançam de 3 em 3–4 s, no mesmo ritmo. A diferença do valor inicial é só porque a 60 FPS a missão começou antes.

---

## Como a engine controla o FPS (achado no código)

**Limitador de quadros:** função em `0x24a8b0` do eboot. A cada quadro, ela:
1. calcula o tempo alvo = `1000 / FPS_alvo` ms;
2. mede o tempo desde o último quadro pelo TSC (`sceKernelReadTsc`);
3. dorme o que falta menos 1 ms e completa com *busy-wait*;
4. grava a **escala de tempo da lógica** = `delta_ms / (1000 / FPS_base)` e limita o delta a 50 ms.

**Os dois valores vêm do arquivo de dados `nativeORBIS/system/sysConf.boot`** (96 bytes, magic `BOT\0`), lido em `0x122370`:

| Offset | Valor original | Significado |
|---|---|---|
| `0x10` | 60.0 | **FPS-base**: a taxa para a qual a lógica foi escrita |
| `0x14` | 30.0 | **FPS-alvo** do gameplay (a trava) |
| `0x1c…` | 1920×1080 … | resoluções |

A conclusão importante: **a lógica do jogo foi feita para 60**. No PS4, a 30 FPS, ela roda com escala de tempo 2.0. Liberar 60 leva a escala para 1.0, que é o modo nativo da engine, e não um hack de velocidade. Nos menus, o jogo já usava 60.

Hipóteses descartadas no caminho:
- a taxa de flip do vídeo é 0 (60 Hz);
- o modo de flip é vsync simples;
- os 3 `sceKernelUsleep` do jogo são de threads auxiliares.

---

## Implementação

`port/src/runtime_file.c`, função `sysconf_file`, marcada com `kr-port:`:
- Quando o jogo abre `…/system/sysConf.boot`, o runtime confere o formato (96 bytes, `BOT\0`) e entrega uma **cópia em memória (memfd)** com o FPS-alvo trocado. **Nenhum arquivo do jogo é modificado.** É o mesmo mecanismo que o bbport usa para corrigir o layout de menu do Bloodborne.
- Opção **`KR_FPS`**:
  - padrão **60**;
  - `KR_FPS=30` mantém o original;
  - aceita de 30 a 240, mas acima de 60 o limite é a tela (vsync de 60 Hz no flip).
- Log: `Runtime: frame rate 60 FPS in gameplay (game's 30; logic base 60) - KR_FPS`.
- Se o arquivo não tiver o formato esperado, nada é alterado e o log avisa.

---

## Validação

| Run | Config | Resultado |
|---|---|---|
| run60 | `sysConf` trocado manualmente na visão `game/` (teste, já desfeito) | 60 FPS, cronômetro 34→57 em 34 s com ~11 s de pausa |
| run61 | patch pelo runtime (padrão) | 60 FPS; cronômetro +16 s em 5 ciclos |
| run30 | `KR_FPS=30`, mesmo roteiro | 30 FPS; cronômetro +16 s em 5 ciclos → **mesma velocidade** |

---

## Ainda falta confirmar jogando

O roteiro automático cobre o menu, o tutorial, o movimento, combos simples e a pausa. Vale você jogar e prestar atenção em:
- **sensação dos combos e janelas de input** (tempo para emendar golpes);
- **cutscenes em tempo real** e sincronia com o áudio;
- **física, partículas e efeitos** (duração e velocidade);
- **chefes e trechos com muitos inimigos**, para ver se segura 60 com carga maior.

Se algo ficar estranho em algum trecho, `KR_FPS=30` volta ao original na hora.

---

## Próximos passos de desempenho (roadmap)

1. **2.1** benchmark reproduzível: o roteiro de hoje vira `tools/bench.sh`.
2. **2.3** desligar os custos do Bloodborne (object motion, FSR sem efeito).
3. **2.2** picos restantes: ainda há janelas com pior quadro de 50–117 ms, nas transições e no loading.
4. Avaliar **120/144 Hz** em monitores de taxa alta: exige flip sem a trava de 60 Hz.

---

## Arquivos alterados neste passo

- `port/src/runtime_file.c`: `sysconf_file` + opção `KR_FPS`
- `patches/sysconf/`: cópias locais usadas no teste (original e 60 FPS; **não distribuir**)
- `docs/relatorios/img/2026-11-12_fase2-60fps_cronometro-60-vs-30.png`
- Logs: `out/run60.log`, `out/run61.log`, `out/run30.log`
