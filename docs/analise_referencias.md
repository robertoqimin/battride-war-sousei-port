# Análise dos projetos de referência (2026-11-11)

Clones (shallow) em `~/kr-port/refs/`. Todos GPL-2.0-or-later → nosso port também precisa ser GPL-2.0+.

| Projeto | Commit | O que é |
|---|---|---|
| bloodborne_pc (bbport) | 0fc2921 | Port nativo: eboot roda direto na CPU, runtime C próprio substitui libs PS4, GPU própria (derivada do shadPS4, reescrita) |
| SpartanPort (GoW III) | 6a70960 | Fork do bbport (via Supermedo). Mesmo runtime em C, mas GPU = video_core do shadPS4 vendorizado (commit b4e7ae7) + launcher Rust |
| shadPS4 | 4788f0b | Emulador. Fonte da HLE de quase todas as libs, tradutor GCN→SPIR-V e video_core |
| shadPS4-Infamous-FixBuild | cbc9a01 | Fork do shadPS4 com tuning de performance para inFAMOUS (profilers, threads, address space). Ideias de otimização; pouco reaproveitável direto |

## Arquitetura comum (bbport / SpartanPort)

1. **Preparação offline** (`scripts/prepare.py`, `link_libc.py`, `link_modules.py`; no SpartanPort em Rust):
   lê o SELF, aplica relocações e **linka o libc.prx e libSceFios2.prx do próprio jogo** como código nativo numa imagem plana.
2. **Loader** (`src/probe.c`): carrega a imagem, instala hooks, pula pro entry point.
3. **Runtime** (`src/runtime_*.c`, ~10–14 mil linhas C): kernel, threads, mutex/sema/rwlock, memória, arquivos, áudio, pad, saves, serviços. Resolve imports por nome (`runtime_resolve` → cada módulo → `bbgpu_resolve`).
4. **GPU** (`gpu/`): GnmDriver + VideoOut + command processor + tradutor de shaders → Vulkan.
5. **Hooks específicos do jogo** separados (`gpu/games/bloodborne/`, `src/*_hook.h` no SpartanPort) — localizados por assinatura de bytes.

## Cobertura das 681 importações do nosso eboot (v1.02)

Gerado por `tools/list_imports.py` + cruzamento; detalhe em `imports_v102_coverage.json`.

- bbport cobre 283, SpartanPort 164, shadPS4 479; união 485.
- **libc (135) e libSceFios2 (23)**: o jogo traz `sce_module/libc.prx` e `libSceFios2.prx`, igual a Bloodborne/GoW → resolvidos pela linkagem nativa do bbport, não precisam de HLE.
- GnmDriver (64/64), VideoOut, AvPlayer, kernel (60/60 no shadPS4), Pad, SaveData, AudioOut: cobertos.
- **Lacunas reais**:
  - `libSceJson2` (62, classes C++ `sce::Json`): ninguém implementa. Provavelmente uso online/telemetria → stub ou implementação pequena sobre uma lib JSON.
  - `libSceAudiodec` (5) + `libSceNgs2` (12, quase só stubs no shadPS4): **áudio é o maior buraco**. Jogo usa ATRAC9 (10.823 .at9) via Audiodec/Ngs2; Bloodborne/GoW usam Ajm. Precisa decodificador AT9 (FFmpeg/libatrac9) + mixer Ngs2 mínimo.
  - Np* online, Facebook, UsbStorageDialog, InGameMessage: stubs retornando "não disponível".

## O que reaproveitar

| Peça | De onde | Observação |
|---|---|---|
| SELF→imagem, relocação, link de libc/Fios2 | bbport `scripts/` | Python, genérico; trocar `game_check.py` / `SUPPORTED_TITLES` |
| Runtime base (kernel, thread, sync, memory, file, time, rtc, pad, savedata, audio out) | bbport `src/` | Remover endereços hardcoded do Bloodborne (runtime_services/ajm/audio têm vários) |
| GPU | SpartanPort `gpu/` (video_core do shadPS4 quase intacto) | Mais genérico que o renderer reescrito do bbport → melhor ponto de partida para outro jogo |
| Hooks por assinatura | SpartanPort `src/signature.h`, `tools/make_signatures.py` | Base para FPS/resolução/widescreen |
| Ngs2, Json, dialogs, Np stubs | shadPS4 `src/core/libraries/` | Portar para a interface do runtime |
| Launcher | SpartanPort `launcher-rust/` | Fase final |
| Perf (threads, profiler) | Infamous-FixBuild `src/common/` | Só como referência |
