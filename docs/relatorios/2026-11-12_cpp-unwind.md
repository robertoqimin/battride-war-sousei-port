# C++ unwind (exceções do jogo)

**Data:** 2026-11-12

## O que é

O jogo é escrito em C++, e o C++ trata erros com **exceções**: o código faz `throw` e a execução salta direto para um `catch` lá atrás na cadeia de chamadas. Para chegar lá, o programa **desenrola a pilha** (*unwind*):
- percorre de trás para frente as funções em andamento e restaura os registradores de cada uma;
- chama os destrutores dos objetos locais (libera memória, destrava locks);
- até achar o `catch` certo.

As instruções de como desenrolar cada função ficam nas tabelas `.eh_frame`, que o compilador gera, com um índice ordenado no `.eh_frame_hdr`. Quem desenrola é o código de unwind do **`libc.prx` do próprio jogo**, que roda nativo. Ele pergunta ao "kernel", que aqui é o nosso runtime, de qual módulo é cada endereço e onde ficam as tabelas dele.

Este jogo usa isso bastante:

| No eboot | Quantidade |
|---|---|
| Funções com tabela de unwind | **28.000** |
| Blocos `catch` | 1.224 |
| Pontos de limpeza (destrutores no caminho) | 1.737 |
| Trechos dentro de `try` (a maioria `catch(...)`) | 7.399 |
| `throw` | 15 (por exemplo, `std::bad_alloc` no `operator new`) |

**Antes:** as 5 funções que o desenrolador pede não existiam. A **primeira exceção** de qualquer origem (falta de memória, dados corrompidos, erro de sistema) parava o jogo com `STOP: first unsupported PS4 import`. Nas rotas testadas isso nunca tinha acontecido, mas era uma bomba-relógio para casos raros.

## O que foi implementado (`port/src/runtime_krunwind.c`)

| Função | O que faz |
|---|---|
| `sceKernelGetModuleInfoForUnwind` | o módulo que contém um endereço, com o `.eh_frame_hdr`, o `.eh_frame` e o segmento de código dele |
| `sceKernelGetModuleInfoFromAddr` | o mesmo, no formato completo de informação de módulo |
| `__elf_phdr_match_addr` | se um endereço cai num segmento executável de um módulo (`dl_iterate_phdr` da libc FreeBSD) |
| `_is_signal_return` | se um quadro é do trampolim de sinal (nunca aqui) |
| `sigreturn` | não usado pelo unwind; recusado, com aviso no log |

- **Na carga,** o loader (`probe.c`) registra as regiões dos módulos na imagem: o eboot, o `libc.prx` e o `libSceFios2.prx`, cada um com seu segmento executável.
- **Na primeira consulta,** o runtime acha o `.eh_frame_hdr` de cada região pelo cabeçalho (`01 1b 03 3b`), valida a tabela (endereços de código da região, em ordem crescente) e decodifica onde começa o `.eh_frame`.
- **Os layouts** das estruturas seguem os documentados pelo shadPS4.
- **Conferido contra os ELFs:** eboot `.eh_frame_hdr` em `0x9dcd70` e `libc.prx` em `0xb02f8`, iguais ao `PT_GNU_EH_FRAME` dos cabeçalhos.

O verificador de imports (`tools/missing_imports.py`) agora mostra **0 funções não implementadas** das 588 que o jogo pede ao sistema.

## Teste com uma exceção de verdade

Não bastava as funções existirem: era preciso ver o desenrolador do jogo funcionar com elas. Para isso:

1. **Mapear os `try`/`catch` do jogo.** Escrevi um leitor das tabelas de exceção (LSDA) do eboot, que deu os 7.399 trechos. Cruzei com as chamadas a funções do sistema.
2. **Achar um ponto que roda de verdade.** A chamada a `scePthreadSetaffinity` em `eboot+0x2af7e8` fica dentro de um `try` com `catch(...)`, e o jogo passa por ela na abertura.
3. **`KR_UNWIND_TEST=0x2af7ed`** (o endereço de retorno dessa chamada): na primeira passagem, em vez de executar, o runtime lança um **`std::bad_alloc` exatamente como o `operator new` do jogo**. Ele aloca a exceção com o `__cxa_allocate_exception` do `libc.prx`, põe a vtable do `bad_alloc` e salta para o `__cxa_throw`, deixando na pilha só quadros do jogo.

**Resultado:**
```
Runtime: KR_UNWIND_TEST: throwing std::bad_alloc at the call returning to eboot+0x2af7ed
Runtime: unwind tables of libc.prx: .eh_frame_hdr at image+0xd102f8 (2084 functions), .eh_frame 95508 bytes
Runtime: unwind query libc.prx+0x54917 / +0x5d2d3 / +0x5d1b5      (o __cxa_throw e o desenrolador)
Runtime: unwind tables of eboot.bin: .eh_frame_hdr at image+0x9dcd70 (28000 functions), .eh_frame 1672500 bytes
Runtime: unwind query eboot.bin+0x2af7ec                           (fase 1: procura o catch)
Runtime: unwind query eboot.bin+0x2af7ec                           (fase 2: desenrola até ele)
```
- **O `catch(...)` do jogo pegou a exceção**, sem `std::terminate`.
- **O jogo seguiu normalmente** até a missão, a 60 FPS.

## Arquivos alterados

- `port/src/runtime_krunwind.c` (novo): as 5 funções e o `KR_UNWIND_TEST`.
- `port/src/runtime.c`, `runtime.h`, `probe.c`: registro das regiões e resolução.
- `README.md`: exceções C++ na tabela de estado; `KR_UNWIND_TEST` e as outras ferramentas de diagnóstico.

**Ainda não foram enviadas ao GitHub.**
