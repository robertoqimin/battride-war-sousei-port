# Validação funcional do jogo

**Data:** 2026-11-12
**Pedido:** validar se o jogo é 100% funcional antes de seguir com desempenho.

---

## Resposta curta

**Ainda não dá para afirmar 100%.** A validação automática cobriu tudo o que o save atual permite alcançar e encontrou **dois bugs reais de áudio, um deles com crash**. Os dois foram corrigidos.

O que falta exige progresso na história (modos bloqueados) ou alguém jogando de verdade (terminar missões, chefes, vídeos de história).

| | Resultado |
|---|---|
| Crashes encontrados | **1** (decodificador ATRAC9) → **corrigido** |
| Bugs de áudio | **2** (crash e loop de BGM quebrado) → **corrigidos** |
| Telas e modos validados | boot, título, menu principal, Story (prólogo, tutorial, 1ª missão por 8 min), pausa, Custom Soundtrack (editor de playlist), Contents → Figure Viewer, Options → configurações / controles |
| Erros no log depois das correções | **0** (decodificação, *host fault*, STOP) |
| Desempenho depois das correções | combate a **60,0 FPS**, pior quadro 16,7 ms |

---

## Bug 1: crash no decodificador ATRAC9 (corrigido)

**Sintoma:** ao trocar de tela, quando o jogo criava a thread `Render Audioout BGM thread`, o processo morria com *segfault* em `ApplyBandExtension` (LibAtrac9), chamado pelo `sceNgs2SystemRender`.

**Causa (duas camadas):**
1. **Estado do decodificador nunca era reiniciado.** A LibAtrac9 guarda internamente em que quadro do superquadro está (`IndexInSuperframe`), além dos parâmetros reaproveitáveis e da sobreposição do IMDCT. O mixer reaproveitava o decodificador entre sons com a mesma configuração sem zerar nada, e nem o `Atrac9InitDecoder` da biblioteca zera esse estado.
   - Quando um som parava no meio de um superquadro, o próximo começava desalinhado.
   - A partir daí, **todo quadro falhava**: a música ficava muda.
2. **A LibAtrac9 não valida um índice.** Com dado desalinhado, `BexGroupInfo[QuantizationUnitCount - 13]` lia fora do vetor e derrubava o processo.

**Correção:**
- `LibAtrac9`:
  - nova função `Atrac9ResetDecoder`, que limpa o estado do quadro sem regerar as tabelas globais;
  - validação do índice nos três pontos que acessam `BexGroupInfo` (marcados com `kr-port`).
- `runtime_ngs2.c`: o decodificador é reiniciado a cada som, a cada início de stream e depois de cada erro. Os erros agora vão para o log (os 30 primeiros).

## Bug 2: o loop das músicas quebrava (corrigido)

**Sintoma:** ao fim de cada volta da BGM, por exemplo aos 64 s na galeria, vinham vários erros de decodificação e trechos mudos ou picotados.

**O que o jogo faz (descoberto agora):**
- o stream começa com *play* sem dados;
- o 1º pedaço vem com `flags=0x1` e traz `skip` e uma contagem de amostras **que é o fim do loop** (`smpl`), não o total do arquivo;
- os pedaços seguintes vêm com `0x3`, e o jogo envia **o arquivo inteiro**;
- para repetir, o jogo manda um novo pedaço `0x1` a partir do ponto de loop. Exemplo da galeria: `skip 189` e `n 2813736` = 3.088.100 − 274.365 + 1.

O mixer encerrava a voz quando a contagem zerava. O jogo então dava *play* de novo e os pedaços seguintes chegavam desalinhados.

**Correção (`runtime_ngs2.c`):** o stream passou a ser tocado em **iterações**:
- cada pedaço `0x1` marca o início de uma nova volta;
- depois da contagem, o restante do arquivo é **descartado** e a próxima volta segue **sem encerrar a voz**;
- a voz só termina se nenhuma nova volta chegar em 0,5 s (faixas sem loop).
- Além disso, *stop* e *kill* limpam a fila do stream.

