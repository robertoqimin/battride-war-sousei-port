# Crash na thread "update ngs2 stream sound"

**Data:** 2026-11-13
**Relato:** jogando a fase final, o jogo fechou com `Fault (signal 11) at RIP 0x1, address 0x1`, thread `update ngs2 str`.

## Resposta curta

- **Causa ainda não confirmada.** O crash não se repetiu em ~22 min de luta automática na fase final (4 sessões, até 697 sons em stream).
- **Descartei três suspeitas, com medição** (abaixo).
- **Duas mudanças prontas:**
  1. **Relatório de falha melhorado:** num salto para endereço inválido, agora mostra de onde veio (os endereços do jogo na pilha) e os registradores principais. A próxima ocorrência aponta o culpado.
  2. **Trava na pausa de 1 ms:** a pausa das threads de stream (a economia de CPU) só acontece **enquanto o som toca**. No fim do som (estado 4 ou 5), a thread volta a sair em microssegundos, como no original. Remove justamente a janela que a minha mudança de ontem tinha aumentado. A economia de CPU continua: as threads de stream ficam em ~0% na luta.

## O que a thread faz (eboot)

```
0x2d74c0: while (stream->state != 5) {   // done(): state == 5
              stream->update();            // vtable +0x30 = 0x2d7c10
              scePthreadYield();
          }
```

- **`update()`:** consulta a voz (`sceNgs2VoiceGetState` / `GetStateFlags`), alimenta o stream lendo do disco pelo Fios2 (`sceFiosOpIsDone`) e manda os pedaços (`sceNgs2VoiceControl`).
- **`stop()` (`0x2d7680`):** põe `state = 4`. Quem leva o estado a 5 é o próprio `update()`.
- **O destrutor (`0x2d75d0`):** para o stream e faz `scePthreadJoin` antes de liberar o objeto.
- **A função que inicia um stream (`0x2d7790`):** cria a thread **sem** esperar a anterior.
- **`RIP 0x1`:** sinal de chamada por um ponteiro inválido (vtable ou endereço de retorno corrompido).

## Suspeitas descartadas

| Suspeita | Como verifiquei | Resultado |
|---|---|---|
| Reaproveitamento do objeto de stream com a thread antiga ainda viva (a pausa de 1 ms aumentava essa janela) | detector `KR_STREAM_RACE_CHECK=1`: duas threads no mesmo objeto, e o intervalo de reaproveitamento | **0 sobreposições** e **nenhum reaproveitamento em menos de 20 ms**, em 3 sessões de 5 min (com pausa sem trava, com trava, e com o laço girando como no original) |
| Pilha reaproveitada cedo demais (pool de pilhas) | código | o `join` usa `pthread_join`: a pilha só volta quando a thread saiu |
| `sceNgs2VoiceGetState` escrevendo além do buffer (0x30 bytes na pilha do jogo) | código | escreve exatamente o tamanho pedido |

## Ferramentas novas

- **`KR_STREAM_RACE_CHECK=1`:** conta threads de stream simultâneas no mesmo objeto e os reaproveitamentos rápidos (log a cada 10 s).
- **`KR_STREAM_YIELD_GATE=0`:** pausa sem a trava por estado (o comportamento de ontem), para comparar.
- **`KR_STREAM_YIELD_US=0`:** sem pausa nenhuma (o laço gira como no original).

## Para você

Jogue normalmente. **Se cair de novo**, mande as últimas linhas do terminal: agora elas trazem `game addresses on the stack: [rsp+..] +0x...` e os registradores. Com isso eu acho a chamada exata.

## Arquivos alterados

- `port/src/runtime_thread.c`: o yield com a trava por estado (`kr_thread_yield`) e o detector de corrida.
- `port/src/probe.c`: o relatório de falha.

**Ainda não foram enviadas ao GitHub.**
