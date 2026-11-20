# Validação da versão 2

## Resultados desta revisão

| Verificação | Resultado | Cobertura |
| --- | --- | --- |
| Testes C++ | **2 suítes aprovadas** | Rampa, debounce, temporizadores, melodia de 24 notas, LEDs, bip, perda de link e prazo dos comandos. A suíte de automação foi atualizada para a melodia de 4,346 s já presente no firmware do painel. |
| Leitor UART2 | **Verificação isolada aprovada** | Os dois cabeçalhos passaram pela checagem da pinagem, do prazo de 3 s e do recebimento de quadros fragmentados. O leitor descartou quadros inválidos e retomou a leitura no seguinte. |
| Renderização do LCD | **Simulação aprovada** | O código do painel foi executado em C++ com uma tela simulada. Verifiquei as transições de estado, a indicação sem sinal e o relógio. A imagem com o último toque usa data e horários fictícios. |
| Testes do transporte do app | **3 testes aprovados** | Normalização WS/WSS, validação da URL e deduplicação do histórico. |
| Configuração e sintaxe | **11 JS/JSX e 5 JSON válidos**, **3 cabeçalhos C++ verificados** | Parse dos arquivos do app e do servidor e compilação de sintaxe dos exemplos de configuração dos ESP32 e da CA do servo. O Expo conferiu as dependências em modo offline. |
| SQLite e fila de push | **Verificação isolada aprovada** | Inicialização em memória, validação das chaves e processamento de notificação com recibo fornecido pelo provedor simulado. |
| Suíte WebSocket do servidor | **Bloqueada pelo ambiente** | Os 10 casos precisam abrir um socket em `127.0.0.1`, operação rejeitada com `EPERM`. A verificação isolada acima cobre somente a inicialização e o processamento da fila. |
| Arduino ESP32, sketch isolado do buzzer | **Compilado** | ESP32 clássico com core Espressif 3.3.11. A compilação gera o binário. Ouvir o som depende da bancada. |
| Arduino ESP32, sketches do painel e do servo | **Compilação bloqueada por dependências** | A biblioteca Adafruit ST7735 and ST7789 falta para o painel, e WebSockets de Markus Sattler falta para o servo. |
| Docker Compose | **Configuração validada** | `docker compose config --quiet` passou com exemplos fictícios. A validação cobre o arquivo Compose, com hospedagem WSS ainda a validar. |
| Documentação e apresentação | **Verificadas localmente** | Links relativos resolvidos, imagens de montagem e LCD decodificadas e 8 slides editáveis reabertos e renderizados, com notas nos 8 slides. |

Node.js usado na revisão: **24.20.0**.

## Repetir os testes

Execute da raiz do projeto, em Bash, com Node.js 24, npm e g++:

```bash
(cd server && npm ci && npm test)
(cd app && npm ci && npm test)

g++ -std=c++17 -Wall -Wextra -Werror firmware/test/controller_test.cpp -o controller-test
./controller-test
g++ -std=c++17 -Wall -Wextra -Werror firmware/test/automation_test.cpp -o automation-test
./automation-test
```

A suíte WebSocket usa o mesmo `GateSocket` do app, conexões em loopback, frames representando o servo e um provedor de push simulado. Os testes C++ exercitam os controladores em memória. O [guia de instalação](docs/instalacao.md) lista as dependências dos sketches completos e os comandos para build do app.

## Ensaiar com a bancada

| Ação | Critério esperado |
| --- | --- |
| Inicializar as duas placas e conectar o app | Painel recebe estado fechado por UART2, LED fica apagado e app indica servo e painel online. Assim que o servo sincroniza via NTP, o LCD exibe hora e data em UTC−4. |
| Enviar `O` e depois `X` no Serial do servo | Servo movimenta a maquete, LCD acompanha a estimativa e LED apaga ao chegar ao estado fechado. A abertura nova aciona o buzzer ativo com link ao vivo. |
| Apertar e segurar o botão do painel | Uma melodia por toque aceito, aviso no LCD e um evento no histórico. Repetições exigem soltar e respeitar o intervalo de 2 s. |
| Interromper a UART2 com a bancada desligada, religar e tocar a campainha | LCD com **SEM SINAL** e som local. Após restaurar a UART2, o evento é encaminhado se ficar registrado na fila LittleFS. |
| Pedir abertura e fechamento no app | Estado muda durante o movimento. Confirme fisicamente os dois sentidos e consulte os horários no histórico. |
| Ativar push em build próprio e pôr o app em segundo plano | Notificação de campainha no celular físico com internet e permissão. Repita para uma abertura concluída. |
| Conectar o app e o servo usando WSS | Acesso externo funciona com certificado válido. O painel continua ligado ao servo por UART2. |

Gravação nas placas, som, movimentação física, teste da UART nas duas placas, build do app, push real e acesso externo dependem de execução na bancada ou no ambiente de hospedagem.
