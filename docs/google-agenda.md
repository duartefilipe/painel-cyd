# Conectar o Google Agenda

O painel lê a agenda pela API oficial do Google, com OAuth. Para isso ele precisa de uma
**credencial de app** criada por você no Google Cloud. É gratuito, leva uns 5 minutos e só
se faz uma vez. Cada pessoa que usa o painel cria a própria credencial: ela **não** vem no
repositório.

## 1. Criar o projeto e ativar a API

1. Abra o [Google Cloud Console](https://console.cloud.google.com/projectcreate) e crie um
   projeto (por exemplo, `painel-cyd`).
2. Com o projeto selecionado no topo, ative a
   [Google Calendar API](https://console.cloud.google.com/apis/library/calendar-json.googleapis.com).

## 2. Tela de consentimento

Em [Google Auth Platform](https://console.cloud.google.com/auth/overview):

1. **Branding:** nome do app (`painel-cyd`) e o seu e-mail de suporte.
2. **Público-alvo:** tipo **Externo**.
3. **Público-alvo → Usuários de teste → Add users:** adicione **o seu próprio e-mail**.

> **Não pule o passo 3.** Com o app em "Testando", o Google só deixa entrar quem está na
> lista de testadores, inclusive você, que criou o app. Sem isso, o login para em
> **"Erro 403: access_denied — o app não concluiu o processo de verificação"**.

## 3. Criar a credencial

1. Em [Clientes](https://console.cloud.google.com/auth/clients), clique em **Criar cliente**.
2. Tipo de aplicativo: **App para computador**.
3. Baixe o JSON gerado.

## 4. Conectar pelo painel

1. Abra `http://localhost:8765`.
2. No card **Google Agenda**, envie o JSON baixado.
3. Clique em **Conectar Google Agenda** e entre com a sua conta.
4. O Google avisa que o app **não foi verificado**. É esperado, porque o app é seu: clique
   em **Continuar** e marque as duas permissões da agenda.

De volta ao painel, escolha quais agendas aparecem na tela. Os compromissos chegam na
placa em até 2 minutos; o botão **Atualizar agora** busca na hora.

## Permissões pedidas

| Escopo | Para quê |
|---|---|
| `calendar.readonly` | listar as suas agendas para você escolher quais exibir |
| `calendar.events` | ler os eventos e criar eventos pelo formulário do painel |

O painel não apaga nem altera eventos existentes.

## O login vence a cada 7 dias

Enquanto o app estiver em **"Testando"**, o Google invalida o token de renovação depois de
7 dias. Nesse momento:

- a tela da placa mostra **"Reconecte o Google"** no lugar da agenda;
- o card do painel ganha o botão **Reconectar**, e um clique resolve.

Para acabar com isso, publique o app: **Público-alvo → Publicar app**. O botão só acende
depois que a página **Branding** tiver **URL da página inicial** e **URL da política de
privacidade**. Para uso pessoal, o app publicado continua mostrando o aviso de "não
verificado" no login, mas o token não vence mais em 7 dias.

## Desconectar

**Desconectar** no painel apaga o token local (`data/token.json`). Para revogar também do
lado do Google: [myaccount.google.com/permissions](https://myaccount.google.com/permissions).
