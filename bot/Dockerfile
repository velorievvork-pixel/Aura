# Aura Bot: Telegram-бот + HTTP API для APK + планировщик в одном процессе.
FROM python:3.11-slim

ENV PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY aura_bot ./aura_bot

# SQLite-база — в томе /data (DATABASE_URL=sqlite+aiosqlite:////data/aura.db)
VOLUME ["/data"]
ENV DATABASE_URL=sqlite+aiosqlite:////data/aura.db API_PORT=8080
EXPOSE 8080
RUN useradd -r -u 10001 aura && mkdir -p /data && chown aura /data
USER aura
CMD ["python", "-m", "aura_bot"]
