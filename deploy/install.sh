#!/usr/bin/env bash
# Установка Aura на чистый сервер Ubuntu 22.04 / 24.04 одной командой.
#
#   curl -fsSL https://raw.githubusercontent.com/velorievvork-pixel/Aura/main/deploy/install.sh | sudo bash
#
# Что делает: ставит Docker и git, скачивает проект в /opt/aura, генерирует все
# секреты в /opt/aura/.env (в git они не попадают), спрашивает API-ключ LLM
# (Enter — пропустить, вставить позже: aura llm-key), собирает и запускает
# PostgreSQL + AI-сервис + сервер и печатает адрес для приложения.
#
# Необязательные переменные (перед командой: VAR=значение curl … | sudo VAR=… bash):
#   AURA_DOMAIN=api.example.com — домен, A-запись которого указывает на этот
#       сервер: тогда TLS-сертификат получает Caddy, адрес приложения wss://домен.
#   OPENAI_API_KEY, OPENAI_BASE_URL, AURA_LLM_MODEL — LLM без вопросов.
#   AURA_BRANCH (main), AURA_REPO, AURA_DIR (/opt/aura).
# Повторный запуск безопасен: .env не перезаписывается, проект обновляется.
set -euo pipefail

AURA_DIR=${AURA_DIR:-/opt/aura}
AURA_REPO=${AURA_REPO:-https://github.com/velorievvork-pixel/Aura.git}
AURA_BRANCH=${AURA_BRANCH:-main}
AURA_DOMAIN=${AURA_DOMAIN:-}
# Для проверки скрипта на уже подготовленной машине (тесты): пропустить apt/git.
AURA_SKIP_PACKAGES=${AURA_SKIP_PACKAGES:-0}
AURA_SKIP_GIT=${AURA_SKIP_GIT:-0}

say() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
die() { printf '\nОшибка: %s\n' "$*" >&2; exit 1; }

# Ввод с клавиатуры работает и при «curl … | bash» (stdin занят скриптом).
ask() {
    local prompt=$1 secret=${2:-0} answer=""
    if [ -r /dev/tty ]; then
        if [ "$secret" = 1 ]; then
            read -r -s -p "$prompt" answer </dev/tty || true
            echo >/dev/tty
        else
            read -r -p "$prompt" answer </dev/tty || true
        fi
    fi
    printf '%s' "$answer"
}

[ "$(id -u)" -eq 0 ] || die "запустите от root: … | sudo bash"

# ------------------------------------------------------------------ пакеты
if [ "$AURA_SKIP_PACKAGES" != 1 ]; then
    say "Устанавливаю Docker и git"
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y -qq ca-certificates curl git openssl >/dev/null
    if ! command -v docker >/dev/null 2>&1; then
        apt-get install -y -qq docker.io >/dev/null
    fi
    if ! docker compose version >/dev/null 2>&1; then
        apt-get install -y -qq docker-compose-v2 >/dev/null 2>&1 \
            || apt-get install -y -qq docker-compose-plugin >/dev/null 2>&1 \
            || die "не удалось установить docker compose v2"
    fi
    systemctl enable --now docker >/dev/null 2>&1 || true

    # Сборка C++-сервера требует памяти: на 1–2 ГБ без swap компилятор падает.
    mem_kb=$(awk '/MemTotal/ {print $2}' /proc/meminfo)
    if [ "$mem_kb" -lt 3000000 ] && [ "$(swapon --show --noheadings | wc -l)" -eq 0 ]; then
        say "Мало памяти — добавляю swap 2 ГБ"
        fallocate -l 2G /swapfile && chmod 600 /swapfile && mkswap /swapfile >/dev/null && swapon /swapfile
        grep -q '^/swapfile ' /etc/fstab || echo '/swapfile none swap sw 0 0' >> /etc/fstab
    fi
fi
docker compose version >/dev/null 2>&1 || die "нет docker compose v2"

# ------------------------------------------------------------------ проект
if [ "$AURA_SKIP_GIT" != 1 ]; then
    if [ -d "$AURA_DIR/.git" ]; then
        say "Обновляю проект в $AURA_DIR"
        git -C "$AURA_DIR" fetch --quiet origin "$AURA_BRANCH"
        git -C "$AURA_DIR" checkout --quiet "$AURA_BRANCH"
        git -C "$AURA_DIR" pull --quiet --ff-only origin "$AURA_BRANCH"
    else
        say "Скачиваю проект в $AURA_DIR"
        git clone --quiet --branch "$AURA_BRANCH" "$AURA_REPO" "$AURA_DIR" \
            || die "не удалось скачать $AURA_REPO (если репозиторий приватный — AURA_REPO=https://<токен>@github.com/…)"
    fi
fi
cd "$AURA_DIR"
[ -f docker-compose.yml ] || die "в $AURA_DIR нет docker-compose.yml"

# ------------------------------------------------------------------ секреты
env_get() { grep -E "^$1=" .env 2>/dev/null | tail -1 | cut -d= -f2- || true; }
env_set() {
    local key=$1 value=$2
    if grep -qE "^$key=" .env; then
        # Значение без экранирования sed: заменяем строку целиком через awk.
        awk -v k="$key" -v v="$value" 'BEGIN{FS=OFS="="} $1==k {print k "=" v; next} {print}' .env > .env.tmp
        mv .env.tmp .env
    else
        printf '%s=%s\n' "$key" "$value" >> .env
    fi
    chmod 600 .env
}

if [ ! -f .env ]; then
    say "Генерирую секреты в $AURA_DIR/.env"
    umask 077
    {
        echo "# Создано deploy/install.sh $(date -u +%Y-%m-%dT%H:%M:%SZ). Не публиковать, в git не класть."
        for key in AURA_JWT_SECRET AURA_2FA_KEY AURA_AI_TOKEN POSTGRES_PASSWORD AURA_DB_PASSWORD AURA_AI_DB_PASSWORD; do
            echo "$key=$(openssl rand -hex 32)"
        done
        echo "# Почта: log — письма не отправляются (подтверждение: aura verify EMAIL);"
        echo "# smtp — настоящая отправка (AURA_SMTP_HOST/PORT/USER/PASSWORD/FROM)."
        echo "AURA_MAIL_DRIVER=log"
        echo "AURA_LLM_PROVIDER=mock"
        echo "AURA_LLM_MODEL=gpt-4o-mini"
        echo "OPENAI_API_KEY="
        echo "OPENAI_BASE_URL="
        echo "AURA_ADMIN_EMAILS="
        echo "AURA_DOMAIN="
    } > .env
    chmod 600 .env
fi

# ------------------------------------------------------------------ LLM
key=${OPENAI_API_KEY:-}
if [ -z "$key" ] && [ -z "$(env_get OPENAI_API_KEY)" ]; then
    say "LLM"
    echo "Вставьте API-ключ LLM (OpenAI или совместимый). Enter — пропустить, вставить позже: aura llm-key"
    key=$(ask "API-ключ: " 1)
fi
if [ -n "$key" ]; then
    env_set OPENAI_API_KEY "$key"
    env_set AURA_LLM_PROVIDER openai
    base=${OPENAI_BASE_URL:-}
    if [ -z "$base" ] && [ -z "${OPENAI_API_KEY:-}" ]; then
        base=$(ask "Адрес API (Enter — OpenAI): ")
    fi
    [ -n "$base" ] && env_set OPENAI_BASE_URL "$base"
    [ -n "${AURA_LLM_MODEL:-}" ] && env_set AURA_LLM_MODEL "$AURA_LLM_MODEL"
fi

# ------------------------------------------------------------------ домен и TLS
[ -n "$AURA_DOMAIN" ] && env_set AURA_DOMAIN "$AURA_DOMAIN"
domain=$(env_get AURA_DOMAIN)
compose=(docker compose -f docker-compose.yml)
if [ -n "$domain" ]; then
    compose+=(-f deploy/compose.tls.yml)
    env_set AURA_BIND 127.0.0.1   # порт 9000 — только для Caddy на этой машине
fi

if command -v ufw >/dev/null 2>&1 && ufw status 2>/dev/null | grep -q "Status: active"; then
    if [ -n "$domain" ]; then ufw allow 80/tcp >/dev/null; ufw allow 443/tcp >/dev/null
    else ufw allow 9000/tcp >/dev/null; fi
fi

# Команда управления: aura status | logs | verify EMAIL | llm-key | llm-check | update | backup
ln -sf "$AURA_DIR/deploy/aura.sh" /usr/local/bin/aura
chmod +x "$AURA_DIR/deploy/aura.sh"

# ------------------------------------------------------------------ запуск
say "Собираю и запускаю (первый раз — 5–15 минут)"
"${compose[@]}" up -d --build

say "Жду готовности сервера"
for _ in $(seq 1 90); do
    state=$("${compose[@]}" ps --format '{{.Service}} {{.Health}}' 2>/dev/null | awk '$1=="server"{print $2}')
    [ "$state" = "healthy" ] && break
    sleep 5
done
[ "${state:-}" = "healthy" ] || die "сервер не поднялся за 7 минут — посмотрите: aura logs server"

if [ -n "$domain" ]; then
    address="wss://$domain"
else
    ip=$(curl -fsS --max-time 5 https://api.ipify.org 2>/dev/null || hostname -I | awk '{print $1}')
    address="ws://$ip:9000"
fi
llm=$(env_get AURA_LLM_PROVIDER)

cat <<EOF

────────────────────────────────────────────────────────────
 Aura работает.

 Адрес сервера для приложения:  $address
   (Aura → экран входа → «Адрес сервера» → вставить → «Сохранить адрес»)

 LLM: $( [ "$llm" = openai ] && echo "подключена (проверка: aura llm-check)" || echo "заглушка — вставить ключ: aura llm-key" )
 Почта: письма не отправляются — после регистрации подтвердите email:
   aura verify ваш@email
 Прочее: aura status · aura logs · aura backup · aura update
────────────────────────────────────────────────────────────
EOF
