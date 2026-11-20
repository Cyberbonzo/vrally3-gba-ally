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

SHA-256 do módulo de vídeo testado e instalado: `43ad6ed981c8bc4177b5c5533041caa428850fc1cd5ffb03408441fa421b112d`.
