# Créditos e licenças

Este projeto só existe por causa do trabalho das pessoas abaixo. As licenças de cada parte são mantidas nos próprios arquivos.

## Base do port

### bbport: port nativo de Bloodborne para PC
- <https://github.com/deadinside28/bloodborne_pc> (fork a partir do commit `0fc2921`)
- Copyright 2026 bbport contributors. **GPL-2.0-or-later.**
- Todo o diretório `port/` vem do bbport:
  - runtime HLE em `src/`;
  - camada de GPU e tradutor de shaders em `gpu/`;
  - scripts de preparação e de link, testes e `build.sh`.
- O aviso original está em `port/NOTICE.bbport`. As alterações deste projeto estão marcadas com comentários `kr-port:`.

### shadPS4: emulador de PS4
- <https://github.com/shadps4-emu/shadPS4>
- Copyright shadPS4 Emulator Project. **GPL-2.0-or-later.**
- A camada de GPU do bbport começou do *video core* e do recompilador de shaders do shadPS4.
- Algumas funções deste port também foram portadas do shadPS4: Rtc, troféus e DiscMap. A origem está marcada onde o código é usado.

## Bibliotecas de terceiros (cada uma com sua licença na própria pasta)

| Biblioteca | Onde | Licença |
|---|---|---|
| LibAtrac9 (Alex Barney): decodificador ATRAC9 usado pelo mixer de áudio | `port/third_party/LibAtrac9` | MIT |
| sirit (emissor SPIR-V) | `port/gpu/third_party/sirit` | BSD-3-Clause |
| Dear ImGui | `port/gpu/third_party/imgui` | MIT |
| AMD FidelityFX (FSR 1/3, FFX API) | `port/gpu/third_party/amd-fsr1`, `ffx-api-2.4`, `fsr-vulkan` | MIT |
| Cabeçalhos GCN/PM4 da AMD (PAL) | `port/gpu/third_party/gcn` | MIT (AMD) |
| SMAA | `port/gpu/third_party/smaa` | MIT |
| half | `port/gpu/third_party/half` | MIT |
| cpp-httplib | `port/gpu/third_party/httplib` | MIT |
| wepoll | `port/gpu/third_party/wepoll` | BSD-2-Clause |
| Fontes DejaVu | `port/gpu/third_party/fonts` | licença Bitstream Vera / DejaVu |
| Módulo online herdado do bbport (BBSC / shadNet) | `port/gpu/bbnet20` | GPL-2.0+ / GPL-3.0+ conforme cada arquivo |
| Ponte DLSS (de IFreemz/shadPS4-Bloodborne-DLSS-FSR) | `port/gpu/dlss_bridge` | MIT |

Para usar no build: SDL3 (zlib), Vulkan, Vulkan Memory Allocator e xbyak.

## Referências consultadas
- **SpartanPort** e o fork **shadPS4-Infamous-FixBuild**: consultados para comparar soluções; nenhum código foi copiado.
- Ghidra (NSA), usado para analisar o executável do jogo do próprio usuário.

## Deste projeto
- **robertoqimin**, com a assistência do Claude (Anthropic). Trabalho próprio:
  - o mixer Ngs2 com ATRAC9;
  - o patch de 60 FPS por memória;
  - os stubs offline;
  - o layout de controles do jogo;
  - o registro de rwlock em tabela hash;
  - as ferramentas de benchmark.
- Os arquivos fora de `port/` estão sob a licença **MIT** (`LICENSE`). Dentro de `port/` vale a **GPL-2.0-or-later** (`port/LICENSE`).

## Jogo
*Kamen Rider: Battride War Genesis* (仮面ライダー バトライド・ウォー 創生) © Bandai Namco Entertainment, © Ishimori Productions / Toei. Este projeto **não** é afiliado à Bandai Namco, à Toei nem à Sony Interactive Entertainment, e **não contém** nenhum código ou dado do jogo.
