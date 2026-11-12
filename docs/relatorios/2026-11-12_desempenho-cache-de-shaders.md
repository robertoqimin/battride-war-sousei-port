# Desempenho 1: cache de shaders

**Data:** 2026-11-12

## Resposta curta

**Não precisou de trabalho novo.** O port herdou do bbport um cache de pipelines completo:
- tudo o que é compilado vai para o disco (`port/user/cache/CUSA02716/`);
- na abertura seguinte, o cache é pré-carregado em **6 ms**, e os pipelines gráficos são criados em segundo plano em **2 ms**;
- shaders novos são compilados em threads separadas (`BB_ASYNC_PIPELINES`);
- o cache se reconstrói sozinho se mudar a versão do port ou o driver.

Medi o pior caso, um jogador novo sem nada em cache, e mesmo assim o engasgo é pequeno.

## Medição

A mesma rota foi rodada duas vezes (menu → Story → 150 s de combate na 1ª missão, 240 s no total), com `BB_FRAME_STATS=1` e `BB_FRAME_LOG`:

| | Cache vazio | Cache cheio |
|---|---|---|
| Quadros | 14.314 | 14.348 |
| Shaders/pipelines compilados durante o jogo | 183 | **0** |
| Quadros com compilação que passaram de 16,7 ms | **3** (33 ms cada, 1 vsync perdido) | 0 |
| Quadros acima de 17,5 ms (total) | 54 | 8 |

Sem cache, compilar 183 shaders custou **3 quadros perdidos de 33 ms** espalhados pela primeira vez em cada tela. Isso é imperceptível. Depois disso, o cache fica cheio e esses quadros somem.

## O que sobra (não é shader)

Os outros quadros lentos aparecem **com e sem cache**, então não têm nada a ver com compilação:

1. **Cenas com ~1.500–1.700 draws por quadro** (aos 117–133 s): o quadro dobra para 33 ms. Na rodada sem cache houve uma sequência de ~45 quadros assim, em 126–133 s; na com cache, 3. A diferença é o que estava na tela, porque o combate automático não é determinístico. O tempo de CPU do estágio A fica em 3,6–5,2 ms, então o gargalo deve ser a GPU ou a sincronização do envio. **É o próximo alvo de desempenho**, e serve também para resolução maior e >60 FPS.
2. **Um quadro de 67 ms aos 15 s**, nas duas rodadas: é uma transição de tela (5 draws), provavelmente o próprio jogo carregando.

## Sobre distribuir um cache pronto

**Não dá para distribuir.** O cache é SPIR-V traduzido dos shaders do jogo: é derivado de código proprietário, igual aos arquivos do jogo. Cada usuário gera o seu na primeira vez em que joga, com o custo medido acima.

## Mudanças

Nenhuma no código. O cache local foi restaurado depois do teste: a versão anterior, com 93 pipelines, cobre mais telas (galerias e menus).

## Próximo passo sugerido

Investigar as cenas com ~1.700 draws. Antes de mexer na resolução, preciso saber se o tempo vai para a GPU ou para o envio. Se for GPU, aumentar a resolução piora; se for envio, há margem.
