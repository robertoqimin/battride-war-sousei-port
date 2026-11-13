# CPU em máquinas fracas, tremor dos vídeos, áudio e câmera para os upscalers

**Data:** 2026-11-13

## Resumo

| Item | Resultado |
|---|---|
| **6a. Thread de vídeo a 100%** | ✅ **corrigido**: filmes de **156% → 47%** de CPU no processo; a thread `Update AvPlayer` de **65% → 0%**. Cadência e quadros iguais |
| **6b. Thread principal girando à espera da GPU** | ⚠️ **opção, desligada por padrão**: `KR_FLIP_WAIT_MARGIN_US=1000` leva a thread principal de **46–58% → 17–20%**, mas custou ~meio quadro perdido a mais por minuto de combate pesado (1 → 4 em 5 rodadas). Vale para CPUs com poucos núcleos |
| **5. Tremor dos vídeos** | ✅ **corrigido**: cadência perfeita, todos os 118 quadros 2× (antes 15 com 1×, 88 com 2×, 15 com 3×), 0 quadros errados |
| **3. Áudio "incompleto"** | ✅ **verificado: não falta nada**. O jogo não usa reverb, envelope nem filtros; o mastering vem configurado sem processamento |
| **4. Câmera para os upscalers** | 🔎 **câmera encontrada**: falta ligá-la ao upscaler (próximo passo) |
| Compilação e memória | ✅ o `build.sh` limita as tarefas paralelas pela RAM disponível (2 GiB por tarefa; `BB_JOBS=N`): na compilação, a memória livre não passou abaixo de ~3,8 GB |

## 6a. Thread de vídeo do jogo

A thread `Update AvPlayer` do jogo consulta o próximo quadro em laço (`GetVideoData` + `scePthreadYield`), igual às threads de som. Ela agora dorme 1 ms por volta (`KR_AVPLAYER_YIELD_US`, padrão 1000; 0 = antigo). Os quadros vêm a cada 33 ms, então isso não atrasa nada.

| Abertura do jogo (filme) | Antes | Depois |
|---|---|---|
| CPU do processo | 156% | **47%** |
| `Update AvPlayer` | 65% | **0%** |
| Cadência / quadros errados | 15·88·15 / 0 | 15·88·15 / 0 |

## 6b. Thread principal esperando o buffer da GPU

O laço de flip do jogo (`0x62d5f0`) gira em `scePthreadYield` até a GPU liberar o buffer (5–11 ms por quadro). Testei três formas:

1. **Pausas fixas de 100 e de 50 µs:** quadros lentos a mais em cenas pesadas.
2. **Acordar pelo label (futex):** não funciona, porque o label é escrito **pela própria GPU**, sem passar pela CPU do port (0 acordadas, 30 mil prazos estourados).
3. **Espera prevista:** dorme até o menor tempo das últimas 8 esperas menos 1 ms e gira o final, como antes. É a melhor, mas ainda custa ~meio quadro a mais por minuto:

| 5 rodadas de combate | Thread principal | Quadros lentos fora da troca de cena |
|---|---|---|
| Laço girando (padrão) | 46–58% | 1 |
| `KR_FLIP_WAIT_MARGIN_US=1000` | 17–20% | 4 |

Nesta máquina a CPU sobra, por isso ficou **desligada por padrão**.

## 5. Tremor dos vídeos (cadência 1-2-3)

O relógio do vídeo segue o áudio ("último bloco de áudio que o jogo pegou + tempo desde então"). O jogo pega o áudio adiantado e em rajadas, então o relógio vira um **dente de serra**, e quadros perto da fronteira de uma consulta de 60 Hz apareciam 1 ou 3 vezes.

- **Por que a suavização de ontem não funcionou:** ela corrigia 5% *por consulta*, e o jogo consultava milhares de vezes por segundo.
- **Agora:** o relógio é suavizado **pelo tempo** (fecha `dt/500 ms` da diferença; `KR_AVPLAYER_SMOOTH_MS`, 0 = desligado) e salta só para diferenças acima de 150 ms (avanço ou pausa).
- **Resultado:** os 118 quadros do trecho aparecem exatamente 2 vezes cada.

## 3. Áudio

O que o jogo pede ao Ngs2 (log e `KR_TRACE_NGS2`):
- **Racks:** só **sampler** (0x1000) e **mastering** (0x3000), em 4 sistemas. **Nenhum** rack de reverb, submixer ou EQ.
- **Parâmetros de voz:** todos tratados. Nenhum envelope, filtro ou evento ignorado.
- **Mastering:** parâmetro `0x30000000` (8 canais ou 1) e `0x30000005` **todo zerado** (sem compressor nem limitador) → o "passar direto" do mixer é o comportamento correto.
- **Saída:** 8 canais (7.1) entregues ao SDL, que faz a mixagem para o seu dispositivo; o centro e os traseiros entram no estéreo.

