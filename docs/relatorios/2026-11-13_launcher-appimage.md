# Launcher em AppImage

**Data:** 2026-11-13

## Resultado

**`out/appimage/BattrideWarGenesis-x86_64.AppImage`** (~97 MiB), gerada por `tools/appimage/build_appimage.py` (MIT), adaptado do empacotador do battride-war-recomp.

| O que vai dentro | Detalhes |
|---|---|
| Launcher | `launcher.py` + `core.py`, os mesmos da cópia do repositório |
| O port compilado | `bb-probe` e `libbbgpu.so`, **sem símbolos de depuração**: a `libbbgpu.so` cai de 207 MB para 24 MB |
| Scripts de preparação | `port/scripts/*.py` (só biblioteca padrão do Python) |
| Python 3.14 + PyGObject + GTK 4 + libadwaita | biblioteca padrão (sem Tk e testes), `gi`, `cairo`, typelibs, esquemas GSettings |
| 152 bibliotecas | tudo o que o `ldd` encontra, **menos** o que precisa ser do sistema: libc, libstdc++, Vulkan, GL/EGL, DRM, X11/Wayland, VA-API/VDPAU, OpenCL e systemd. A lista segue a do recomp |
| Licenças | `LICENSE` (MIT), GPL do `port/`, `NOTICE` do bbport, `CREDITS.md`; `app/library-sources.json` diz de onde veio cada biblioteca |
| Ícone | um ícone **próprio** (o do jogo é da Bandai Namco e não vai no pacote) |

**Nenhum código ou dado do jogo** vai no pacote: o jogador escolhe o próprio dump, e a preparação roda no computador dele.

### Como funciona

- O `AppRun` define `PORT_APPDIR`, e o launcher passa a usar o `bb-probe`, os scripts e os créditos de dentro da AppImage.
- **Sem `LD_LIBRARY_PATH`:** os executáveis e bibliotecas têm RPATH relativo (`$ORIGIN/...`), herdado pelas dependências, então nada vaza para os programas do sistema.
- O "abrir pasta" chama o `xdg-open` do sistema **sem** as variáveis do Python e do GTK do pacote.
- A pasta de dados padrão é `~/.local/share/battride-war-sousei-port`; pode ser trocada na página Game.
- O jogo roda com a pasta de dados como diretório atual, então o cache de shaders também fica nela.

### Ferramentas (versões fixas, em `~/tools/appimage`, fora do repositório)

| Ferramenta | Versão | SHA-256 |
|---|---|---|
| appimagetool | 1.9.0 | `46fdd785…83d1` |
| runtime tipo 2 | continuous | `617aa773…8a46` |
| patchelf (estático) | 0.18.0 | `b699cb82…04e5` |

## Testes (rodando a AppImage, não a pasta)

- **`--check`**, com FUSE: confere o dump (CUSA02716 v01.00 + v01.02) e a build embutida.
- **`--prepare`**, com `APPIMAGE_EXTRACT_AND_RUN=1` (sem FUSE): prepara numa pasta temporária, e o `boot-linked.bin` sai **idêntico** ao do repositório.
- **Interface** (`--screenshot`): abre com o GTK embutido; o botão mostra "Play" (já preparado).
- **Jogo:** o `bb-probe` embutido abriu a janela Vulkan e entrou no código do jogo. `LD_DEBUG` confirma que SDL3, FFmpeg e o resto vêm de `AppDir/usr/lib`.

## Limite importante: compatibilidade

A AppImage foi gerada **nesta máquina (CachyOS, glibc 2.44)** e só roda em sistemas com **glibc 2.44 ou mais nova**: Arch, CachyOS e distros muito recentes. Para rodar em qualquer distro, como a do recomp (glibc 2.35, Ubuntu 22.04), é preciso **compilar o port e gerar a AppImage dentro de um Ubuntu 22.04**. Isso exige:
1. instalar `podman` ou `docker` (precisa de sudo, que só você pode rodar);
2. compilar lá dentro o SDL3 e um GCC recente (o Ubuntu 22.04 não tem SDL3), e o resto das dependências do port.

O empacotador já está pronto para isso: ele mostra o piso de glibc com que foi gerado (`app/package.json`).

## Arquivos

- `tools/appimage/build_appimage.py` (novo).
- `tools/launcher/core.py`, `launcher.py`: os caminhos de dentro da AppImage e o ambiente do sistema para o "abrir pasta".
- `README.md`: como gerar.

**Ainda não foi enviado ao GitHub** (junto com o launcher).
