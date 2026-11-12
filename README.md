# battride-war-sousei-port

Port nativo para PC (Linux) de **Kamen Rider: Battride War Genesis** (仮面ライダー バトライド・ウォー 創生, PS4, **CUSA02716**, versão 01.02).

Funciona como o Wine + DXVK, mas feito para um único jogo:
- o **código x86-64 original do jogo roda direto na CPU** do PC;
- um runtime escrito para este jogo substitui as bibliotecas de sistema do PS4;
- os gráficos do PS4 (GCN/PM4) são traduzidos para **Vulkan**.

O projeto é um fork do [bbport](https://github.com/deadinside28/bloodborne_pc), o port nativo de Bloodborne, que por sua vez nasceu do [shadPS4](https://github.com/shadps4-emu/shadPS4). Os créditos completos estão em [CREDITS.md](CREDITS.md).

> **Nenhum arquivo do jogo está incluído.** Você precisa do **dump do seu próprio jogo**: o base CUSA02716 v01.00 e o update v01.02, tirados do seu console.
> Este projeto não é afiliado à Bandai Namco, à Toei nem à Sony Interactive Entertainment.

---

## Estado

**Experimental, jogável.** Testado numa AMD RX 6750 XT (Mesa/RADV, Linux).

| Área | Estado |
|---|---|
| Boot, menus, vídeos, save | ✅ |
| Story Mode, tutorial, combate | ✅ (validado até as primeiras missões) |
| **60 FPS no gameplay** | ✅ padrão (o original trava em 30); a velocidade do jogo foi conferida |
| Fluidez | ✅ combate com 100% dos quadros em 16,7 ms na rota de benchmark |
| Áudio (BGM, efeitos, vozes; ATRAC9 via Ngs2) | ✅ sem reverb, envelope ou mastering ainda |
| Controle (DualShock, DualSense, Xbox via SDL) | ✅ |
| Teclado e mouse com o layout do jogo | ✅ |
| Online (rankings etc.) | ❌ responde "desconectado" |
| Windows / Steam Deck | não testado |

Os detalhes de cada etapa estão em [`docs/relatorios/`](docs/relatorios/). Os relatórios citam capturas de tela que **não são publicadas** (são conteúdo do jogo).

---

## Como montar

### 1. Dependências (Linux)
Você vai precisar de:
- GCC ou Clang, CMake, Ninja e pkg-config;
- os cabeçalhos e o loader do **Vulkan**, **SDL3** e zlib;
- Python 3.

Veja `port/build.sh`. No NixOS, o `shell.nix` do bbport serve de referência.

### 2. Compilar
```sh
cd port
./build.sh            # gera port/out/bb-probe e port/out/gpu/libbbgpu.so
```

### 3. Preparar o seu jogo
O dump precisa estar **descriptografado**: um `eboot.bin` em ELF/SELF em texto plano, como os dumpers modernos geram.

```sh
# visão mesclada base + update (só links simbólicos; nada é copiado nem alterado)
tools/make_game_view.sh /caminho/CUSA02716 /caminho/CUSA02716-patch game

# análise e link do executável e dos módulos do jogo (gera out/boot-linked.bin)
python3 port/scripts/prepare.py      game --out out
python3 port/scripts/link_libc.py    game --out out
python3 port/scripts/link_modules.py game --out out
```

### 4. Jogar
```sh
cd port
./out/bb-probe ../out/boot-linked.bin --app0 ../game --user ../user --timeout 0
```
Os saves ficam em `user/savedata/`.

---

## Opções (variáveis de ambiente)

| Variável | Efeito |
|---|---|
| `KR_FPS=30` | volta ao limite original de 30 FPS no gameplay (padrão **60**) |
| `KR_NGS2_GAIN=x` | ganho geral do áudio |
| `BB_CONFIG=arquivo.ini` | remapear controles (`key.<entrada>=Teclas`, `pad.<entrada>=botões`; veja `port/src/runtime_pad/bindings.c`) |
| `BB_FPS_PRINT=1` | mostra o FPS no terminal a cada 5 s |

Diagnóstico: `KR_TRACE_NGS2`, `KR_NGS2_PROF`, `KR_IO_TRACE`, `KR_STALL_PROFILE`, `KR_PAD_TRACE` e `BB_FRAME_LOG=<csv>`.

---

## Controles

O jogo é japonês: **○ confirma e ✕ volta.**

| Ação | Controle PS4 | Teclado e mouse |
|---|---|---|
| Mover / câmera | analógicos | WASD / mouse (ou teclado numérico 8-4-5-6) |
| Ataque normal | □ | botão esquerdo do mouse, J |
| Especial 1 | △ | botão direito do mouse, I |
| Especial 2 / confirmar | ○ | E, L, Enter |
| Pulo / Rider Cancel / voltar | ✕ | Espaço, K, Backspace |
| Especial 3 | △ + ○ | F |
| Rider Glide | □ + ✕ | V |
| Super especial | R2 | R, botão do meio do mouse |
| Defesa | L1 | Shift esquerdo |
| Trocar rider (TAG) | R1 | Tab |
| Pausa / próxima página | OPTIONS | Esc |

---

## Benchmark

```sh
tools/bench/bench.sh minha_execucao [VAR=valor ...]   # rota fixa automatizada → out/bench/*.json
```

---

## Estrutura

```
port/      runtime + camada de GPU (fork do bbport; GPL-2.0-or-later)
tools/     scripts: visão do jogo, imports, benchmark e rotas automáticas (MIT)
ghidra/    scripts para recriar a análise do executável a partir do SEU dump (MIT)
docs/      mapa do jogo, roadmap e relatórios de cada etapa (MIT)
```

## Licença

- Os arquivos **fora de `port/`** usam a licença **MIT** ([LICENSE](LICENSE)).
- O diretório **`port/`** continua sob a **GPL-2.0-or-later** ([port/LICENSE](port/LICENSE)), como o bbport e o shadPS4 de onde vem. Algumas partes têm outras licenças, indicadas nos próprios arquivos.
- O programa compilado (`bb-probe`) é distribuído sob a GPL.

Kamen Rider: Battride War Genesis © Bandai Namco Entertainment, © Ishimori Productions / Toei.
