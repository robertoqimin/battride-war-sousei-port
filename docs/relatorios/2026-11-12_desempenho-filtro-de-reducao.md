# Desempenho 4: filtro de redução (supersampling)

**Data:** 2026-11-12

## Resposta curta

- Quando a imagem do jogo é **maior que a janela** (`KR_RES=1440p` ou `4k` num monitor 1080p), cada pixel da janela agora é a **média exata, em luz linear, dos pixels que ele cobre** (filtro de área). Antes era uma leitura bilinear de 4 pixels em torno de um ponto.
- **Conferido contra uma referência exata** calculada fora do jogo: **63–64 dB de PSNR**, ou seja, idêntico tirando o arredondamento. O bilinear antigo fica em 43 dB da mesma referência.
- **Custo:** imperceptível. 1440p continua em 60 FPS, com pior quadro de 16,7 ms.
- `KR_DOWNSCALE=0` volta ao bilinear, para comparar.

## Onde ficou (e o que eu achei no caminho)

Minha primeira versão pôs o filtro no último passo, antes da cópia para a tela (`RecordPresent`). O teste mostrou que **o frame do presenter já é criado no tamanho da janela**: aqui 1820×1024, porque a janela não está em tela cheia. A redução de verdade acontece antes, no passe que lê o buffer do jogo e aplica a curva de cor (`present_encode.frag`). Desfiz a primeira versão e coloquei o filtro nesse shader:

- a proporção da redução sai das derivadas da coordenada de textura, sem parâmetro extra;
- cada pixel de saída cobre `proporção × proporção` pixels da fonte e soma cada um com o peso da fração coberta (até 8 por eixo, ou seja, proporções até 7×);
- **SDR:** a média é feita em luz linear, porque média de valores com curva de gama escurece bordas finas e claras. **HDR:** média direto no espaço do sinal;
- sem redução (fonte ≤ janela), tudo continua como antes, inclusive o RCAS de nitidez.

**Efeito colateral bom:** numa janela menor que 1080p, como esta de 1820×1024, até o 1080p nativo passava pelo bilinear. Agora também passa pelo filtro de área.

## Validação

Capturei no mesmo quadro o buffer do jogo (2560×1440, `BB_PRESENT_DUMP_TRIGGER`) e o frame já filtrado (1820×1024, `BB_FINAL_DUMP_TRIGGER`). Depois comparei com referências calculadas fora do jogo:

| Saída da GPU comparada com… | Com filtro de área | Com `KR_DOWNSCALE=0` |
|---|---|---|
| Média por área **exata** em luz linear (script Python) | **64,1 dB** (idêntico) | 43,3 dB |
| Bilinear (ImageMagick) | 40,6 dB | **45,9 dB** |

Observação: o filtro "Box" do ImageMagick **não** é uma média por área exata em proporções não inteiras (ele ignora a cobertura parcial). Por isso a referência foi feita em Python.

![Comparação ampliada 3×: bilinear à esquerda, filtro de área à direita](img/2026-11-12_filtro-reducao_comparacao.png)

Em imagem parada, a 1,4×, a diferença é sutil: bordas das lanças e do minimapa mais regulares. O ganho maior é **em movimento**, com menos tremido em linhas finas, porque agora todos os pixels da fonte entram na média com o peso certo.

## Arquivos alterados

- `port/gpu/render/shaders/present_encode.frag`: o filtro de área.
- `port/gpu/render/present_passes.h`: campo `area_filter` nas configurações do passe.
- `port/gpu/render/presenter.cpp`: `KR_DOWNSCALE`.
- `tools/bench/run_route.sh`: `RR_FULL=1` mantém as capturas no tamanho original.
- `README.md`: `KR_DOWNSCALE`.

**Ainda não foram enviadas ao GitHub.**
