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

O servidor roda em outra máquina da rede, e um cliente do tipo "App para computador" só
pode voltar do login para `localhost`. Por isso o último passo é copiar e colar:

1. Abra `http://<ip-do-servidor>:8765`.
2. No card **Google Agenda**, envie o JSON baixado.
3. Clique em **Abrir o login do Google** e entre com a sua conta.
4. O Google avisa que o app **não foi verificado**. É esperado, porque o app é seu: clique
   em **Continuar** e marque as duas permissões da agenda.
5. O navegador vai para `http://localhost:8765/oauth/callback?state=...&code=...` e mostra
   **erro de conexão**. Isso é normal. **Copie o endereço inteiro** da barra.
6. Cole no campo do painel e clique em **Concluir**.

**Atalho:** se você já tinha o painel antigo (USB) conectado, envie o `token.json` da pasta
`data/` do PC em **Enviar token**. Não precisa refazer o login.

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
- o card do painel volta a mostrar o login (passos 3 a 6 acima).

Para acabar com isso, publique o app: **Público-alvo → Publicar app**. O botão só acende
depois que a página **Branding** tiver **URL da página inicial** e **URL da política de
privacidade**. Para uso pessoal, o app publicado continua mostrando o aviso de "não
verificado" no login, mas o token não vence mais em 7 dias.

## Desconectar

**Desconectar** no painel apaga o token do servidor (`/data/token.json` no volume). Para revogar também do
lado do Google: [myaccount.google.com/permissions](https://myaccount.google.com/permissions).
