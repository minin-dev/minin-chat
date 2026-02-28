```
╔═══════════════════════════════════════════════════╗
║                  MININ-CHAT v1.0                  ║
║        COBOL + FORTRAN + C  CHAT SERVER           ║
║     "Enterprise-grade messaging since 1959"       ║
╚═══════════════════════════════════════════════════╝
```

# MININ-CHAT

Чат-сервис с бекендом на **COBOL** (форматирование сообщений), **Fortran** (шифрование),
и **C** (HTTP-сервер). Фронтенд — ретро Unix-терминал с `/команды` интерфейсом.
Всё упаковано в минимальный Docker-контейнер.

## Архитектура

```
┌─────────────────────────────────────────┐
│           DOCKER CONTAINER              │
│                                         │
│  ┌──────────────────────────────────┐   │
│  │     C HTTP SERVER (server.c)     │   │
│  │   - Socket/HTTP handling         │   │
│  │   - State management             │   │
│  │   - API routing                  │   │
│  │   - Static file serving          │   │
│  └──────┬──────────────┬────────────┘   │
│         │              │                │
│  ┌──────▼──────┐ ┌─────▼────────────┐   │
│  │  FORTRAN    │ │  COBOL           │   │
│  │ encrypt.f90 │ │  chat.cob        │   │
│  │             │ │                  │   │
│  │ XOR-PRNG    │ │ Message format   │   │
│  │ Stream      │ │ MOTD/Help gen    │   │
│  │ Cipher      │ │ Command validat  │   │
│  │             │ │ System messages  │   │
│  │ (linked .o) │ │ (fork/pipe)      │   │
│  └─────────────┘ └──────────────────┘   │
│                                         │
│  ┌──────────────────────────────────┐   │
│  │  FRONTEND (index.html ~4KB)      │   │
│  │  Retro CRT terminal UI           │   │
│  │  Scanlines + glow + flicker      │   │
│  │  /commands interface             │   │
│  │  HTTP polling client             │   │
│  └──────────────────────────────────┘   │
│                                         │
└─────────────────────────────────────────┘
```

## Технологии

| Компонент | Язык | Назначение |
|-----------|------|------------|
| HTTP Server | **C** | Сокеты, маршрутизация, управление состоянием |
| Шифрование | **Fortran 90** | XOR-PRNG поточный шифр (iso_c_binding) |
| Форматирование | **COBOL** | Обработка сообщений, MOTD, валидация команд |
| Фронтенд | **HTML/CSS/JS** | Ретро Unix-терминал, минифицированный |

## Шифрование (Fortran)

Сообщения шифруются Vigenère-PRNG гибридным шифром:
- Ключевой поток генерируется линейным конгруэнтным ГПСЧ
- Каждый символ сдвигается в пределах printable ASCII [32-126]
- Шифрование/дешифрование симметрично с одинаковым ключом

```fortran
! Ядро шифрования в encrypt.f90
state = ieor(state * 1103515245 + 12345, ishft(state, -16))
shift = mod(iand(abs(state), 65535), 95)
ch = mod(ch - 32 + shift, 95) + 32
```

## COBOL Процессор

Пайп-делимитированный протокол обмена:

```
Вход:  FORMAT|nickname|message|room
Выход: OK|[14:30:22] <nickname> message

Вход:  MOTD
Выход: OK|=== MININ-CHAT v1.0 === ...

Вход:  HELP
Выход: OK|=== MININ-CHAT COMMANDS === ...
```

## Запуск

### Docker (рекомендуется)

```bash
# Сборка и запуск
docker-compose up --build

# Или напрямую
docker build -t minin-chat .
docker run -p 3000:3000 minin-chat
```

Откройте `http://localhost:3000` в браузере.

### Локальная сборка (Linux)

```bash
# Зависимости (Debian/Ubuntu)
sudo apt install gcc gfortran gnucobol4 make

# Сборка
cd backend
make all

# Копировать фронтенд
mkdir -p /app/static
cp ../frontend/index.html /app/static/
cp server chat /app/

# Запуск
/app/server
```

## Команды чата

```
/nick <name>     Сменить никнейм
/join <room>     Присоединиться к комнате
/w <user> <msg>  Личное сообщение (whisper)
/users           Список пользователей в комнате
/rooms           Список активных комнат
/status          Статус сервера
/clear           Очистить терминал
/uptime          Время сессии
/help            Справка по командам
/quit            Отключиться
```

Просто вводите текст без `/` для отправки сообщения в текущую комнату.

## API

| Метод | Путь | Тело | Описание |
|-------|------|------|----------|
| GET | `/` | — | Фронтенд (index.html) |
| POST | `/api/login` | `n=NICK` | Подключение, получение токена |
| POST | `/api/send` | `t=TOKEN&m=MSG` | Отправка сообщения |
| GET | `/api/poll?t=TOKEN&a=N` | — | Получение новых сообщений |
| POST | `/api/cmd` | `t=TOKEN&c=CMD` | Выполнение команды |

## Структура проекта

```
minin-chat/
├── backend/
│   ├── server.c        # C HTTP сервер (~450 строк)
│   ├── encrypt.f90     # Fortran шифрование (~100 строк)
│   ├── chat.cob        # COBOL процессор (~200 строк)
│   └── Makefile        # Система сборки
├── frontend/
│   └── index.html      # Терминал UI (~4KB)
├── Dockerfile          # Multi-stage Docker сборка
├── docker-compose.yml  # Compose конфигурация
└── README.md           # Документация
```

## Оптимизация размера

- **Docker**: Multi-stage build, только runtime библиотеки в финальном образе
- **Бинарники**: `strip` для удаления отладочной информации
- **Фронтенд**: Инлайн CSS/JS, без внешних зависимостей, компактный код
- **Контейнер**: read-only filesystem, лимит памяти 64MB
- **Runtime**: Только `libgfortran5` + `libcob4` — никаких компиляторов

## Лицензия

MIT
