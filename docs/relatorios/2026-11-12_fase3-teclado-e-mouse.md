# Fase 3.2 — Teclado e mouse próprios do jogo

**Data:** 2026-11-12
**Item do roadmap:** 3.2 (entrada). Antes o teclado usava o layout do Bloodborne: Enter era ✕ (que neste jogo é "voltar"), Espaço era esquiva/corrida e o mouse fazia R1/L2.

---

## Resumo

- O layout padrão de teclado e mouse agora segue **os controles deste jogo**, tirados da tela de configuração de controles dele (操作設定).
- **Testado jogando só com o teclado:** do boot ao combate, com 28 inimigos derrotados, combo de 46, o super especial (R2), a pausa e a volta da pausa. Todas as teclas geraram o botão certo.
- O controle (DualShock/Xbox) não mudou: os botões ficam nas mesmas posições do PS4.

![Tela de controles do próprio jogo (操作設定)](img/2026-11-12_fase3-teclado_operacao-do-jogo.png)

---

## Os controles do jogo (descobertos neste passo)

Capturei as telas de tutorial, a lista de golpes (必殺技確認) e a configuração de controles (ゲーム設定 → 操作設定) com rotas automáticas:

| Botão PS4 | Ação no jogo |
|---|---|
| □ | ataque normal (combos □□□…) |
| △ | especial 1 |
| ○ | especial 2 / **confirmar** nos menus |
| ✕ | pulo / Rider Cancel / **voltar** nos menus |
| R2 | super especial (超必殺技) |
| L1 | defesa |
| △ + ○ | especial 3 |
| □ + ✕ | Rider Glide |
| L1 / R1 | trocar o rider da dupla (TAG) |
| OPTIONS | pausa / próxima página do tutorial |
| analógico esquerdo | mover |

---

## Layout novo de teclado e mouse

| Ação | Teclas |
|---|---|
| Mover | **W A S D** (Alt esquerdo segurado = andar devagar) |
| Câmera | **mouse**; teclado numérico 8 / 4 / 5 / 6 |
| □ ataque normal | **botão esquerdo do mouse**, J |
| △ especial 1 | **botão direito do mouse**, I |
| ○ especial 2 / confirmar | **E**, L, **Enter** |
| ✕ pulo / voltar | **Espaço**, K, **Backspace** |
| △+○ especial 3 | **F** (uma tecla aperta os dois) |
| □+✕ Rider Glide | **V** (uma tecla aperta os dois) |
| R2 super especial | **R**, botão do meio do mouse |
| L1 defesa | **Shift esquerdo**, botão lateral 1 do mouse |
| R1 trocar rider | **Tab**, botão lateral 2 do mouse |
| L2 / L3 / R3 | Q / Ctrl esquerdo / C |
| OPTIONS pausa | **Esc** |
| Direcional | setas, 1–4, roda do mouse (cima/baixo) |
| Touchpad | G |

Para jogar só no teclado, **I J K L** formam o losango dos botões (△ □ ✕ ○), na mesma posição do controle.

**Remapear:** como no bbport, um arquivo apontado por `BB_CONFIG` com linhas `key.<entrada>=<teclas>` e `pad.<entrada>=<botões>`. Exemplo: `key.circle=E,Return`. Isso será substituído pelo arquivo de configuração próprio na Fase 4.

O mouse só controla o jogo depois que a janela captura o ponteiro (comportamento herdado do bbport). Os botões do mouse não disparam golpes antes disso.

---

## Mudanças de código

- `port/src/runtime_pad/bindings.c`:
  - `bind_defaults` traz o layout novo, marcado com `kr-port`;
  - saíram a esquiva, a corrida e o pulo do Bloodborne, que geravam pulsos de ○ (neste jogo, ○ é o especial 2);
  - entrou uma checagem de limite: no máximo 4 teclas por entrada.
- **Ferramentas de teste** (`runtime_pad/sample.c`):
  - **`key:<nome>` no `BB_PAD_FILE`**: simula uma tecla ou um botão do mouse pelo mesmo caminho do teclado de verdade, por exemplo `key:Space` e `key:Mouse_Left`. Isso permite testar o teclado sem `xdotool` (o sistema usa Wayland);
  - **`KR_PAD_TRACE=1`**: mostra os botões, gatilhos e o analógico que o jogo recebe a cada mudança.
- `tools/bench/route_keyboard.sh`: rota de teste só com teclado.
- `tools/bench/route_tutorial_dump.sh`, `route_pause_dump*.sh`, `route_ctlcfg_dump.sh`: capturam o tutorial, a lista de golpes e as telas de configuração.

---

## Validação (run `out/run_kbd.log`)

Cada tecla foi apertada uma vez e conferida no trace:

| Teclas | Botão recebido |
|---|---|
| Mouse esq., J | □ `0x8000` |
| Mouse dir., I | △ `0x1000` |
| E, L, Enter | ○ `0x2000` |
| Espaço, K, Backspace | ✕ `0x4000` |
| F | △+○ `0x3000` |
| V | □+✕ `0xC000` |
| Shift, Mouse X1 | L1 · Tab, Mouse X2 → R1 |
| R, Mouse meio | R2 (gatilho 255) · Q → L2 (255) |
| C / Ctrl / G | R3 / L3 / touchpad |
| setas, 1, 2 | direcional |
| W / D | analógico esquerdo subindo suavemente (128 → 37 / 128 → 208) |
| Esc | OPTIONS |

![Rota só com teclado: combate, super especial (R), movimento e pausa](img/2026-11-12_fase3-teclado_rota-so-teclado.jpg)

---

## O que fica para depois

- **Você testar com teclado e mouse de verdade**, principalmente a sensibilidade da câmera no mouse (`mouse_sensitivity` no `BB_CONFIG`).
- Os ícones do jogo continuam sendo os do PS4. Trocar por ícones de teclado ou Xbox exige substituir texturas; fica opcional.
- Os botões do controle Xbox ficam na posição física do PS4 (A = ✕, B = ○). Como o jogo é japonês, **B confirma e A volta**. Se preferir o contrário, dá para remapear com `pad.circle=a` / `pad.cross=b`.

## Próximos passos sugeridos

1. **2.5 Loading**: medir a leitura de arquivos nas transições.
2. **2.7 Resolução**: 1440p/4K.
3. Unwind de exceções C++ (as 5 funções que faltam).
