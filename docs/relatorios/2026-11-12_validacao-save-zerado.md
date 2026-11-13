# Validação com o save zerado

**Data:** 2026-11-12

## Resumo

Você forneceu um save com o jogo zerado (`~/Downloads/CUSA02714(...)/SAVEDATA00`). Ele foi instalado e **funcionou na hora**. A automação validou tudo até o começo da **missão final**. O teste da fase final em diante fica com você, como combinado.

## O save

- Os dois `SAVEDATA.DAT`, o fornecido e o do port, têm o **mesmo formato**: 2.242.664 bytes, cabeçalho `N13c` com checksum, **descriptografados**. Não precisou de conversão.
- Instalei só o `SAVEDATA.DAT`, em `user/savedata/1/CUSA02714/SAVEDATA00/`. O original em Downloads ficou intacto.
- **O save anterior do port está guardado** em `user/savedata_backup_2026-11-12_antes-save-zerado/`. Para voltar, basta copiar de volta. Nada disso vai para o git.

## O que foi validado (automação)

| Etapa | Resultado |
|---|---|
| Carregar o save | ✅ montado e lido sem erros |
| Avisos de desbloqueio na entrada | ✅ dificuldades **VERY HARD** e **HELL**; na loja, **Ending**, tema da Gorgom, tema de chefe |
| Menu principal | ✅ **todos os modos liberados**: Story, Time Survival, **Free Mission**, Custom Soundtrack, Contents, Options |
| Story Mode → mapa | ✅ próxima missão **終止符** (a final, Shadow Moon), ranks das missões anteriores |
| Seleção de rider | ✅ elenco grande; **Agito nível 99**, 626.400 de Rider Soul |
| Seleção TAG | ✅ **Decade nível 77** como parceiro |
| Tela de preparação (skills, Custom Soundtrack, loja) | ✅ |
| Cena no motor do jogo | ✅ BLACK contra Shadow Moon e o **創世王** (Rei da Criação), com legendas |
| Início do combate na arena final | ✅ HUD do Agito e diálogos durante a luta |
| Pausa → lista de golpes (必殺技確認) | ✅ formas do Agito (Ground Form…) e golpes, como Rider Kick e Form Change |
| Estabilidade | ✅ 0 erros (decodificação de áudio, *host fault*, imports); **1 quadro lento em 23.353** |
| Memória | **221 → 316 MB** em ~7,5 min. Cresce enquanto carrega a missão e depois estabiliza; nesta máquina isso é irrelevante |

![Menu e avisos de desbloqueio](img/2026-11-12_save-zerado_menu.png)
![Missão final: seleção, preparação, cena e combate](img/2026-11-12_save-zerado_missao-final.png)

## Fica com você

1. **A fase final inteira:** o chefe 創世王, a troca de formas no combate, o TAG com o Decade e a cena e os créditos do final.
2. **Time Survival e Free Mission:** agora liberados.
3. **Contents:** Figure Viewer, galeria de vídeos e de som e loja, todos com o conteúdo completo.
4. **A forma escura no canto da tela**, se aparecer de novo (relatório de validação anterior).

### Como jogar
```sh
cd ~/kr-port/port
./out/bb-probe ../out/boot-linked.bin --app0 ../game --user ../user --timeout 0
```
Opcional: `KR_RES=1440p` (supersampling, 60 FPS nesta placa).

Controles no teclado: ○ = E/Enter, ✕ = Espaço, □ = botão esquerdo, △ = botão direito, super = R, pausa = Esc (tabela completa no README).

**O que me trazer se algo der errado:** o momento (fase e o que estava acontecendo) e, se o jogo fechar, as últimas linhas do terminal. Com isso eu reproduzo e corrijo.
