# Viabilidade: SMAA, FXAA e MSAA neste jogo

**Data:** 2026-11-13

## Resposta curta

| Técnica | Viável? | Esforço | Risco | Observação |
|---|---|---|---|---|
| **SMAA** | ✅ **sim** | ~meio dia a 1 dia | baixo | o bbport já tem o SMAA 1x pronto; falta adaptar o gatilho e o formato |
| **FXAA** | ✅ sim | poucas horas **depois** do SMAA | baixo | mesmo encanamento do SMAA + um shader; mais barato e mais borrado que o SMAA |
| **MSAA** | ⚠️ possível, mas caro | vários dias | **alto** | mexe em imagens, pipelines e na memória espelhada do port |

**O jogo não tem antisserrilhado próprio:** a captura de um quadro não mostra nenhum passe de AA (cena → cópia → bloom → tela).

## SMAA: a melhor relação custo/benefício

O bbport tem o SMAA 1x (`upscale/vk_smaa.cpp`, MIT), que roda "na cena LDR pronta (RGBA8, espaço gama), antes de a UI ser desenhada por cima". É o caso deste jogo. O que ele exige:

1. **Profundidade e câmera da cena:** ✅ já ligadas (commit `4e58e02`).
2. **A cena como RGBA8 UNORM:** aqui é **sRGB**, lida por uma visão UNORM, que dá os valores em espaço gama que o SMAA espera.
3. **O momento:** no Bloodborne, antes do primeiro draw da UI (pelo hash do shader dela). Aqui pode ser **mais simples e mais cedo**: no **fim do passe da cena** (#0), antes do passe #2 ler a cena. Assim, o bloom já sai do quadro suavizado e o HUD fica intocado, porque só é desenhado depois.

## FXAA

O FXAA que o bbport oferece é o **do próprio Bloodborne** (um efeito do jogo), e não existe aqui. Para ter FXAA seria preciso um shader novo, por exemplo o FXAA 3.11 da NVIDIA, de licença permissiva, no mesmo ponto do SMAA. O SMAA dá resultado melhor por um custo parecido; o FXAA só se justifica como opção mais leve.

## MSAA

Conceitualmente favorável, porque o jogo desenha a cena direto num único alvo e os passes seguintes **não leem a profundidade**. Mas no port isso exige:
- criar versões multiamostradas da cor e da profundidade do passe da cena;
- pipelines com N amostras para esses draws;
- resolver a cor no fim do passe de volta para a imagem 1x, que o port espelha na memória do jogo e outras partes leem.

É o ponto mais delicado da camada de GPU do bbport. Além disso, folhagem e grades com transparência por recorte (*alpha test*) continuariam serrilhadas sem *alpha-to-coverage*. Custo de GPU estimado no 4× a 1080p: algo como 30–50% a mais no passe da cena (hoje 4,5–7,5 ms) — cabe na sua placa, mas o risco de trabalho é alto.

## Recomendação

**SMAA primeiro**: rápido, seguro e o melhor resultado entre as opções baratas. O FXAA pode vir junto, como opção mais leve. O MSAA só se o SMAA não satisfizer.

Nenhum código mudou neste passo.
