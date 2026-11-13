# Vídeos "cortando, voltando frame"

**Data:** 2026-11-12
**Relato:** nos vídeos (cutscenes pré-renderizadas), a imagem parecia cortar e voltar um quadro.

## Resposta curta

**Corrigido.** Era uma corrida no reaproveitamento dos buffers de vídeo: o decodificador regravava um buffer antes da GPU terminar de ler o quadro que estava nele. O jogo mostrava por um instante um quadro **do futuro**, e a imagem voltava. Agora os últimos quadros entregues ficam retidos um pouco mais antes de voltar ao pool.

| Mesmo trecho do vídeo de abertura, 240 quadros (4 s) | Quadros errados (que voltam) |
|---|---|
| Antes (2 rodadas) | **3 e 4** |
| Depois (3 rodadas) | **0, 0 e 0** |

## Como medi

- Capturei **240 quadros seguidos** apresentados durante o vídeo de abertura (`BB_PRESENT_DUMP_COUNT=240`).
- Um quadro "errado" é o que **difere dos dois vizinhos enquanto os vizinhos são parecidos entre si**: o padrão A → B → A.
- Antes, ele aparecia de 3 a 4 vezes a cada 4 s. Na série de diferenças: `… 0,6 / 2,5 / 2,4 / 0,8 …`, com o quadro 72 voltando a ser quase o 70.

## A causa

O vídeo (H.264, 1920×1080, **30 fps**, áudio AAC 44,1 kHz) é decodificado pelo AvPlayer do runtime num pool de buffers na memória do jogo. O jogo desenha o vídeo como uma textura que **aponta direto para o buffer** do quadro atual.

1. O jogo pega o quadro F1, e o buffer do F0 voltava **na hora** para o pool.
2. O decodificador, adiantado, já gravava ali o F2.
3. A thread de GPU do port roda **um quadro atrás** do jogo. Quando ela ia ler o buffer para desenhar o quadro anterior, encontrava o F2.
4. No quadro seguinte, mostrava o F1 de novo: **pula para frente e volta**.

O pool tinha exatamente o número de buffers que o jogo pede, sem folga para esse atraso.

## A correção (`port/gpu/platform/avplayer/source.cpp|.h`)

- Os buffers dos quadros que o jogo já deixou para trás ficam **retidos** (`KR_AVPLAYER_HOLD`, padrão 3) antes de voltar ao pool.
- O pool ganha o mesmo número de buffers extras, para o decodificador não ficar sem espaço.
- Custo: 3 buffers NV12 a mais por vídeo, ~9 MB.
- `KR_AVPLAYER_HOLD=0` reproduz o comportamento antigo, para comparar.

## O que sobra (menor)

O ritmo ideal de um vídeo de 30 fps a 60 Hz é cada quadro aparecer **2 vezes**. Hoje, a cada 6 ou 7 quadros do vídeo, um aparece 1 vez e outro 3 vezes (padrão `…2 2 1 2 3 2 2…`, um leve tremor em movimento):

- Isso já acontecia antes e **não é a "volta"**: nenhum quadro retorna.
- **O relógio de reprodução está exato.** Medi cada quadro entregue ao jogo: sai exatamente no seu carimbo de tempo (`ts 6633 / clock 6633`…).
- **Testei suavizar o relógio:** nenhuma diferença mensurável, então removi.
- **A irregularidade vem depois da entrega**, entre a thread do jogo que pega os quadros (ela gira a 100%) e o momento em que o jogo desenha.

Se o tremor incomodar, dá para investigar o lado do jogo; diga se ainda nota algo.

## Arquivos alterados

- `port/gpu/platform/avplayer/source.cpp`, `source.h`: quadros retidos, `KR_AVPLAYER_HOLD`.
- `README.md`: `KR_AVPLAYER_HOLD`.

**Ainda não foram enviadas ao GitHub** (junto com o C++ unwind e a validação com o save zerado).
