# GitHub: commit das correções da validação

**Data:** 2026-11-12

## O que foi enviado

- **Commit:** `1ffb2ea`, "Fix ATRAC9 crash and BGM stream looping in Ngs2 mixer"
- **Repositório:** <https://github.com/robertoqimin/battride-war-sousei-port> (privado, branch `main`, sincronizada com `origin/main`)

| Arquivo | Mudança |
|---|---|
| `port/src/runtime_ngs2.c` | reset do decodificador, log de erros, stream em iterações, limpeza no stop/kill |
| `port/third_party/LibAtrac9/C/src/` (6 arquivos) | `Atrac9ResetDecoder` e checagem de índice no `BexGroupInfo` |
| `tools/bench/run_route.sh` | novo: executor de rotas com capturas |
| `docs/relatorios/2026-11-12_validacao-funcional.md` | relatório da validação |

## Conferência

- Não entrou nenhum dado do jogo. As capturas citadas no relatório (`img/2026-11-12_validacao_*.png`) continuam fora do git pelo `.gitignore`.
- O que fica pendente é o mesmo do relatório de validação: você jogar a 1ª missão até o fim, e depois o trabalho de desempenho.
