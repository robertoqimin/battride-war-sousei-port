# Desempenho 6: picos de troca de cena

**Data:** 2026-11-12

## Resposta curta

- **Causa entendida:** o pico é um **estouro do orçamento de latência por ~1 ms** no primeiro quadro em que uma leva nova de inimigos aparece. Não é a criação das imagens (0,3 ms) nem um único culpado grande.
- **Frequência:** 0 a 2 quadros de 33 ms por troca de cena, em ~60% das vezes. Na rota de teste isso é **~1 quadro por minuto de combate**.
- **Os ajustes de folga do bbport não mudam o quadro** de forma mensurável (tabela abaixo).
- **Recomendação: não mexer.** Um quadro de 33 ms isolado numa troca de cena é quase imperceptível, e as correções possíveis são estruturais e arriscadas. Ficaram ferramentas novas de diagnóstico.

## O que acontece no quadro do pico

O evento é reproduzível: na rota de teste, aos ~98 s, o jogo entra numa área nova da missão e o quadro salta de **~280 para ~2.100 draws**, com **~60 imagens novas** (texturas BC1 dos inimigos) e ~55 alocações Vulkan.

1. **Criar as imagens é barato:** 0,3 ms de criação e 0,08 ms de alocação.
2. **A lógica do jogo não pesa:** as threads do jogo continuam esperando a maior parte do tempo (perfil `KR_GAME_PROFILE`).
3. **A GPU tem o quadro mais pesado da rodada nesse momento:** **8,69 ms**, contra ~7,4 ms nos quadros pesados normais. A diferença é o envio das texturas novas (0,6 + 0,5 ms) e os primeiros passes com os inimigos novos.
4. **O flip do jogo é de fim de pipeline:** ele só sai quando a GPU termina o quadro. O caminho do quadro é: o jogo monta os comandos, o port traduz e grava (~3,5 ms com 2.000 draws) e a GPU executa (~7,5–8,7 ms). Com 2.000 draws isso já fica **rente aos 16,7 ms**. No quadro das texturas novas, passa por ~1 ms e o flip escorrega para o vsync seguinte (33 ms). Na thread principal do jogo, aparece como uma espera de flip de 26–28 ms em vez de ~10.

## Ajustes testados (5 rodadas de cada; quadros lentos nos eventos de 55 s e 98 s)

| Variante | Quadros lentos por rodada | Total |
|---|---|---|
| Base | 0, 0, 1, 1, 2 | 4 |
| `BB_FRAMES_AHEAD=2` (o port adianta mais um quadro) | 0, 0, 0, 1, 1 | 2 |
| `BB_TEXTURE_UPLOAD_MB_PER_FRAME=8` (espalha o envio de texturas) | 0, 0, 0, 1, 2 | 3 |

São números pequenos e dentro do ruído. Nenhum ajuste vira padrão.

## O que resolveria de verdade (não feito)

- **Gravar os comandos em paralelo com a execução**, para cortar parte dos ~3,5 ms do port no caminho crítico. É uma mudança grande no núcleo do bbport.
- **Enviar texturas novas numa fila de transferência separada**, para tirá-las do quadro. Também é mudança de arquitetura.

Para ganhar ~1 ms num quadro por minuto, não compensa o risco agora. Na prática, **`KR_RES` abaixo da resolução da tela** ou uma GPU mais forte dão a folga direto.

## Ferramentas novas

- **`BB_GPU_PROFILE_SLOW_MS=<ms>`** (com `BB_GPU_PROFILE=1`): cada quadro com mais que esse trabalho de GPU é impresso sozinho, com os passes mais caros.
- **`KR_GAME_PROFILE_THREADS`** aceita threads do port (`bb:`, ou nomes como `bb:DrawRec`), listadas pelo `/proc/self/task`.
- **Correção no amostrador:** os nomes de símbolo do port (`dladdr`) agora ficam em cache. Antes, o escritor travava ~90 ms a cada lote e abria buracos nos dados.

## Arquivos alterados

- `port/gpu/render/vk_gpu_profiler.cpp`: `BB_GPU_PROFILE_SLOW_MS`.
- `port/src/runtime_thread.c`: threads do port no `KR_GAME_PROFILE` e cache de símbolos.

**Ainda não foram enviadas ao GitHub.**
