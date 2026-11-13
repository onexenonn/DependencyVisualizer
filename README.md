# DependencyVisualizer

**DependencyVisualizer** — консольное приложение на C++, которое анализирует зависимости пакетов Rust из репозитория [crates.io](https://crates.io).

Программа получает информацию о выбранном пакете, строит граф его зависимостей, определяет корректный порядок загрузки пакетов и создаёт визуализацию графа.

## Возможности

- Чтение параметров из XML-файла конфигурации.
- Получение данных о пакете через crates.io API.
- Разбор JSON-ответов собственным парсером без сторонних библиотек.
- Построение полного графа зависимостей методом BFS.
- Фильтрация зависимостей по подстроке.
- Определение порядка загрузки пакетов с помощью топологической сортировки.
- Обнаружение циклических зависимостей.
- Создание файла Graphviz DOT.
- Создание PNG-изображения графа.
- Вывод ASCII-дерева зависимостей в консоль.
- Запуск встроенных тестов без доступа к сети.

## Используемые технологии

- C++
- Visual Studio
- MSXML6
- Windows COM
- crates.io API
- Graphviz
- XML
- JSON

## Структура проекта

```text
DependencyVisualizer/
├── src/
│   ├── main.cpp
│   ├── common.h
│   ├── stage1_config.cpp
│   ├── stage2_fetch.cpp
│   ├── stage3_graph.cpp
│   ├── stage4_order.cpp
│   └── stage5_visualization.cpp
├── tests/
│   └── test_graph.txt
├── config.xml
├── config_itoa.xml
├── config_ryu.xml
├── config_memchr.xml
└── README.md
```

## Описание этапов

### Этап 1. Конфигурация

Программа читает настройки из XML-файла. Пользователь указывает имя пакета, адрес репозитория, путь к тестовому файлу, режим работы, фильтр зависимостей и необходимость вывода ASCII-дерева.

Все параметры проверяются перед началом работы: имя пакета, HTTPS-адрес репозитория, логические значения и доступность тестового файла.

Соответствующий код: `src/stage1_config.cpp`.

### Этап 2. Получение зависимостей

Программа выполняет HTTP-запросы к crates.io API. Сначала получает метаданные пакета и определяет его последнюю версию, затем запрашивает зависимости выбранной версии.

Из ответа выбираются только обычные зависимости (`kind = normal`). JSON-ответ разбирается собственным парсером, реализованным в проекте.

Соответствующий код: `src/stage2_fetch.cpp`.

### Этап 3. Построение графа зависимостей

Программа строит граф зависимостей методом BFS (обход в ширину). Начиная с выбранного пакета, она получает его зависимости, затем зависимости найденных пакетов и так далее.

Каждый пакет обрабатывается только один раз, поэтому циклические зависимости не приводят к бесконечной работе программы. Поддерживается фильтрация зависимостей по подстроке.

Соответствующий код: `src/stage3_graph.cpp`.

### Этап 4. Порядок загрузки

Программа определяет порядок загрузки пакетов с помощью топологической сортировки. Сначала обрабатываются пакеты без зависимостей, затем пакеты, у которых все зависимости уже готовы.

Если в графе существует цикл, корректный порядок загрузки невозможен, и программа сообщает об ошибке.

Пример результата для пакета `itoa`:

```text
Load order: unicode-ident -> proc-macro2 -> quote -> syn -> no-panic -> itoa
```

Соответствующий код: `src/stage4_order.cpp`.

### Этап 5. Визуализация

Программа сохраняет построенный граф в файл `graph.dot` в формате Graphviz. Затем Graphviz преобразует его в изображение `graph.png`.

Дополнительно в консоль выводится ASCII-дерево зависимостей. Повторно встречающиеся пакеты помечаются как уже показанные.

Соответствующий код: `src/stage5_visualization.cpp`.

## Сборка проекта

1. Открой решение в Visual Studio.
2. Выбери конфигурацию `Debug` и платформу `x64`.
3. Выполни `Build -> Rebuild Solution`.
4. Запусти программу через `Ctrl + F5` или из командной строки.

Для создания PNG-изображения необходимо установить [Graphviz](https://graphviz.org/download/) и добавить папку `bin` в переменную среды `PATH`.

Пример пути:

```text
C:\Program Files\Graphviz\bin
```

## Запуск программы

Перейди в папку с конфигурационными файлами:

```bat
cd /d "C:\Users\User\Desktop\DependencyVisualizer\ConsoleApplication1"
```

Добавь Graphviz в `PATH` для текущего окна терминала:

```bat
set "PATH=C:\Program Files\Graphviz\bin;%PATH%"
```

Запусти полный анализ пакета `itoa`:

```bat
"C:\Users\User\Desktop\DependencyVisualizer\x64\Debug\ConsoleApplication1.exe" config_itoa.xml
```

## Режимы запуска

### Полный анализ зависимостей

```bat
ConsoleApplication1.exe config_itoa.xml
```

Программа получает зависимости пакета, строит граф, определяет порядок загрузки и создаёт визуализацию.

### Прямые зависимости

```bat
ConsoleApplication1.exe --direct config_itoa.xml
```

Программа получает и выводит только прямые зависимости выбранного пакета.

### Запуск без создания PNG

```bat
ConsoleApplication1.exe --no-image config_itoa.xml
```

Программа создаёт только `graph.dot`, но не создаёт и не открывает `graph.png`.

### Запуск тестов

```bat
ConsoleApplication1.exe --test
```

Программа выполняет встроенные тесты и выводит результат:

```text
ALL 12 TESTS PASSED
```

## Пример конфигурации

```xml
<config>
    <package>itoa</package>
    <repository>[https://crates.io](https://crates.io)</repository>
    <test_file>tests/test_graph.txt</test_file>
    <filter></filter>
    <test_mode>false</test_mode>
    <ascii_tree>true</ascii_tree>
</config>
```

## Пример вывода

```text
Configuration:
package = itoa
repository = [https://crates.io](https://crates.io)
test_mode = false
test_file = tests/test_graph.txt
ascii_tree = true
filter =

Fetched: itoa 1.0.19
Fetched: no-panic 0.1.37
Fetched: proc-macro2 1.0.107
Fetched: quote 1.0.47
Fetched: syn 3.0.10
Fetched: unicode-ident 1.0.27

Dependency graph:
itoa -> no-panic
no-panic -> proc-macro2 quote syn
proc-macro2 -> unicode-ident
quote -> proc-macro2
syn -> proc-macro2 quote unicode-ident
unicode-ident -> (no dependencies)
Total packages: 6

ASCII tree:
itoa
`-- no-panic
    |-- proc-macro2
    |   `-- unicode-ident
    |-- quote
    |   `-- proc-macro2 [already shown]
    `-- syn
        |-- proc-macro2 [already shown]
        |-- quote [already shown]
        `-- unicode-ident [already shown]

Load order: unicode-ident -> proc-macro2 -> quote -> syn -> no-panic -> itoa
Saved: graph.dot
Saved: graph.png
```

## Тестовый режим

В тестовом режиме программа не обращается к интернету. Граф зависимостей читается из файла `tests/test_graph.txt`.

Пример файла:

```text
A: B C
B: D
C: D E
D: F
E: F
F:
Z:
```

Запуск тестового режима:

```bat
ConsoleApplication1.exe config.xml
```

## Обработка ошибок

Программа обрабатывает следующие ситуации:

- некорректный или отсутствующий XML-файл конфигурации;
- неверное имя пакета;
- неверный HTTPS-адрес репозитория;
- отсутствие доступа к сети;
- HTTP-ошибки crates.io;
- некорректный JSON-ответ;
- циклические зависимости;
- неизвестный пакет в тестовом режиме;
- отсутствие Graphviz в переменной `PATH`.

## Автор

Учебный проект по дисциплине «Конфигурационное управление».  
Вариант №18: XML-конфигурация, Cargo/crates.io, BFS, Graphviz, PNG и ASCII-дерево.
