# Testar o buzzer passivo e a campainha

O buzzer passivo da campainha fica no **GPIO 32** do ESP32 B. O buzzer ativo no **GPIO 25** é usado no aviso de nova abertura. No firmware do painel, a melodia de estilo 8 bits usa PWM a 50%, **24 notas** entre 1568 e 2349 Hz e duração nominal de **4,346 s**.

## No firmware principal

Abra `firmware/esp32_painel/esp32_painel.ino` com os cabeçalhos da pasta e grave na placa do painel. Quando aparecer **PAINEL PRONTO**, envie pelo monitor serial a **115200 baud**:

| Letra | Resultado |
| --- | --- |
| **C** | Toca a melodia atual |
| **T** | Toca um tom de 2 kHz por 2 s no buzzer passivo |
| **S** | Interrompe o áudio passivo |

C, T e S testam somente o som. Para testar o aviso no LCD, o histórico e a notificação, pressione o botão físico. No Serial, procure **BOTAO: campainha pressionada.**. O firmware exige um intervalo mínimo de 2 s entre toques aceitos.

## No sketch isolado

Grave `firmware/teste_buzzer_8bits/teste_buzzer_8bits.ino` no ESP32 B. Esse sketch usa apenas o core ESP32 3.x e seu perfil próprio de áudio. Ao ligar, toca **2 kHz por 1 s** e depois uma melodia de 24 notas com **2,944 s**. A tecla **T** toca **2 kHz por 2 s**. **C** toca a melodia e **S** interrompe. O botão no GPIO 14 reinicia a melodia.

Se houver erro de PWM no Serial, confira a versão do core e a seleção da placa. Se houver PWM sem som audível, confira o tipo, a polaridade, a tensão e a corrente do buzzer. Após o diagnóstico, regrave o firmware principal no painel.
