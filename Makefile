.PHONY: check bot-test bot-run ai-test server-build server-test protocol-check e2e schema-check docker-up docker-down docker-logs ai-run server-run

# Полный прогон всех проверок
check: bot-test ai-test server-test protocol-check

# Aura Bot (TG-бот + API): тесты и запуск
bot-test:
	cd bot && python -m pytest -q

bot-run:
	cd bot && python -m aura_bot

# Python AI-сервис: тесты
ai-test:
	cd ai && PYTHONPATH=. pytest -q

# Python AI-сервис: запуск (dev)
ai-run:
	cd ai && python -m aura_ai.app

# C++ сервер: сборка
server-build:
	cmake -S server -B build/server -G Ninja -DAURA_BUILD_TESTS=ON
	cmake --build build/server

# C++ сервер: тесты (ctest)
server-test: server-build
	cd build/server && ctest --output-on-failure

# C++ сервер: запуск (dev)
server-run:
	./build/server/aura-server

# Проверка протокола (iOS + Android vs сервер)
protocol-check:
	python3 tools/check_ios_protocol.py
	python3 tools/check_android_protocol.py

# E2E-сценарий (141 проверка): нужны запущенные AI :8000 и сервер :9000
e2e:
	AURA_E2E_GOOGLE_MOCK=1 AURA_E2E_PUSH_MOCK=1 python3 tools/e2e.py

# Применить схему БД + seed-данные
schema-check:
	psql "$(PG_URI)" -f schema/schema.sql
	psql "$(PG_URI)" -f schema/seed.sql

# Docker Compose: запуск prod-стека (postgres + ai + server)
docker-up:
	docker-compose up -d

docker-down:
	docker-compose down

docker-logs:
	docker-compose logs -f
