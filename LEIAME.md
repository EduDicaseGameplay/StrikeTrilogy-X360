# StrikeTrilogy-X360

*[English](README.md) | Português*

Desert Strike: Return to the Gulf (1992), Jungle Strike (1993) e Urban Strike (1994), as versões de Mega Drive, rodando **nativos** no Xbox 360 desbloqueado: um `.xex` por jogo, 60 fps, 1280x720 com a visão mais larga em 16:9, minimapa ao vivo e save state.

Não é um emulador rodando o jogo instrução por instrução. Ele parte do [StrikeHDCollection](https://github.com/Icedomega13/StrikeHDCollection), do Icedomega13, que traduz o código 68000 de cada jogo para C (conferido instrução por instrução contra o núcleo 68000 Musashi), e eu portei para o Xbox 360: vídeo na GPU do console (D3D9), XInput, XAudio2, arquivos, threads para imagem e som. A tradução é feita no seu PC a partir das **suas** ROMs e compilada dentro do `.xex`. Os chips de som (Z80, YM2612, PSG) e o de vídeo continuam emulados em volta.

> **Aqui não tem jogo nenhum.** Nem ROM, nem o código traduzido, nem `.xex` pronto, nem o SDK da Microsoft. Você precisa das suas ROMs e das ferramentas abaixo para compilar. Não me peça ROM.

---

## O que funciona

- Os três jogos completos, cada um aberto direto pelo seu `.xex`
- 60 fps, um quadro do jogo por atualização da TV (testado no console)
- 1280x720, 16:9 com o campo de batalha ampliado (menus, mapas e cenas ficam em 4:3), ou o 4:3 original
- Minimapa ao vivo do mapa de pausa, com os objetivos; LB / RB trocam o objetivo
- Save state (10 slots), velocidade Suave ou Original (com a lentidão do console), imagem nítida ou suave, configurações salvas por jogo
- Imagem desenhada em 4 threads de hardware, som FM num núcleo próprio

No PC, o código do port dá o mesmo estado do jogo que a referência Musashi, quadro a quadro, e o mesmo som, amostra por amostra.

## O que você precisa

### No console
- Xbox 360 desbloqueado (RGH/JTAG) com Aurora, FreeStyle Dash ou XeXMenu

### No PC (Windows 10/11)
1. **WSL** com Ubuntu. A compilação do Xbox roda nele.
2. **[OXDK](https://github.com/MrMilenko/OXDK)** no WSL (`~/OXDK`), com o LLVM dele (`~/oxdk-llvm`) compilado com os patches do Xenon que vêm em `OXDK/xbox360/llvm`, mais o `Tools/patches/llvm-lld-thunk-r0.patch` (thunk de desvio longo do lld que não usa o r12).
3. **Arquivos do SDK do Xbox 360 (XDK)** onde o OXDK procura (`~/xdk360/XDK`).
4. **Python 3** com `capstone` e `pillow` (`pip install capstone pillow`), para o tradutor da base.
5. **Git**.
6. Opcional: **Visual Studio 2022 Build Tools** com C++ e o clang, só para as conferências no PC.

### As ROMs
As versões USA/Europa de Mega Drive, conferidas pelo SHA-1:

| Jogo | Nome do arquivo | SHA-1 |
|---|---|---|
| Desert Strike: Return to the Gulf | `desert_strike.bin` | `d7e7d8c358eb845b84fb08f904cc0b95d0a4053d` |
| Jungle Strike | `jungle_strike.bin` | `7a7568e39341b1bb218280ee05c2b37c273317b5` |
| Urban Strike | `urban_strike.bin` | `897a8f6a08d6d7d6e5316f8047532b3e4603e705` |

O código traduzido funciona por endereço, então outras versões (ou os jogos de SNES) não servem. O `.xex` confere o SHA-1 e avisa se estiver errado.

## Compilando

**1. Baixe este projeto e a base:**

```bash
git clone https://github.com/EduDicaseGameplay/StrikeTrilogy-X360.git
cd StrikeTrilogy-X360
git clone https://github.com/Icedomega13/StrikeHDCollection.git Reference/StrikeHDCollection
git -C Reference/StrikeHDCollection checkout 9c0be81
```

O port foi feito em cima desse commit da base (v1.0.0).

**2. Traduza os jogos.** Coloque suas ROMs em `Reference\StrikeHDCollection\rom\` com os nomes acima e rode:

```
Tools\gen_all.bat
```

**3. Compile os três `.xex`** no WSL:

```bash
bash Xbox360/Build/build.sh
```

O `make` não aceita espaço no caminho, então o script chega ao projeto por um link, `~/strike-trilogy`. Dá para compilar um jogo só: `bash Xbox360/Build/build.sh jungle`.

O resultado sai em `Builds/DesertStrike/DesertStrike.xex`, `Builds/JungleStrike/JungleStrike.xex` e `Builds/UrbanStrike/UrbanStrike.xex`, e cada um também é copiado como `Builds/<Jogo>/Deploy/<Jogo>/default.xex`.

**Conferências no PC (opcional,** Visual Studio 2022 Build Tools**):** `Tools\pcbuild.bat` compila os programas de teste lockstep da base, e `Tools\pc_check\build_pc_check.bat` compila uma ferramenta que roda um jogo com o código de som e imagem deste port e grava o log de estado, o áudio e capturas para comparar com a base.

## No console

Crie uma pasta para cada jogo, por exemplo:

```
Hdd1:\Games\DesertStrike\
    default.xex
    rom\desert_strike.bin
```

e o mesmo para `JungleStrike` (`rom\jungle_strike.bin`) e `UrbanStrike` (`rom\urban_strike.bin`). A ROM também pode ficar ao lado do `default.xex` ou em `roms\`. Adicione a pasta aos caminhos de busca do Aurora, ou abra o `default.xex` pelo XeXMenu.

### Controles

| Xbox 360 | Mega Drive |
|---|---|
| Direcional / analógico esquerdo | Direcional |
| X ou LT | A (Hellfire; segurado: strafe) |
| A | B (Hydras) |
| B ou RT | C (metralhadora) |
| Start | Start (pausa / mapa) |
| Y (segurado) ou analógico direito ←/→ | strafe sem disparar; X durante o strafe dispara um Hellfire |
| LB / RB | objetivo anterior / seguinte no minimapa |

Com **Back** segurado: LB salva o estado, RB carrega, direcional ←/→ troca o slot, Y velocidade, X imagem nítida/suave, A proporção, B limite de sprites (piscar do console liga/desliga), direcional ↑ widescreen, direcional ↓ minimapa, LT + RT por 2 segundos reinicia. **Back + Start por 3 segundos** sai do jogo.

Ao lado do `.xex` o jogo grava `<jogo>.cfg` (configurações), `states\`, `minimap\` (o mapa de cada fase, capturado uma vez) e um log, `desertstrike_log.txt` (e assim por diante), que mostra onde parou se algo der errado.

## Créditos

- [StrikeHDCollection](https://github.com/Icedomega13/StrikeHDCollection), do Icedomega13 e colaboradores (MIT): o tradutor, o modelo do hardware e os módulos dos jogos em que este port se baseia
- [OXDK](https://github.com/MrMilenko/OXDK), do MrMilenko (domínio público): as ferramentas de compilação
- Musashi, o z80 do Nicolas Allemand (superzazu) e o ymfm do Aaron Giles, que vêm com a base
- A camada do Xbox 360 vem dos meus ports anteriores de Mega Drive (SoR1/SoR2/SoR3-X360)

Veja `THIRD_PARTY.txt` e `LICENSE`. Desert Strike, Jungle Strike e Urban Strike são marcas da Electronic Arts. Projeto de fã, sem vínculo com a EA ou a Sega.
