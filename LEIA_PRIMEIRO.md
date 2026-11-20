# Primeira bancada

1. Identifique o ESP32 A do servo e o ESP32 B do painel. Monte a alimentação e os periféricos conforme o [guia de montagem](docs/montagem.md).
2. Suba o servidor com Node.js 24 e gere três chaves com `npm run keys`. Prepare os arquivos locais a partir dos exemplos conforme o [guia de instalação](docs/instalacao.md).
3. Grave o servo e o painel. Ligue a UART2 com TX de cada placa no RX da outra e GND comum. No Serial do painel, espere por `SERVO UART: conectado.`
4. Confira os comandos seriais `O`/`X` no servo e `C`/`T`/`S` no painel. O monitor serial fica em 115200 baud.
5. Conecte o app ao servidor, confirme os dois ESP32 online e ensaie botão, aviso ao vivo, abertura, fechamento e histórico. Use o [roteiro de demonstração](docs/demonstracao.md).
