# Fase 0: mapa do executável (eboot v1.02)

Endereços do Ghidra (base 0x100000; endereço do ELF = Ghidra - 0x100000).
Arquivo de análise: `eboot_v102_ghidra_reloc.elf` (relocações PS4 aplicadas).
Projeto persistente: `ghidra/KRBattrideWar.gpr` (recriar: `ghidra/make_project.sh`).

## Identidade do build
- Nome interno: "MaskedRiderWars3", versão "01.00", "$Rev: 701 $", "Feb 16 2016 10:20:02" (`0x757880`).

## Inicialização
| Endereço | Nome | Papel |
|---|---|---|
| 0x3491b0 | entry | `_init_env` → atexit → init estático → `main` → exit |
| 0x226310 | (init estático) | registra a fábrica da aplicação (`0x757880`) no slot `0xbe69c8` |
| 0x757880 | app_factory | cria (uma vez) o objeto app em `0xd501d8`, vtable `0xb91ad0` |
| 0x34b3f0 | main | `app->init / start / while(frame) / shutdown` |

Ponteiro global para a app: `0xbe69c0` (usado em centenas de lugares).

## Vtable da aplicação (0xb91ad0)
| Offset | Função | Papel |
|---|---|---|
| +0x58 | 0x756440 | hook vazio (`return 1`) |
| +0x60 | 0x756450 | hook vazio (`return 1`) |
| +0x70 | 0x2224d0 | init: cria subsistemas (heaps, áudio "/MUSIC", cenas) e instancia `cStartupScene` |
| +0x78 | 0x34a6a0 | start |
| +0x80 | 0x34a550 | **frame** (loop principal; retorna 0 para sair) |
| +0x88 | 0x34a7a0 | shutdown 1 |
| +0x90 | 0x2252e0 | shutdown 2 |

## Frame (0x34a550)
1. Pré-frame (`0x34bab0`) se subsistema `0xd50108` existe.
2. Sincronização (`0x368860` / `0x3688f0`).
3. `0x727a30(gfx)`: início do frame gráfico (`gfx` = `0xd500b0`).
4. `app->+0x58`, `app->+0x60` (hooks vazios nesta classe).
5. Gerenciador de cenas (`0xd50128`): cena atual `+0x198`, método `+0x78`; `0x3bae10`.
6. `0x7278b0 / 0x727920 / 0x727970(gfx)`: espera GPU / submitDone / flip (ver tabela abaixo).
7. `0x34b590(0xd50618)`: pós-frame.

## Camada gráfica do frame (objeto `gfx` = `0xd500b0`, dispositivo em `gfx+0x38`)
| Endereço | Papel (confirmado) |
|---|---|
| 0x727a30 | begin frame: alterna o buffer duplo de comandos (`+0x20`), contador de frame |
| 0x7278b0 | espera a GPU liberar o buffer (spin no estado `+0x60`: 0 livre, 2 em uso) → `0x72d0f0` |
| 0x727920 | fecha os command buffers (`0x7249c0`, `0x72afc0`) → `0x72d460` → **`sceGnmSubmitDone`** (via `0x6aab00`) |
| 0x727970 | `0x72d5f0` → **`sceVideoOutSubmitFlip`**; libera recursos adiados (16 slots em `0xd4fe30`); frame++ |
| 0x6aa9a0 | wrapper de `sceGnmSubmitCommandBuffers` ← `0x688b50` ← `0x688b40` (chamado indiretamente) |

Camada `0x6aa9xx`: wrappers do driver Gnm compilados no eboot; todas as chamadas `sceGnm*` passam por ela.

O trabalho real por frame não está em update/render da app (são vazios) e sim no
gerenciador de cenas (`0xd50128`, cena atual em `+0x198`, método `+0x78`) e em `0x3bae10`.

## Utilitários do motor
| Endereço | Papel |
|---|---|
| 0x35e3f0 | alocador `alloc(heap_id, size, align)` (heaps 6, 0xf, 0x12 vistos) |
| 0x35e530 | free |
| 0x34e2e0 | fábrica de classes por nome (`"cStartupScene"`) — sistema tipo RTTI próprio |
| 0x350db0 | kr_FileOpenSync: `"%s/%s"` → `sceFiosFHOpenSync` + `FHStatSync` |

## Próximos alvos
- `0x727a30`…`0x727970`: camada gráfica → onde chama `sceGnmSubmit*` / `sceVideoOutSubmitFlip`.
- Carregador de `.fpk` (strings `%s/_fpk/%s.fpk` em `0x894e8c`).
- Shaders (`_vs.vso` / `_fs.fso` em `0x8afad6`).
- Áudio: `0x720480`… (`"/MUSIC"`), uso de Ngs2/Audiodec.

