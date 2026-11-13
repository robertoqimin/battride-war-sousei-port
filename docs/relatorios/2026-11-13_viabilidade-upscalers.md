# Viabilidade: FSR / DLSS neste jogo

**Data:** 2026-11-13

## Resposta curta

**É viável.** O jogo **já separa a cena do HUD**, e o bbport tem quase toda a máquina necessária em forma genérica. As adaptações deste jogo são pontuais. O maior risco não é técnico, é de **qualidade**: sem vetores de movimento por objeto, personagens e inimigos se movendo tendem a deixar rastro (*ghosting*).

Estimativa: **2 a 4 dias** para a versão básica (vetores de movimento só da câmera); vetores por objeto são um passo extra e incerto.

## Como o jogo monta um quadro (captura `BB_CAPTURE_TRIGGER`, em combate)

| Passe | O quê | Alvos |
|---|---|---|
| **#0** (774 draws) | **a cena 3D**, com efeitos transparentes no mesmo passe | cor `0x10c7bc0000` **RGBA8 sRGB** 1920×1080 + profundidade `0x10c9ba8000` **D32S8** 1920×1080 (lê o mapa de sombra 2048²) |
| #1 | um draw sem alvo | — |
| #2 | cópia da cena para um alvo UNORM | lê a cena |
| #3–#8 | bloom: 960×540 → 480×270, com blur | — |
| passe de tela | composição: cena + bloom **+ HUD** no buffer de tela | — |

**Correção do que eu disse antes:** a cena **não** é desenhada direto na tela; ela tem alvo próprio. O overlay de depuração falhou porque o bbport o aplica no início do passe de tela, que aqui é a composição.

## As três etapas

### 1. Desenhar a cena numa imagem reduzida e separada: **viável, risco baixo a médio**

- A máquina de cena reduzida do bbport (`SceneTargets`) é **genérica**: todo alvo do tamanho nativo, num formato que possa ser copiado com escala, ganha um *proxy* reduzido. O D32S8 tem suporte a *proxies* de profundidade.
- As leituras posteriores da imagem nativa (passe #2) recebem a versão resolvida.
- **Falta:** dizer ao port o tamanho nativo e que o passe #0 é a cena (hoje as estatísticas mostram "reduced-size draws 0/frame").

### 2. Onde a cena termina e o HUD começa: **viável, risco médio**

- A cena termina **no fim do passe #0**. O passe #2 lê a cena, e o upscale entra entre os dois.
- O HUD só aparece no **passe de tela**, depois do bloom: ele já fica em resolução cheia por natureza.
- **Falta:** a composição de UI do bbport reconhece a interface pelos *hashes* dos shaders do **Scaleform** (Bloodborne). Para este jogo, é preciso identificar os shaders do HUD no passe de tela, ou uma regra mais simples ("tudo no passe de tela depois da composição é UI").

### 3. Ligar cor, profundidade e movimento ao FSR/DLSS e recompor: **viável, risco de qualidade**

- **Câmera:** ✅ já ligada (commit `4e58e02`): proporção 16:9, FOV ~38°, plano próximo 1, distante ~10.000.
- **Jitter:** ✅ genérico no bbport (deslocamento de subpixel nos draws com a profundidade da cena).
- **Cor da cena:** o upscaler espera um alvo **RGBA16F** (HDR, do Bloodborne). Aqui é **RGBA8 sRGB (LDR)**: é preciso aceitar esse formato e configurar o FSR em modo LDR.
- **Movimento de objetos:** ❌ não existe. O bbport tinha vetores por objeto só para o Bloodborne. Sem eles, o FSR usa só o movimento da câmera, e **personagens e inimigos em movimento podem deixar rastro**, o que pesa num jogo com hordas. Uma máscara reativa (efeitos transparentes) ajuda nas partículas, mas não nos personagens. Vetores por objeto exigiriam as matrizes de cada objeto e, nos personagens com esqueleto, as dos ossos, o que é bem mais difícil.

## Para que serviria

- **Nesta máquina, em 1080p:** pouco. O jogo já roda a 60 FPS com folga.
- **Saída em 4K:** é o caso útil. Hoje `KR_RES=4k` fica em 43–55 FPS em combate; cena em 1440p ampliada para 4K pelo FSR deve segurar 60.
- **GPUs fracas e Steam Deck:** cena em 540p/720p ampliada.

## Recomendação

Fazer em duas fases:
1. **Versão básica:** etapas 1–3 com movimento só da câmera e o FSR 3 (AMD, roda na sua placa). Avaliar o rastro em combate com capturas.
2. **Se o rastro incomodar:** estudar os vetores por objeto, ou usar o FSR 1 espacial (sem rastro, qualidade menor, e já existe).

Nenhum código mudou neste passo.
