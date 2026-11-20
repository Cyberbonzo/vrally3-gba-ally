# Revisão de pistas e vídeo no ROG Ally

## Provas testadas

O teste abriu cada seleção pelo menu do jogo, com um perfil de teste que dava acesso a todo o catálogo. Cada uma das 57 sessões chegou à largada, aceitou aceleração e direção, entrou em pausa e retornou à corrida. A inspeção comparou a imagem da largada, da curva, da pausa e da retomada. Os mapas de seleção das 57 sessões tiveram assinaturas diferentes.

| Seleção | Provas | Verificação adicional |
| --- | ---: | --- |
| França, Suécia, Portugal, Quênia, Alemanha, Finlândia e Grã-Bretanha | 42 | Três especiais por país em cada sentido, com troca para a câmera interna e retorno à externa |
| Circuitos de Cross em contra o relógio | 10 | Dois sentidos para cada um dos cinco circuitos |
| Licenças de V-Rally Cross com adversários | 5 | Departamental, regional, nacional, europeia e internacional. A revisão acompanhou 120 quadros de apresentação consecutivos por largada |

Os dez carros selecionáveis foram testados em corrida, na câmera externa e na interna. A matriz cobre as provas individualmente. A chegada também foi verificada em duas especiais de uma sessão gravada de rali. Em ambas, o jogo exibiu a classificação e avançou para a próxima especial.

## Correções de cenário

Na Suécia, as copas das árvores eram cortadas na antiga borda do GBA. O ajuste projeta os dois lados do objeto antes de calcular o recorte de tela e de textura. A comparação usou 55 quadros da mesma passagem, com a mesma câmera e os mesmos comandos. A pista de Cross europeia também revelou uma interrupção do horizonte no espaço novo das laterais. O compositor passou a repetir os blocos do panorama com a largura e a ordem de desenho originais.

## Progresso e apresentação

Os testes rodaram em processos separados da partida principal e usaram cópias de save. Duas chegadas produziram telas de resultado. Uma nova inicialização de teste leu os mesmos 8.192 bytes de EEPROM registrados após a segunda etapa. A classificação visível e a persistência da EEPROM foram critérios separados.

O terreno e os objetos 3D usam 1920×1080. A arte de bitmap mantém a resolução de origem. O atalho solicita 1080p para tela interna e externa. O dispositivo conectado durante a revisão foi o painel do Ally. Os testes distinguiram quadros de apresentação das atualizações da lógica original de GBA.

A cobertura não inclui uma chegada em todas as 52 configurações de pista e sentido, cada combinação de carro e clima, nem multiplayer por cabo Link.

SHA-256 do módulo de vídeo usado na revisão das árvores e do horizonte: `43ad6ed981c8bc4177b5c5533041caa428850fc1cd5ffb03408441fa421b112d`.

## Filtro de pista e cadência de apresentação

Na mesma curva da Suécia, comparei 90 amostras com os mesmos comandos e números de quadro do jogo. Em um recorte da pista de 470×73 pixels, a diferença média de luminância entre amostras consecutivas passou de 19,62 para 18,82 na escala de 0 a 255. A medição inclui o movimento da câmera. A comparação visual original está na pasta privada de logs.

Em 60 medições do mesmo passe, a mediana do tempo de rasterização foi de 3,66 ms com o filtro desligado e 4,28 ms com ele ligado. A revisão do compositor reduziu sua mediana de 11,75 ms para 6,56 ms no replay da Suécia. A verificação comparou os 90 quadros reduzidos dessa passagem e 38 quadros completos de 1080p em seis provas, cobrindo corrida, pausa e retomada. A saída do compositor otimizado coincidiu com a do compositor anterior usando o mesmo filtro.

Na corrida aberta pela Steam, a janela ocupou 1920×1080 no painel do Ally. O runtime registrou 2.400 apresentações em 40,17 s, com média de 59,74 FPS. Nos intervalos de 120 apresentações, a taxa variou de 59,37 a 60,09 FPS. O motor do GBA atualizou a cena 800 vezes nesse período.

O módulo de vídeo instalado tem SHA-256 `38cf5317454ff0927e2e841191a7f66b4679b761f25109a6031e212a5572ed4d`.