# Etapa 3: pontos que o runtime do port precisa atender

## Arquivos e pacotes .fpk
| Endereço | Papel |
|---|---|
| 0x2fdec0 | monta o caminho `"<raiz>/_fpk/<nome>.fpk"` (raiz = `*(cfg 0xd501c8 + 0x2f0) + 0xc`), testa se existe e monta como `"_fpk/<nome>"` |
| 0x357200 | **fpk_mount(fs, nome, flags, heap)**: procura no cache; se não estiver carregado, lê o tamanho (`0x356ae0`), aloca o arquivo inteiro no heap pedido, lê tudo para a RAM (`0x3530d0`) e registra o pacote em memória (`0x774ac0`) |
| 0x351840 | file_exists |
| 0x350db0 | abertura síncrona via `sceFiosFHOpenSync` |

Os `.fpk` são carregados inteiros na RAM e os arquivos internos são servidos da memória.
Para o port, basta a E/S de arquivos do Fios2 (linkado nativamente a partir de `libSceFios2.prx`) sobre o
sistema de arquivos do PC; o formato `.fpk` não precisa de suporte no runtime (só em ferramentas/mods).

## Shaders
| Endereço | Papel |
|---|---|
| 0x469650 | carrega o par `<nome>_vs.vso` (tipo 1) + `<nome>_fs.fso` (tipo 2) |
| 0x4696e0 | localiza o binário do shader por nome e tipo |
| 0x469b20 | cria o objeto de shader a partir do binário |

Os `.vso`/`.fso` são binários GCN prontos → tradução GCN→SPIR-V no runtime (video_core do shadPS4 / SpartanPort).

## Memória de GPU
| Endereço | Papel |
|---|---|
| 0x72ba40 | gpu_mem_init(80 MB, 944 MB) → `0x732c10` (direct memory) |
| 0x732e60 | gpu_alloc(pool, tamanho, alinhamento) |
| 0xd4fd00 | pool de 80 MB (64 blocos) |
| 0xd4f6b0 | pool de 944 MB (16384 blocos) |

Outros usuários de `sceKernelAllocateDirectMemory`: `0x13b600`, `0x3578d0`, `0x792b50`.
Total visível à GPU ~1 GB: o runtime precisa mapear isso em memória do host acessível pelo Vulkan
(o modelo de memória do bbport resolve exatamente isso).

## Vídeo (0x72c3d0, init do dispositivo gráfico)
1. Inicializa a memória de GPU (`0x72ba40`) e command buffers (`0x688a30`, 2× 16 MB).
2. Cria **2 render targets de tela** (`0x6a7b20`, formato `0xf2e00a`) com largura/altura vindas de `+0x60/+0x64`.
3. `sceVideoOutOpen` → `SetBufferAttribute` (resolução em `dev+0x140`: largura & 0xffff, altura >> 16) →
   `SetFlipRate(0)` (60 Hz) → equeue `"RegisterRenderTargetForDisplay"` + `AddFlipEvent` →
   `RegisterBuffers(2)` → `SetWindowModeMargins(0x6c, 0x6c)`.
4. Inicializa estados/pipelines padrão (`0x728a60`, `0x729a90`, `0x72a030`).

A resolução vem de um campo do dispositivo: bom ponto para resolução livre no port.

## Áudio
| Endereço | Papel |
|---|---|
| 0x3d8990 | ngs2_init: `sceNgs2SystemCreateWithAllocator` (alocador próprio `0x3d9600/0x3d9640`), rack `0x3000` (mastering) + rack `0x1000` (sampler, N vozes), sample rate, grain; thread `"render ngs2 system"` (`0x3d8fa0`, prioridade 700) |
| 0x3dc740 | cria decodificador conforme o formato interno: formato 1 → `sceAudiodecCreateDecoder(tipo 2)`; formatos 3/4/5 → tipo 3 (quadros de 4608 bytes) |
| 0x3dced0…0x3dd5b0 | outras 6 rotas de criação de decoder |

Os IDs de codec do `sceAudiodec` (tipo 2 e tipo 3) ainda precisam ser confirmados contra documentação
pública (provavelmente ATRAC9 para os `.at9`).
O runtime precisa de um Ngs2 funcional (mastering + sampler racks + mixagem por grain) e de um
Audiodec que decodifique ATRAC9 (FFmpeg/libatrac9).
