# Servidor do Painel CYD. O Portainer constroi esta imagem direto do repositorio.
FROM python:3.12-slim

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PAINEL_DATA=/data \
    PAINEL_PORT=8765

WORKDIR /app
COPY servidor/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY servidor/ .

# Usuario sem privilegios; /data e o volume com config, credencial e token do Google.
RUN useradd -r -u 10001 painel && mkdir -p /data && chown painel /data
USER painel
VOLUME ["/data"]
EXPOSE 8765

HEALTHCHECK --interval=30s --timeout=5s --start-period=20s \
  CMD python -c "import urllib.request,sys; sys.exit(0 if urllib.request.urlopen('http://127.0.0.1:8765/healthz',timeout=4).status==200 else 1)"

CMD ["python", "app.py"]
