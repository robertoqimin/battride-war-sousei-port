# Estado geral do port e mudança de máquina

**Data:** 2026-11-13
**Último commit:** `c220c54` (launcher e AppImage). Repositório: <https://github.com/robertoqimin/battride-war-sousei-port> (público, branch `main`).

## 1. Onde o port está

| Área | Estado |
|---|---|
| Boot, menus, vídeos, saves | ✅ (vídeos sem voltar quadro e com cadência perfeita) |
| Story Mode, combate, missão final (com save zerado) | ✅ até o início da luta final, que você testa jogando |
| 60 FPS (o original é 30), velocidade correta | ✅ padrão; `KR_FPS=30` volta ao original |
| Resolução de renderização (`KR_RES`: 720p a 4K) + redução por área | ✅ 1440p segura 60 FPS na RX 6750 XT |
| Áudio (Ngs2/ATRAC9, BGM em loop, 7.1 → SDL) | ✅ verificado: o jogo não usa reverb nem envelope |
| Exceções C++ (unwind) | ✅ 0 imports faltando (588 implementados) |
| Teclado e mouse, controle | ✅ |
| Launcher (GTK 4) e AppImage | ✅ AppImage com glibc ≥ 2.44 (gerada no CachyOS) |
| CPU | threads de som e de vídeo sem espera ativa; filmes de 156% → 47% de CPU |
| Online | ❌ "signed out" (por decisão; o módulo online do Bloodborne foi removido) |

## 2. Pendências

1. **Crash na thread de som** (`RIP 0x1`, visto uma vez por você): não reproduziu em ~22 min de testes, e três hipóteses foram descartadas. O relatório de falha agora mostra de onde veio o salto. **Se cair de novo, mande as últimas linhas do terminal ou do Log do launcher.**
2. **SMAA:** viável e rápido (~meio dia a 1 dia). FXAA logo depois; MSAA só se o SMAA não bastar.
3. **Upscalers (FSR 3/4, DLSS):** viáveis (~2 a 4 dias). A câmera já está ligada; falta a cena reduzida, a UI deste jogo e o formato LDR. O risco é o rastro sem vetores por objeto.
4. **AppImage portátil** (qualquer distro): exige compilar num Ubuntu 22.04 (`podman`/`docker`).
5. **Suporte a `.pkg`:** só fPKG (os oficiais são criptografados pelo console). Extrator próprio no launcher, ~1 a 2 dias.
6. **Launcher:** sensibilidade do mouse e zona morta dos sticks ainda não aparecem.
7. **Validação jogando:** chefe final, encerramento e créditos, Time Survival, Free Mission e galerias.

## 3. O que NÃO está no GitHub (copie para a máquina nova)

| O quê | Onde está aqui | Tamanho | Precisa? |
|---|---|---|---|
| **Seu dump do jogo** (base CUSA02716 + atualização v01.02) | `~/Downloads/kamen rider battride wars genesis/` | — | **sim** |
| **Saves**: o zerado em uso + o backup do antigo | `~/kr-port/user/` (inclui `savedata_backup_2026-11-12_antes-save-zerado/`) | 4,3 MB | **sim** |
| O save zerado original | `~/Downloads/CUSA02714(Kamen Rider Battride War Genesis)/` | 2,2 MB | recomendado |
| Ferramentas de build sem sudo (cmake/ninja locais, cabeçalhos Vulkan, xbyak, VMA) + `buildenv.sh` | `~/tools/` (`buildenv`, `vulkan-headers`, `xbyak`, `vma`, `buildenv.sh`) | parte de 1,6 GB | sim, **ou** instale pelo gerenciador de pacotes |
| Ferramentas da AppImage (appimagetool, runtime, patchelf) | `~/tools/appimage/` | ~20 MB | só para gerar a AppImage |
| Ghidra, JDK e Node (engenharia reversa) | `~/tools/ghidra_*`, `jdk-*`, `node-*` | ~1,5 GB | só para engenharia reversa |
| Clones de referência (shadPS4, bloodborne_pc `0fc2921`) | `~/kr-port/refs/` | 1,0 GB | opcional (dá para clonar de novo) |
| Projeto do Ghidra | `~/kr-port/ghidra/KRBattrideWar.*` | 235 MB | opcional (`ghidra/make_project.sh` recria) |
| Imagens dos relatórios (capturas do jogo) | `~/kr-port/docs/relatorios/img/` | 32 MB | opcional |
| Minhas notas do projeto (para eu continuar de onde parei) | `~/.claude/projects/-home-roberto/memory/` | 24 KB | recomendado |
| Cache de shaders | `~/kr-port/port/user/cache/` | 3,9 MB | não (é recriado jogando) |

**Atenção:** nesta máquina o launcher usa o próprio `~/kr-port` como pasta de dados. Os saves ficam em `~/kr-port/user/`, e **não estão no git**.

## 4. Montando na máquina nova

```sh
git clone https://github.com/robertoqimin/battride-war-sousei-port.git ~/kr-port
cd ~/kr-port/port
# dependências: GCC/Clang, CMake, Ninja, pkg-config, Vulkan (cabeçalhos + loader), SDL3, zlib,
# FFmpeg (libavcodec/format/util/swscale/swresample), xbyak, VMA, Python 3, PyGObject + GTK 4 +
# libadwaita (launcher). Ou copie ~/tools e rode: . ~/tools/buildenv.sh
./build.sh          # limita as tarefas pela RAM livre (2 GiB cada; BB_JOBS=N força)
```

Depois, **uma** das duas:
- **Launcher:** `tools/launcher/launcher.py`. Escolha as pastas do jogo base e da atualização e a pasta de dados. Copie os seus saves para `<pasta de dados>/user/` **antes** do primeiro Play.
- **Como antes:** `tools/make_game_view.sh <base> <update> game` e os três scripts de `port/scripts` (README), com os saves em `~/kr-port/user/`.

Se a máquina nova tiver **glibc mais antiga que 2.44**, a AppImage gerada aqui não roda nela. Gere outra lá (`tools/appimage/build_appimage.py`).

Para eu continuar com o mesmo contexto, copie a pasta de notas para o caminho equivalente na máquina nova: `~/.claude/projects/<home com "-">/memory/`.

## 5. Commits desta sessão (13/11)

| Commit | O quê |
|---|---|
| `102b3b8` | remoção do que o jogo não usa (online e perfil do Bloodborne; −121 mil linhas) |
| `b432108` | relatório de falha mostra de onde veio um salto inválido |
| `ce92e07` | CPU nos filmes, relógio dos vídeos suavizado, limite de RAM na compilação |
| `4e58e02` | câmera do jogo ligada ao `CameraMotion` |
| `c220c54` | launcher (GTK 4) e AppImage |