**A afirmação de que faltavam reverb e envelope estava errada:** vinha do que o Ngs2 oferece, não do que este jogo usa.

## 4. Câmera para os upscalers

A captura de câmera do bbport era toda do Bloodborne: um bloco de 864 bytes com "plano distante = 3000" e posições fixas, e a cena principal identificada como um G-buffer de 5 alvos. Para este jogo:
- **Captura nova** (`KR_CBUF_DUMP=<arq> KR_CBUF_FRAMES=a,b`): grava os buffers de constantes dos vertex shaders em dois quadros.
- **A câmera:** nos shaders da cena (slot 0, 176–352 bytes), os **16 primeiros floats** são iguais em todos os shaders do quadro: a **matriz vista×projeção**, coluna por coluna. A 4ª linha (direção da vista) tem comprimento 1, e |linha y| / |linha x| = 2,904 / 1,634 = **1,777 (16:9)**.
- **Próximo passo:** separar vista e projeção (o formato que o upscaler usa), reconhecer o passe da cena deste jogo (cor R8G8B8A8 + profundidade D32S8 em 1920×1080), ligar ao `CameraMotion` e conferir com o modo de visualização dos vetores de movimento (`BB_DEBUG_MOTION=1`).

## Ferramentas e opções novas

- **CPU:**
  - `KR_AVPLAYER_YIELD_US` (1000);
  - `KR_FLIP_WAIT_MARGIN_US` (0 = desligado; 1000 para CPUs fracas).
- **Vídeo:** `KR_AVPLAYER_SMOOTH_MS` (500).
- **Câmera:** `KR_CBUF_DUMP` / `KR_CBUF_FRAMES`.
- **Compilação:** `BB_JOBS`, e o `build.sh` decide sozinho pela RAM.

## Arquivos alterados

- `port/src/runtime_thread.c`: o yield com o laço da thread de vídeo e a espera prevista do flip.
- `port/gpu/platform/avplayer/source.cpp|.h`: o relógio suavizado.
- `port/gpu/render/raster_bind.cpp`, `rasterizer.h`: a captura de constantes.
- `port/src/runtime_ngs2.c`: o trace de mastering com contador próprio.
- `port/build.sh`, `port/gpu/CMakeLists.txt`: o limite de tarefas.

**Ainda não foram enviadas ao GitHub** (junto com as duas mudanças do crash da thread de som).

---

## Atualização: a câmera ligada ao `CameraMotion`

- **`CameraMotion::OnViewProjection`:** separa a vista×projeção deste jogo:
  - a 4ª linha é o eixo de profundidade da vista (w = z da vista);
  - as linhas x e y normalizadas são os eixos, e os comprimentos delas são as escalas;
  - a 3ª linha dá `z = zs·z_vista + zo`.
- **Passe da cena (jogo sem perfil):** um alvo de cor com profundidade D32S8 do mesmo tamanho (≥ 640 de largura). A câmera vem do primeiro draw da cena com constantes de vertex shader no slot 0 de 176–352 bytes.
- **Resultado:** `x 1,6336, y 2,9042 (proporção 1,7778 = 16:9), zs 1,0001, zo −1,0001`, ou seja, **campo de visão vertical ~38°, plano próximo em 1 e distante em ~10.000**. Uma câmera coerente.
- **O que descobri sobre a renderização deste jogo:** a cena é desenhada **direto no buffer de tela** (RGBA8 sRGB 1920×1080), com fumaça e HUD por cima, sem um alvo de cena separado como no Bloodborne. Por isso a visualização dos vetores (`BB_DEBUG_MOTION=1`) dispara no ponto errado e não serve para validar aqui.

### O que falta para os upscalers temporais (projeto à parte)

1. Redirecionar o passe da cena para um alvo **reduzido e separado** (o upscaler precisa da cena sem o HUD).
2. Achar onde termina a cena (antes de fumaça e transparências?) e onde começa o HUD.
3. Ligar a cor reduzida, a profundidade, os vetores e o jitter ao FSR 3/4 ou ao DLSS, e recompor com o HUD na resolução da tela.
4. Validar os vetores de movimento.

É trabalho de vários dias. A câmera, que era o pedido deste item, está achada e ligada. Nada disso afeta o jogo normal: o caminho só liga com `BB_UPSCALER` diferente de `none` (o padrão) ou com `BB_DEBUG_MOTION=1`.
