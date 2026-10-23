FROM python:3.12-slim

RUN apt-get update \
 && apt-get install -y --no-install-recommends git openssh-client ca-certificates \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY app.py stats.py ./
COPY dashboard ./dashboard

RUN useradd --create-home inkdeck && mkdir -p /data && chown inkdeck /data
USER inkdeck
ENV NOTES_PATH=/data/notes PORT=8080
EXPOSE 8080
HEALTHCHECK CMD python -c "import urllib.request; urllib.request.urlopen('http://localhost:8080/healthz')"
CMD ["python", "app.py"]
