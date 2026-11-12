# Preparação para o GitHub — o que é proprietário e o `.gitignore`

**Data:** 2026-11-12
**Pedido:** verificar tudo o que é proprietário antes de publicar no GitHub e criar o `.gitignore`.

---

## Resultado

- `~/kr-port/.gitignore` criado.
- Conferi com um `git add --dry-run` num repositório temporário, sem criar `.git` na pasta: entrariam **2.204 arquivos (~18 MB)**.
  - A maior parte é código: o port (`port/src`, `port/gpu`, `port/tests`), `third_party` com licenças livres, os scripts e a documentação.
- **Nenhum arquivo do jogo e nenhum binário derivado dele entra.** A única imagem que entra é o gráfico de tempo de quadro, que é nosso (gerado a partir dos logs).

## O que é proprietário e fica de fora

| Caminho | O que é | Por quê |
|---|---|---|
| `game/` | visão do jogo: `eboot.bin`, `nativeORBIS`, `sce_sys`, `sce_module` (links para o seu dump) | dados do jogo (Bandai Namco / Toei) |
| `eboot_v100.elf`, `eboot_v102*.elf` | executável do jogo descriptografado | código do jogo |
| `ghidra/` (projeto, `.rep`, `.snapshot`, log) | o jogo desmontado e decompilado | derivado do código do jogo |
| `imports_v102*.json`, `plt_v102.json` | tabelas extraídas do eboot | derivado do binário; os scripts em `tools/` regeneram a partir do dump |
| `patches/sysconf/` | cópias do `sysConf.boot` (original e 60 FPS) | arquivo do jogo |
| `out/`, `port/out/` | `boot-linked.bin` (contém o código do jogo ligado), logs, dumps de quadros e de áudio | código e conteúdo do jogo |
| `user/`, `port/user/` | saves, cache e logs | dados pessoais / do jogo |
| `docs/relatorios/img/*` (menos o gráfico) | capturas de tela do jogo | arte do jogo |
| `docs/relatorios/audio/` | amostra de áudio do jogo | som do jogo |

Além disso, por segurança, o `.gitignore` bloqueia as extensões do PS4 em qualquer lugar: `*.elf`, `*.self`, `*.prx`, `*.sprx`, `eboot.bin`, `*.fpk`, `*.at9`, `*.atp`, `*.pkg`, `*.sfo`, `sce_discmap*.plt`, `*.raw`, `*.f32` e `*.frames`.

## Fica de fora por outros motivos

- `refs/`: clones de terceiros (bloodborne_pc, shadPS4, SpartanPort). Não são nossos; quem quiser, clona.
- `port/pgo/`: está vazio.

## O que entra (e pode ir)

- `port/`: o port, fork do bbport. Mantém **GPL-2.0+** (`LICENSE`), `NOTICE` e `NOTICE.bbport`, e `third_party` com as licenças de cada biblioteca (LibAtrac9, imgui, sirit, FSR…).
- `tools/`: scripts nossos (imports, relocações e benchmark com as rotas automáticas).
- `ghidra/make_project.sh` e `ghidra/scripts/ApplyPs4Names.java`: recriam o projeto do Ghidra **a partir do dump de quem usar**.
- `docs/`: mapa, análise e relatórios. Citam endereços do eboot, nomes de funções do SDK e textos curtos da interface, o que é documentação e não conteúdo do jogo.
- Não encontrei e-mail nem o caminho do seu dump (`~/Downloads/...`) nos arquivos que entram. O único arquivo com esse caminho (`patches/sysconf/original_link.txt`) está ignorado.

## Atenção antes do push

1. Sete relatórios apontam para imagens ou áudio que **não vão subir**. No GitHub esses links aparecem quebrados, e o texto continua legível.
2. Faltam um **README** na raiz e uma **LICENSE na raiz**. Hoje a licença está só em `port/LICENSE`. Sugestão: GPL-2.0+ para o repositório todo, por causa do bbport.
3. O jogo depende de tabelas que os scripts geram a partir do dump (`prepare.py`, `link_*.py`). O README precisa explicar esse passo, porque o repositório não traz nada do jogo.
4. Comando sugerido:
   ```sh
   cd ~/kr-port && git init && git add -A && git status   # conferir antes do commit
   ```
