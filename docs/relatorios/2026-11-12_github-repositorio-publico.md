# GitHub: repositório público

**Data:** 2026-11-12

## O que foi feito

- <https://github.com/robertoqimin/battride-war-sousei-port> agora é **PUBLIC**.

## Conferência antes de abrir

O histórico inteiro foi verificado, não só o último commit:
- **Dados do jogo:** nenhum `.elf/.bin/.self/.prx/.at9/.fpk/.sfo/.mp4/.raw`, nem dump, sysconf ou disassembly (`text.asm`, `plt_v102.json`) em nenhum commit.
- **Imagens:** só uma, o gráfico de tempo de quadro (`docs/relatorios/img/2026-11-12_fase2-rwlock_tempo-de-quadro.png`), que é nosso.
- **Binários:** todos os arquivos binários e JSON são de bibliotecas de terceiros (fontes, SPIR-V do FSR, gramáticas SPIR-V, perfil Vulkan).
- **Código decompilado:** nenhum código decompilado do jogo nos docs.
- **Segredos:** nenhuma chave ou token.

## Pontos de atenção

- **E-mail nos commits:** o e-mail dos commits (`qimingroberto@gmail.com`) agora fica visível no histórico. Para esconder nos próximos, dá para usar o e-mail `noreply` do GitHub; os commits já feitos só mudariam reescrevendo o histórico.
- **Licença no GitHub:** a página mostra a licença como "Other". O arquivo `LICENSE` é MIT, mas tem uma seção de escopo (`port/` é GPL), e o GitHub não reconhece texto misto. Isso não muda nada legalmente.
- **Commits pendentes:** os relatórios do cache de shaders e este ainda **não têm commit**.
