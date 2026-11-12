# Fase 1 — Primeira execução com GPU (Vulkan)

**Data:** 2026-11-12
**Projeto:** port nativo de *Kamen Rider: Battride War Genesis* (CUSA02716, patch v01.02) para PC Linux
**Passo:** sair do modo `--cpu-only` e rodar o jogo com a camada gráfica (Vulkan, RX 6750 XT)

---

## Resumo

O jogo **roda com gráficos**. Numa execução de ~65 s com apertos de botão automáticos, ele passou por:

1. o diálogo de criação de save (o save `SAVEDATA00` foi criado e depois reaberto);
2. o aviso legal (ご注意);
3. o logo da Eighting;
4. o **vídeo de abertura** (AvPlayer decodifica e exibe);
5. a tela de título (*Memorial TV Sound Edition*);
6. as telas de loading com dicas (Kamen Rider Ghost);
7. o texto do prólogo;
8. o **primeiro tutorial dentro do cenário 3D** (cidade renderizada, HUD e painel de comandos).

Nenhuma parada por função não implementada e nenhum crash. As execuções só terminam pelo watchdog de tempo, que fui eu que defini.

![Sequência de telas capturadas](img/2026-11-12_fase1-gpu_sequencia.png)

![Tutorial dentro do cenário 3D](img/2026-11-12_fase1-gpu_tutorial-3d.png)

---

## O que foi feito neste passo

### 1. Ganchos do Bloodborne desativados na camada gráfica
**Sintoma (run10):** crash (`SIGSEGV`) em `BbGameMenu::PatchImage`, logo depois de `VideoOut` abrir.
**Causa:** `bbgpu_patch_image` aplica três patches no código do jogo em **offsets fixos do eboot do Bloodborne**: os wrappers Gnm, o menu "Sistema" do bbport e o limitador de quadros. No nosso eboot esses endereços são outra coisa.
**Correção:** `port/gpu/shim/bbgpu.cpp`. `bbgpu_patch_image` não aplica mais nenhum dos três patches (comentário `kr-port:`).

### 2. AppContent sem `--content-profile`
**Sintoma (run11):** `STOP: AppContent needs --content-profile`.
**Causa:** no bbport, o perfil de conteúdo vem de um arquivo `BBCONT01` com os parâmetros do Bloodborne.
**Correção:** `port/src/runtime_content.c`. Sem o perfil, o runtime usa o do jogo completo, como uma cópia retail instalada informa: SKU 3 e parâmetros 1–4 em 0.

### 3. Lista de DLC com *service label* ≠ 0
**Sintoma (run12):** `STOP: nonzero AppContent service label unsupported`. O jogo consulta add-ons com service label 1.
**Correção:** o mesmo arquivo. A lista é aceita e aparece no log uma vez. Os add-ons continuam sendo as pastas em `<user>/addcont`, e hoje não há nenhuma.

### 4. Entrada automática e captura de quadros (ferramentas do bbport, sem alteração)
- `BB_PAD_FILE=<arquivo>`: o arquivo lista os botões apertados (`circle`, `cross`…), e o runtime relê sempre que ele muda.
- `BB_PRESENT_DUMP_TRIGGER=<arquivo>` + `BB_DUMP_DIR`: criar o arquivo salva o quadro exibido como `.raw` BGRA 1920×1080.
- Descoberta: **o botão de confirmar é o círculo** (jogo japonês). O X não confirma.

---

## Progresso por execução

| Run | Modo | Resultado |
|---|---|---|
| run9 | CPU | para em `sceVideoOutOpen`, o fim do que dá para fazer sem GPU |
| run10 | GPU | janela + Vulkan ok; crash no patch do menu do Bloodborne |
| run11 | GPU | passa do vídeo; `STOP` no AppContent (perfil) |
| run12 | GPU | 8 lotes de comandos de GPU, shaders compilando; `STOP` no service label |
| run13 | GPU | **roda continuamente**; tela "save não encontrado" esperando botão |
| run14–15 | GPU + capturas | o círculo confirma; save criado em `user/savedata/1/CUSA02714/SAVEDATA00` |
| run16 | GPU + círculo a cada ~4 s | aviso → logo → vídeo → título → loading → prólogo → **tutorial 3D** |

Comando usado (run16):
```sh
BB_PAD_FILE=$P BB_DUMP_DIR=$D BB_PRESENT_DUMP_TRIGGER=$T \
  ./out/bb-probe ~/kr-port/out/boot-linked.bin \
  --app0 ~/kr-port/game --user ~/kr-port/user --timeout 68
```

---

## Estado atual

| Área | Estado |
|---|---|
| CPU / sistema (threads, event flags, arquivos, zlib, save) | ✅ funcionando |
| Vídeo (VideoOut, Vulkan, shaders GCN→SPIR-V) | ✅ menus, vídeo e cenário 3D aparecem |
| Vídeos (AvPlayer) | ✅ a abertura toca |
| Entrada (pad/teclado via SDL) | ✅ funciona (testado por arquivo de script) |
| Save | ✅ cria e reabre |
| **Áudio** | ⚠️ **mudo**: Ngs2 e Audiodec são stubs silenciosos |
| Online / NP / Json2 | ⛔ desativado (modo offline) |
| Desempenho | sem limite de quadros (`frame limit 0`): o jogo roda bem acima de 60 FPS nos menus. Não foi medido em combate. |

---

## Riscos e observações

- **Patches do Bloodborne na camada GPU:** os três principais foram desligados. Outros ganchos (`bbport_engine_fixes`, `BbGuestHooks`) conferem os bytes antes de aplicar, então falham sem estragar nada. Ainda vale revisar um por um.
- **Ritmo do jogo:** sem o limitador do Bloodborne, falta confirmar se a lógica do jogo depende de 30/60 FPS. Se as animações ficarem rápidas demais, é preciso ligar o vsync ou um limite.
- **FSR/upscaler:** está ativo por padrão, procurando um "scene color" pela heurística do Bloodborne. Nos logs aparece "frame without upscaler" em todos os quadros, o que não causa problema, mas a configuração precisa ser revista.
- O jogo pede o teclado de hardware (IME) e USB; as duas coisas respondem "indisponível" sem efeito visível.

---

## Próximos passos sugeridos

1. **Testar jogando de verdade** na janela (teclado/controle), além dos apertos automáticos, para ver combate, câmera e desempenho.
2. **Áudio:** implementar o mixer Ngs2 de verdade e o decodificador ATRAC9 (o LibAtrac9 já está linkado) para tirar o jogo do mudo.
3. Revisar os ganchos restantes do Bloodborne na camada GPU e o upscaler.
4. Limitador de quadros / vsync, se a lógica depender de taxa fixa.

---

## Arquivos alterados neste passo

- `port/gpu/shim/bbgpu.cpp`: patches de imagem do Bloodborne desligados
- `port/src/runtime_content.c`: perfil padrão do jogo completo; service label aceito
- `docs/relatorios/img/*.png`: capturas desta execução (só locais; não distribuir, são imagens do jogo)
- Logs: `out/run10.log` … `out/run16.log`
