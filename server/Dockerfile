FROM python:3.12-slim

RUN apt-get update \
 && apt-get install -y --no-install-recommends git openssh-client ca-certificates tzdata \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
# Built from the repository root (see compose.yaml), for the prompt library.
COPY server/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY server/app.py server/stats.py server/companion.py server/enrich.py server/prompts.py ./
COPY server/dashboard ./dashboard
COPY prompts/library.txt ./prompts/library.txt

RUN useradd --create-home inkdeck && mkdir -p /data && chown inkdeck /data
USER inkdeck
ENV NOTES_PATH=/data/notes PORT=8080 PROMPTS_LIBRARY=/app/prompts/library.txt
EXPOSE 8080
HEALTHCHECK CMD python -c "import urllib.request; urllib.request.urlopen('http://localhost:8080/healthz')"
CMD ["python", "app.py"]
