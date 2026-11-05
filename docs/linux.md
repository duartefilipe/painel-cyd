# Linux

O mesmo código roda no Linux: o programa do PC é Python puro, e a placa é encontrada pelo
identificador USB, que não muda entre sistemas. O firmware não muda nada, porque ele fica
na placa.

Feito para Debian/Ubuntu e derivados, Fedora e Arch. Precisa de `python3`, com
`python3-venv` no Debian/Ubuntu, e de `systemd` para iniciar sozinho.

## Instalar

```bash
git clone https://github.com/duartefilipe/painel-cyd.git
cd painel-cyd
bash instalar-linux.sh
```

O script:

1. cria um ambiente virtual em `.venv/` e instala as dependências. Debian e Ubuntu
   bloqueiam `pip install` no Python do sistema (PEP 668), por isso o `.venv`;
2. confere se o seu usuário pode abrir a porta serial (grupo `dialout`, ou `uucp` no Arch);
3. avisa se o `brltty` estiver instalado (ver abaixo);
4. instala e liga o serviço `painel-cyd`, que sobe a cada login e reinicia se cair.

Para só instalar, sem o serviço: `bash instalar-linux.sh --sem-servico`, e depois
`./iniciar.sh`.

Ele **não roda nada com `sudo`**. Quando falta permissão, ele mostra o comando para você
rodar.

## Permissão na porta serial

No Linux, a placa aparece como `/dev/ttyUSB0`, e só usuários do grupo dono dela podem
abri-la:

```bash
sudo usermod -aG dialout $USER     # Arch: uucp no lugar de dialout
```

**Saia da sessão e entre de novo:** grupo novo só vale em sessão nova. Sem isso, o painel
mostra `Permission denied: '/dev/ttyUSB0'` no card Placa.

## brltty sequestra a placa (Ubuntu 22.04+)

O `brltty` é um leitor de braille que vem instalado no Ubuntu e reivindica dispositivos com
o chip **CH340**, que é justamente o da CYD. Sintoma: a placa conecta, o `dmesg` mostra
`ch341-uart converter now attached to ttyUSB0` e, logo depois, `interface 0 claimed by
ch341 while 'brltty' sets config`. A `/dev/ttyUSB0` some.

```bash
sudo apt remove brltty
```

Depois, desconecte e reconecte a placa.

## Levar a conexão com o Google

A pasta `data/` não vem do git, porque tem a credencial e o token da sua conta. Para não
refazer o processo do Google Cloud, copie do Windows para o Linux:

| Arquivo | O que é |
|---|---|
| `data/client_secret.json` | credencial do app (a mesma serve em qualquer máquina) |
| `data/token.json` | login da sua conta (funciona em outra máquina) |
| `data/config.json` | cidade, agendas, brilho, cores, rotação |

Em dual boot, o Linux costuma enxergar a partição do Windows: monte-a pelo gerenciador de
arquivos e copie `D:\Anakin\cyd-painel\data` para `painel-cyd/data`. Copie por pendrive ou
pela rede local, **nunca pelo git**: o repositório é público.

Também dá para não copiar nada e conectar de novo pelo painel: é só enviar o
`client_secret.json` e clicar em **Conectar Google Agenda**.

> Com o token copiado, os dois sistemas usam o mesmo login. Se ele vencer (modo "Testando",
> 7 dias), reconectar em um deles não atualiza o outro: será preciso reconectar nos dois,
> ou copiar o `token.json` de novo.

## Dia a dia

| Para | Comando |
|---|---|
| abrir o painel | `./iniciar.sh` (só abre o navegador se o serviço já estiver rodando) ou `http://localhost:8765` |
| ver se está rodando | `systemctl --user status painel-cyd` |
| ver o log | `journalctl --user -u painel-cyd -f` ou `data/painel.log` |
| reiniciar (depois de `git pull`) | `systemctl --user restart painel-cyd` |
| parar | `systemctl --user stop painel-cyd` |
| tirar da inicialização | `systemctl --user disable --now painel-cyd` |

O serviço é **de usuário**: sobe quando você entra na sessão e para quando você sai. Para
ele rodar com o PC ligado e ninguém logado, use `sudo loginctl enable-linger $USER`.

## Gravar o firmware no Linux

```bash
python3 -m venv ~/.pio-venv && ~/.pio-venv/bin/pip install platformio
systemctl --user stop painel-cyd          # solta a porta
cd firmware && ~/.pio-venv/bin/pio run -t upload
systemctl --user start painel-cyd
```
