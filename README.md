# Painel de Controle de Acesso com BitDogLab

Este projeto foi desenvolvido utilizando a placa BitDogLab. Tem como objetivo simular um sistema de controle de acesso a ambientes, utilizando botões, display OLED, buzzer e LED RGB.

O sistema funciona da seguinte forma:

* **Entrada (Botão A):** adiciona um usuário. Se o limite máximo for atingido, o sistema emite um beep e não permite novas entradas.
* **Saída (Botão B):** remove um usuário, se houver alguém presente.
* **Reset (Botão do Joystick):** zera o contador de usuários e emite um beep duplo de confirmação.

Recursos visuais e sonoros:

* **Display OLED:** exibe o número atual de usuários.
* **LED RGB:** indica o nível de ocupação com diferentes cores (azul, verde, amarelo ou vermelho).
* **Buzzer (PWM):** emite sons curtos em situações específicas.

Todas as funcionalidades foram implementadas como **tarefas independentes com FreeRTOS**, utilizando **semáforos e mutex** para comunicação e sincronização.
