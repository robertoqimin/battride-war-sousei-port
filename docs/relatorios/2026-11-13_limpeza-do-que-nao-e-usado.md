# Limpeza: o que não é usado

**Data:** 2026-11-13

## Resumo

| | Antes | Depois |
|---|---|---|
| Linhas no repositório | ~1,03 milhão | **~909 mil** (−121 mil, 223 arquivos) |
| Disco do projeto (local) | ~6,4 GB | **~2,2 GB** (−4,2 GB) |
| Funcionamento | — | igual: combate a **60 FPS**, 1.465 draws/quadro, 0 erros; imports não implementados: **0** |

## Código removido do repositório (verificado como sem uso neste jogo)

| O quê | Por que não é usado |
|---|---|
| `port/gpu/bbnet20` (93 mil linhas) | módulo online do **Bloodborne** (BBSC/shadNet). Só carregava com `BB_ONLINE=1`; este jogo joga offline pelos stubs (`runtime_kroffline.c`) |
| `port/gpu/third_party/httplib`, `wepoll` | usados **só** pelo módulo online |
| `port/gpu/games/bloodborne*` (perfil e ganchos) | ativados só pelos seriais do Bloodborne; aqui o log sempre dizia `Game profile: none for CUSA02716` |
| `port/tests/test_online.c` | teste do módulo online |
| `tools/bench/route_*_dump*.sh`, `route_ctlcfg_dump.sh`, `series1.sh` | rotas de diagnóstico de uso único (do save antigo) e um A/B de recursos do Bloodborne que não existem mais |

Ajustes para continuar compilando:
- **`runtime_net.c`** virou funções vazias (o resto do runtime chama essa interface).
- **`shim/bbport_guest_hooks_none.cpp`** (novo): as funções de ganchos que o núcleo consulta (câmera, efeitos, aspecto) respondem "indisponível", **exatamente** como antes neste jogo, que nunca tinha ganchos instalados.
- **Perfis de jogo:** o seletor continua, com a lista vazia.
- **Compilação:** sem o bloco do módulo online no CMake e no `build.sh`.
- **Créditos e licença:** o `CREDITS.md` deixou de listar httplib, wepoll e o módulo online (e diz o que ficou fora do fork); o `LICENSE` não tem mais partes GPL-3.0. O `NOTICE.bbport` original ficou intacto, como a licença exige.

Adicionado: **`tools/bench/route_combat.sh`**, uma rota de combate reutilizável para regressão, e `RR_USER=<dir>` no `run_route.sh`, para testar com outro diretório de saves.

## Arquivos locais apagados (fora do git; podiam ser gerados de novo)

- **Saídas de teste e capturas:** `out/val`, `out/audio*`, `out/dump*`, `out/kbd`, `out/pad*`, `out/bench`, logs `out/run*.log` e outros (~3,7 GB).
- **ELFs sem uso:** `eboot_v100.elf` (versão base), `eboot_v102.elf` (cópia idêntica de `out/eboot.elf`) e `eboot_v102_ghidra.elf` (etapa intermediária).
- **Clones de referência só consultados:** `refs/SpartanPort` e `refs/shadPS4-Infamous-FixBuild`.
- **Outros:** `ghidra/headless_import.log` e um gráfico avulso.

## O que ficou (é usado)

- `refs/shadPS4` (o `tools/missing_imports.py` lê) e `refs/bloodborne_pc` (comparação com o bbport original).
- O projeto do Ghidra e `eboot_v102_ghidra_reloc.elf`: análise do executável.
- As saídas da preparação do jogo: `out/boot-linked.bin`, que é o que roda, e os JSON do link.
- `user/` com o seu save zerado e o **backup do save antigo**.
- As imagens dos relatórios (`docs/relatorios/img`, fora do git).
- O dump do jogo e os arquivos em Downloads: **não foram tocados**.

## Como verifiquei

- **Compilação:** completa, sem erros.
- **Combate:** a rota de combate rodou com uma **cópia isolada** do save antigo (`RR_USER`), para não mexer no seu save zerado. Resultado: 60 FPS, pior quadro de 16,7 ms, 0 erros. Conferi por `cmp` que o seu save continua idêntico ao original.
- **Imports:** `tools/missing_imports.py` mostra 0 não implementados.

## Em aberto (decisão sua)

**Upscalers** (FSR 3/4, DLSS, geração de quadros: ~450 mil linhas em `third_party/fsr-vulkan` e mais): desligados por padrão neste jogo, mas oferecidos no menu do overlay. Removê-los cortaria quase metade do repositório, só que exige mexer a fundo no pipeline de renderização. Não removi sem a sua confirmação.

**Nada foi enviado ao GitHub.**
