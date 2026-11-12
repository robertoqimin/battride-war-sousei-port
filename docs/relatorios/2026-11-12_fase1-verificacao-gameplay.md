# Fase 1 — Verificação de gameplay

**Data:** 2026-11-12
**Passo:** verificar se o jogo vai além do tutorial: menu principal, Story Mode, combate, estabilidade e desempenho.

---

## Resumo

**O jogo é jogável no PC.** Numa execução de 160 s com entrada automática:

- passou pela abertura e chegou ao **menu principal** (メインメニュー: Story Mode, Time Survival, Free Mission, Custom Soundtrack, Contents, Options);
- entrou no **Story Mode** e no primeiro tutorial;
- **combate funcionando**: o Ghost anda, ataca em combo (□), usa especial (△), e aparecem efeitos de partículas, diálogo, HUD e minimapa;
- o **menu de pausa** (ポーズメニュー) abre e fecha, com missão, tempo e parâmetros do personagem;
- **30 FPS estáveis** no gameplay e **60 FPS** nos menus. O próprio jogo controla o ritmo e não foi preciso limitador;
- **nenhum crash**, nenhuma parada por função não implementada e nenhum erro de GPU durante o combate.

![Combate](img/2026-11-12_fase1-verificacao_combate.png)

![Menu principal](img/2026-11-12_fase1-verificacao_menu-principal.png)

![Sequência: tutorial, gameplay e pausa](img/2026-11-12_fase1-verificacao_gameplay-sequencia.png)

---

## O que foi feito neste passo

### 1. Contador de FPS corrigido
`BB_FPS_PRINT=1` não imprimia nada: o bbport contava os quadros dentro do overlay, que só roda com o menu dele aberto.
**Correção:** a contagem agora fica em `Presenter::PresentImage`, a cada quadro exibido (`gpu/render/presenter.cpp`). A do overlay foi desligada (`gpu/shim/bbport_overlay.cpp`). Os dois pontos estão marcados com `kr-port:`.

### 2. Botões descobertos
| Situação | Botão |
|---|---|
| Confirmar em diálogos e menus | **○ círculo** (jogo japonês) |
| Passar as páginas do tutorial | **OPTIONS** (indicado na tela) |
| Ataque normal / especial | □ / △, ○, △+○ |
| Pausa | OPTIONS |

### 3. Execuções
| Run | O que fez | Resultado |
|---|---|---|
| run17 | 125 s, ○ e depois movimento | chegou ao menu principal e ao tutorial; ficou no painel do tutorial (precisava de OPTIONS) |
| run18 | 145 s + FPS | FPS medido: 30 no jogo, 60 nos menus |
| run19 | 160 s, ○ → OPTIONS → luta | **gameplay completo**: movimento, combos, efeitos, pausa |

FPS do run19, um valor a cada 5 s:
`30.4 30.2 53.6 30.1 30.2 30.1 36.1 60.0 60.0 60.0 60.0 58.0 30.1 30.1 30.2 30.1 …`. Os primeiros são do boot e da abertura, os de 60 são do menu e os de 30 são do Story Mode.

---

## Log durante o gameplay

- Avisos de formato de textura (R32G32B32, A2B10G10R10 com sinal): aparecem uma vez na inicialização porque a RX 6750 XT não suporta esses formatos de forma nativa. Isso é normal no shadPS4/bbport e não afetou a imagem.
- Stubs do kr-port: cada um aparece uma vez (IME, USB, NetCtl, compartilhamento, streaming e afins), como esperado.
- Memória de vídeo estável: nenhuma alocação nova durante a luta.

---

## Estado atual

| Área | Estado |
|---|---|
| Boot, menus, save | ✅ |
| Vídeos (AvPlayer) | ✅ |
| Renderização 3D, efeitos, HUD | ✅ visualmente correta |
| Combate e entrada | ✅ testado com controle virtual (por script) |
| Desempenho | ✅ 30 FPS estáveis (o mesmo do PS4) |
| **Áudio** | ⚠️ **mudo**: Ngs2/Audiodec ainda são stubs |
| Teclado | ⚠️ o mapeamento padrão é o do Bloodborne (ex.: Espaço = esquiva, Enter = ✕). Funciona, mas não foi pensado para este jogo. **Recomendo um controle** por enquanto. |
| Online | ⛔ desligado |

---

## Como jogar agora (manualmente)

```sh
cd ~/kr-port/port
./out/bb-probe ~/kr-port/out/boot-linked.bin \
  --app0 ~/kr-port/game --user ~/kr-port/user --timeout 0
```
`--timeout 0` desliga o watchdog. Com um controle conectado: ○ confirma e OPTIONS passa o tutorial e pausa.

---

## Próximos passos sugeridos

1. **Áudio de verdade**, o maior item que falta: o mixer Ngs2 (vozes, racks, mastering) e a decodificação ATRAC9 (LibAtrac9 já linkado).
2. **Teclado próprio do jogo**: refazer o mapeamento padrão para Battride War (○ confirmar, □/△ ataques, OPTIONS).
3. Tirar o resto da camada do Bloodborne: menus do overlay, upscaler com a heuristica de *scene color* dele, ganchos de câmera/HUD.
4. Testar fases mais longas: troca de forma (Damashii), chefes, a troca entre riders (TAG) e o salvamento no meio do jogo.

---

## Arquivos alterados neste passo

- `port/gpu/render/presenter.cpp`: contador de FPS no present (kr-port)
- `port/gpu/shim/bbport_overlay.cpp`: contador do overlay desligado
- `docs/relatorios/img/2026-11-12_fase1-verificacao_*.png`: capturas (só locais, não distribuir)
- Logs: `out/run17.log`, `out/run18.log`, `out/run19.log`
