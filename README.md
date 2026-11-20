# V-Rally 3 de GBA no ROG Ally

Adaptação da edição americana para Bazzite e Linux x86-64 com GBA-Recomp v0.4. Use uma cópia própria do cartucho. A parte recompilada roda no processador do Ally e o runtime interpreta os blocos ARM que ainda não entraram na tradução.

A corrida usa um campo de visão 16:9 e um renderizador de 1920×1080. Os gráficos de bitmap, o painel e os menus conservam a definição da arte do cartucho. Os menus redistribuem o espaço entre os elementos e prolongam o fundo nas bordas. No Ally, RT acelera e LT freia. O jogo usa entradas digitais. O arquivo de progresso fica ao lado do cartucho, em `rom/vrally3-us.sav`.

## Compilação

O kit contém o código do vídeo, o remapeamento, as ferramentas de especialização e o patch do runtime. A montagem HD requer a tradução C primária, a alternativa e os dados de cobertura produzidos localmente durante sessões de jogo.

1. Use o GBA-Recomp no commit `de4edf59b872d887046d6a3b005e2df551b6d44c`, aplique `project/v3d/runtime-1080p.patch` na raiz desse projeto e compile com `cargo build --release --locked -p recomp`. Copie o executável para `tools/recomp-v3d`.
2. Coloque sua ROM em `rom/vrally3-us.gba`. A revisão compatível tem SHA-256 `5818fa74fdcacf39b53a1acea99432c4362979dd656c8153fd9861094aca51f7`. Crie as pastas `logs` e `native/v3d`.
3. Use `recomp play rom/vrally3-us.gba --no-bios --record-labels` para registrar a cobertura. Execute também uma corrida. Guarde separadamente as traduções C geradas por `recomp build` e as respectivas imagens IWRAM para as duas variantes de código. A documentação do recompilador explica a coleta de rótulos. [GBA-Recomp](https://github.com/JRickey/gba-recomp/blob/de4edf59b872d887046d6a3b005e2df551b6d44c/docs/labels.md)
4. Execute `ampliar-compilacao.py` para cada tradução. Os parâmetros são a pasta do C original, o destino, a ROM e a imagem IWRAM de 32 KiB correspondente. Compile as unidades especializadas da tradução alternativa em `additional.so`, com o compilador C, `-O0 -shared -fPIC` e `project/v3d` no caminho de includes.
5. Gere `overlays.h` com `mapear-overlays.py`, passando as duas pastas de tradução e o destino do cabeçalho. Compile `juntar-traducao.c` com esse cabeçalho, `-O2 -shared -fPIC -DVRALLY3_USE_OVERLAYS -ldl`, para produzir `vrally3-us.so`. Mantenha essa biblioteca junto de `additional.so` em uma pasta de trabalho.
6. Execute `python3 project/v3d/montar-video.py <pasta-do-C-primário-especializado> <pasta-das-duas-bibliotecas> native/v3d`. A montagem confere se a tradução contém os blocos de projeção esperados. O resultado depende da cobertura registrada.
7. Inicie pelo script `jogar.sh` em uma sessão gráfica do Bazzite. O script usa o renderizador e a tradução HD quando a revisão da ROM e as bibliotecas correspondem.

O painel do Ally foi testado em 1080p. A apresentação acompanha a saída de 60 Hz. A lógica e a animação mantêm a cadência do GBA. A saída para monitor externo está configurada em 1080p e aguarda um teste físico.

[Resultados da revisão de vídeo](VALIDACAO.md)
