# Demonstração ao vivo

**Duração sugerida de 4 a 5 minutos.** Uma pessoa representa a visita e fica perto do painel. Outra apresenta e atende pelo celular longe da maquete. A terceira pode alternar a projeção entre LCD, mecanismo e app.

## Preparação

1. Ligue as fontes, as duas placas e o servidor. Confirme pelo monitor serial o Wi-Fi do servo e o link UART2 nas duas direções. Aguarde a hora no LCD e deixe o servo na posição fechada.
2. Abra o app no celular que será projetado. Conecte previamente para poupar tempo e evitar expor a chave de acesso. Confirme **CONECTADO**, servo e painel online e estado **Fechado**.
3. Confirme em ensaio que o botão toca a melodia, o servo abre e fecha, e o histórico é atualizado. Para mostrar push, ative as notificações e faça um teste com o app em segundo plano antes da apresentação.

## Roteiro

| Tempo | Ação | O que mostrar e dizer |
| --- | --- | --- |
| 0:00–0:40 | A visita pressiona a campainha. | “Estou longe do portão quando a visita chega.” Mostre a melodia, **Campainha tocando** e a atualização do último toque no LCD. |
| 0:40–1:25 | Mostre o aviso no celular. | No app conectado, mostre **Campainha tocou** e a entrada adicionada ao histórico. Se o push já foi validado, mostre a notificação fora da tela do app e reabra o app. |
| 1:25–2:20 | Escolha **Abrir portão** e acompanhe a maquete. | Mostre a passagem de **Abrindo** para **Aberto**. Aponte o LED, o LCD e o bip de abertura. Explique que o estado vem do ângulo comandado. |
| 2:20–3:05 | Selecione **Fechar portão** quando a abertura terminar. | Mostre **Fechando** e **Fechado**. O LED apaga ao chegar à posição comandada de fechamento. |
| 3:05–4:15 | Abra a seção **Histórico**. | Mostre toque, pedidos do app e registros de abertura e fechamento com horário. Use **Atualizar** ou, quando disponível, **Carregar anteriores**. |

## Se o ensaio apontar uma falha

- **Push indisponível.** Deixe o app aberto e demonstre o evento recebido via WebSocket. Ao mostrar a prévia de push dos slides, diga que ela representa o fluxo configurado.
- **Servidor indisponível.** Use `O` e `X` no Serial do servo para demonstrar o mecanismo e o estado no painel com as placas ligadas por UART2. Identifique essa operação como teste local da maquete.
- **UART indisponível.** Mostre **SEM SINAL** no LCD e o som da campainha física. Use as prévias da apresentação para explicar a parte remota.
- **Áudio baixo.** Confira o botão e o áudio com o [teste de buzzer](../TESTE_BUZZER.md). Mantenha o LCD e o celular projetados para acompanhar o toque.

O [PPTX com notas de fala](../apresentacao/Portao-Inteligente-KJW-UART.pptx) traz uma renderização do LCD a partir do firmware e prévias do app com horários fictícios.
