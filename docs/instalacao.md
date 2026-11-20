# Instalação e configuração

Valores de IP, rede e domínio a seguir são fictícios. Substitua-os pelos parâmetros da bancada. Mantenha os arquivos `.env`, `config.h` e `root_ca.h` preenchidos apenas no seu ambiente. As variáveis `EXPO_PUBLIC_*` fazem parte do app distribuído.

## 1. Servidor Node.js 24

Na raiz do projeto, em Bash:

```bash
cd server
npm ci
npm run keys
cp .env.example .env
```

`npm run keys` imprime três chaves independentes de 64 caracteres hexadecimais. Copie `USER_API_TOKEN` para o `.env` do servidor e informe-o na tela de acesso do app. Use `SERVO_API_TOKEN` no servidor e no `config.h` do servo. Use `PEER_SHARED_KEY` com o mesmo valor no `config.h` de cada placa.

```dotenv
USER_API_TOKEN=SUBSTITUA_PELA_CHAVE_DE_USUARIO_GERADA
SERVO_API_TOKEN=SUBSTITUA_PELA_CHAVE_DE_SERVO_GERADA
SERVO_ID=servo-01
HOST=0.0.0.0
PORT=3000
DB_PATH=./data/portao.sqlite
EXPO_ACCESS_TOKEN=
```

Com o arquivo local preenchido, execute `npm start` na pasta `server`. `GET /health` é a rota HTTP auxiliar. O app usa `ws://192.168.50.10:3000/ws`, onde `192.168.50.10` representa o IP do computador. Permita a porta 3000 apenas na rede da bancada. Prefira uma única instância por arquivo SQLite e faça backup consistente antes de reaproveitar um banco existente.

## 2. Firmwares

Na Arduino IDE 2, instale o pacote **esp32 by Espressif Systems 3.x** e a biblioteca **ArduinoJson 7.x** para ambas as placas. Para o servo, instale **WebSockets by Markus Sattler (Links2004)**. Para o painel, instale **Adafruit GFX Library** e **Adafruit ST7735 and ST7789 Library**, incluindo as dependências. Selecione o ESP32 clássico correspondente à sua placa e uma partição com filesystem disponível. A saída do servo usa o periférico MCPWM do ESP32.

Na raiz do projeto:

```bash
cp firmware/esp32_servo/config.example.h firmware/esp32_servo/config.h
cp firmware/esp32_painel/config.example.h firmware/esp32_painel/config.h
cp firmware/esp32_servo/root_ca.example.h firmware/esp32_servo/root_ca.h
```

| Local | Campos a conferir |
| --- | --- |
| Ambas as placas | `PEER_SHARED_KEY` (64 caracteres hexadecimais) e `PANEL_ID` (`painel-01` nos exemplos) com valores iguais nos dois cabeçalhos |
| ESP32 A · servo | `WIFI_SSID`, `WIFI_PASSWORD`, `SERVER_HOST=192.168.50.10` (endereço do computador, sem protocolo ou caminho), `SERVER_PORT=3000`, `SERVER_USE_TLS=false`, `SERVO_ID=servo-01` e `SERVO_API_TOKEN` |
| ESP32 B · painel | GPIOs do LCD, botão, relé e buzzers conforme a [montagem](montagem.md). A comunicação usa a UART2, com pinagem definida em `PeerUart.h` |

Nas strings do cabeçalho C++, mantenha as aspas duplas. Grave `firmware/esp32_servo/esp32_servo.ino` no A e `firmware/esp32_painel/esp32_painel.ino` no B. Com as placas desligadas, ligue a [UART2 conforme a pinagem](montagem.md#uart2-entre-os-dois-esp32). Ligue as placas e abra o monitor serial de cada uma a **115200 baud**. Procure `SERVO UART: conectado.` no painel e `PAINEL UART: conectado.` no servo. Se faltar confirmação, confira a continuidade dos dois fios, TX/RX, GND, a versão do firmware, o ID e a chave. No servo, procure também `IP DO SERVO` e `SERVIDOR: autenticado.`

Se o LittleFS não montar, o Serial oferece 10 s para enviar `F` maiúsculo. Formatar apaga a fila local. Use esse comando somente em bancada nova ou depois de verificar que pode apagar os eventos pendentes.

## 3. App Expo

O lockfile da v2 fixa **Expo 58.0.22**, **React 19.3.0** e **React Native 0.88.1**. Na pasta `app`:

```bash
npm ci
cp .env.example .env
npm run check
```

```dotenv
EXPO_PUBLIC_API_URL=ws://192.168.50.10:3000/ws
EXPO_PUBLIC_ALLOW_LAN_HTTP=1
EXPO_PUBLIC_EAS_PROJECT_ID=SUBSTITUA_PELO_UUID_DO_EAS
```

Para o laboratório, use o IP do servidor alcançável pelo celular. O build aceita WS sem TLS apenas para endereço privado ou loopback quando a opção de bancada está habilitada. Em um celular, `localhost` aponta para o próprio aparelho. Uma mudança nessa opção altera a configuração nativa e precisa de novo build.

O repositório oferece `npm run android` e `npm run ios` para build local com a toolchain respectiva. Para desenvolvimento Android via EAS, execute na pasta `app`:

```bash
npx eas-cli@latest login
npx eas-cli@latest init
npx eas-cli@latest build --profile development --platform android
npm start
```

Vincule um projeto EAS e preencha o UUID em `EXPO_PUBLIC_EAS_PROJECT_ID`. Ajuste `android.package` ou `ios.bundleIdentifier` antes de configurar os serviços. Para iOS use `--platform ios` e as credenciais de assinatura Apple. Instale o development build no celular e conecte ao servidor com a chave de usuário gerada.

### Push no celular físico

- **Android.** Cadastre o package no Firebase, disponibilize o arquivo de cliente `google-services.json` e configure `GOOGLE_SERVICES_JSON=./google-services.json`. Cadastre a conta de serviço FCM V1 nas credenciais EAS.
- **iOS.** Configure as credenciais APNs e a assinatura do aplicativo no EAS.
- **Build em nuvem.** Disponibilize no ambiente do build o UUID público, as variáveis do app e o arquivo de cliente Firebase quando necessário. Com Enhanced Push Security, preencha `EXPO_ACCESS_TOKEN` no servidor.
- **Celular.** Use development build ou build próprio e pressione **Ativar notificações**. Autorize os avisos no sistema e verifique a campainha em segundo plano com internet. Com o app conectado, o aviso em tempo real funciona independentemente dessa ativação.

## 4. Acesso externo com WSS

Aponte seu domínio para o servidor e libere as portas 80 e 443 para o Caddy. Na pasta `server`, acrescente `API_DOMAIN=portao.example.com` ao `.env` local e substitua pelo seu domínio. Execute:

```bash
docker compose up -d --build
```

O volume `gate_data` guarda o SQLite e a API permanece interna na porta 3000 do Compose. No app, use `wss://portao.example.com/ws` e `EXPO_PUBLIC_ALLOW_LAN_HTTP=0`. No firmware do servo, use `SERVER_HOST="portao.example.com"`, `SERVER_PORT=443` e `SERVER_USE_TLS=true`. Insira em `ROOT_CA` no `root_ca.h` o certificado raiz público correspondente à cadeia servida pelo domínio, incluindo as linhas de início e fim do PEM. O servo aguarda NTP para iniciar o WSS. O painel segue ligado ao servo por UART2.
