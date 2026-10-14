.PHONY: check ai-test server-build server-test protocol-check e2e schema-check docker-build docker-up docker-down docker-logs ai-run server-run

# Полный прогон всех проверок
check: ai-test server-test protocol-check

# Python AI-сервис: тесты
ai-test:
	cd ai && PYTHONPATH=. pytest -q

# Python AI-сервис: запуск (dev). Слушает 127.0.0.1:8000. Без AURA_AI_TOKEN
# сервис закрыт (503), поэтому для локальной разработки включаем
# AURA_AI_INSECURE_DEV=1; если токен задан — он проверяется всегда.
# В production AURA_AI_INSECURE_DEV не задавать.
ai-run:
	cd ai && AURA_AI_INSECURE_DEV=$${AURA_AI_INSECURE_DEV:-1} PYTHONPATH=. python -m aura_ai.app

# C++ сервер: сборка
server-build:
	cmake -S server -B build/server -G Ninja -DAURA_BUILD_TESTS=ON
	cmake --build build/server

# C++ сервер: тесты (ctest)
server-test: server-build
	cd build/server && ctest --output-on-failure

# C++ сервер: запуск (dev). AURA_DEV_MODE=1 разрешает dev-почту и запуск без
# секретов; в продакшене не задавать (см. docs/SETUP.md).
server-run:
	AURA_DEV_MODE=$${AURA_DEV_MODE:-1} ./build/server/aura-server

# Проверка протокола (iOS + Android vs сервер)
protocol-check:
	python3 tools/check_ios_protocol.py
	python3 tools/check_android_protocol.py

# E2E-сценарий (268 проверок со всеми моками): нужны запущенные AI :8000 и сервер :9000
e2e:
	AURA_E2E_GOOGLE_MOCK=1 AURA_E2E_PUSH_MOCK=1 python3 tools/e2e.py

# Применить схему БД + seed-данные
schema-check:
	psql "$(PG_URI)" -f schema/schema.sql
	psql "$(PG_URI)" -f schema/seed.sql

# Docker Compose: prod-стек (postgres + ai + server). Секреты — в .env
# (см. .env.example); без них compose откажется стартовать.
docker-build:
	docker-compose build

docker-up:
	docker-compose up -d

docker-down:
	docker-compose down

docker-logs:
	docker-compose logs -f
