#!/usr/bin/env bash
# Dev-стек для проверки клиентов: AI-сервис :8000 + сервер :9000 со встроенной
# БД, dev-почтой (код подтверждения в ответе) и песочницей инструментов.
# Секреты генерируются на запуск, если не заданы в окружении.
#   cmake -S . -B build -DAURA_WITH_LIBPQ=OFF && cmake --build build
#   tools/dev_stack.sh            # логи и база — в build/devstack
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
RUN=${AURA_RUN_DIR:-$ROOT/build/devstack}
SERVER=${AURA_SERVER_BIN:-$ROOT/build/server/aura-server}
PYTHON=${PYTHON:-python3}
mkdir -p "$RUN"

wait_port() {  # host port seconds
    for _ in $(seq 1 "$3"); do
        (exec 3<>"/dev/tcp/$1/$2") 2>/dev/null && return 0
        sleep 1
    done
    return 1
}

# Общий секрет сервер <-> AI: без него AI-сервис закрыт (503).
export AURA_AI_TOKEN=${AURA_AI_TOKEN:-$(openssl rand -hex 32)}
(cd "$ROOT/ai" && PYTHONPATH=. nohup "$PYTHON" -m aura_ai.app > "$RUN/ai.log" 2>&1 &)
wait_port 127.0.0.1 8000 60 || { echo "AI-сервис не поднялся"; tail -40 "$RUN/ai.log"; exit 1; }

export AURA_JWT_SECRET=${AURA_JWT_SECRET:-$(openssl rand -hex 32)}
export AURA_2FA_KEY=${AURA_2FA_KEY:-$(openssl rand -hex 32)}
export AURA_EMBEDDED_DB=${AURA_EMBEDDED_DB:-$RUN/db.json}
export AURA_MAIL_DRIVER=dev AURA_TOOLS_MODE=sandbox AURA_AI_URL=http://127.0.0.1:8000
nohup "$SERVER" > "$RUN/server.log" 2>&1 &
wait_port 127.0.0.1 9000 60 || { echo "Сервер не поднялся"; tail -40 "$RUN/server.log"; exit 1; }
echo "dev-стек: AI http://127.0.0.1:8000, сервер ws://127.0.0.1:9000 (логи: $RUN)"
