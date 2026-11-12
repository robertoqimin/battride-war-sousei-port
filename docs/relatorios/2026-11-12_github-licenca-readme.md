# GitHub: licença, créditos, README e repositório privado

**Data:** 2026-11-12

## O que foi feito

- **Repositório privado:** <https://github.com/robertoqimin/battride-war-sousei-port>
  - visibilidade **PRIVATE**, branch `main`;
  - 1 commit inicial com 2.209 arquivos (~23 MB).
- **`LICENSE`** (raiz): **MIT**, com uma seção "escopo da licença":
  - o MIT vale para tudo **fora de `port/`**: `tools/`, `docs/`, os scripts do Ghidra e os arquivos da raiz;
  - **`port/` continua GPL-2.0-or-later**.
- **`CREDITS.md`**:
  - bbport (commit `0fc2921`) e shadPS4;
  - todas as bibliotecas de terceiros, com a licença de cada uma conferida no arquivo de licença dela: LibAtrac9, sirit, imgui, FidelityFX, cabeçalhos GCN da AMD, SMAA, half, httplib, wepoll, DejaVu, módulo online e ponte DLSS;
  - referências consultadas;
  - aviso de marca do jogo.
- **`README.md`**:
  - o que é o projeto e o estado atual;
  - dependências, compilação e preparação do dump do usuário;
  - como jogar, opções `KR_*`, tabela de controles, benchmark, estrutura e licença.
- **`tools/make_game_view.sh`** (novo): monta a visão mesclada base + update com links simbólicos. Assim o README tem um passo reproduzível, que antes só existia na minha máquina.

## Por que não deu para pôr tudo em MIT

O `port/` é um fork do bbport, que é **GPL-2.0-or-later**, e parte dele vem do shadPS4 (também GPL). A GPL **não permite relicenciar** esse código como MIT, nem as nossas modificações nele. Por isso:
- o código do port e o programa compilado ficam sob a GPL;
- o MIT cobre só o que é 100% nosso e fica fora do port.

Isso não impede o repositório de ser privado. Só vale para o dia em que o binário for distribuído: aí é preciso fornecer o código-fonte sob a GPL.

## Conferência antes do push

Depois do `git add -A`, conferi de novo: nenhum `.elf`, `.bin`, `.at9`, `.fpk` ou `.sfo`, nada de `game/`, `out/`, `user/`, `refs/` ou `patches/sysconf`. A única imagem é o gráfico de tempo de quadro, que é nosso.
