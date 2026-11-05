# Instalação

## Hardware

A placa é a **ESP32-2432S028R**, vendida como "Cheap Yellow Display" (CYD):

| Item | Valor |
|---|---|
| Microcontrolador | ESP32-D0WD-V3, dual core 240 MHz, 4 MB de flash |
| Tela | 2,8" ILI9341, 320×240, SPI |
| Toque | resistivo XPT2046 (ainda não usado) |
| USB | conversor CH340 (alguns lotes: CP210x) |

Pinagem da tela, já configurada em `firmware/platformio.ini`:

| Sinal | GPIO |
|---|---|
| MISO / MOSI / SCLK | 12 / 13 / 14 (barramento HSPI) |
| CS / DC | 15 / 2 |
| Backlight | 21 (controlado por PWM para o brilho) |

> Existe uma variante da CYD com **duas portas USB** (micro-USB + USB-C) que usa a tela
> **ST7789** em vez da ILI9341. Nela, troque `-DILI9341_2_DRIVER=1` por
> `-DST7789_DRIVER=1` no `platformio.ini`.

### Driver USB

O Windows 11 costuma instalar o driver do CH340 sozinho. Se a placa não aparecer em
**Gerenciador de Dispositivos → Portas (COM e LPT)**, instale o driver CH340 da WCH.
Cabo USB "só de carga" (sem os fios de dados) também deixa a placa invisível.

## Firmware

Precisa do PlatformIO (`pip install platformio`). Na primeira compilação ele baixa as
ferramentas do ESP32, o que leva alguns minutos.

```bash
cd firmware
pio run                 # só compila
pio run -t upload       # compila e grava
pio device monitor      # ver o que a placa imprime (115200)
```

Se houver mais de uma porta serial no PC, indique a da placa: `pio run -t upload --upload-port COM3`.

> **A porta só aceita um programa por vez.** Se o programa do PC estiver rodando, clique em
> **Liberar porta** no painel antes de gravar, e em **Retomar** depois.

Logo depois de gravar, a tela mostra **"aguardando o PC..."** com um ponto vermelho. É o
esperado: a placa só desenha os dados quando o programa do PC começa a mandá-los.

## Programa do PC

```bash
cd pc
pip install -r requirements.txt
python app.py              # abre o navegador em http://localhost:8765
python app.py --no-browser # sem abrir o navegador
```

Na primeira execução, ele cria a pasta `data/` com a configuração padrão (cidade: Santa
Maria, RS). Troque a cidade pelo painel.

A placa é encontrada pelo identificador USB (CH340 `1A86:7523` ou CP210x `10C4:EA60`),
então trocar de porta USB não exige configurar nada. Se houver outro aparelho com o mesmo
conversor, escolha a porta manualmente no painel.

### Iniciar junto com o Windows

`iniciar.bat` sobe o programa com `pythonw`, sem janela de terminal. Para ele rodar no
login:

1. `Win + R` → `shell:startup` → Enter
2. Crie ali um atalho para o `iniciar.bat`

Para parar: Gerenciador de Tarefas → `pythonw.exe` → Finalizar tarefa.
O log fica em `data/painel.log`.

## Ajustes de tela

No card **Placa** do painel, sem precisar regravar o firmware:

- **Brilho:** de 10 a 255.
- **Inverter cores:** alguns lotes da CYD saem de fábrica com o painel em negativo (fundo
  claro, cores trocadas).
- **Girar 180°:** para a placa em pé com o USB para o outro lado.
