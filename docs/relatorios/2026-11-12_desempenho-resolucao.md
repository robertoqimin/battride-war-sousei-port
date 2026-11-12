# Desempenho 3: resolução

**Data:** 2026-11-12

## Resposta curta

- **O jogo já renderiza em 1920×1080 nativo**, igual ao seu monitor.
- Achei o campo do `sysConf.boot` que define a resolução de renderização e criei a opção **`KR_RES`**. Ela vale de 720p até 8K, e o jogo inteiro acompanha: buffers auxiliares, telas e HUD.
- **RX 6750 XT:**

| `KR_RES` | Combate | Quadros >17,5 ms | Uso |
|---|---|---|---|
| `720p` | 60 FPS | 0 | PCs fracos, Steam Deck |
| `1080p` (padrão) | 60 FPS | 1 em 4.754 | — |
| **`1440p`** | **60 FPS** | **4 em 4.754** | monitor 1440p, ou supersampling num 1080p |
| `4k` | **43–55 FPS** | 670 em 4.090 | limitado pela GPU nesta placa |

O padrão continua sendo o 1080p original.

## Como foi achado

O `sysConf.boot` (96 bytes, já servido em memória para o patch de 60 FPS) tem três pares 1920×1080, em `0x1c`, `0x24` e `0x3c`.
- Criei a variável de teste `KR_SYSCONF_SET=off=valor,...` e troquei cada par por 2560×1440, observando o tamanho dos passes de renderização com o profiler de GPU.
- **Resultado:** só o par **`0x3c`/`0x40`** muda algo, e muda tudo junto: cena 2560×1440, meia resolução 1280×720 e um quarto 640×360. Os buffers de tela também passam para 2560×1440.
- Os pares `0x1c` e `0x24` não alteraram nada visível. O par 720×408 em `0x34` ficou intocado.
- Os mapas de sombra (2048×2048) não dependem da resolução.

## Qualidade

- **HUD, minimapa, textos e diálogos** ficam no lugar e na proporção certa em 720p e em 1440p (capturas locais em `out/val/k720` e `out/val/k1440`).
- O presenter reduz a imagem para a janela com filtro **bilinear**:
  - **4K → 1080p** (exatamente 2×) vira um supersampling 4× de verdade;
  - **1440p → 1080p** (1,33×) melhora as bordas, mas o bilinear descarta parte das amostras. Um filtro de redução dedicado deixaria o 1440p num monitor 1080p mais nítido. É uma melhoria possível para depois.

![1440p em combate](img/2026-11-12_resolucao_1440p.png)

## Uso

```sh
KR_RES=1440p ./out/bb-probe ...    # ou 720p, 1080p, 4k, 3200x1800 ...
```

Valores fora de 640×360 até 7680×4320 são recusados com uma mensagem no log, e a resolução fica a original.

## Arquivos alterados

- `port/src/runtime_file.c`: `KR_RES` e `KR_SYSCONF_SET` na cópia em memória do `sysConf.boot`.
- `tools/bench/run_route.sh`: as capturas usam o tamanho real da imagem (antes supunha 1920×1080).
- `README.md`: `KR_RES` na tabela de opções.

**Ainda não foram enviadas ao GitHub.**

## Próximos passos sugeridos

1. Filtro de redução melhor no presenter, para o supersampling 1440p→1080p.
2. Testar proporções diferentes de 16:9 (ultrawide 2560×1080): o HUD do jogo pode não se adaptar.
3. A CPU: o perfil da thread principal nas cenas pesadas.