**Validação:**
- 3 min parado na galeria, atravessando o loop: BGM contínua, **0 erros, 0 estalos**. O único silêncio é de 0,3 s, na troca de tela.
- Rota de combate: **74 streams sem loop terminaram normalmente** (efeitos e falas), 0 erros, 60 FPS.

---

## O que foi validado (rotas automáticas com captura de tela)

| Área | Situação |
|---|---|
| Boot, logo, vídeo de abertura, título | ✅ |
| Menu principal (6 modos) | ✅ navegação; **Time Survival e Free Mission estão bloqueados** no save atual (o cursor pula os dois). É regra do jogo: liberam com progresso na história |
| Story Mode: dificuldade, prólogo, tutorial | ✅ |
| 1ª missão (8 min de combate automático) | ✅ o objetivo de 100 inimigos foi cumprido, e vieram novos objetivos, áreas e tipos de inimigo; combos de até 73 hits; especiais e super especial. Sem erros |
| Menu de pausa (golpes, skills, configurações, sair da missão) | ✅ (validado antes) |
| Custom Soundtrack | ✅ lista de playlists, criar playlist (Simples/Dramática/Maníaca), escolher slot e música original |
| Contents → Figure Viewer | ✅ (0/208 figuras) |
| Options → configurações de jogo, câmera, som; tela de controles | ✅ |
| Teclado e mouse | ✅ (passo anterior) |

![Missão 1 em 8 minutos de combate automático](img/2026-11-12_validacao_missao-8min.png)

## O que ainda NÃO foi validado

| Item | Motivo | Como validar |
|---|---|---|
| **Terminar uma missão** (resultado, recompensa, save pós-missão) | a missão exige ir até pontos do mapa; o combate automático não navega | jogando, ou com uma rota gravada (`BB_PAD_REPLAY`) |
| Time Survival, Free Mission | bloqueados até avançar na história | depois do item acima |
| Shop, Sound Gallery, Ranking (online) | a automação dos menus não chegou neles: o menu ignora toques durante a animação e lembra a última posição | rota ajustada ou jogando |
| Chefes, troca de forma, TAG, outros riders | só aparecem mais adiante na história | jogando |
| Vídeos de história (61 `.mp4`) | só a abertura foi vista | Sound/Movie gallery ou progresso |
| Sessão longa (1–2 h) e memória | a medição de memória desta rodada falhou (o script pegou o processo errado) | repetir |

### Ponto para você conferir

Durante a missão apareceu uma **forma escura esverdeada, grande, no canto superior direito**. Pode ser um efeito do jogo perto da câmera (fumaça ou portal dos Ganma) ou um erro de renderização. Daqui não dá para saber sem a referência do PS4. Se você vir isso jogando e parecer errado, me avise.

![Forma escura: efeito ou erro?](img/2026-11-12_validacao_forma-escura.png)

---

## Ferramentas novas

- `tools/bench/run_route.sh <nome> <rota> <segundos> [VAR=valor]`: roda o jogo com uma rota (`press`, `hold`, `shot`, `w`) e salva as capturas em PNG em `out/val/<nome>/`.

## Arquivos alterados

- `port/src/runtime_ngs2.c`: reset do decodificador, log de erros, stream em iterações (marcadores `0x1`, descarte, fim por espera), limpeza no stop.
- `port/third_party/LibAtrac9/C/src/`:
  - `decinit.c/.h` e `libatrac9.c/.h`: `Atrac9ResetDecoder`;
  - `unpack.c` e `band_extension.c`: validação de índice.
- `tools/bench/run_route.sh` (novo).

**Ainda não foram enviadas ao GitHub.** Ficam pendentes até você pedir.

## Próximos passos sugeridos

1. **Você jogar a 1ª missão até o fim.** É o que destrava o resto da validação: modos bloqueados, resultado e save pós-missão. Também serve para conferir a forma escura.
2. Gravar essa partida com `BB_PAD_RECORD`/`BB_PAD_REPLAY` (do bbport), para virar rota de regressão automática.
3. Sessão longa com medição de memória.
