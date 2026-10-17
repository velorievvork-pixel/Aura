#!/usr/bin/env bash
# Управление установленной Aura (ставится как команда «aura» скриптом install.sh).
#
#   aura status            — состояние контейнеров и модели LLM
#   aura logs [сервис]     — последние строки логов (server | ai | postgres | caddy)
#   aura verify EMAIL      — подтвердить email вручную (если почта не настроена)
#   aura llm-key           — вставить/сменить API-ключ LLM и проверить модель
#   aura llm-check         — проверить, что отвечает настоящая модель
#   aura backup            — резервная копия базы в backups/ (хранятся 14 последних)
#   aura update            — скачать обновление и пересобрать
#   aura restart [сервис]  — перезапустить
set -euo pipefail

AURA_DIR=${AURA_DIR:-$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)}
cd "$AURA_DIR"

env_get() { grep -E "^$1=" .env 2>/dev/null | tail -1 | cut -d= -f2- || true; }
env_set() {
    local key=$1 value=$2
    if grep -qE "^$key=" .env; then
        awk -v k="$key" -v v="$value" 'BEGIN{FS=OFS="="} $1==k {print k "=" v; next} {print}' .env > .env.tmp
        mv .env.tmp .env
    else
        printf '%s=%s\n' "$key" "$value" >> .env
    fi
    chmod 600 .env
}

compose=(docker compose -f docker-compose.yml)
[ -n "$(env_get AURA_DOMAIN)" ] && compose+=(-f deploy/compose.tls.yml)

llm_check() {
    # Проверка изнутри контейнера AI: там есть AURA_AI_TOKEN и сеть до провайдера.
    "${compose[@]}" exec -T ai python - http://127.0.0.1:8000 < tools/llm_check.py
}

cmd=${1:-status}
case "$cmd" in
    status)
        "${compose[@]}" ps
        echo
        "${compose[@]}" exec -T ai python -c \
            "import json,urllib.request;print('LLM:', json.load(urllib.request.urlopen('http://127.0.0.1:8000/healthz'))['llm'])" \
            2>/dev/null || echo "AI-сервис не отвечает"
        ;;
    logs)
        "${compose[@]}" logs --tail 100 "${@:2}"
        ;;
    verify)
        email=${2:-}
        [ -n "$email" ] || { echo "использование: aura verify ваш@email"; exit 1; }
        # Значение — переменной psql (:'email' экранирует как литерал), не подстановкой в SQL.
        # psql подставляет переменные только в командах со stdin, не в -c.
        if echo "UPDATE users SET email_verified = true WHERE lower(email) = lower(:'email');" \
            | "${compose[@]}" exec -T postgres psql -v ON_ERROR_STOP=1 -U postgres -d aura -v email="$email" \
            | grep -q "UPDATE 1"; then
            echo "Email $email подтверждён — входите в приложении."
        else
            echo "Пользователь $email не найден — сначала зарегистрируйтесь в приложении."
            exit 1
        fi
        ;;
    llm-key)
        printf 'API-ключ LLM: '
        read -r -s key </dev/tty; echo
        [ -n "$key" ] || { echo "Ключ пустой — ничего не изменено."; exit 1; }
        printf 'Адрес API (Enter — OpenAI%s): ' "$( [ -n "$(env_get OPENAI_BASE_URL)" ] && echo ", сейчас $(env_get OPENAI_BASE_URL)")"
        read -r base </dev/tty
        printf 'Модель (Enter — %s): ' "$(env_get AURA_LLM_MODEL)"
        read -r model </dev/tty
        env_set OPENAI_API_KEY "$key"
        env_set AURA_LLM_PROVIDER openai
        [ -n "$base" ] && env_set OPENAI_BASE_URL "$base"
        [ -n "$model" ] && env_set AURA_LLM_MODEL "$model"
        "${compose[@]}" up -d ai
        sleep 5
        llm_check
        ;;
    llm-check)
        llm_check
        ;;
    backup)
        mkdir -p backups && chmod 700 backups
        file="backups/aura-$(date -u +%Y%m%d-%H%M%S).sql.gz"
        "${compose[@]}" exec -T postgres pg_dump -U postgres aura | gzip > "$file"
        chmod 600 "$file"
        ls -1t backups/aura-*.sql.gz | tail -n +15 | xargs -r rm -f
        echo "Резервная копия: $AURA_DIR/$file"
        ;;
    update)
        git pull --ff-only
        "${compose[@]}" up -d --build
        ;;
    restart)
        "${compose[@]}" restart "${@:2}"
        ;;
    *)
        sed -n '2,13p' "$0"
        exit 1
        ;;
esac
