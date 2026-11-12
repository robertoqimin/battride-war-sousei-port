# Fase 2.6 — Custo do áudio (mixer Ngs2)

**Data:** 2026-11-12
**Pergunta:** o mixer de áudio (decodificar ATRAC9, reamostrar, mixar e limitar) pesa no jogo?
**Resposta:** **não.** Ficou bem abaixo da meta, e nenhuma otimização é necessária.

---

## Medição

Criei o **`KR_NGS2_PROF=1`** em `runtime_ngs2.c`. A cada 5 s, ele mostra quanto tempo cada render levou para mixar, quanto esperou pelo lock do mixer e quantas vozes tocavam.

Rota de benchmark completa (`out/bench/au1.log`): boot, menus, tutorial e ~70 s de combate a 60 FPS.

| Trecho | Renders / 5 s | Mixagem média | Pior render | Espera no lock | Vozes (média / máx.) |
|---|---|---|---|---|---|
| Boot / vídeo | ~1.880 | 0,001 ms | 0,010 ms | 0 | 0 / 0 |
| Menus (BGM) | ~2.810 | 0,006–0,007 ms | 0,045 ms | 0 | 0,3 / 1 |
| **Combate** (golpes, vozes, BGM) | ~2.810 | 0,007–0,021 ms | **0,219 ms** | 0 | 1,9 / **12** |

**Meta do roadmap:** menos de 0,5 ms por bloco de 256 amostras, no pior combate. **O pior caso medido foi 0,22 ms**: menos da metade da meta e cerca de 4% do tempo real de um bloco (5,3 ms).

- O lock global do mixer **nunca teve disputa**: a espera ficou em 0,000 ms.
- Não precisa decodificar fora do lock, separar locks por sistema nem usar SIMD.

## 2.8 Threads (avaliação)

Com a CPU sobrando (o laço do jogo usa ~1–3 ms por quadro), o áudio custando microssegundos e a GPU ~92% ociosa, **não há disputa a resolver** com prioridades ou afinidade de threads. O item fica **sem ação**. Se aparecer engasgo de áudio em máquinas mais fracas (Steam Deck), ele volta.

---

## Arquivos alterados

- `port/src/runtime_ngs2.c`: perfil `KR_NGS2_PROF` (desligado por padrão, sem custo quando desligado).

## Próximos passos sugeridos

1. **2.7 Resolução** 1440p/4K e ultrawide: a próxima grande frente de desempenho e qualidade.
2. Unwind de exceções C++ (as 5 funções).
3. Fase 4: limpar a herança do Bloodborne e criar o arquivo de configuração próprio.
