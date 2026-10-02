# План доработок R (macOS-этап)

Актуальный приоритет от 15 сентября 2026: [расширение и завершение языка
R](language-completeness-roadmap.ru.md). Настоящий документ сохраняет историю
предыдущих этапов и их результаты.

Дата: 11 сентября 2026. Платформа этапа: только arm64-apple-darwin, профиль
`hosted-native-async`. Linux-порт и публичный shared-library ABI
намеренно вынесены за пределы этапа (см. §8).

Нормативные источники: `specification/R_LANGUAGE_SPECIFICATION_0_1.en.adoc`
(draft.32), `specification/R_STANDARD_LIBRARY_SPECIFICATION_0_1.en.adoc`
(draft.9).

## 1. Исходное состояние (проверено на текущем бинарнике)

| Проверка | Результат | Команда |
| --- | --- | --- |
| Полный CTest Debug | 1257 / 1257 | `ctest --test-dir build-debug` |
| Source-surface audit | 872 / 872 операций резолвятся, 0 без lowering, контроль неизвестного типа проходит | `tools/audit_library_source_surface.py` |
| Приёмочная матрица | 41 / 41; 51 / 51 после M6 | `tools/audit_compiler_implementation.py` |
| Явно неполные ветки семантики | 64 сайта, 57 уникальных сообщений `R-DIAG-SLICE-001` до этапа; 70 и 62 после M2.1, новые пять называют границы среза FFI; 27 и 24 после M6 | `tools/audit_semantic_slices.py` |
| C style / format gates | 969 файлов valid | `c-style-check`, `format-check` |
| Target manifest | `conformance_claim = false`, 4 компонента `integrated-partial` | `targets/arm64-apple-darwin.hosted-native-async.json` |

Разделы `docs/compiler-implementation-audit.md` о 97 нерезолвимых операциях,
5 отклонённых пробах и стиль-гейте описывают историческое состояние и
закрываются пунктом M0.

Подтверждённые разрывы, воспроизводимые на macOS:

1. Импорт `extern "C" { ... }` отклоняется целиком:
   `R-DIAG-SLICE-001: module bindings are valid R but outside semantic slice 2`
   (`compiler/semantic/semantic.c`, `r_semantic_collect_source`). Парсер блок
   уже разбирает (`r_parse_extern_block`, `r_parse_c_declaration`).
   Атрибуты `@link`, `@header`, `@abi`, `@link_name`, `@c_type`,
   `@c_constant`, `opaque struct` в семантике не обрабатываются; манифест
   ссылок читается только для `std.c::link_available`.
2. Профиль компиляции не проверяется: `--profile freestanding` принимает
   `new` и `std.array`, `--profile hosted` принимает `async i32 main()`.
   Профиль попадает только в артефакты MIR/link-plan.
3. В стандартной библиотеке нет типа секрета и стирания памяти;
   `std.hash` явно не даёт constant-time гарантий.
4. Нет политики запрета паникующего `new` для привилегированного кода.
5. Нет измерений накладных расходов генерируемого кода относительно
   эквивалентного кода на C.

## 2. Порядок этапов

| Этап | Содержание | Зависимости |
| --- | --- | --- |
| M0 | Гигиена: актуализация аудиторских документов, первый commit | нет |
| M1 | Проверка профилей `R-DIAG-PROFILE-001` | нет |
| M2 | FFI-импорт `extern "C" { }` по срезам 2.1–2.4 | нет |
| M3 | Модуль `std.secret` | нет |
| M4 | Политика запрета паникующей аллокации | нет |
| M5 | Бенчмарки накладных расходов | нет |
| M6 | Разбор остатка `R-DIAG-SLICE-001` | после M2 |
| M7 | Freestanding core-runtime и манифест (выполнено 2026-09-12) | после M1 |
| M8 | Методы с явным `this` и трейты со статической диспетчеризацией (выполнено 2026-09-12) | после M6 |
| M9 | Локальные лямбды и замыкания, `auto`, callable-ограничения (выполнено 2026-09-12) | после M8 |
| M10 | Протокол итератора (`core::Iterator`), `for (T x in …)`, диапазоны `lo..hi`, операторы `in` / `not in` (`core::Contains`), ассоциированные типы трейтов (выполнено 2026-09-12) | после M9 |
| M13 | Статическая рефлексия: `core::enum_count/min/max/variants/name/ordinal/at/from_name`, `variant_count/variant_name`, `type_name`, `field_count/field_name`, `target_name/profile_name` (выполнено 2026-09-12) | после M12 |
| M11 | Выражения-коллекции, comprehension, однородные вариативные параметры `T... name` (выполнено 2026-09-12) | после M10 |
| M14 | Статические условия `@if`/`@else` над типовыми параметрами, трейтами, профилем и целью (выполнено; статус зафиксирован 2026-09-17, см. раздел 15.3) | после M13 |
| M12 | Слой стандартной библиотеки на самом R: карта `library/r/library.map`, `--library-map`, модули `std.cmp`, `std.iter`, `std.set`, `std.deque`, `std.heap`, `std.sorted`, `std.slice`, `std.text` и компиляторные предпосылки (выполнено 2026-09-12) | после M11 |
| M15 | Гетерогенные пакеты типов `@generic<T...>` — отложено, см. раздел 15.4 | после M12 |

M1 и M2 независимы; M2 самый объёмный и самый ценный, поэтому
реализуется вертикальными срезами с исполняемым тестом на каждом срезе.

## 3. M0 — гигиена

- В `docs/compiler-implementation-audit.md` добавить датированный раздел
  «Current state» с результатами §1 и пометить исторические разделы.
- Репозиторий не имеет ни одного коммита. Первый commit делается только по
  явному решению владельца.

Гейт: `ctest` зелёный, аудиты запускаются одной командой из README.

## 4. M1 — проверка профилей (R-CONF-0005, R-CONF-G005, R-SLIB-PROFILE-0001)

Правило: недоступная в профиле возможность диагностируется до трансляции,
а не линкуется к заглушке. Код диагностики `R-DIAG-PROFILE-001`, rule ID
`R-CONF-G005`.

Матрица (кумулятивно, как в R-SLIB-PROFILE-0001):

| Профиль | Разрешено | Диагностируется |
| --- | --- | --- |
| `freestanding` | язык, `core` | `new`/`new arc`/`new rc`, `array`/`list`/`dict`, `arc`/`rc`, любой `std.*`, `async`/`await`/`task`, `std.thread`, `std.sync`, async-формы `main` |
| `allocation` | + `new`, контейнеры, `std.alloc::alloc_error`, `std.arc`, `std.rc`, `std.array`, `std.list`, `std.dict` | остальные `std.*`, async, потоки |
| `hosted` | + `std.alloc` ops, `std.error`, `std.bytes`, `std.hash`, `std.utf8`, `std.bits`, `std.string`, `std.convert`, `std.format`, `std.json`, `std.math`, синхронный `std.time`, `std.env`, `std.c`, `std.process::exit/abort` | `std.thread`, `std.sync`, `std.async`, `std.io`, `std.fs`, `std.net`, остальной `std.process`, async `std.time`, async-функции |
| `hosted-thread` | + `std.thread`, `std.sync` | async-семейство |
| `hosted-native-async` | всё | ничего |

`std.json` в спецификации не приписан к профилю; до правки спецификации он
считается частью `hosted` (зависит от `std.string`). Это фиксируется в
документе и в тестах как implementation note.

Реализация:

1. `compiler/include/r_frontend.h`: новая операция
   `r_frontend_set_profile(context, profile)`; значение по умолчанию `hosted`.
   `compiler/cli/main.c` вызывает её до семантики из уже разобранного
   `--profile`.
2. `compiler/source/frontend_internal.h`: поле `profile` в контексте плюс
   enum `RFrontendProfile`.
3. `compiler/semantic/semantic.c`: проверки в точках
   `R_HIR_NEW` (аллокация), резолв стандартных операций
   (`r_body_standard_simple_operation` и семейства контейнеров/`std.sync`/
   `std.thread`), сбор функций (`is_async`), тип `task<...>`, `await`,
   `spawn`/`thread_scope`. Сообщение называет возможность и профиль.
4. Тесты: негативные фикстуры на каждую строку матрицы через
   `tests/check_semantic_regression.cmake` c `REQUIRED_DIAGNOSTIC=R-DIAG-PROFILE-001`,
   позитивные контроли для каждого профиля, обновление
   `docs/compiler-implementation-audit.md`.

Гейт: все существующие тесты зелёные при профиле по умолчанию;
`examples/unzip` собирается только в `hosted-native-async`;
`examples/generics` собирается в `allocation`.

Статус (11 сентября 2026): реализовано. Умолчание CLI и контекста переведено на
`hosted-native-async`, так как это единственный профиль с манифестом цели;
`r_frontend_set_profile` добавлен в публичный C API; проверки стоят в резолве
стандартных типов и операций, в `new`, в типах `task`/`array`/`list`/`dict`/
`bytes`/`arc`/`rc`/`weak`, в async-функциях и `thread_scope`. Пятнадцать
тестов `r_frontend_profile_*` покрывают матрицу. Пути к enum-константам
модулей (`std.fs::error_code::x`) отдельно не проверяются: операции и типы
модуля уже закрыты. Freestanding core-runtime и манифест остаются в M7.

## 5. M2 — FFI-импорт `extern "C" { }`

Целевая возможность: вызывать любую C-библиотеку (криптография,
COSE, архивы) из R без ручных C-обёрток на каждую
функцию. Нормативная база: §23.1–23.7, Annex F.6, R-CONF-G009.

### 2.1 Срез: импорт функций со скалярными сигнатурами, прямой путь

- Семантика блока: `@link(name, kind)` (singleton), `@header(...)`
  (repeatable), `@abi(...)` (singleton); обязательность хотя бы одного из
  `@header`/`@abi` (`R-DIAG-FFI-006`, R-FFI-0038); `protected extern "C"`.
- Импорт функции: типы только R-FFI-0004 (`r_semantic_c_abi_type` уже
  допускает скаляры, raw-указатели, raw fn, `@repr(C)` Copy-агрегаты);
  запрет `async`/`throws`; обязательный `@safety` (`R-FFI-0010`);
  `@link_name`; проверка идентификатора по R-FFI-0007/0045/0057.
- Вызов: только в `unsafe`; аргументы и результат по номиналу C ABI;
  fenv-изоляция через `r_runtime_c_call_begin/end` как у косвенных вызовов;
  ingress-гейт R-FFI-0056 для non-null указателей и raw fn результата.
- HIR/MIR: узел прямого C-вызова с символом импорта (аналог
  `R_HIR_INDIRECT_CALL`, но с известным идентификатором).
- C17: прямой путь R-CMAP-0017 — одна `extern` декларация с фактическим
  именем в generated TU, когда идентификатор не зарезервирован и типы не
  зависят от заголовка. Никаких определений импортируемой сущности.
- Link plan: список логических библиотек программы в порядке зависимостей
  манифеста; `--link-manifest` обязателен при непустом блоке;
  `R-DIAG-LINK-001/002/003` для неизвестного имени, конфликта identity и
  отсутствия символа в `symbols` провайдера. Формат записи `symbols[]`
  определяется по R-FFI-0031: `c_identifier`, `linkage_spelling`, `kind`
  (`function|data|tls`), `binding` (`strong|weak`), `provider`.
- Тесты: фикстура импорта `abs`/`strlen` из `system.libc` с манифестом
  `tests/fixtures/codegen_link_manifest.json`, исполняемый codegen-тест,
  негативы на каждый диагностический код.

Статус 2.1 (11 сентября 2026): реализовано. Семантика собирает блок и импорты
(`r_semantic_collect_extern_block`), вызов понижается через
`R_HIR_FUNCTION_ADDRESS` и существующий косвенный вызов с fenv-изоляцией и
ingress-проверкой, эмиттер печатает `extern`-прототипы, новый модуль
`compiler/source/link_resolve.c` резолвит провайдеров и инвентарь символов,
link-plan содержит секцию `link`. Формат `symbols[]` в манифесте:
`{c_identifier, kind: function|data|tls, binding: strong|weak}`, плюс
`kind` и `implicit_c_runtime` на уровне записи. Покрытие: исполняемые
`codegen_ffi_import`/`codegen_async_ffi_import` с C-библиотекой
`tests/fixtures/ffi/probe_library.c` и 13 негативных/позитивных проб
`r_frontend_ffi_*`. Зарезервированные имена libc, агрегаты, объекты,
константы и variadic отклоняются как вне среза до 2.2–2.3.

### 2.2 Срез: верификация заголовков (R-FFI-0042, R-CMAP-0024)

- Генерация verifier TU `<program>.abi-verify.c`: `#include` заголовков из
  `header_roots` провайдера, `feature_test_definitions` из манифеста,
  прототипные проверки через присваивание указателя на функцию с
  warnings-as-errors, `_Static_assert` для констант и `@repr(C)` layout.
- Сборка: `tests/check_codegen_program.cmake` компилирует verifier тем же
  компилятором и флагами, что и generated C; несборка даёт `R-DIAG-FFI-004`.
- Bridge TU (R-CMAP-0026) для зарезервированных идентификаторов и
  header-owned typedef: приватная переадресующая функция с уникальным именем.

Статус 2.2 (11 сентября 2026): реализовано. Новые артефакты
`--emit=abi-verifier` и `--emit=c17-bridge` в
`compiler/codegen/c_abi_bridge.c`: верификатор содержит макросы
`feature_test_definitions` провайдеров, заголовки блоков и по одному
присваиванию указателя на функцию на импорт; тестовый драйвер компилирует его
`-fsyntax-only` с флагами generated C и трактует отказ как `R-DIAG-FFI-004`.
Зарезервированные имена libc идут через `r_bridge_<identifier>` в bridge TU,
который линкуется с программой; для них обязателен `@header`. Манифест
получил `feature_test_definitions`, конфликт значений между провайдерами
диагностируется на резолве. Покрытие: исполняемый `codegen_ffi_bridge`
(`abs`, `strlen`), три сценария `r_frontend_abi_verifier_*` включая
намеренное несовпадение прототипа, негативы на bridge без `@header` и на
конфликт макросов, unit-тесты парсера определений. Header roots остаются
в build-манифесте: драйвер передаёт `-I` сам.

### 2.3 Срез: типы и константы

- `opaque struct Name;` (R-FFI-0015), `@c_type` (R-FFI-0016),
  `@repr(C)` struct/enum внутри блока с полной инвентаризацией членов
  (R-FFI-0017) через host-инструмент `tools/generate_c_abi_record.py`
  на `clang -Xclang -ast-dump=json`, `@c_constant` (R-FFI-0018/0019),
  импорт объектов и `thread_local` (R-FFI-0022/0023), variadic (R-FFI-0005).

Статус 2.3 (11 сентября 2026): реализовано полностью, включая подсрез 2.3b
(полные `@repr(C)` агрегаты внутри блока с инвентаризацией членов через ABI
record). Сделано:

- `opaque struct` регистрируется как неполный агрегат (`is_opaque`), допустим
  только за `raw`-указателем и в `raw fn`; по-значению в локале, параметре,
  возврате, поле, `new` и разыменовании — `R-DIAG-FFI-003 [R-FFI-0015]`.
  `@c_type(kind = "struct" | "union")` даёт в основном TU
  `typedef struct CName r_aNNNNNNNN;`, совместимый с объявлением заголовка;
  `kind = "typedef"` и зарезервированные теги — header-owned: основной TU
  держит приватный тег, импорт идёт через bridge с приведениями указателей
  (R-CMAP-0018); `kind = "enum"` для opaque отклоняется (в C17 нет неполных
  перечислений).
- `@c_constant(name = "C_NAME") const CTYPE NAME = literal as CTYPE;` —
  целочисленная константа без хранения; verifier сравнивает значение
  `_Static_assert((C_NAME) == ((CTYPE)VALUE))` и тип
  `_Static_assert(_Generic((C_NAME), CTYPE: 1, default: 0))` (R-CMAP-0019);
  представимость и форма инициализатора проверяются на семантике.
- Импорт объектов `TYPE name;` / `thread_local TYPE name;` / `const TYPE
  name;`: `extern [_Thread_local] TYPE identifier;` без зеркального хранения,
  доступ только в `unsafe` (`R-DIAG-UNSAFE-001 [R-FFI-0023]`), инвентарь
  манифеста должен содержать `data`/`tls` (`R-DIAG-LINK-003`), verifier —
  `_Generic(&(identifier), TYPE *: 1, default: 0)`.
- Variadic: флаг `R_SEMANTIC_TYPE_FLAG_VARIADIC` на raw-типе функции импорта,
  генерируемый C вызывает через `R (*)(P, ...)`, дополнительные аргументы
  обязаны иметь промотированный C-тип (`R-DIAG-FFI-002 [R-FFI-0005]`); bridge
  для variadic невозможен и отклоняется.
- Покрытие: исполняемый `codegen_ffi_types` (тег, typedef через bridge,
  четыре константы включая отрицательный и беззнаковый макрос и enumerator,
  три объекта, variadic), 13 новых семантических/линковочных негативов
  `r_frontend_ffi_*`, четыре отказа verifier (`static assertion`,
  несовпадение тега). `tests/check_abi_verifier.cmake` принимает
  `FAIL_PATTERN`.

Статус 2.3b (11 сентября 2026): реализовано.

- `@repr(C) @c_type(name, kind = "struct" | "enum" | "typedef") struct/enum`
  внутри `extern "C"` регистрируется как C-объявленный полный агрегат
  (`is_c_declared`); блок обязан именовать ABI record через `@abi(...)`,
  иначе `R-DIAG-FFI-004 [R-FFI-0041]`; отсутствие `@repr(C)`/`@c_type`,
  `union` и несоответствие kind объявлению — `R-DIAG-FFI-003`.
- Инструмент `tools/generate_c_abi_record.py`: по запросу
  `r-front --emit=abi-inventory` (схема `r-abi-inventory-request-0.1`)
  компилирует заголовки блока целевым clang и строит запись
  `r-abi-record-0.1`: полный инвентарь членов из `-ast-dump=json`, раскладку
  из `-fdump-record-layouts-complete`, совместимый целочисленный тип
  перечисления из `_Generic`-проб; ничего не исполняется на хосте
  (R-CMAP-0024). Запись содержит идентичность компилятора, опции,
  feature-test-макросы и digest'ы заголовков (R-FFI-0040).
- Компилятор (`compiler/source/abi_record.c`): `--abi-record FILE` загружает
  запись, `r_frontend_verify_abi_records` сверяет один-к-одному имена,
  порядок, C-написание типов, смещения, размер и выравнивание структур и
  представление плюс имена/значения перечислителей (алиасы отклоняются) —
  `R-DIAG-FFI-004 [R-FFI-0017]`. Пропущенный C-член ловится по счётчику даже
  внутри паддинга.
- Генерируемый C: `struct CName { … }` один-к-одному под тегом заголовка
  (R-CMAP-0018), перечисление — совместимый целочисленный тип; typedef-структуры
  идут через bridge с `memcpy` между приватным и header-типом; verifier
  дополнительно проверяет `sizeof`/`_Alignof`/`offsetof`/`_Generic` по членам
  и каждый перечислитель (R-FFI-0042).
- Покрытие: исполняемый `codegen_ffi_records` (запись генерируется в тесте
  через `tests/abi_record_support.cmake`), пять негативов
  `r_frontend_abi_record_*`, три семантических негатива, verifier-отказ
  `ffi_verify_record_claim` на записи с ложными утверждениями.

### 2.4 Срез: managed-token адаптеры и конформанс

- `std.arc::into_raw`/`from_raw` адаптеры (R-FFI-0060).
- Конформанс R-CONF-G009 на реальной минимальной C17 static library в
  `tests/fixtures/ffi_library/` со всеми перечисленными негативами.

Гейт M2: `R-CONF-G009` проходит на симуляторной библиотеке; аудит
`audit_semantic_slices` больше не содержит «module bindings».

Статус 2.4 (11 сентября 2026): реализовано; библиотека остаётся в
`tests/fixtures/ffi/probe_library.{h,c}` (а не в `ffi_library/`).

- Адаптеры managed-token: `codegen_ffi_userdata` — create/retain/release
  вокруг `std.arc::into_raw`/`from_raw` с явным `raw const void*`
  round-trip; C-сторона удерживает два обязательства и освобождает оба,
  что доказывается `strong_count` удержанного владельца.
- Новые проверки компилятора: коллизия одного C-имени у двух провайдеров
  (`R-DIAG-FFI-003 [R-FFI-0007]`); соответствие ABI-записи провайдеру блока,
  `target_triple` целевого манифеста и опциям C17 (`R-DIAG-FFI-004
  [R-FFI-0040]`); валидация enum-параметров на C-входе в trampoline
  (`contract_violation`, R-CMAP-0020).
- Матрица R-CONF-G009 с привязкой каждого пункта к тесту:
  `docs/ffi-conformance-g009.md`. Покрыты: скрытый член в паддинге, ложные
  утверждения записи, отсутствующий артефакт/типизированный символ,
  variadic-определение, null- и invalid-enum-вход, fenv (режим
  `verified-runtime-helper`: восстановление режима округления и флагов на
  исходящих вызовах и C-входах), вложенный повторный вход, вход с
  созданного C потока, TLS, `c_wint`/`WEOF`. Неприменимые на darwin пункты
  (immutable-canonical и contract-only fenv, runtime-load failure,
  неподдерживаемый link kind) отмечены явно.
- R-FFI-0044 (доделано 12 сентября 2026): запись хранит `compiler_identity` и
  `headers[].sha256`; фронтенд сверяет идентичность компилятора с
  `toolchain.c_compiler_build` манифеста цели и дайджест каждого заголовка с файлом,
  найденным по корням `--abi-header-dir` (новая опция CLI, драйверы тестов передают те
  же корни, что и `-I` генератору записи); расхождение или недоступный заголовок —
  `R-DIAG-FFI-004 [R-FFI-0044]`, а запись без идентичности компилятора отклоняется.
  Дайджест документа записи и дайджесты заголовков входят в `--emit=interface` и
  `--emit=link-plan` (`(abi-record present=... sha256=...)`, `(abi-header record=...
  spelling=... sha256=...)`). Тесты: `ffi_record_stale_header`, `ffi_record_wrong_compiler`
  (статические записи `probe_abi_{stale_header,wrong_compiler}.json`),
  `abi_record_interface_fingerprint`; статические записи `probe_abi_claim.json` и
  другие несут настоящую идентичность компилятора и дайджест `probe_library.h`
  (пересчитать при изменении заголовка). Константа `R_FRONTEND_CORE_REVISION`
  приведена к 0.1.0-draft.35 (golden-файлы MIR обновлены). Проверка: полный ctest
  1430/1430, санитайзерный набор 141/141, аудиты и ворота зелёные.

Гейт M2 закрыт на этой стадии.

## 6. M3 — `std.secret`

Область: buffers для ключей/токенов с гарантированным стиранием.

Публичный API (черновик, уточняется в спецификации до реализации):

- тип `std.secret::buffer` — Move-only, не Send-прозрачный по умолчанию
  (Send, не Sync), без `clone`; дроп стирает содержимое через volatile
  store, затем освобождает память;
- `std.secret::with_capacity(usize) -> buffer throws std.alloc::alloc_error`;
- `std.secret::from_bytes(array<u8> source) -> buffer throws alloc_error` —
  консумирует и стирает источник;
- `std.secret::as_slice(const buffer*) -> const u8[]`,
  `std.secret::as_slice_mut(buffer*) -> u8[]`, `std.secret::len`;
- `std.secret::zeroize(u8[] target) -> void` для чужих буферов;
- `std.secret::constant_time_equal(const u8[], const u8[]) -> bool`.

Работы: разделы спецификации EN+RU (нормативные правила
`R-SLIB-SECRET-0001..`, Annex A, Annex D), регенерация inventory,
`library/std/secret` с одной операцией на файл, регистрация типа и
операций в компиляторе, тесты с инъекцией аллокации и проверкой стирания.

Гейт: `specification-check`, `verify_library_inventory`,
`library-coverage-check`, исполняемые тесты sync/async.

Статус M3 (11 сентября 2026): реализовано.

- Спецификация: раздел 7.5 «Secret byte buffers» (EN+RU, якорь `[[secret]]`),
  правила R-SLIB-SECRET-0001..0004, строки Annex A и B, R-SLIB-CONF-D018;
  `std.secret` добавлен в `hosted` (R-SLIB-PROFILE-0001, R-LIB-0004). Редакция
  библиотечной спецификации поднята до 0.1.0-draft.10 (193 правила).
  Уточнения черновика API: `with_length(usize)` вместо `with_capacity`
  (buffer ровно из length нулевых байт; нулевая длина не аллоцирует);
  `from_bytes(bytes)` без `throws` — адопция аллокации без копирования
  (storage источника становится storage buffer, поэтому отдельное стирание
  источника не нужно).
- Библиотека: `library/std/secret` (одна операция на файл), `r_std_secret.h`
  с `RStdSecretBuffer = RRuntimeArray` и static inline glue
  `r_std_secret_buffer_move_initialize/destroy`; destroy стирает всю capacity
  через volatile stores и затем освобождает. `constant_time_equal` использует
  volatile-аккумулятор без data-dependent branch; разная длина даёт false.
  Unit-тест `tests/library_secret_tests.c`: инъекция отказа аллокации
  (`r_runtime_allocator_set_failure`), size_overflow, проверка стирания
  length и capacity, zeroize, сравнение (равная длина с разными байтами,
  разная длина, пустые срезы).
- Компилятор: тип `std.secret::buffer` — Send и не Sync (capability), семь
  операций зарегистрированы в semantic/MIR/HIR/C17 (sync и async), link-plan
  содержит `std.secret`; тесты `codegen_secret_buffer`,
  `codegen_async_secret_buffer` (плюс `_format`), `secret_send` (accept),
  `secret_not_sync` (reject `R-DIAG-ASYNC-001`: shared borrow не Send).
- Попутно исправлена скрытая ошибка эмиттера C17: cleanup-блоки effect-exit
  (drop локалов на пути checked-ошибки) не проходили preflight, поэтому для
  локала, позже перемещённого в вызов (`bytes` → `from_bytes`), drop glue не
  объявлялся; воспроизводилось и на `std.string::from_bytes`.
- Ограничение вне M3: в async-функциях срезы фиксированных массивов
  (`u8[N]` → `u8[]`) не lowerable в C17 (то же для `std.bytes::fill`);
  async-фикстура использует views buffer.
- Пины редакции: target-манифест `targets/arm64-apple-darwin.hosted-native-async.json`
  теперь ссылается на библиотечную редакцию 0.1.0-draft.10, поэтому
  `runtime/include/r_runtime_target_abi.h` перегенерирован
  (`tools/generate_target_abi.py`, новый SHA-256 манифеста);
  `tools/check_target_manifest.py`, `tools/generate_rule_inventory.py` и
  `tests/tooling/test_spec_contracts.py` ожидают 193 библиотечных правила.
- Закрытые каталоги: инвентарь runtime entry stack
  (`targets/arm64-apple-darwin.hosted-native-async.runtime-entry-stack.json`)
  перегенерирован с ревизией 15 (747 внешних входов, 104 static inline
  helper'а), `tools/generate_runtime_entry_stack.py` знает модуль `secret`;
  реестр named-move ABI — 49 записей и 12 заголовков (пины в
  `tests/codegen_tests.c`, fixture `codegen_named_move_abi.r` получила
  pass-through `std.secret::buffer`); регрессия эмиттера закрыта тестом
  `codegen_moved_local_effect_exit`.

Гейт M3 закрыт на этой стадии.

## 7. M4, M5, M6

- M4: атрибут модуля `@deny_panic_alloc` и опция `--deny-panic-alloc`;
  диагностика `R-DIAG-ALLOC-001` на языковое `new`, `new arc`, `new rc` и
  любую операцию, чей inventory помечен panic-аллокацией. Позитивный путь:
  `std.alloc::try_new`.

  Статус M4 (11 сентября 2026): реализовано.

  - Спецификация языка (EN+RU): правило R-OBJ-0012 (атрибут модуля
    `@deny_panic_alloc`, translation-wide опция, диагностика на `new`/`new arc`/
    `new rc` и на операции с inventory-пометкой panic-аллокации), грамматика
    `module-declaration = { attribute }, "module", ...`, R-GRAM-0007 расширен,
    строка `R-DIAG-ALLOC-001` в Annex B; редакция Core поднята до
    0.1.0-draft.33 (439 правил), grammar manifest и target ABI header
    перегенерированы, target-манифест ссылается на draft.33.
  - Инвентарь: у каждой операции поле `panics_on_allocation_failure`
    (закрытый каталог `PANIC_ALLOCATION_OPERATIONS`, по R-LIB-0007 пуст);
    сгенерированный реестр операций несёт флаг, компилятор проверяет его в
    точке диспетчеризации std-вызовов.
  - Компилятор: парсер принимает атрибуты перед `module` (сканер интерфейса и
    lookahead translation unit), семантика хранит флаг источника и контекста,
    `r_frontend_set_deny_panic_alloc`, CLI `--deny-panic-alloc`;
    `R-DIAG-ALLOC-001 [R-OBJ-0012]` на `new`, misplaced/duplicate атрибут —
    `R-DIAG-SYN-002`.
  - Тесты: `deny_panic_alloc_{new,arc,rc}` (reject), `deny_panic_alloc_option`
    (accept без опции / reject с опцией), `deny_panic_alloc_try_new` и
    исполняемый `codegen_deny_panic_alloc` (позитивный путь через
    `std.alloc::try_new`), `deny_panic_alloc_{misplaced,duplicate}`.
  - Гейт: `tools/audit_library_source_surface.py --deny-panic-alloc` — вся
    закрытая поверхность библиотеки резолвится под политикой без отказов.
- M5: `tests/bench/` с парами R/C программ; метрики: пустой вызов со stack
  preflight, индексирование в цикле, checked-арифметика; отчёт в
  `docs/benchmarks.md`. Решение о том, какие горячие пути держать в C,
  принимается по этим числам.

  Статус M5 (11 сентября 2026): реализовано.

  - `tests/bench/`: пары `baseline`, `call_overhead`, `index_fixed`,
    `index_slice`, `checked_arith` (`.r` + `.c` с одним алгоритмом и общей
    контрольной суммой в коде завершения). R-программы собираются через
    `tests/check_codegen_program.cmake` в режиме `COMPILE_ONLY` на `-O2`
    (stack fixed point, границы кадров с `-O0`), C-зеркала — тем же
    компилятором и флагами; харнесс `tests/bench/run_benchmarks.py`.
  - Цель CMake `benchmarks` пишет `docs/benchmarks.md` и `benchmarks.json`
    (медиана из 7 запусков, вычет `baseline`, нс/итер, Δ, R/C); ctest
    `r_bench_pairs_agree` собирает пары и сверяет коды завершения.
  - Первые числа (Apple M4, Apple clang 21, `-O2`, медианы 7 запусков): вызов
    функции R с preflight в точке вызова — 1.54 нс против 1.04 нс в C
    (R/C ≈ 1.5, Δ ≈ +0.5 нс на вызов; между прогонами 1.48–1.61);
    индексирование `u32[4096]` напрямую и через срез — паритет
    (0.72 нс/элемент, проверки границ убраны clang); checked i64-арифметика —
    1.00 против 0.98 нс/итер (R/C ≈ 1.02). Решение о горячих путях
    остаётся за владельцем; отчёт в `docs/benchmarks.md`.
- M5b: статический стек — ацикличный граф вызовов, бюджет стека на точку
  входа, снятие per-call preflight; итеративный drop самовложенных
  владеющих типов. Решение принято после обсуждения M5: динамическая
  проверка на каждом вызове (+0.5 нс) заменяется статической гарантией.

  Статус M5b, часть A (11 сентября 2026): реализовано.

  - Спецификация Core (EN+RU): R-FUNC-0004 переписан — граф вызовов функций
    R (включая drop-хуки через владение, initializers `call_once`/
    `get_or_init` и async-старты) shall быть ацикличным, ни один агрегат не
    достигает себя через владеющее хранилище, implementation выводит
    статический worst-case на каждую точку входа и проверяет его один раз;
    R-FUNC-0007 — вызов через `raw fn` трактуется как вызов каждой `@callback`
    функции с совпадающей сигнатурой; новая диагностика `R-DIAG-STACK-001` (Annex B);
    редакция 0.1.0-draft.34 (439 правил, пины и ABI-заголовок обновлены).
  - Манифест `core.stack`: policy `static-entry-bound`, `entry_budget_bytes`
    393216, per-call и glue-gates без проверок; `check_target_manifest.py` и
    `generate_target_abi.py` пинят новый контракт.
  - Компилятор: семантический проход `r_semantic_validate_stack_graph`
    (SCC по функциям с сообщением-цепочкой `f -> g -> f`, цикл владения по
    агрегатам); эмиттер снимает preflight с вызовов R→R и type-glue gates,
    оставляет одну проверку `R_STACK_ENTRY(<entry>)` на входах (main, entry
    потока, async step/helper gates, call_once initializer, C callback),
    маркирует косвенные вызовы `/* R_STACK_INDIRECT: ... */` (перечисляются только
    `@callback` с совпадающей сигнатурой, R-FFI-0025); drop-хуки попадают в граф только через
    владеемое хранилище (объявления, параметры по значению, временные, `move`, замена при
    присваивании), place-выражения и borrow рёбер не дают.
  - Фикстуры `codegen_json_stream.r`/`codegen_async_json_stream.r`: рекурсивный
    `struct Node { ...; array<Node> children; }` заменён на пару `Leaf`/`Node`, так как
    derived JSON самовложенных типов отклоняется (см. часть C).
  - Pipeline: `tools/compute_stack_entries.py` считает `R_STACK_ENTRY_<entry>`
    = кадр + самый длинный ацикличный путь по графу сгенерированного C
    (из `.su`), дописывает в заголовок fixed point, падает при превышении
    бюджета; интегрирован в `check_codegen_stack_usage.cmake`.
  - Тесты: негативы `stack_recursive_{direct,mutual,container,
    async}` и прежний `codegen_recursive` (теперь reject), позитив
    `codegen_stack_entries`; юнит-тест codegen и артефакт-проверки обновлены
    (glue-gates без preflight, entry-gates с `R_STACK_ENTRY`).
  - Что потерял клиент: рекурсию функций (прямую, взаимную, async),
    самовложенные владеющие типы (`own Node*?`, `list<Tree>`) до части B.
  - Части B и C — см. статусы ниже.

- Статус M5b, часть B (11 сентября 2026): реализовано.
  - Спецификация: R-FUNC-0004 разрешает самовложение агрегата только через
    члены `own`/`rc`/`arc` с агрегатом-указуемым (опционально внутри одного
    `o<...>`) и by-value члены struct/enum; остальные маршруты (контейнеры,
    `r`, fixed arrays, `o` от агрегата, стандартные типы, вложенные обёртки) —
    `R-DIAG-STACK-001`. Разрушение самовложенного агрегата следует R-INIT-0010
    с глубиной стека, не зависящей от данных. Манифест
    `core.stack.preflight.call_graph` обновлён (ABI-заголовок перегенерирован),
    редакция остаётся 0.1.0-draft.34.
  - Семантика: вместо запрета циклов владения — SCC по графу агрегатов с
    метками рёбер DIRECT/HEAP/OTHER (`r_stack_validate_type_graph`, итеративный
    Тарьян); компонента с ребром OTHER отклоняется, остальные получают
    `RSemanticAggregate.recursive_group`.
  - Runtime: `runtime/source/drop.c` + `r_runtime_drop.h` —
    `r_runtime_drop_iterative` (обход в стиле Дойча–Шорра–Уэйта: состояние
    возврата хранится в словах `size`/`alignment` записи типа исчерпанного слота
    `own` или control block `rc`/`arc`), протокол
    `r_runtime_{rc,arc}_destroy_{begin,value,scratch,finish}`; хуки pre-order,
    освобождение post-order, порядок членов как у рекурсивного glue.
  - Эмиттер: для агрегатов группы — `r_type_cursor_a<id>` (описание члена по
    индексу: hook, LEAF, OWN/RC/ARC, `o<...>`-слот, делегирование by-value
    члену), `r_type_cursor_count_a<id>`, дескриптор `r_type_node_a<id>`, обёртка
    хука `r_type_hook_a<id>`; drop-glue группы — один вызов деструктора с
    маркером `/* R_STACK_INDIRECT: ... */` для инструмента границ; замыкания glue
    помечаются на всю группу и на рекурсивные указуемые; `(void)x;` не считается
    ребром вызова в `compute_stack_entries.py`.
  - Тесты: runtime `r_runtime_iterative_drop` (цепочки 10^6 узлов own/rc/arc,
    дерево 500 000 узлов, порядок хуков); codegen
    `stack_recursive_{own,rc,arc,enum,value}` (списки в 10^6 узлов, enum-payload,
    взаимное by-value вложение) с проверкой порядка и числа хуков; негатив
    `stack_recursive_wrapper` (fixed array владельцев), `stack_recursive_container`
    сохранён; инвентарь runtime entry stack rev. 16 (748 записей); бенч-гарнесс и
    `check_codegen_program.cmake` линкуют `drop.c`.
  - Что потерял клиент относительно части A: ничего — самовложенные владеющие
    типы через `own`/`rc`/`arc` снова доступны. По-прежнему запрещены рекурсивные
    вызовы; самовложение через контейнеры снято в части C.
  - Бенчмарки после M5b (`docs/benchmarks.md`, Apple M4, -O2, медианы 7 запусков):
    `call_overhead` R/C 0.99 (Δ −0.02 нс/вызов; при per-call preflight было 1.48 и
    +0.50 нс), `index_fixed` 1.00, `index_slice` 0.99, `checked_arith` 1.04.
  - Проверка: полный ctest 1388/1388, санитайзерный набор 130/130 (включая
    `r_runtime_iterative_drop` и codegen-кейсы `stack_recursive_*`), четыре аудита,
    ворота формата/стиля/спецификации/библиотеки — зелёные.

- Статус M5b, часть C (11 сентября 2026): реализовано.
  - Спецификация: R-FUNC-0004 разрешает самовложение через `own`/`rc`/`arc`, `o<...>`,
    fixed arrays, элементы `array`/`list`, значения `dict` и by-value struct/enum в любой
    вложенности; отклоняются маршруты через ключ `dict`, стандартный тип, `task`, atomic и
    derived JSON encoder/decoder самовложенного агрегата (его glue рекурсирует по данным).
    Манифест `core.stack.preflight.call_graph` обновлён (ABI-заголовок перегенерирован).
  - Runtime: протоколы `r_runtime_{array,list,dict}_destroy_*` (count/element/locate/
    scratch/finish; список освобождает узлы `release_last`, словарь дропает ключ после
    значения); деструктор обходит контейнеры как последовательности элементов-узлов
    (последний элемент первым, R-INIT-0010), состояние возврата — в словах `size`/
    `alignment` записи типа элемента в заголовке контейнера; члены курсора отдают
    абсолютные адреса.
  - Семантика: метки рёбер SUPPORTED/OTHER (`r_stack_type_collect`); флаг
    `RSemanticAggregate.json_derived` выставляется в JSON-визиторах операций и
    отклоняется для рекурсивных групп.
  - Эмиттер: единая модель курсоров по типам — агрегатные курсоры делегируют любой член,
    достигающий рекурсивного агрегата, курсору его типа; для обёрток генерируются
    `r_type_cursor_t<id>`/`r_type_cursor_count_t<id>`/`r_type_node_t<id>` (owners и
    контейнеры — один член-слот/последовательность, `o<...>` и fixed arrays —
    делегирование payload/элементам); замыкание маркера `R_STACK_INDIRECT` по всем
    достижимым курсорам; замыкания glue помечаются по всем поддерживаемым обёрткам.
  - Тесты: runtime `r_runtime_iterative_drop` (+ лес массивов, массив владельцев, список,
    словарь по 250 000 вложений); codegen `stack_recursive_{array,list,dict,fixed,option,
    owner_array}` (200 000–250 000 вложений, порядок и число хуков); негативы
    `stack_recursive_standard` (`std.sync::once_lock`) и `stack_recursive_json`;
    прежние негативы `container`/`wrapper` стали позитивами; json-фикстуры остаются на
    паре `Leaf`/`Node` (derived JSON рекурсивных типов запрещён).
  - Что потерял клиент: derived JSON для самовложенных типов (обход — потоковый
    `std.json::reader`/`decoder` с явным циклом); всё остальное из части C возвращено.
  - Проверка: полный ctest 1400/1400, санитайзерный набор 136/136, четыре аудита,
    ворота формата/стиля/спецификации/библиотеки, `runtime-entry-stack-inventory-check` —
    зелёные.

- M6 (2026-09-11, выполнено): классификация всех сайтов `R-DIAG-SLICE-001`
  (71 → 27 сайтов, 61 → 24 уникальных сообщения), Core-редакция 0.1.0-draft.35.
  - Реализовано: `panic(message)` (R-ERR-0004, категория `explicit`, sync и async
    кадры; `R_STANDARD_CALL_CORE_PANIC`), expression statement типа `never` завершает
    путь (R-TYPE-0029/R-FUNC-0003, R-STMT-0001 уточнено), открытые диапазоны
    `a[lo..]`/`a[..hi]`/`a[..]` (HIR SLICE с двумя детьми, MIR operand2 invalid),
    константные выражения модульных констант (литералы, ранние константы, унарные и
    бинарные целые операции; переполнение → `R-DIAG-CONST-001 [R-INIT-0002]`), унарные
    `+ - ~` над малыми целыми, сравнения `char` всеми операторами, преобразования
    `char`↔integer и fieldless `@repr(C)` enum↔integer (`r_c17_cast_integer_kind`,
    проверка скалярного значения Unicode и объявленного дискриминанта через `switch`,
    паника `invalid_conversion`).
  - Переведено в нормативные диагностики: неизвестные имена типов (R-DIAG-NAME-001),
    `void`-поля, атрибуты агрегатов, вызов не-функции, `await` в borrow, `drop` view,
    состояния try/catch, нечисловые `as` (R-EXPR-0016); недостижимые ветки —
    `r_body_invariant`/`r_body_cascade`.
  - Оставшиеся 27 сайтов записаны как ограничения в §25.1 спецификации (контейнеры и
    сигнатуры с borrow/slice, `atomic raw T*?`, невычислимые формы модульных констант,
    FFI-агрегаты и зарезервированные имена в импортах, staged-Move ABI для
    list/dict/rc/weak/task, borrow-провенанс callee без тела, Move вне поддерживаемых
    форм, откат async start через try, pointer↔integer `as`, `fallthrough` в tagged
    switch).
  - Тесты: codegen `panic`, `async_panic`, `scalar_casts`, `async_scalar_casts`,
    `char_compare`, `char_cast_failure`, `enum_cast_failure`, `async_enum_cast_failure`,
    `open_ranges`, `fixed_array_move`; семантические `never_statement`,
    `panic_message_type`, `cast_operands` (метка `normative`); приёмочная матрица 51/51.
  - Проверка: полный ctest 1423/1423, санитайзерный набор 136/136, четыре аудита
    (`semantic slices: 27 call sites, 24 unique messages`), ворота формата/стиля/
    спецификации/библиотеки — зелёные; юнит-ожидания обновлены (`bool as i32` →
    R-DIAG-TYPE-001, мутация async MIR slice «нет нижней границы» вместо «нет верхней»).

## 8. M7 — Freestanding core-runtime и манифест (R-CONF-0005, Annex G.3, R-LIMIT-0005)

Цель: программа R в профиле `freestanding` компилируется в строгий C17 без
хостового runtime, линкуется только с ядром runtime и средой, которую даёт
встраивающий код. Среда
поставляет обработчик паники и границы стека потока; ядро runtime не
аллоцирует, не делает ввод-вывод и не создаёт потоков (R-LIB-0003).

Контракт среды (`runtime/freestanding/include/r_runtime_freestanding.h`):

| Символ | Кто определяет | Назначение |
| --- | --- | --- |
| `r_runtime_environment_panic(category, span)` | среда | получает каждую панику, не возвращается (Annex G.3 «handler supplied by environment») |
| `r_runtime_freestanding_stack_adopt(low, high)` | ядро runtime | поток принимает границы своего стека до первого входа в R; `R_RUNTIME_STACK_PROTECTED_LOW_BYTES` над `low` не используются |
| `r_runtime_freestanding_stack_release()` | ядро runtime | забывает границы; дальнейшие входы — паника `stack_exhaustion` |
| `r_runtime_freestanding_thread_exit()` | ядро runtime | выполняет установленный программой деструктор thread-local объектов |
| `r_freestanding_main()` | генерируемый C | вход программы с R `main`: preflight стека, статические объекты, вызов `main`, drop статических объектов; `@callback`-функции — остальные входы из C |

Что входит:

1. Разделение заголовков: `runtime/include/r_runtime_core.h` — категории паники,
   `RRuntimeSourceSpan`, `r_runtime_panic`, preflight стека, установка
   thread-local деструктора; `r_runtime_0_1.h` включает его и хранит только
   хостовые объявления. Ядро runtime `runtime/freestanding/source/{panic,stack,
   thread_local}.c` собирается с `-ffreestanding` в `r_runtime_freestanding`.
2. Манифест `targets/arm64-apple-darwin.freestanding.json`: те же данные
   C ABI, что у хостового (общие секции проверяются на равенство), стратегия
   паники `environment-handler`, границы стека `environment`, аллокатор
   недоступен, потоки — среда. `tools/check_target_manifest.py --manifest`
   проверяет оба манифеста; `tools/generate_target_abi.py` генерирует для него
   `runtime/freestanding/include/r_runtime_target_abi.h` (без `<wchar.h>` и
   константы аллокатора); порядок include при сборке freestanding —
   `runtime/freestanding/include` раньше `runtime/include`.
3. Компилятор: в профиле `freestanding` генерируемый C включает
   `r_runtime_core.h` и `r_runtime_freestanding.h` вместо `r_runtime_0_1.h`, не
   генерирует хостовый `main`, не оборачивает входы из C и вызовы в C в
   guard'ы плавающего окружения (`floating_environment.mode =
   environment-managed`), `main` может отсутствовать. Семантика дополнительно
   отклоняет в `freestanding` типы `own` и `core::adopt` (без аллокатора
   владелец не может быть ни создан, ни освобождён).
4. Тесты: `r_runtime_freestanding` (юнит ядра: категории, границы, паника через
   среду, thread-local хук), `tests/check_freestanding_program.cmake`
   (генерация, компиляция с `-ffreestanding`, список неопределённых символов
   объекта строго в allowlist, линковка с хостовой средой-стендом
   `tests/freestanding/environment.c`, запуск, перехват паники), фикстуры
   `freestanding_program.r`, негативы профиля `own`/`core::adopt`.

Гейт: `ctest` зелёный, `specification-check` проверяет оба манифеста,
`nm -u` объекта freestanding-программы не содержит символов libc кроме
`memcpy`/`memset`.

Статус (12 сентября 2026): реализовано.
- Runtime: `runtime/include/r_runtime_core.h` выделен из `r_runtime_0_1.h`;
  `runtime/freestanding/{include/r_runtime_freestanding.h, source/panic.c, stack.c,
  thread_local.c}` собираются в `r_runtime_freestanding` с `-ffreestanding`;
  юнит `r_runtime_freestanding` (категории, границы, паника через среду,
  thread-local хук) в `tests/runtime_freestanding_tests.c`.
- Манифест `targets/arm64-apple-darwin.freestanding.json`; `check_target_manifest.py`
  ветвится по `identity.profile` и сверяет общие секции с хостовым манифестом;
  `generate_target_abi.py` знает профиль (заголовок без `<wchar.h>`, без константы
  аллокатора, без проверки `wint_t`) и генерирует таблицу дайджестов
  `compiler/codegen/target_manifest_digests.generated.inc`, по которой эмиттер
  привязывает компиляцию к манифесту выбранного профиля (хостовые профили — к
  hosted-native-async). Оба манифеста и оба заголовка входят в `specification-check`;
  инвентарь входов стека пересчитан (revision 17).
- Компилятор: режим `r_c17_freestanding` (включения `r_runtime_core.h` /
  `r_runtime_freestanding.h`, `r_freestanding_main`, без guard'ов C-входов и C-вызовов,
  без стандартных заголовков и `<wchar.h>`); семантика отклоняет `own` и
  `core::adopt` в `freestanding` (`profile_freestanding_{own,adopt}`).
- Тесты: `tests/check_freestanding_program.cmake` + `tests/freestanding/environment.c`
  + `tests/fixtures/freestanding_program.r` (`@callback`-входы, `r_freestanding_main`,
  drop статического объекта, паника `bounds` и `stack_exhaustion`, перехваченные средой;
  `nm -u` ⊆ `allowed_undefined_symbols.txt`, где на Darwin к `memcpy`/`memset`
  добавляется `__tlv_bootstrap` для `_Thread_local`); tooling-тесты
  freestanding-манифеста и заголовка.
- Ограничение: обработчик паники среды на Darwin-стенде возвращается через
  `longjmp`; настоящая встраивающая среда решает сама (аварийное завершение, перезапуск). Stack bounds
  для потоков среды принимаются вручную через `r_runtime_freestanding_stack_adopt`.
- Проверка: полный ctest 1427/1427, санитайзерный набор 138/138, четыре аудита,
  ворота формата/стиля (998 файлов)/спецификации (оба манифеста и оба
  ABI-заголовка)/библиотеки, `runtime-entry-stack-inventory-check` — зелёные.

## 9. M8 — Методы с явным `this` и трейты (R-FUNC-0013, R-FUNC-0014, R-TYPE-0041 — R-TYPE-0043, R-NAME-0010)

Первый срез работы над выразительностью языка, выбранной по итогам SWOT-анализа.
Дорожная карта целиком: M8 методы и трейты → M9 локальные лямбды и замыкания →
M10 протокол итератора и `for (T x in ...)` → M11 выражения-коллекции →
M12 слой стандартной библиотеки на самом R. Решения владельца от 2026-09-12:
замыкания статические и мономорфные, захват по неявному заёму плюс явный `move(...)`,
трейты только со статической диспетчеризацией, новые контейнеры пишутся на R.

### Что реализовано

- **Методы.** `Ret Owner::name(receiver, params)` в модуле, определяющем `Owner`.
  Receiver — первый параметр с именем `this` и ровно одной из форм `const Owner*`,
  `Owner*`, `Owner`. Объявление без receiver — ассоциированная функция
  (`Point::origin()`). Для generic-владельца заголовок `@generic` повторяет параметры
  схемы владельца префиксом. Имя метода не совпадает с именем поля или варианта.
- **Вызов.** `value.name(args)` и `pointer->name(args)` передают receiver первым
  аргументом; вид заёма берётся из объявленной формы. Suffix вызова метода —
  последний суффикс выражения, аргументы остаются call-free: `f(p.name())` и
  `a.b().c()` не разбираются (осознанная цена сохранения R-EXPR-0020).
- **Трейты.** `trait Name { прототипы };` над `Self`; `impl Name for Type { … };`
  для nominal, standard и built-in типов, включая скаляры (`impl Shown for i32`).
  Orphan-правило, единственность impl на пару, точное совпадение сигнатур после
  подстановки `Self`, новый код `R-DIAG-TRAIT-001`.
- **Ограничения-трейты.** `@generic<T: Shown & copy>`; вызов метода на `T`
  разрешается через ограничения, мономорфизация связывает вызов с конкретной impl.
  Vtable, `dyn` и runtime-метаданные отсутствуют; в сгенерированном C только прямые
  вызовы, поэтому граф вызовов R-FUNC-0004 остаётся точным. (С Core draft.59 есть
  заимствуемые dyn-интерфейсы над конечным набором реализаций, R-TYPE-0051; граф вызовов
  остаётся точным, см. [план языка](language-completeness-roadmap.ru.md), L5.)

### Как это устроено внутри

Трейт — это схема с единственным параметром `Self`, а его прототипы — открытые
generic-функции без тела. Поэтому существующая машинерия дженериков
(`r_generic_substitute_type`, `r_generic_apply_function`, `r_generic_clone_node`)
переиспользована целиком: вызов метода на `T` создаёт инстанс прототипа, а при
мономорфизации `r_generic_bind_trait_call` подменяет символ на метод выбранной impl.
Новый файл `compiler/semantic/traits.inc` содержит сбор трейтов и impl, разрешение
`Self`, таблицу методов, проверку impl и понижение вызова метода. Хвост
`r_body_lower_call` вынесен в `r_body_lower_resolved_call`, который принимает уже
понижённый receiver как нулевой аргумент; `r_generic_infer_call` выводит типовые
аргументы в том числе из receiver.

### Что не входит в срез

Тела методов трейта по умолчанию, generic-трейты, ассоциированные типы,
супертрейты, trait objects и динамическая диспетчеризация. Ассоциированные типы
нужны M10 для `Iterator::Item` и будут добавлены расширением синтетической схемы
`[Self]` до `[Self, Item…]`.

### Проверка

- Спецификация 0.1.0-draft.36, 445 правил (было 439): шесть новых правил, амендменты
  R-TYPE-0017/0031/0033/0037/0040, R-FUNC-0009, R-OWN-0005, R-EXPR-0020,
  R-BORROW-0001, R-NAME-0001, строка Annex B `R-DIAG-TRAIT-001`, Annex A (A.3, A.4,
  A.6), паритет EN/RU.
- Интерфейсная схема 5 → 6: записи `(trait …)` и `(impl …)`, trait-ограничения в
  `generic=(…)`.
- Тесты: codegen `methods`, `traits`, `generic_methods`, `async_methods`,
  `method_example`; десять нормативных фикстур (accept + девять reject);
  `method_syntax` и `method_recovery` в `tests/frontend_tests.c`; пример
  `examples/methods`; пробы приёмки 51 → 57.
- Проверка: полный ctest 1450/1450, санитайзерный поднабор 153/153 (с методами,
  трейтами и нормативными фикстурами), четыре аудита (приёмка 57/57, безопасность
  101/101, срезы 27/24, поверхность библиотеки 879/879), ворота формата/стиля
  (998 файлов)/спецификации/библиотеки, `runtime-entry-stack-inventory-check` —
  зелёные.

## 10. M9 — Локальные лямбды и замыкания (R-FUNC-0015 — R-FUNC-0017, R-TYPE-0044, R-NAME-0011)

### Что реализовано

- **Лямбда** — именованное локальное объявление `fn Ret name(params) [move(a, b)] { … }`
  внутри блока; не выражение и не аргумент. Имя входит в область после объявления,
  поэтому лямбда не может ссылаться на себя; тело — обычная функция в графе вызовов.
- **Захваты.** Свободные имена тела, разрешающиеся в локали/параметры внешнего тела,
  захватываются: по значению, если перечислены в `move(...)` (внешняя локаль
  потребляется), иначе по заёму, сформированному в точке объявления: exclusive, если
  тело присваивает имени, инкрементирует, берёт адрес или вызывает на нём метод, shared
  иначе. Окружение держит заёмы до последнего использования имени лямбды — правила
  раздела 13 действуют (конфликт присваивания при живом exclusive-захвате
  диагностируется).
- **Вызов.** `name(args)` — прямой вызов тела с окружением в роли receiver (exclusive,
  если есть exclusive-захват). Захваченная лямбда вызывается так же.
- **Callable-ограничение.** `@generic<F: fn(i32) -> i32>`; внутри `f(x)` на `const F*`
  или `F`; замыкание передаётся как `&name`. Сигнатура — точное совпадение типов.
- **`auto`.** `auto x = initializer;` — тип локали из инициализатора (unsuffixed
  integer literal → `i32`); единственный способ хранить замыкание вне имени лямбды.

### Как это устроено внутри

Замыкание = анонимная структура-окружение (поля — захваты, `$closureN`) плюс метод
`call` с receiver-окружением: переиспользована вся машинерия методов M8.
Callable-ограничение = синтетический трейт, ключ которого — интернированная сигнатура
`fn(P…) -> R` (тип `RAW_FUNCTION` с флагом `CALLABLE`, никогда не тип значения), с
единственным прототипом `call`; `call` лямбды регистрируется как impl этого трейта,
мономорфизация подменяет вызов прототипа телом лямбды. В сгенерированном C —
обычные функции с параметром `struct r_aNN *` и только прямые вызовы. Новый файл
`compiler/semantic/closures.inc`; вынесены помощники `r_body_symbol_place`,
`r_body_move_from_place`, `r_body_form_deref_place`, `r_body_declare_local`;
`r_hir_build_function` создаёт скрытый параметр окружения; `r_body_resolve_name`
возвращает `R_BODY_LOOKUP_CAPTURE` для захватов.

### Что не входит в срез

Лямбды внутри generic-определений и внутри async-кадров (диагностика), лямбды,
возвращающие заёмы, замыкания в полях структур, `throws` у лямбд, изменение
захваченного по значению поля внутри тела.

### Проверка

- Спецификация 0.1.0-draft.37, 445 → 450 правил; Annex A (A.2 `auto`, A.3
  `callable-constraint`, `auto` в object-declaration, A.5 `lambda-declaration`), Annex B.
- Тесты: codegen `closures` (прямые вызовы, exclusive/shared/move-захваты, callable-
  ограничения с одним и двумя параметрами, вложенный захват лямбды, `auto`);
  нормативные `semantic_closures_accept`, `semantic_closure_constraint_unsatisfied`,
  `semantic_closure_moved_capture`, `semantic_closure_borrow_conflict`,
  `semantic_lambda_in_generic`, `semantic_auto_storage`; парсерные `lambda_syntax`,
  `lambda_recovery`; пробы приёмки 57 → 61.
- Санитайзер нашёл use-after-free на указателях в таблицу типов (интернирование
  переаллоцирует её); типы теперь копируются по значению до интернирования.
- Проверка: полный ctest 1458/1458, санитайзерный поднабор 160/160 (с замыканиями),
  пробы приёмки 61/61, безопасность 101/101 (в том числе санитайзерным компилятором),
  срезы 27/24, поверхность библиотеки 879/879, ворота формата/стиля (998 файлов),
  спецификации, библиотеки и entry-stack — зелёные.

## 11. M10 — Протокол итератора, range-for, диапазоны, принадлежность, ассоциированные типы (R-STMT-0014, R-EXPR-0029, R-TYPE-0045, R-TYPE-0046)

### Что реализовано

- **Ассоциированные типы трейтов.** `type Item;` в трейте, `type Item = T;` в impl,
  `Self::Item` внутри трейта и impl, проекция `P::Item` на ограниченном параметре
  дженерика. Трейт стал схемой `[Self, A1…An]`; проверка impl подставляет `[Target,
  bindings…]`; проекция — синтетический параметр схемы, который подстановка при
  инстанциации разрешает через impl подставленного основания.
- **Core-трейты.** `core::Iterator { type Item; o<Self::Item> next(Self* this); }` и
  `core::Contains { type Item; bool contains(const Self* this, const Self::Item* value); }`
  объявляются компилятором при первом упоминании (программа без них не меняется);
  реализуются для номинальных типов модуля; в ограничениях пишутся `@generic<I: core::Iterator>`.
- **Range-for** `for (T x in iterable) { … }` (`auto` допустим): диапазон `lo..hi` одного
  целого типа (обе границы вычисляются один раз; присваивание переменной не влияет на
  обход); заимствованная последовательность `&place` — `[T; N]`, `T[]`, `const T[]`,
  `array<T>` (переменная `const T*`, `T*` или копия `T`; последовательность заимствована на
  весь цикл); `&list<T>` и `&dict<K, V>` через стандартные курсоры; стандартный курсор или
  реализация `core::Iterator` по значению (`move it`, результат вызова) либо через
  exclusive-заём (`&it`, параметр `I*`); цикл завершается на первом `o::none`.
- **Принадлежность** `a in b`, `a not in b` — лист условия `if`/`while` и обычный `bool`:
  диапазон (`a >= lo && a < hi`, `a` call-free), место `dict<K, V>` (`std.dict::contains`),
  реализация `core::Contains` (`b.contains(&a)`); операнды заимствуются shared. `in` —
  ключевое слово, `not` — контекстное слово перед `in`.

### Как это устроено внутри

Range-for понижается на общее ядро циклов: `r_body_lower_while` вынесено в
`r_body_lower_loop` с колбэками условия и тела, так что фикс-пойнт состояний объектов,
`break`/`continue` и дропы R-STMT-0004 работают без изменений. Скрытые локали
(`$counter`, `$limit`, `$sequence`, `$iterator`, `$next`) объявляются через
`r_body_declare_local` и именуются в C по символу (`r_hNNNNNNNN_MMMMMMMM`). Итераторный цикл
читает тег скрытого `o<Item>` через shared-заём (`R_HIR_VARIANT_TAG`), делает синтетический
`break` на `o::none` и переносит payload (`R_HIR_VARIANT_PAYLOAD`) в переменную цикла; MIR
и эмиттер C17 получили понижение этих узлов (`.r_tag`, `.r_payload.r_some`). Прототипы
трейтов получили консервативное отображение заёмов результата (все заимствующие
параметры), поэтому вызовы прототипов с зависимым результатом больше не упираются в
«slice 3». Интерфейс модуля — схема 7 (`associated=(…)`, `bindings=(…)`, `core::Name`,
`(projection …)`). Новый файл `compiler/semantic/iteration.inc`.

### Что не входит в срез

Ограничения на проекции (`where P::Item: Trait`), `str` и массивы в правой части `in`
без `core::Contains` (появятся в M12 вместе с `Equal`), первоклассный тип диапазона
`core::range`, intrinsic-impl `core::Iterator` для стандартных курсоров (range-for
обходит их напрямую), обход `dict` по значению, лямбды-итераторы.

### Проверка

- Спецификация 0.1.0-draft.38, 450 → 454 правил; Annex A (A.2 `in`, A.3
  `associated-type-declaration`/`-binding`, `core-trait-name`, A.4
  `associated-type-projection`, A.5 `for-in-statement`, A.6 `membership-expression`,
  `range-expression`), Annex B, пример в G.4.
- Тесты: codegen `range_for` (все формы, `break`/`continue`, generic по `core::Iterator`),
  `membership` (диапазоны, `char`, dict через место и заём, `core::Contains`, generic),
  `associated_types`; нормативные `semantic_range_for_accept`,
  `semantic_for_in_type_mismatch`, `semantic_for_in_not_iterable`,
  `semantic_for_in_move_required`, `semantic_membership_operand`,
  `semantic_membership_unsupported`, `semantic_impl_associated_missing`,
  `semantic_projection_unknown`; парсерные `range_for_syntax`, `range_for_recovery`;
  пробы приёмки 61 → 65.
- Проверка: полный ctest 1472/1472, санитайзерный поднабор 171/171 (с range-for,
  принадлежностью и ассоциированными типами), пробы приёмки 65/65, безопасность 101/101
  (в том числе санитайзерным компилятором), срезы 27/24, поверхность библиотеки 879/879,
  ворота формата/стиля, спецификации (паритет EN/RU, 454 правила, манифест грамматики 245
  продукций), библиотеки и entry-stack — зелёные. Ничего не закоммичено.

## 12. M11 — Выражения-коллекции, comprehension и однородные вариативные параметры (R-EXPR-0030, R-FUNC-0018)

### Что реализовано

- **Выражения-коллекции.** `[e1, e2, …]` и `[]` строят `array<T>`, `{k1: v1, …}` — `dict<K, V>`;
  тип берётся только из назначения (инициализатор, правая часть присваивания, инициализатор
  поля), элементы понижаются с типом элемента как контекстом. Понижение — скрытая локаль
  `$collection` (`std.array::create` / `std.dict::create`), `push`/`insert` на каждый элемент
  (поздняя запись с равным ключом замещает и дропает раннее значение) и перемещение скрытой
  локали в результат; эффекты `std.array::push_error<T>` / `std.dict::insert_error<K, V>`
  обязаны быть пойманы или объявлены (R-ERR-0001). `{}` остаётся агрегатным
  инициализатором; выражение-коллекция не call-free — не аргумент и не операнд `return`.
- **Comprehension.** `[x * x for (i32 x in 0..10) if (x % 2 == 0)]`,
  `{*k: len(*k) for (const str* k in &words)}`, любое число вложенных `for`/`if` после первого
  `for`; заголовок `for` — тот же, что у range-for (диапазон, `&последовательность`, курсоры,
  `core::Iterator`, `auto`), `if` — условие R-STMT-0002 с обычным слиянием состояний объектов.
- **Вариативные параметры.** `i32 sum(i32... values)` объявляет `const i32[] values`
  (последний параметр; функция обычная — не generic, не `async`, не `extern "C"`, не
  `@callback`). Вызов `sum(1, 2, 3)` собирает хвостовые аргументы в скрытый массив `[T; n]`
  вызывающего (`$pack`, дропается после возврата; Move-элементы — с `move`) и передаёт его
  shared-срез; `sum()` — пустой срез; `sum(...operand)` пересылает существующий срез,
  `array<T>` или фиксированный массив без копирования и является единственным упакованным
  аргументом. Результат вызова не может заимствовать из пакета (`R-DIAG-BORROW-002`).
  Интерфейс модуля — схема 8 (`variadic=true` у функций).

### Как это устроено внутри

Парсер: `R_SYNTAX_COLLECTION_EXPRESSION` (только в обычном режиме выражений, поэтому
call-free-позиции отвергают `[…]` синтаксически), `R_SYNTAX_DICT_EXPRESSION` /
`R_SYNTAX_DICT_ENTRY` (фигурный инициализатор переквалифицируется в dict, когда за первым
неназначенным элементом идёт `:`), `R_SYNTAX_COMPREHENSION_FOR` / `_IF`,
`R_SYNTAX_SPREAD_ARGUMENT`, `...` после типа параметра. Семантика — новый файл
`compiler/semantic/collections.inc`: план коллекции, создание/цель/эффекты, `push`
(`R_HIR_STANDARD_CALL` с выходами эффектов), `insert` (скрытая `$previous` + неявный дроп),
comprehension через `r_body_lower_for_in_parts` с колбэком тела, который дописывает
следующую clause или вставку прямо в операторы тела цикла — так вложенные clauses не
добавляют уровней блоков в C, а сгенерированный код остаётся clang-format-чистым. Вызовы
вариативных функций: `r_body_pack_variadic_arguments` строит `R_HIR_ARRAY_INIT` `[T; n]`,
скрытую локаль `$pack` и `R_HIR_SLICE` с origin заёма; вызов оборачивается в
`R_HIR_VALUE_SCOPE` (префикс — объявления, суффикс — дроп пакета). Эмиттер C17 получил
перенос строки `RStdArrayPushResult … = r_std_array_push(…)` по правилам clang-format.

### Что не входит в срез

Set-литералы и выражения-коллекции для `list<T>`, пустое dict-выражение (`{}` — агрегат),
генераторы (ленивые comprehension), вариативные параметры у generic/async/extern функций и
гетерогенные пакеты типов (M15), смешивание упакованных аргументов со spread в одном вызове.

### Проверка

- Спецификация 0.1.0-draft.39, 454 → 456 правил (R-EXPR-0030, R-FUNC-0018; амендменты
  R-EXPR-0020, R-FUNC-0001, R-STMT-0014, R-NAME-0011); Annex A (A.3 `parameter` с `...`,
  `dict-expression`, `dict-entry`; A.6 `collection-expression`, `comprehension-clause`,
  `comprehension-for`, `comprehension-if`, `argument`, `spread-argument`), 253 продукции;
  Annex B; пример в G.4.
- Тесты: codegen `collections` (литералы, comprehension с `if`, вложенные `for`, dict-литерал
  с заменой ключа, dict-comprehension) и `variadic` (упаковка, пустой пакет, spread,
  Copy-агрегаты); нормативные `semantic_collections_accept`, `semantic_variadic_accept`,
  `semantic_collection_no_destination`, `semantic_dict_into_array`,
  `semantic_comprehension_element_mismatch`, `semantic_spread_non_variadic`,
  `semantic_variadic_generic`, `semantic_variadic_argument_mismatch`,
  `semantic_variadic_result_borrow`; парсерные `collection_syntax`, `collection_recovery`;
  пробы приёмки 65 → 69.
- Проверка: полный ctest 1485/1485, санитайзерный поднабор 180/180 (регэксп дополнен
  `collections|variadic|comprehension|spread`; ASan поймал и помог исправить use-after-free в
  планировщике последовательностей range-for — указатель на тип переживал переаллокацию
  таблицы типов при обходе среза-параметра), пробы приёмки 69/69, безопасность 101/101 (в том
  числе санитайзерным компилятором), срезы 27/24, поверхность библиотеки 879/879 (20
  констант, 161 тип), ворота формата/стиля (998 файлов), спецификации (паритет EN/RU, 456
  правил, манифест грамматики 253 продукции), библиотеки и entry-stack — зелёные. Ничего не
  закоммичено.

## 13. M12 — Слой стандартной библиотеки на самом R (R-MOD-0002, R-SLIB-RSRC, R-SLIB-CMP/ITER/SET/DEQUE/HEAP/SORTED/SLICE/TEXT)

Решение владельца 2026-09-12: идти по порядку, M12 сразу после M11 (M13/M14 позже).

### Что показало исследование (проверено пробами на компиляторе после M11)

- Кросс-модульные трейты работают: `impl m.cmp::Ordered for Point` в другом модуле и
  `this->x.compare(&other->x)` через impl для `i32` из модуля трейта разрешаются.
- Не работало и было нужно для библиотеки на R: агрегаты с полями-заёмами в сигнатурах
  («outside semantic slice 2», пробел §25.1); квалифицированные имена трейтов в ограничениях
  `@generic<T: m.n::Trait>` (AST теряет `.`/`::`); ассоциированные функции generic-владельца
  `set<i32>::create()` (проверка выводимости R-TYPE-0036); вывод параметра из сигнатуры
  callable-ограничения (`U` из `F: fn(I::Item) -> U`) и callable-ограничения у
  generic-агрегатов/impl; параметры-заёмы у лямбд и callable-ограничений; `len(dict)`.
- Инструменты библиотеки знали только закрытый набор из 28 C-модулей с одним файлом на
  операцию; R-слою понадобился новый вид реализации `r_source`.
- Правила R, в которых пишется библиотека: условия требуют явного сравнения; аргументы вызова
  и операнд `return` call-free; тела clauses `switch` без фигурных скобок; payload-привязки
  `switch` — заёмы, для владения нужно `move v`; generic-значения передаются с `move`; `o` и
  `module` — ключевые слова; `len` — intrinsic (не имя метода); имена параметров и локалей не
  должны совпадать с функциями модуля и компонентами путей модулей; `while (true)` требует
  завершающего `return`; рекурсия запрещена (R-FUNC-0004) — сортировки и просеивания
  итеративные; `array<T>[i]` не индексируется (только `std.array::get`).

### Что реализовано

- **Механизм R-слоя (M12.1).** Карта `library/r/library.map` (`MODULE = PATH [PROFILE]`,
  пути относительно `library/r/`, профиль — минимальный кумулятивный, по умолчанию
  `freestanding`) и опция драйвера `--library-map FILE`: достижимый `import std.name;`
  модуля, которого нет ни в `--module-map`, ни в закрытой C-библиотеке, загружает исходник
  как библиотечный (`RSource.is_library`, `minimum_profile`); транзитивные импорты
  подгружаются так же; запрет R-MOD-0001 на библиотечные исходники не распространяется;
  импорт из профиля ниже карты — `R-DIAG-PROFILE-001` (R-CONF-G005). Трейты, типы, функции и
  методы модуля разрешаются как у любого импортированного модуля; регистры стандартных типов
  и операций компилятора именуют только распознаваемые компилятором элементы, промах регистра
  для загруженного библиотечного модуля уходит в обычное разрешение агрегатов;
  `std.name::Type<args>` разбирается как generic-тип, когда `std.name` — загруженный
  библиотечный модуль с таким generic-агрегатом. У модуля нет своего C-символа: его
  элементы попадают в C17 только через трансляцию использующей программы. Тесты получают
  `-DLIBRARY_MAP=library/r/library.map` (codegen, format, normative).
- **`std.cmp` (freestanding).** `enum ordering { less, equal, greater }`, трейты `Equal`
  (`eq`) и `Ordered` (`cmp`), impl для `bool`, `char`, всех целых, `f32`/`f64` (тотальный
  порядок), помощники `min`/`max`/`clamp`/`is_less`/`is_equal`.
- **`std.iter` (allocation).** Источники `of_slice(T)`, `range(i32)`, `range_of_usize`;
  адаптеры `map`, `filter_map`, `take`, `skip`, `enumerate` (`indexed<T>`), `zip`
  (`pair<L, R>`), `chain` — агрегаты-итераторы с `impl core::Iterator` и заёмом замыкания
  `const F*`; потребители `count`, `fold`, `any`, `all`, `position`, `find_map`, `last`,
  `nth`, `collect_array` (эффект `push_error`), `collect_list`.
- **Контейнеры (allocation).** `std.set::set<T: key>` над `dict<T, bool>` с итератором ключей
  `set_iter` поверх курсора dict; `std.deque::deque<T>` — две стопки над `array<T>` (`heads` в
  обратном порядке, `tails` в прямом, перебалансировка при опустошении стороны);
  `std.heap::heap<T: Ordered & copy>` — бинарная куча с итеративными просеиваниями;
  `std.sorted::set<K>` / `map<K, V>` над Copy-ключами — плоские сортированные массивы с
  бинарным поиском `lower_bound`.
- **`std.slice` и `std.text` (freestanding).** Срезы: `contains`, `index_of` (Equal),
  `first`, `last`, `min_of`, `max_of`, `is_sorted`, `binary_search` (Ordered), `swap`,
  `reverse`, `sift_down`, итеративный heapsort `sort` (Copy). Байтовый текст: `starts_with`,
  `ends_with`, `find`, `contains`, `count_byte`, `is_ascii`, `to_ascii_lower`,
  `equal_ignore_ascii_case`.
- **Компиляторные предпосылки (M12.0, Core).** Агрегаты с полями-заёмами/срезами в
  параметрах, receiver и результатах (аргумент-агрегат вносит origins своих полей,
  результат наследует консервативно; утечка — `R-DIAG-BORROW-002`, конфликт —
  `R-DIAG-BORROW-001`); квалифицированные трейты в ограничениях (группировка идентификаторов
  между `&`, разрешение по модулю; неизвестный — `R-DIAG-TYPE-001`); вывод префикса схемы из
  написания владельца ассоциированной функции; унификация сигнатуры callable-ограничения с
  сигнатурой замыкания (`U` выводится) и callable-ограничения в схемах агрегатов/impl;
  параметры-заёмы и срезы у лямбд и callable-ограничений (результат остаётся без заёмов);
  generic-аргумент `&fixed` к параметру-срезу образует срез целиком; скрытые стандартные
  схемы (`entry_ref`, `alloc_error`, `target_info`, `json`, `net`) готовятся по требованию
  при инстанциации; `len` для `list`/`dict`; диагностика смешивания spread и упаковки.

### Как это устроено внутри

Драйвер: `RModuleMapEntry.profile`, `r_cli_load_map_file(options, path, library)`,
`r_cli_read_source(..., library_profile, ...)`, обобщённый `r_cli_load_reachable_modules`
(индексный режим — только с `--module-map` и `--entry`; библиотечные записи подгружаются
транзитивно, зарезервированные корни пропускаются). Frontend API:
`r_frontend_add_library_source`, `r_frontend_profile_from_name`. Семантика:
`r_semantic_module_source`, проверка профиля в `r_semantic_validate_imports`,
`r_semantic_standard_type_from_ast` молча возвращает false для имён загруженных
библиотечных модулей; парсер — `r_visible_qualified_generic_aggregate` и
`r_skip_balanced_parentheses` для `std.m::Name<args>`. Заёмы в агрегатах:
`r_semantic_type_is_borrow_bearing_aggregate_input`, `r_semantic_collect_parameter_types`
с флагом `allow_aggregate_borrows`, `r_body_ast_postfix_uses_whole_symbol`, уточнение
конфликтов в `r_body_call_argument_access_conflicts`, очистка referent перемещённой локали.
Дженерики: группировка квалифицированных ограничений в `r_generic_register_headers`,
`r_generic_closure_signature` / `r_generic_infer_from_callables` /
`r_generic_effective_callable_trait`, засев префикса владельца в `r_generic_infer_call`,
фикспойнт выводимости в `r_generic_validate_definitions`,
`r_generic_lower_argument(..., pattern_type, ...)`; трейты —
`r_semantic_resolve_trait_name_in_module`, callable-фолбэк `r_generic_bind_trait_call`;
замыкания — прототип callable-трейта получает `r_semantic_prototype_borrow_mapping`,
`r_semantic_type_is_nonescaping_input`. Инструменты: `generate_library_inventory.py`
читает карту и объявления (`r_source_declaration`, `r_source_generic_arity`), пишет записи
модулей `implementation_language: "r"` и элементов `{kind: r_source, source, r_symbol,
generic_arity}`; `check_library_layout.py` проверяет, что исходник объявляет модуль и
элементы; `audit_library_source_surface.py` пробует элементы через `import` с картой;
`generate_standard_type_registry.py` и `generate_standard_operation_registry.py`
пропускают `r_source`.

### Что не входит в срез

C-примитив `std.array::swap` (поэтому `heap`, `sorted` и `std.slice::sort` требуют Copy),
индексирование `array<T>[i]`, `split`/`join` над массивами `str` (контейнеры заёмов — пробел
§25.1), `filter` без преобразования (выражается через `filter_map`), `min`/`max` итераторов,
B-дерево, расширения самих C-модулей `std.array`/`std.dict`/`std.string` R-файлами,
`impl Equal/Ordered for str` (`str` — заём).

### Проверка

- Спецификация ядра 0.1.0-draft.40, 456 правил (амендменты R-MOD-0002, R-TYPE-0036,
  R-TYPE-0043, R-TYPE-0044, R-FUNC-0013, R-FUNC-0015, §25.1); спецификация библиотеки
  0.1.0-draft.11, 193 → 210 правил (новый раздел 21 «Standard modules written in R»:
  R-SLIB-RSRC-0001..0002, CMP-0001..0002, ITER-0001..0003, SET-0001, DEQUE-0001, HEAP-0001,
  SORTED-0001..0002, SLICE-0001..0002, TEXT-0001; Annex A.1, Annex B `R-SLIB-MAP-B010`,
  R-SLIB-CONF-D019; амендмент R-SLIB-PROFILE-0001); паритет EN/RU.
- Тесты: codegen `library_cmp`, `library_iter`, `library_containers`, `library_slice_heap`,
  `library_sorted_text`, `borrow_fields`, `iterator_adapter`, `container_len`,
  `generic_set`; нормативные `semantic_library_import_accept`,
  `semantic_borrow_fields_accept`, `semantic_borrow_field_escape`,
  `semantic_borrow_field_conflict`, `semantic_qualified_constraint_accept`,
  `semantic_qualified_constraint_unknown`, `semantic_callable_signature_mismatch`; пробы
  приёмки 69 → 72 (`borrow_fields`, `iterator_adapter`, `generic_owner_spelling`).
- Инвентарь библиотеки: 36 модулей (28 C + 8 R), 946 элементов (66 `r_source`);
  поверхность библиотеки 924/924 операций (в том числе под `--deny-panic-alloc`), 20
  констант, 182 типа.
- Проверка: полный ctest 1510/1510 (тест инструментов инвентаря научен записям `r_source` без C-дескриптора и закрытому набору из 36 модулей), санитайзерный поднабор 278/278 (регэксп дополнен
  `library|borrow_field|iterator_adapter|container_len|generic_set|qualified_constraint|callable_signature`),
  пробы приёмки 72/72, безопасность 101/101 (в том числе санитайзерным компилятором),
  срезы 27/24, ворота формата/стиля (998 файлов), спецификации (паритет EN/RU, 456 + 210
  правил, манифест грамматики 253 продукции), библиотеки (раскладка, покрытие, инвентарь) и
  entry-stack — зелёные. Ничего не закоммичено.

## 14. M13 — Статическая рефлексия (R-REFL-0001..0004, R-LIB-0024)

Решение владельца 2026-09-12: «давай M13» сразу после M12. Принцип из раздела 15.2: только
время трансляции, без RTTI; закрытые имена `core::…`; работает в freestanding-профиле.

### Что реализовано

- **Константы рефлексии (call-free).** `core::enum_count(T)` (`usize`), `core::enum_min(T)` /
  `core::enum_max(T)` (варианты с наименьшим/наибольшим дискриминантом в знаковости
  underlying-типа), `core::enum_variants(T)` (`T[N]` в порядке объявления),
  `core::variant_count(T)` (tagged union), `core::field_count(T)`, `core::field_name(T, index)`
  (index — целочисленная константа; за пределами — `R-DIAG-CONST-001`), `core::type_name(T)`
  (каноническое написание: `module.path::Name<args>` для агрегатов, исходное написание
  для остальных типов). Формы с типовым операндом разбираются рядом со стандартными
  type-call (`R_SYNTAX_STANDARD_TYPE_CALL`) и допустимы там, где допустимы call-free
  выражения (аргументы вызовов), но не являются константными выражениями R-INIT-0002 в
  этой редакции.
- **Выборки времени выполнения (вызовы).** `core::enum_name(value)` (`constexpr str`),
  `core::enum_ordinal(value)` (`usize`), `core::enum_at(T, index)` и
  `core::enum_from_name(T, name)` (`o<T>`), `core::variant_name(const T* value)` по активному
  варианту tagged union; `core::target_name()` и `core::profile_name()` — `constexpr str`
  из манифеста цели и выбранного профиля.
- **В generic-телах.** Форма над параметром `T` становится зависимым узлом, который клон
  инстанциации сворачивает или проверяет после подстановки (`type_name(T)` внутри generic
  даёт написание конкретного типа; `enum_variants` требует конкретного enum).
- **Профиль freestanding.** Все формы доступны; выборки не используют ни библиотечных,
  ни runtime-символов; `enum_from_name` требует `str`, которого в freestanding-программах
  сейчас нет (эмиттер печатает `RRuntimeStringView` без определения — существующий пробел,
  не связанный со срезом).

### Как это устроено внутри

Парсер: `r_reflection_type_call_ahead` / `r_reflection_type_call_kind` (виды
`R_STANDARD_TYPE_CALL_REFLECTION_TYPE`, `…_TYPE_CONSTANT`, `…_TYPE_VALUE`), константы
разрешены в `R_EXPRESSION_CALL_FREE`. Семантика — новый файл
`compiler/semantic/reflection.inc`: классификация форм, каноническое написание типов
(`r_reflection_spell_type`), построители узлов (`R_HIR_LITERAL`, `R_HIR_ENUM_CONSTANT`,
`R_HIR_ARRAY_INIT`, `R_HIR_STRING_LITERAL` с интернированием байтов), проверка
зависимости от параметра (`r_reflection_type_is_dependent`), выборки как
`R_HIR_STANDARD_CALL` с `auxiliary_type` = тип-субъект, хук клона
`r_reflection_fold_clone` (по образцу свёртки `sizeof(T)` в `r_generic_clone_node`).
Новые операции `R_STANDARD_CALL_CORE_ENUM_NAME … CORE_VARIANT_NAME` (выборки),
`CORE_TARGET_NAME/PROFILE_NAME` и `CORE_REFLECT_*` (зависимые константы, до C17 не
доходят). Эмиттер: общий preflight (субъект, формы операндов, имена вариантов как
program strings) регистрирует одного помощника на пару «выборка × enum»
(`RC17ReflectionHelper`); помощники печатаются один раз как статические функции
`r_reflection_<выборка>_a<агрегат>` со `switch` по значению/индексу/тегу (для
`enum_from_name` — с таблицей имён `static const … r_names[]` и общим
`r_reflection_find_name`), а точка вызова в синхронном и async-путях — одно присваивание
вызова помощника через `RC17ReflectionRef` и `r_c17_reflow_assignment_call`. Целевой triple —
новый генерируемый файл `compiler/semantic/target_identity.generated.inc`
(`generate_target_abi.py`).

### Что не входит в срез

Константные выражения модуля из рефлексии (`const usize N = core::enum_count(T)`),
рефлексия методов и трейтов, доступ к полям по индексу во время выполнения, `typeid`,
динамические таблицы (R-TYPE-0039), `enum_variants` над параметром generic.

### Проверка

- Спецификация ядра 0.1.0-draft.41, 456 → 460 правил (новое семейство R-REFL-0001..0004
  в конце §14, строка `R-REFL` в H.2; амендмент R-EXPR-0020; Annex A: `reflection-constant-
  expression`, `standard-type-call-expression` с `core::enum_at/enum_from_name`, 254 продукции;
  Annex B; R-LIBREF-0001 → R-LIB-0024). Спецификация библиотеки 0.1.0-draft.12, 210 → 213
  правил (R-LIB-0024 в §5.1, Annex A.1, R-SLIB-MAP-B011, R-SLIB-CONF-D020); паритет EN/RU.
- Тесты: codegen `reflection`, `async_reflection`, пример `examples/reflection` (модель
  уровней логирования: `enum_from_name`/`enum_at`/`enum_ordinal`, tagged union, поля struct,
  `type_name` в generic; CTest-кейс `reflection_example`); freestanding-программа расширена
  рефлексией; нормативные `semantic_reflection_accept`, `semantic_reflection_not_enum`,
  `semantic_reflection_tagged_form`, `semantic_reflection_field_index` (CONST-001),
  `semantic_reflection_instantiation`; парсерные `reflection_syntax`, `reflection_recovery`;
  пробы приёмки 72 → 75 (`enum_reflection`, `variant_reflection`, `type_reflection`).
- Инвентарь библиотеки: 946 → 961 элементов (15 intrinsic `core::…` под R-LIB-0024),
  реестры операций перегенерированы; поверхность библиотеки 939/939 операций (в том числе под `--deny-panic-alloc`), 20 констант, 182 типа.
- Проверка: полный ctest 1521/1521, санитайзерный поднабор 288/288 (регэксп дополнен
  `reflection|freestanding`), пробы приёмки 75/75, безопасность 101/101, срезы 27/24,
  ворота формата/стиля (998 файлов), спецификации (паритет EN/RU, 460 + 213 правил, манифест
  грамматики 254 продукции), библиотеки и entry-stack — зелёные. Ничего не закоммичено.

## 15. Расширение дорожной карты: рефлексия, `@if`, вариативные параметры, `in` (запрос владельца 2026-09-12)

Разбор четырёх идей владельца с привязкой к принципам R: спецификация первична,
строгий C17 без runtime-метаданных (R-TYPE-0039), точный ациклический граф вызовов
(R-FUNC-0004), никаких неявных операций (R-AGG-0003: нет неявного равенства структур).
Каждая идея сводится к статическому механизму, разрешаемому на этапе компиляции.

### 15.1 Range-for, операторы `in` и `not in` → M10 (выполнено)

- **Range-for** уже запланирован в M10: `for (T x in expr)` над `Iterator`, над
  `lo..hi` (`core::range(I)`) и над `&array/&list/&dict/[T; N]/T[]`.
- **Оператор принадлежности.** Новый бинарный оператор уровня сравнения:
  `x in c` и `x not in c`; результат `bool`, допустим как лист `condition-expression`
  (как явное сравнение) и в обычных булевых выражениях. `in` становится ключевым
  словом (нужно и для range-for), `not` — контекстное слово только перед `in`
  (в исходниках репозитория `not` как идентификатор не встречается).
- **Семантика.** `x in c` ≡ `c.contains(&x)` через core-трейт
  `Contains { type Item; bool contains(const Self* this, const Item* value); }`
  (ассоциированные типы — часть M10). Встроенные impl: `dict<K, V>` — существующий
  `std.dict::contains`; `array<T>`, `list<T>`, `[T; N]`, `T[]` — линейный поиск с
  равенством по ключевому контракту (скаляры, `char`, `str`, типы с парой
  `hash`/`equal`; для структур без `equal` — диагностика, что сохраняет R-AGG-0003);
  `str` — поиск скаляра `char` или подстроки `str`; `lo..hi` — проверка границ без
  обхода. Пользовательские типы реализуют `Contains` сами.
- **Грамматика.** `in-expression = relational-expression, [ [ "not" ], "in",
  relational-expression ]` (неассоциативно); A.2: `in`; правило `R-EXPR-0029`
  (принадлежность), амендмент R-STMT-0002 (лист условия).
- **Стоимость.** Малая поверх M10: лексер, одна ветка парсера, десугар в вызов
  метода `contains` (машинерия M8) и intrinsic-impl для встроенных контейнеров.

### 15.2 Статическая рефлексия → новый срез M13 (выполнено, см. раздел 14)

Принцип: только compile-time, без RTTI. Каждый intrinsic сворачивается в константу
или статическую таблицу строк в сгенерированном C; работает в freestanding-профиле.
Форма — закрытые имена `core::…` (как `core::hash`, `core::checked_add`): закрытые
продукции Annex A, `R_STANDARD_CALL_CORE_*`, стандартный путь понижения.

| Intrinsic | Результат | Реализация в C |
| --- | --- | --- |
| `core::enum_name(value)` | `str` (статическое хранилище) | `switch` или таблица `static const char *r_enum_names_aN[]` |
| `core::enum_count(T)` | константа `usize` | свёрнута компилятором |
| `core::enum_min(T)`, `core::enum_max(T)` | константы типа `T` | свёрнуты (по значениям дискриминантов) |
| `core::enum_variants(T)` | `const T[N]` (статический массив, итерируемый в M10) | `static const` массив |
| `core::enum_ordinal(value)`, `core::enum_at(T, index)` | `usize` / `o<T>` | таблица или `switch` |
| `core::enum_from_name(T, name)` | `o<T>` | сгенерированное сравнение имён |
| `core::variant_name(value)` | `str` для payload-enum по активному варианту | `switch` по тегу |
| `core::variant_count(T)` | константа `usize` | свёрнута |
| `core::type_name(T)` | `constexpr str` (каноническое квалифицированное имя) | литерал |
| `core::field_count(T)`, `core::field_name(T, index)` | константа / `constexpr str` | свёрнуты (индекс — константа); таблица имён уже есть у JSON-схем |
| `core::target_name()`, `core::profile_name()` | `constexpr str` из манифеста | литерал |

Что **не** делаем: доступ к полям по индексу во время выполнения, перечисление
методов, `typeid`, динамические таблицы — всё это runtime-метаданные, противоречащие
R-TYPE-0039; обход полей структуры в M12 делается через трейты (`Show`, `Equal`).
Спецификация: новое семейство `R-REFL-0001..0004` (перечисления, варианты, типы,
цель/профиль), строка в H.2, Annex B. Стоимость: малая; срез независим от M10–M12
(рекомендуется сразу после M10, чтобы `enum_variants` был итерируемым).

### 15.3 Статические условия `@if` / `@else` → срез M14 (выполнено)

- **Форма.** `@if (предикат) { … } [@else { … } | @else @if (…) { … }]` как оператор
  внутри тел функций и как внешнее объявление уровня модуля (для предикатов профиля и
  цели). Ветки — области видимости как у блока; невыбранная ветка разбирается
  синтаксически, но не понижается и не порождает кода.
- **Предикаты** (закрытый набор, только статически разрешимые):
  `T is Type` (тождество после канонизации), `T is Trait` (реализация, R-TYPE-0043),
  `T is copy | pod | send | sync | key | error | unborrowed | json_encode | json_decode`
  (существующий словарь R-TYPE-0033), `T is fn(P...) -> R` (callable, R-TYPE-0044),
  `core::profile is freestanding | allocation | hosted | hosted-thread |
  hosted-native-async`, `core::target is "arm64-apple-darwin"`; комбинации `&&`,
  `||`, `!`. Позже (отдельным решением): `@if (constant-expression)` над модульными
  константами.
- **«Есть ли у типа метод X».** Рекомендация: выражать через трейты
  (`@if (T is Shown)`), а не структурно (`T has show`). Причины: спецификация-first
  и R-TYPE-0035 (тело generic проверяется до инстанциации только по доказанным
  операциям): в ветке `@if (T is Shown)` параметр T получает ограничение `Shown`, и
  вызов `value->show()` проверяется штатно; структурная проверка потребовала бы
  указывать полную сигнатуру и ввела бы «случайное» соответствие типов.
- **Семантика для generic.** Тело шаблона проверяется один раз: каждая ветка `@if` —
  под уточнёнными предположениями (в ветке `T is X` параметр T заменён на X; в ветке
  `T is Trait` — с добавленным ограничением); при инстанциации предикат вычисляется
  по конкретным аргументам, в клон попадает только выбранная ветка. Никакой
  специализации, перегрузок и SFINAE: `@if` не меняет сигнатуру функции и не создаёт
  несколько определений одного имени.
- **Реализация.** Парсер: `@` + `if` (новый узел `static-if-statement` /
  `static-if-declaration`); семантика: представление предиката (атомы), временное
  уточнение ограничений типового параметра при понижении ветки, узел
  `R_HIR_STATIC_IF`, отсекаемый в `r_generic_clone_node`; в неgeneric-контексте
  вычисляется сразу. Спецификация: `R-META-0001..0003`, Annex A (A.3, A.5), Annex B
  `R-DIAG-META-001` (неразрешимый предикат, ветка с несовместимыми предположениями).
- **Стоимость.** Средняя-высокая (уточнение ограничений — новая машинерия). Ценность:
  условная компиляция по профилю/цели (Linux-порт) и специализация
  библиотечного кода M12 по наличию трейтов (`@if (T is Ordered)` в `std.sorted`).
- **Состояние (проверено 2026-09-17).** Срез реализован целиком: Core `R-META-0001..0003`
  (раздел 14.3), продукции Annex A `static-if-statement` / `static-if-declaration` /
  `static-predicate`, диагностика `R-DIAG-META-001`; парсер `compiler/parser/static_conditions.inc`,
  семантика `compiler/semantic/static_conditions.inc` (три значения предиката: доказан,
  опровергнут, зависим; факты ветви — уточнение параметра типа, трейта, способности или
  callable; выбор ветви при инстанциации по каноническим аргументам; отбор модульных
  ветвей по профилю и цели). Проверка: `tests/frontend_tests.c` и `tests/semantic_tests.c`
  (разбор, ошибки формы, закрытые и зависимые предикаты), `tests/check_static_conditions.py`
  (межмодульный отбор, все пять профилей, интерфейсы и планы линковки, тест
  `r_frontend_static_condition_metadata`), codegen-фикстуры `codegen_static_conditions.r` /
  `codegen_async_static_conditions.r` и пример `examples/preflight` (выбор политики
  исполнения по профилю). Таблица дорожной карты ранее ошибочно показывала «план».

### 15.4 Вариативные параметры `...` → M11 (однородные, выполнено) и M15 (пакеты типов, отложено)

- **Однородные вариативные параметры** — сахар над срезом: `i32 sum(i32... values)`
  объявляет параметр `const i32[] values`; вызов `sum(1, 2, 3)` создаёт скрытую
  локаль-массив `[i32; 3]` на стеке вызывающего (живёт до конца оператора, как скрытые
  локали `await`) и передаёт срез; `sum()` передаёт пустой срез; единственный аргумент
  типа `const i32[]` передаётся как есть. Только последний параметр; Move-элементы
  требуют `move` у каждого аргумента (владение переходит скрытому массиву). Никаких
  variadic-функций в смысле C: R-FUNC-0004 сохраняет запрет, `raw fn` и C-импорты
  не затрагиваются. Правило `R-FUNC-0018`, продукция `variadic-parameter = type, "...",
  identifier`. Реализуется в M11 вместе с выражениями-коллекциями (общая машинерия
  скрытых массивов).
- **Гетерогенные пакеты типов** `@generic<T...>` — отложено в M15: противоречит
  R-TYPE-0031 («no variadic packs»), требует кортежей/рекурсии по пакету и
  инстанциации по длине; основной сценарий (форматирование) уже закрыт `f"…"`
  (R-EXPR-0028), а M13/M14 в пакетах не нуждаются. Вернуться после M12 с реальным
  запросом из прикладного кода.

### 15.5 Рекомендуемый порядок

M10 (итераторы, `in`/`not in`, ассоциированные типы) → M13 (рефлексия, малый срез)
→ M11 (коллекции, comprehension, `T... args`) → M14 (`@if`) → M12 (библиотека на R,
использует `in`, рефлексию и `@if`) → M15 (пакеты типов, по потребности).

## 16. Атрибут `@discardable` — результат, который разрешено игнорировать (R-FUNC-0021, 2026-09-16)

### Что реализовано

- Новый атрибут функции без аргументов `@discardable` (Core draft.45, правило
  `R-FUNC-0021`; Annex B: `R-DIAG-USE-001` для конфликтов контракта, `R-DIAG-SYN-002` для
  недопустимой позиции). Разрешён там же, где `@must_use` на функции: обычные и associated
  функции, методы, прототипы трейтов (реализация наследует), generic-объявления,
  async-функции, C imports. Запрещён на типах, полях, параметрах, переменных, drop hooks,
  callable-ограничениях и заголовке лямбды.
- Семантика: выражение-оператор, являющееся прямым вызовом `@discardable`-функции
  (`c.bump();`, `helper();`, `identity(1);`), и await-оператор формы вызова
  (`await compute();`) понижаются как `as void` — результат уничтожается ровно один раз с
  обычным cleanup. Остальное неизменно: checked errors, borrow checking, Copy/Move, разрешение
  task (`compute();` для async по-прежнему ошибка R-STMT-0001; `await move t;` для
  именованной задачи — ошибка R-STMT-0012). Разрешение не следует за значением в local,
  операнд, условие, callable-ограничение или raw signature.
- Контракт: `@discardable` несовместим с `@must_use`, не может возвращать `@must_use`-тип и
  требует не-void логического результата (для async — результата await). Interface schema 14
  записывает `discardable=true` (отсутствие = false) на функциях, generic-схемах и методах
  трейтов; несогласованное переобъявление — `R-DIAG-NAME-002`.
- Спецификация: R-GRAM-0007, R-EXPR-0023, R-STMT-0001, R-STMT-0012, новое R-FUNC-0021,
  Annex B, раздел 25.1 и ограничения — EN и RU синхронно; Core `0.1.0-draft.45`, 472 правила;
  инвентарь правил и дайджесты target-манифестов перегенерированы.

### Как это устроено внутри

- `compiler/semantic/must_use.inc`: `r_semantic_discardable_expression` распознаёт
  `R_HIR_CALL` (и `R_HIR_VALUE_SCOPE` над ним), а в режиме await — скрытую локаль `$await`,
  чей инициализатор — `R_HIR_ASYNC_START` `@discardable`-функции;
  `r_semantic_discardable_problem` даёт диагностики контракта; валидатор позиций атрибута
  общий с `@must_use`.
- `compiler/semantic/semantic.c`: общий помощник `r_body_append_discard` (узел
  `R_HIR_DISCARD`) используется и оператором `as void`, и выражением-оператором, и
  await-оператором; флаг `is_discardable` проходит через `RFunctionModifiers`,
  `RSemanticSymbol`, наследование трейтов (`traits.inc`) и вывод interface
  (`mir.c`, `generic_interface.inc`).
- Стандартная библиотека размечена (Library draft.15, правило `R-LIB-0026`, 221 правило).
  Встроенные операции: `core::replace`, `core::atomic_exchange`, `core::atomic_fetch_*`,
  `std.array::pop/remove`, `std.list::pop_front/pop_back/remove`, `std.dict::insert/remove`,
  `std.fs::seek`, `std.sync::barrier_wait` — флаг `discardable_result` в инвентаре
  (`DISCARDABLE_OPERATIONS` в `tools/generate_library_inventory.py`), из него
  `generate_standard_operation_registry.py` порождает таблицу перечислителей
  `standard_discardable_operations.generated.inc`, которую читает проверка
  выражения-оператора для `R_HIR_STANDARD_CALL`. R-модули: `@discardable` в исходниках
  `std/set.r` (insert, remove), `std/deque.r` (pop_front, pop_back), `std/heap.r` (pop),
  `std/sorted.r` (insert/remove у set и map). Не размечены и остаются обязательными:
  результаты с прогрессом/ошибкой (`std.io::write*`, `std.sync::send`, `std.thread::join`),
  передача владения (`core::take`, `core::release`), `core::atomic_compare_exchange`, а также
  `std.list::push_*`/`insert_*`: их результат — заимствование `T*`, а R-EXPR-0023 сейчас не
  отбрасывает заимствованные указатели (`as void` на `T*` — ошибка); расширять R-EXPR-0023 —
  отдельное решение.

### Проверка

- `tests/semantic_tests.c` (кейсы `discardable-*`: вызов, метод, generic, трейт, владелец,
  await, отказ для task и именованной задачи, операнд условия, обычный вызов рядом,
  void/async void, конфликт с `@must_use`, `@must_use`-тип, дубликат, аргументы, поле,
  агрегат, лямбда, переобъявление); `tests/frontend_tests.c` (разбор);
  `tests/check_borrowed_callables.py` (`discardable=true` в interface, межмодульные вызовы);
  codegen-программы `tests/fixtures/codegen_discardable.r` и
  `codegen_async_discardable.r` (обёртка `codegen_user_drop_wrapper.c` проверяет ровно два
  освобождения владельца после drop hook).

## 17. M16 — `std.deflate`: DEFLATE, zlib и gzip на уровне стандартной библиотеки (R-SLIB-DEFLATE-0001..0005, 2026-09-16)

### Что реализовано

- Новый R-source модуль `library/r/std/deflate.r` (профиль `allocation`, запись в
  `library/r/library.map`), Library `0.1.0-draft.16`, 226 правил, 989 записей инвентаря,
  38 модулей. Раздел 21.6 спецификации EN/RU; `R-SLIB-RSRC-0002` перечисляет модуль.
- Потоковый API по образцу `std.json::feed`: `inflater::create(format, window_size,
  output_limit)` / `inflate(this, const u8[] input, u8[] output) -> progress`,
  `deflater::create(format, level, window_size)` / `deflate(this, input, output, flush)`,
  `reset()`, `consumed_bytes()`, `produced_bytes()`; `progress { consumed, produced, state }`
  с состояниями `need_input | need_output | end`; `flush { none, sync, finish }`;
  форматы названы по RFC: `format { rfc1951, rfc1950, rfc1952 }` (сырой DEFLATE, zlib, gzip).
  Одноразовые `std.deflate::inflate(input, format, output_limit)` и
  `std.deflate::deflate(input, format, level)`.
- Ошибки — checked `std.deflate::error { error_code code; usize offset; }` с кодами
  `corrupt_stream`, `checksum_mismatch`, `invalid_level`, `invalid_window`, `unsupported`
  (FDICT, зарезервированные флаги gzip), `output_limit` (zip-бомба — обычная ошибка, не
  исчерпание памяти), `finished`, `poisoned`. Окно — степень двойки 256..32768; шаги не
  выделяют память; async в модуле нет намеренно, композиция со `std.io` — в программе.
- Инфлейтер: возобновляемый автомат стадий (заголовки zlib/gzip с необязательными полями,
  stored/fixed/dynamic блоки, копирование совпадений через кольцевое окно, трейлеры) с
  битовым аккумулятором u64; вход можно рвать на любом байте, непотреблённые байты после
  конца потока возвращаются вызывающему.
- Дефлейтер: LZ77 с хэш-цепочками над буфером в два окна и сдвигом, жадное сопоставление
  (уровни 1–3) и ленивое (4–9) по таблице конфигураций zlib, выбор формы блока по точной
  стоимости в битах (stored / fixed / dynamic), динамические коды Хаффмана с ограничением
  длины (поправка переполнения zlib), RLE описания длин, Adler-32 и CRC-32 без таблиц.
- Пример-приложение `examples/deflate` (id `deflate`, команды `deflate`, `deflate_sync`,
  `inflate`, `pack`, `unpack`, `stats`) — фильтр stdin→stdout поверх `std.deflate`;
  `examples/unzip` и `examples/zip` остались самостоятельными примерами реализации.

### Как это устроено внутри

- Ограничение borrow-модели R: из `this` одновременно доступна одна изменяемая производная
  ссылка, поэтому шаг забирает массивы состояния через `core::replace(&this->window,
  std.array::create(u8))` (`core::take` для массивов недоступен), работает со срезами
  локальных владельцев и возвращает массивы в конце; поля `this` при этом доступны.
- Тела стадий вынесены в плоские helper-функции (`read_code_lengths`, `decode_next`,
  `copy_match`, `read_trailer`, `tokenize_lazy`, `tokenize_greedy`, `flush_block`,
  `slide_window`): на момент написания модуля глубокая вложенность давала сгенерированный
  C17, не проходящий проверку clang-format (тесты `*_format`), а `continue` внутри `for`
  порождал метку с неверным отступом — в модуле такие места заменены на if/else. Оба
  ограничения сняты в генераторе (раздел 19); плоская структура оставлена как читаемая.
- Малые окна (< 262 байт lookahead) требуют токенизации при полном буфере до того, как
  история достигнет окна, иначе шаг зацикливается без прогресса — учтено условием
  `buffer_full`.
- После смены редакции спецификации и перегенерации дайджестов target-манифестов
  (`generate_target_abi.py --write`) нужно пересобрать `r-front`: дайджесты вшиты в бинарник,
  иначе `--target-manifest` даёт молчаливый код выхода 2.

### Что не входит в срез

- Ресурсные атрибуты `@noalloc @nonblocking` на шагах: доказательство упирается в
  `std.array::create` без ресурсного контракта. Проверка CRC заголовка gzip (FHCRC) —
  потребляется без сверки. Предустановленные словари. Потоковый `async pump` поверх
  `std.io` — после появления второго потребителя.

### Проверка

- `tests/fixtures/codegen_library_deflate.r`: векторы zlib/gzip/raw (stored, fixed,
  dynamic), потоковое декодирование по одному байту, `reset` и повтор, хвост после конца,
  `output_limit`, `checksum_mismatch`, `corrupt_stream`, `invalid_window`; раундтрипы
  компрессора по уровням 0–9, форматам и окнам 256/1024/4096/32768, `sync`-флаш,
  `finished`, `invalid_level`.
- `tests/run_deflate_examples.py`: дифференциальный тест против zlib/gzip из Python в обе
  стороны, включая малые окна, куски по 1 байту, gzip с FEXTRA/FNAME/FCOMMENT/FHCRC и все
  коды ошибок; каталог примеров (`coverage.json`, `syntax-coverage.json`) перегенерирован,
  все 989 записей покрыты.

## 18. M17 — `std.xml`: потоковый XML без DOM (R-SLIB-XML-0001..0006, 2026-09-17)

### Что реализовано

- R-source модуль `library/r/std/xml.r` (профиль `allocation`), Library `0.1.0-draft.17`,
  232 правила, 999 записей инвентаря, 39 модулей; раздел 21.7 спецификации EN/RU.
- Потоковый читатель `std.xml::reader` в стиле SAX/pull: `feed(fragment, final) -> progress
  { consumed, state }` с состояниями `need_input | event_ready | end`, событие читается через
  accessors (`kind`, `name`, `prefix`, `local_name`, `namespace`, `text`, `attribute_count`,
  `attribute_name/value`, `attribute(name)`, `path_name(depth)`, `depth`, `offset`),
  `reset`. Фрагмент можно рвать где угодно, незавершённый токен принадлежит читателю.
  Только UTF-8 (с проверкой), пять предопределённых сущностей и ссылки на символы,
  пространства имён по стеку `xmlns`, комментарии, инструкции, CDATA, объявление; DOCTYPE и
  внешние сущности — ошибка `unsupported`. Лимиты глубины и числа атрибутов в `options`.
- Потоковый селектор `std.xml::selector` — подмножество XPath над стеком открытых
  элементов: `/a/b`, `//b`, `a/*`, предикаты `[@k]` и `[@k="v"]` на последнем шаге; обратные
  оси и позиционные предикаты невозможны без DOM и не поддерживаются намеренно.
- Экранирующий писатель `std.xml::writer`: `declaration`, `start`, `attribute`, `text`,
  `cdata`, `comment`, `instruction`, `end`, `finish`, режим pretty с сохранением смешанного
  содержимого.
- Ошибки — checked `std.xml::error { error_code code; usize offset; }` с кодами `malformed`,
  `unsupported`, `invalid_utf8`, `unbalanced`, `depth_limit`, `attribute_limit`,
  `duplicate_attribute`, `invalid_selector`, `misplaced`, `finished`, `poisoned`.
- Пример-приложение `examples/xml` (команды `events`, `events_all`, `select`, `compact`,
  `pretty`) и дифференциальный тест `tests/run_xml_examples.py` против expat и ElementTree
  из Python: случайные документы, подача кусками по 1, 5 и 4096 байт, селекторы против
  независимой реализации, раундтрип через писатель, все коды ошибок.

### Как это устроено внутри

- Стадии разбора — плоские методы `stage_*` с кодом исхода; общий цикл `feed` по байту.
  Первая версия с вложенной цепочкой if/else на 17 стадий не проходила формат-проверку
  сгенерированного C (ограничение генератора, снятое в разделе 19) и содержала два дефекта
  (позиция объявления, длина имени в кадре).
- Событие отбрасывается только при следующем `feed` после `event_ready`; при `need_input`
  буферы сохраняются — иначе разрыв токена между фрагментами терял состояние.
- Строки accessors — `str` над проверенными байтами (`core::validate_utf8`, panic лишь при
  дефекте); пустой диапазон возвращается без среза, потому что срез `[n..n]` за концом
  массива — bounds-паника.
- Правила `R-SLIB-XML-0003..0006` документируют методы внутри правила типа и внесены в
  `UNQUALIFIED_SIGNATURE_RULE_EXEMPTIONS` генератора инвентаря.

### Что не входит в срез

- DOM, DTD/XSD-валидация, XPath за пределами потокового подмножества, XSLT, кодировки кроме
  UTF-8, проверка правил XML NameChar для не-ASCII имён (байты ≥ 0x80 принимаются).
- Связывание со структурами (`@xml`) — только после решения о рефлексии по значениям полей.

## 19. Раскладка сгенерированного C17 по модели clang-format (2026-09-17)

Запрос владельца: подводные камни разделов 17–18 («плоские стадии ради формат-проверки»,
метка `continue` внутри `for`) чинить на уровне компилятора, а не обходить в библиотеке.
Разбор показал: сохранение токена между фрагментами и паника среза `[n..n]` — дефекты
библиотечного кода, не языка (срез `[len..len]` допустим, паникует только начало за концом);
дефекты генератора — два.

### Что реализовано

- `compiler/codegen/c17.c`: метка продолжения цикла `for` печатается на уровне блока, а не
  на уровне операторов (clang-format ставит метки на один уровень левее тела).
- `compiler/codegen/layout.inc` — движок раскладки одного оператора C17, порт решающей
  процедуры clang-format 22.1.8 (TokenAnnotator, ContinuationIndenter, оптимизирующий
  форматировщик) для форм, которые порождает эмиттер: объявления с инициализатором,
  присваивания, вызовы, приведения (эвристика `rParenEndsCast` целиком), составные литералы
  с назначенными инициализаторами и висячей запятой, цепочки `.`/`->`, индексы, унарные и
  бинарные операторы (включая особые правила `<<`), тернарники, `return`, заголовки `if` /
  `while` / `switch` / `for` / `} else if` / `} while`. Токены получают вложенность, силу
  связывания и штрафы разрыва (15 за первый разрыв в скобке, 19 после открывающей скобки,
  1000 после `if (` и `for (`, 2 после присваивания, 1 после запятой, 20 за уровень скобок,
  1 000 000 за символ сверх 100 колонок); поиск Дейкстры по состояниям (колонка, стек
  скобок с полями clang: `Indent`, `LastSpace`, `NestedBlockIndent`, `QuestionColumn`,
  `StartOfFunctionCall`, `FirstLessLess`, флаги `BreakBeforeParameter`/`NoLineBreak`…)
  и фиктивные скобки по приоритетам из ExpressionParser. Результат — те же переносы, что
  у clang-format; проверено дифференциально на 124 649 уникальных операторах и 6 942
  заголовках из сгенерированных фикстур на 17 глубинах отступа (2,2 млн случаев, 0
  расхождений). Не моделируются строковые литералы (`BreakStringLiterals`) и цепочные
  тернарники (выравнивание отдельным проходом WhitespaceManager) — такие операторы
  остаются как записал эмиттер.
- `compiler/codegen/layout_pass.inc` — постпроход по готовой программе: строки тела функции
  группируются в логические единицы (оператор до `;`, заголовок до `{`); единица, занявшая
  несколько строк или вышедшая за 100 колонок, сплющивается и раскладывается заново.
  Однострочные единицы в лимите не трогаются, поэтому существующие фикстуры не меняются.
  Локальные эвристики переноса в эмиттере сохранены — теперь они лишь стартовая форма.
- Фикстура `tests/fixtures/codegen_deep_nesting.r` (более десяти уровней циклов/условий/switch с
  вызовами на шесть аргументов, литералами структур, приведениями, индексами, тернарником,
  форматной строкой и `continue` в `for`) зарегистрирована в обычном и `_format` наборах.

### Проверка

- `ctest -R _format`: 574 теста, все зелёные (три атомарных теста с цепочными тернарниками
  проходят, потому что такие операторы движок не переупаковывает).
- Дифференциальный прогон движка против `clang-format --style=file` на корпусе из
  `build-debug/tests/codegen_*.c` — вспомогательная обвязка в scratchpad сессии, не в репо.

## 20. M15 — гетерогенные пакеты типов `@generic<T...>`: анализ и план (2026-09-17)

Статус (27 сентября 2026): владелец выбрал вариант V2 (26 сентября), он реализован этапом
L18 — Core draft.70, правила R-TYPE-0052 (кортежи) и R-TYPE-0053 (пакеты), interface schema 23
(раздел 15 матрицы полноты, этап L18 дорожной карты). Отличия от плана ниже: номера правил,
синтаксис раскрытия аргумента `...tail` (префиксный spread, как в R-FUNC-0018) вместо
`tail...`, кортеж `(A, B)` вместо `tuple(A, B)`, пакет — последний параметр заголовка (обёртка
пишется `@generic<F: fn(P...) -> R, R, P...>`), отдельного кода `R-DIAG-PACK-001` нет
(используется `R-DIAG-TYPE-001`), пример — `examples/tally`. Текст ниже — исходный анализ.

Исходный статус: анализ по запросу владельца; решение о реализации не принято. Прикладных
потребителей в рабочем каталоге нет, поэтому спрос из реального кода не подтверждён — раздел
описывает, что именно пакеты дали бы, чем они противоречат текущим правилам, во что
обойдутся и какие есть более дешёвые альтернативы.

### 20.1 Что такое пакет и какие задачи он решает

Пакет — параметр generic, связывающий не один тип, а упорядоченную последовательность
типов произвольной длины, известной при инстанциации. Классические сценарии:

1. **Функция над разнотипными аргументами.** Сейчас однородные вариативные параметры
   `T... values` (R-FUNC-0018) — это сахар над срезом `const T[]`: все элементы одного типа,
   функция не generic. Гетерогенный вариант выглядел бы так:

   ```r
   @generic<Head: Shown, Tail...: Shown>
   void show_all(const Head* head, const Tail*... tail) {
       head->show();
       @if (len(Tail...) != 0usize) { show_all(tail...); }
   }
   show_all(&point, &name, &count);   // Head = Point, Tail = (Name, i32)
   ```

2. **Обёртка над callable с произвольной сигнатурой** (apply/bind/compose/measure):

   ```r
   @generic<F: fn(P...) -> R, P..., R>
   R timed(const F* f, P... args) throws std.alloc::alloc_error {
       std.time::instant start = std.time::now();
       R result = f(args...);
       log(f"{core::type_name(F)} took {std.time::since(start)}");
       return move result;
   }
   ```

3. **Кортежи** — записи без объявления структуры: `tuple(i32, str)` как результат функции,
   элементы пары в `dict`, разнотипные payload'ы задач.

4. **Вариант по списку типов** `variant(T...)`, «посетитель» по элементам кортежа.

Что уже закрывает язык без пакетов:

- Разнотипное форматирование и логирование — `f"…"` и `std.format::format` (R-EXPR-0028):
  компилятор знает тип каждого слота, никакой generic-функции не нужно. Это главный
  бытовой сценарий пакетов в других языках, и он закрыт.
- Записи фиксированной формы — именованные `struct`; статическая рефлексия даёт
  `core::field_count(T)` и `core::field_name(T, i)` (R-REFL-0003), но не значение поля по
  индексу — «обход полей» пока невозможен ни через пакеты, ни через рефлексию.
- Разнотипные аргументы фиксированной арности — перегрузки по арности (R-FUNC-0004):
  `log(a)`, `log(a, b)`, `log(a, b, c)` могут быть отдельными generic-функциями с 1–3
  параметрами; генерируемый код может выпускать их механически.
- Выбор ветки по свойствам типа — `@if (T is …)` (M14).

### 20.2 Полный перечень проблем

Каждая проблема помечена: **Спец** (нужно менять нормативный текст), **Ядро** (типовая
система / семантика), **Инфра** (интерфейсы, кодоген, инструменты).

- **P1 (Спец). Прямой запрет.** R-TYPE-0031 и R-TYPE-0047: «Defaults, variadic packs and
  specializations are not introduced». R-FUNC-0018 запрещает вариативный параметр в generic
  функции. R-FUNC-0004: «Generic parameters are compared by position» — перегрузки
  различаются позиционными списками фиксированной длины. Все три правила и их RU-версии
  переписываются, инвентарь и дайджесты перегенерируются.
- **P2 (Ядро). Нет кортежей и нет типа-последовательности.** `RSemanticType` хранит
  `base`, `second`, `length` — арность производного типа не выше двух; список типов
  представим только через интернированные списки сигнатур
  (`R_SEMANTIC_TYPE_FUNCTION_PARAMETER`, помечен «never a source value type»). Пакет
  значений `P... args` требует типа «кортеж» с layout, cleanup, Copy/Move и borrow-правилами
  для каждого элемента — по сути анонимного агрегата, порождаемого на лету, как скрытые
  массивы `[T; n]` в M11, но с разнотипными полями. Это новый kind типа плюс генерация
  агрегата и его hooks (drop, при `key` — hash/equal).
- **P3 (Ядро). Инстанциация по длине.** `RGenericSchema.parameter_count` фиксирован; кэш
  инстансов ключуется схемой и каноническими аргументами позиционно (R-TYPE-0039). С пакетом
  ключ становится переменной длины, канонизация — рекурсивной по списку; `core::type_name`
  для инстанса с пакетом должен сплющивать список (`Name<i32, str>` — спеллинг уже такой,
  но нужно правило для пустого пакета: `Name()`).
- **P4 (Ядро). Вывод типов.** R-TYPE-0036: каждый параметр обязан встречаться в выводимой
  позиции; пакет выводится только из хвоста списка значений (или из аргументов callable-
  ограничения `fn(P...) -> R`, где `P...` сегодня — конкретный список, а не пакет). Пакет
  не в последней позиции, два пакета, пакет внутри вложенного типа (`array<T...>`) — всё
  требует явных запретов. Явных аргументов типов у функций нет (R-TYPE-0036), значит пустой
  пакет выводится только из отсутствия аргументов.
- **P5 (Спец). Синтаксическая коллизия `...`.** `T... name` уже означает однородный срез.
  Нужно различать: `T...` в заголовке `@generic<T...>` объявляет пакет; `T... name` в
  параметрах generic-функции с пакетом `T` — раскрытие пакета; в не-generic функции — прежний
  срез. Правило разрешимо лексически (имя пакета известно из заголовка), но R-FUNC-0018 и
  Annex A нужно переписать, а диагностики — развести.
- **P6 (Ядро). Обход пакета.** Рекурсия `f(head, tail...)` порождает по инстансу на длину;
  граф вызовов (R-FUNC-0004, `R-DIAG-STACK-001`) строится по символам, а замкнутые
  инстансы — отдельные символы, так что цепочка `f(3 типа) → f(2) → f(1) → f(0)` ациклична;
  это надо подтвердить тестом и записать в спецификацию как намеренное. Условие остановки
  `@if (len(Tail...) != 0usize)` — константное выражение, а R-META-0002 прямо запрещает
  константные предикаты: нужно расширение META (в плане 15.3 оно уже заявлено «позже
  отдельным решением»; с Core draft.55 константные условия в `@if` есть, для пакетов
  остаётся сделать `len(T...)` константой). Без рекурсии нужен статический цикл `@for (T in Pack...)` — новая
  конструкция с той же машинерией фактов, что у `@if`.
- **P7 (Ядро). Владение и заимствование поэлементно.** Каждый элемент пакета — Copy или
  Move; `move args...` должен переносить только Move-элементы; анализ заимствований должен вести
  займы каждого элемента кортежа; cleanup — в обратном порядке; async — поэтапные Move-правила
  (R-TYPE-0038) для каждого элемента. Это удваивает поверхность правил R-OWN/R-BORROW для
  нового вида storage.
- **P8 (Ядро). Ограничения и факты.** `@generic<T...: Shown>` — ограничение «для каждого
  элемента»; проверка тела до инстанциации (R-TYPE-0035) должна доказывать операции над
  раскрытием, а не над отдельным типом. `@if (T is copy)` по R-META-0003 уточняет параметр,
  «непосредственно названный» в предикате — для пакета нужна семантика «для всех» / «для
  какого-либо» элемента либо запрет предикатов над пакетом.
- **P9 (Спец/Ядро). Hooks и трейты.** R-TYPE-0037: hook повторяет параметры схемы —
  `drop(Tuple<T...>* self)`; реализации трейтов для типов с пакетом (`impl Shown for
  tuple(T...)`) требуют раскрытия внутри impl-заголовка; ассоциированные типы (R-TYPE-0045)
  над пакетом — отдельный вопрос.
- **P10 (Инфра). Интерфейсы.** Interface schema 14 записывает параметры generic позиционно;
  пакет — schema 15: вид параметра `pack`, канонические аргументы переменной длины,
  fingerprint определений, `check_borrowed_callables.py` и golden `.mir.sexp`.
- **P11 (Инфра). Кодоген C17.** Кортеж → структура с полями `r_0..r_n`, имя по канонической
  идентичности (механизм имён инстансов есть); раскрытие `args...` → обычные аргументы C.
  Порождённые агрегаты попадают в реестр стеков (`generate_runtime_entry_stack.py`)
  и в проверку формата.
- **P12 (Спец). Диагностики и лимиты.** Новые сообщения: пакет не последний, два пакета,
  невыводимый пакет, раскрытие вне допустимой позиции, предикат над пакетом; цепочка
  инстанциации при глубокой рекурсии по пакету (лимит R-TYPE-0039).
- **P13 (Спец). Что исключить сразу.** Пакеты констант `const usize N...`, пакеты в
  структурах-полях без кортежа, специализация по длине, пакет как параметр трейта.
- **P14. Стоимость.** Затрагиваются парсер, вся generic-машинерия (`generics.inc`,
  `generic_constants.inc`, `closures.inc` для callable, `static_conditions.inc`), borrow-
  checker, кодоген, интерфейсы, спецификация (≈8 правил переписать, ≈3 новых, Annex A/B),
  тесты. По объёму сопоставимо с M9 и M11 вместе взятыми — крупнейший языковой срез этапа.

### 20.3 Варианты

- **V0 — не вводить пакеты.** Сценарии закрываются `f"…"`, структурами, перегрузками по
  арности и генерацией кода (генератор знает арность каждого вызова и
  выпускает конкретные типы). Ничего не ломает, стоимость нулевая. **Рекомендуется по
  умолчанию**, пока не пришёл сценарий, который так не закрывается.
- **V1 — кортежи без пакетов.** Встроенный тип `tuple(T1, …, Tn)` фиксированной арности с
  доступом `value.0`, `value.1`, литералом `(a, b)` или `tuple { a, b }`, Copy/Move/drop по
  элементам, `key` при `key`-элементах. Не трогает generic-машинерию (арность конкретна в
  каждом написании), закрывает «вернуть два значения» и «пара в dict». Средняя стоимость:
  новый kind типа, парсер, borrow по полям (есть — как у struct), кодоген как struct. Можно
  сделать первым шагом V2.
- **V2 — полные пакеты.** `@generic<T...>`, раскрытие `T...`, `len(T...)`, расширение
  `@if` константными предикатами, кортеж как тип раскрытого значения. Полная стоимость P1–P14.
- **V3 — пакеты только в callable-ограничениях.** `@generic<F: fn(P...) -> R, P..., R>` для
  apply/bind/timed без кортежей как значений (аргументы сразу передаются дальше). Дешевле V2
  (нет кортежных локалей), но остаются P3–P6, P8, P10; сценарий узкий.

### 20.4 План реализации, если владелец выбирает V2

- **Э0. Требования (владелец).** Собрать 3–5 реальных сценариев и проверить, не
  закрываются ли они V0/V1. Зафиксировать решение в этом разделе.
- **Э1. Спецификация.** Новые правила: `R-TYPE-0049` (объявление пакета: ровно один, последний
  в заголовке, ограничение поэлементно, запрет пакетов констант), `R-TYPE-0050` (раскрытие:
  в списке параметров, аргументов вызова, аргументов типов, инициализаторе кортежа; вывод
  пакета из хвоста; пустой пакет), `R-TYPE-0051` (кортеж как тип раскрытого значения:
  layout, Copy/Move, cleanup, `key`), правка R-TYPE-0031/0036/0037/0039/0040/0047,
  R-FUNC-0004/0018, R-META-0002 (константные предикаты `len(T...)`), Annex A
  (`generic-parameter`, `pack-expansion`, `tuple-type`), Annex B (`R-DIAG-PACK-001`);
  Core draft.46, инвентарь, дайджесты, RU синхронно.
- **Э2. Типовая система.** `R_SEMANTIC_TYPE_PACK` (интернированный список, по образцу
  `FUNCTION_PARAMETER`) и `R_SEMANTIC_TYPE_TUPLE` (агрегат, порождаемый при закрытии);
  канонизация и подстановка по спискам; кэш инстансов с ключом переменной длины;
  `core::type_name` для пакетов.
- **Э3. Парсер.** `T...` в `@generic`, раскрытия `T... name`, `expr...`, `tuple(...)`;
  различение с однородным вариативным параметром по имени пакета.
- **Э4. Семантика.** Вывод пакета из хвоста аргументов и из callable-ограничения; проверка
  тела до инстанциации с операциями «для каждого элемента»; раскрытие при понижении в HIR
  (клонирование с подстановкой списка); владение и займы поэлементно; `@if (len(T...) …)`.
- **Э5. Кодоген C17.** Кортежи как структуры, раскрытие как аргументы; регистрация в
  реестре стеков; проверка формата (движок раскладки уже покрывает вызовы любой арности).
- **Э6. Интерфейс и инструменты.** Schema 15, golden `.mir.sexp`,
  `check_borrowed_callables.py`, инвентарь библиотеки без изменений.
- **Э7. Проверка.** frontend/semantic-тесты (объявление, раскрытие, вывод, пустой пакет,
  ошибки позиции), codegen-фикстуры sync/async (владение элементов, рекурсия по пакету с
  проверкой ацикличности графа), пример `examples/packs`, запись в `known-limitations.md`.

Оценка: Э1–Э7 — самый большой срез после M9/M11; порядок этапов фиксирован, потому что
Э2 нельзя проектировать раньше формулировок Э1, а Э4 зависит от решения по META.

## 21. Дешёвая разработка конкурентных сервисов: диагноз и программа (2026-09-17)

Статус (27 сентября 2026): владелец решил реализовать программу этапами L21, L23 и L24
[дорожной карты языка](language-completeness-roadmap.ru.md): A2 — структурный дедлайн,
наследуемый как отмена (выполнено на L23 вместо объекта контекста: оператор
`deadline (value) { ... }`, R-STMT-0019, задаёт дедлайн задачи, который получает всё запущенное
внутри, а аргумент `deadline` стандартных операций стал необязательным, R-SLIB-ASYNC-0008;
`netlab` не передаёт дедлайн ни разу); A4 — наследование ошибок с общим корнем стандартных ошибок вместо
набора `std.error::fault` (выполнено на L21: корень получил имя `std.error::fault`, переносимую
ошибку даёт `std.error::from_fault`, R-SLIB-ERR-0004, пример `examples/settings`); B4 —
асинхронные каналы как участники `select` (выполнено на L24: `receiver.receive()`, R-LIB-0016);
A3 — каркас `std.service` (выполнено на L24: `serve`/`serve_with`, R-SLIB-SERVICE-0001..0003,
отказ при переполнении, таймаут соединения, отчёт, остановка каналом и отменой; на L25 добавлена
политика `overflow::wait` на барьере группы `vacancy`, а ошибки вызова передаются одним путём,
так что `std.error::fault` можно объявлять в любой функции без роста стека под ASan); A5 —
`examples/service` и переписанный `netlab`, где выполнены все метрики 21.3 (вызов I/O — одна
строка, дедлайн — ноль передач, `catch` в `main` — одна клауза, `deliver` — 18 строк вместо 37);
B1 и B2 не делаются (их
заменяет наследование), B3 отклонён (условие остаётся явным сравнением). Дополнительно введены
цепочки методов по `@chain` (L19), неявный `break` и сопоставители меток (L20), вычисление
при трансляции для ошибок, float и контейнеров (L22). A1 выполнен раньше (21.5). Текст ниже —
исходная программа.

Запрос владельца: «дешёвая разработка обычных
конкурентных сервисов». Диагноз поставлен по реальному коду примеров `netlab`
(`examples/netlab/src/tcp.r`), `fanout`, `jobs`, `runner` и по контрактам `std.net` /
`std.io` / `std.sync` / `std.async`. Цель — убрать церемонию, не гарантии: владение,
checked effects, отсутствие GC и явные отказы остаются.

### 21.1 Где сегодня дорого (с цифрами из примеров)

- **Результаты I/O как tagged-типы.** `std.net::tcp_read` возвращает
  `tcp_read_result::read/end/failed` с буфером внутри (R-SLIB-NET-0005): каждый вызов —
  `switch (move result)` на 5–6 строк плюс `bytes buffer = std.alloc::bytes(...)` перед ним.
  В `tcp.r` на четыре вызова I/O приходится 20 строк разбора.
- **Дедлайн протягивается вручную.** Параметр `o<std.time::instant> deadline` есть у каждой
  операции (R-SLIB-ASYNC-0008); в `deliver` он передаётся девять раз.
- **Лестницы `catch` инфраструктурных ошибок.** `main` в `netlab` ловит семь типов
  (`parse_error`, `alloc_error`, `start_error`, `time_error`, `duration_error`, …) и
  вручную сопоставляет коды выхода; `fanout` — четыре. Это повторяется в каждом сервисе.
- **Двухфазный запуск.** Каждый вызов async возвращает `task<T>` и может бросить
  `start_error`; наблюдение потребляется отдельным `await move t`. Для сервиса «принял —
  обработал — ответил» это два дополнительных понятия на каждый шаг.
- **Нет мультиплексирования.** `task_scope::first` ждёт только задачи группы; ожидания
  «канал или таймер или отмена» нет — приходится строить вспомогательные
  задачи.
- **Нет каркаса сервиса.** Цикл `listen → accept → задача на соединение → лимит
  одновременности → graceful shutdown` каждый сервис собирает из `task_scope`, `listener.accept`
  и ручных дедлайнов.
- **Мелкий синтаксический шум.** Условие обязано быть явным сравнением (R-GRAM-0005):
  `while (complete == false)` вместо `while (!complete)`; литералы, напротив, уже
  подстраиваются под тип (`n == 1` для `usize` проходит) — суффиксы в примерах не
  обязательны.

### 21.1a Найденный пробел (проба 2026-09-17)

Проба `@scoped async usize fill(u8[] target)` в `task_scope(1)`: семантический слой принимает
исключительный займ среза `u8[]` в scoped-задачу (по R-TYPE-0026 `u8[]` — Send, по
R-STMT-0017 исключительные займы допустимы), HIR/MIR/interface порождаются, а `--emit=c17`
завершается кодом 1 без единой диагностики. Тот же код с `const u8[]` компилируется.
Воспроизводится и бинарником от 14 сентября, то есть пробел старый. Два дефекта: (1) генератор
не поддерживает исключительные scoped-займы (предикат `r_c17_async_parameter_type_is_supported`
или понижение займа); (2) отказ молчаливый — должен быть `R-DIAG-SLICE-001` с сообщением.
Это первый обязательный шаг для A1: без исключительного займа буфера в задачу
`await std.net::read(&stream, buffer.as_slice_mut(), ctx)` невозможен даже как копирующая обёртка.

### 21.2 Программа

Ярус A — библиотека и соглашения, без изменений языка (наибольший выигрыш на строку):

- **A1. Бросающие обёртки I/O с владением буфера у вызывающего.** Внутри `task_scope`
  задачи могут заимствовать (`@scoped`, пример `fanout`), поэтому возможны scoped-варианты
  `await std.net::read(&stream, buffer.as_slice_mut(), deadline) -> usize throws net_error`
  и `write_all(&stream, const u8[] …)`. Вне scope остаются tagged-результаты (буфер
  обязан вернуться из native-очереди). Ожидаемый эффект: вызов I/O — одна строка.
- **A2. Контекст операции.** `std.async::context { deadline; cancel; budget }` как один
  параметр вместо `deadline` в каждом вызове; `ctx.with_timeout(d)`, `ctx.child()`.
  Наследование дедлайна и отмены по дереву задач — то, что план уже требует в «P0 —
  lifecycle».
- **A3. Каркас сервиса `std.service`.** `serve(listener, ctx, capacity, handler)`:
  accept-цикл, задача на соединение в ограниченной группе, отказ при переполнении,
  graceful shutdown по отмене контекста, учёт открытых соединений. Каркас
  для TCP-сервисов.
- **A4. Свёртка инфраструктурных ошибок.** Набор `std.error::fault` с `from`-конверсиями
  из `alloc_error`, `start_error`, `time_error`, `duration_error`, `io_error`, `net_error` и
  функция `std.process::status_of(fault)`; `main` ловит одно исключение.
- **A5. Шаблон `examples/service`**: echo/registry-сервис на A1–A4 как эталон и как
  метрика (см. 21.3).

Ярус B — сахар языка, каждый пункт отдельным правилом:

- **B1. Именованные наборы ошибок.** `error_set Infra = std.alloc::alloc_error |
  std.async::start_error | …;` в `throws Infra` и `catch (Infra failure)`; раскрывается в
  точный набор (R-ERR-0001 сохраняется, набор остаётся закрытым и явным).
- **B2. Мульти-catch** `catch (A | B failure)` с общим телом, когда тело не обращается к
  полям.
- **B3. Условие по булевому имени.** Разрешить в условии идентификатор типа `bool` и
  его отрицание `!name` (изменение R-GRAM-0005; сравнения остаются обязательными для
  всего остального).
- **B4. `select`** после библиотечного прототипа (A2/A3): ветви «получение из канала»,
  «таймер», «завершение задачи», «отмена»; проигравшие ветви отменяются, их владельцы
  возвращаются — правило судьбы проигравших должно быть записано до синтаксиса.
  *Состояние 24 сентября (L9, Core draft.60, R-STMT-0018):* `select` по участникам группы и
  дедлайну реализован раньше A2/A3; судьба проигравших записана и явная — select их не
  отменяет, они остаются select-pending до `group.cancel_all()` или выхода из группы. Ветви
  каналов и отмены ждут библиотечных A2/A3.
- **B5. Однофазный `await f()`** без видимого `task`: уже работает как выражение; нужно
  лишь документировать идиому и разрешить `start_error` входить в наборы B1.

Ярус C — инструменты (вне языка, но определяют «дешевизну» не меньше): шаблон проекта,
единая команда build/test/run, LSP, трассировка задач.

### 21.3 Критерий приёмки

Переписать `examples/netlab/src/tcp.r` и добавить `examples/service` на A1–A4 (+B1–B3,
если приняты). Целевые метрики относительно текущего кода: вызов I/O — 1 строка вместо
5–6; передача дедлайна — 0 явных параметров вместо 9; `catch` в `main` — 1 клауза вместо
7; объём `deliver` — не более половины нынешнего; ни одной новой гарантии не потеряно
(те же тесты владения, отмены и дедлайнов зелёные).

### 21.4 Порядок и стоимость

A2 → A1 → A3 → A4/A5 (библиотека, средняя стоимость, Library draft.18–19), затем B1–B3
(малые правила Core), B4 — только после A3 и решения о проигравших ветвях. Ярус A не
блокирует Linux-порт и не меняет контракты runtime: обёртки строятся над существующими
операциями A.2 библиотечной спецификации.

### 21.5 Реализация A1: scoped-операции и займ до подтверждения backend'а (2026-09-17)

Решение владельца: чинить пробел из 21.1a, ввести правило «срок займа привязан к
подтверждению backend'а» и адаптировать все стандартные операции с буферами.

- **Пробел закрыт.** Генератор C17 принимает исключительные срезы `u8[]`, захваченные
  scoped-запуском, и представления, образованные внутри `@scoped`-тел (предикат
  `r_c17_preflight_async_slice`); отказ `NOT_LOWERABLE` больше не молчаливый — CLI печатает
  `R-DIAG-SLICE-001`. Фикстура `codegen_scoped_views.r`, случаи срезов в
  `tests/scoped_task_tests.inc`.
- **Правило.** Core `0.1.0-draft.46`: R-BORROW-0024 (исключение для scoped-стандартных
  операций), R-STMT-0017 (абзац о займе, который заканчивается только после подтверждения
  backend'ом завершения или отмены), §25.1. Library `0.1.0-draft.18`, 236 правил: новое
  общее R-SLIB-ASYNC-0012 (scoped-операции: вызов только при активной `task_scope`, view —
  займ группы, публикация исхода = подтверждение, без копий), R-SLIB-IO-0008, R-SLIB-FS-0013,
  R-SLIB-NET-0010 (тип `std.net::datagram`), поправки R-SLIB-GEN-0004/0008,
  ASYNC-0003/0004/0007, API-A002, CONF-D008, строки A.1/A.2. Инвентарь: 1011 записей.
- **Операции (11).** `std.io::read_into/write_from/write_all_from`,
  `std.fs::read_into/write_from/write_all_from`, `std.net::tcp_read_into/tcp_write_from/
  tcp_write_all_from/udp_send_from/udp_receive_into`; сигнатуры `usize | void |
  std.net::datagram throws <ошибка модуля>`; методы `stream.read_into(...)` и т. п.
  Whole-file операции (`read_file`, `write_file_atomic_no_replace`) и JSON-ридер остаются
  owning: им нужен весь буфер или собственное состояние.
- **Компилятор.** Дескрипторы `compiler/source/standard_scoped_operations.h`; семантика
  `compiler/semantic/scoped_operations.inc` (проверка `task_scope`, `r_scope_record_call`
  регистрирует займы handle и view); ветка MIR с маской call-bounded на handle и view;
  `compiler/codegen/scoped_async_preflight.inc` и `scoped_async_emit.inc` (view передаётся
  структурой `{data, length}` по значению); реестр `standard_scoped_operations.generated.inc`
  из инвентаря (`SCOPED_OPERATIONS` в `generate_library_inventory.py`).
- **Библиотека и runtime.** Адаптеры `async_read/async_write/async_datagram` (net),
  `async_stream` (io), `async_payload` (fs) получили режим `borrowed`: view уходит в native
  запрос как `RRuntimeDarwinIoBuffer` без владельца, результат заполняется и публикуется
  только в существующем шаге finalize перед `r_runtime_task_external_acknowledge`; UDP-приём
  копирует из bounce-хранилища только при успехе. Тесты `library_{net,io,fs}_scoped_tests.c`
  проверяют round trip, no-op нулевой длины, истёкший deadline и порядок подтверждения при
  отмене (буфер не тронут, пока native не отпустил).
- **Проверка и примеры.** Фикстуры `codegen_scoped_net.r` (TCP+UDP loopback),
  `codegen_scoped_console.r` (stdin→stdout), `codegen_scoped_file.r`; пример
  `examples/streams` (stdin → файл → TCP → UDP через scoped-операции, `@scoped`-стадии,
  усечённая датаграмма); `examples/bytepipe` переведён на `read_into`/`write_from`/
  `write_all_from`. Метрика A1 на `bytepipe`: чтение — одна строка вместо `switch` на три
  ветви, буфер один на весь поток вместо аллокации на каждый вызов.
- **Поимённое освобождение займов.** Потребляющий `await` (именованного участника или
  прямого scoped-вызова) завершает займы именно этого участника: к моменту возврата исход
  опубликован, а для стандартной scoped-операции публикация и есть подтверждение
  backend'а (R-STMT-0017 и R-SLIB-ASYNC-0012 уточнены). Займы хранятся по узлу запуска
  (`scope_start_call` у символа участника, `r_scope_release_call`); после потребляющей
  отмены и для непотреблённых наблюдений займы по-прежнему держатся до барьера или выхода.
  Цикл чтения в один буфер поэтому не требует `await group.all()`.
- **Toolchain.** Машина обновила Xcode: перепинованы `clang-2100.3.34.2` и SDK `macOS 27.0`
  в манифестах целей, `check_target_manifest.py`, `generate_target_abi.py`,
  `generate_runtime_entry_stack.py`; манифесты и инвентари перегенерированы.

## 22. Проверка производительности: где лишний оверхед (2026-09-17)

Набор `tests/bench` расширен с 5 до 12 пар (`baseline_async`, `async_task_start`,
`async_scoped_start`, `async_fs_read_into`, `array_push`, `format_int`, `own_alloc`);
`r_bench_pairs_agree` проходит на всех парах, `docs/benchmarks.md` перегенерирован. Ниже —
измерение на Apple M4 (медианы 7 запусков минус база) и разбор по профилям `sample`.

| Ядро | Итераций | R, нс/итер | C, нс/итер | R/C | Вердикт |
| --- | ---: | ---: | ---: | ---: | --- |
| `call_overhead` | 200M | 1.04 | 1.11 | 0.94 | паритет |
| `index_fixed` | 204.8M | 1.08 | 0.92 | 1.17 | проверка границ, необходимо |
| `index_slice` | 204.8M | 0.85 | 0.80 | 1.06 | паритет |
| `checked_arith` | 200M | 1.11 | 1.05 | 1.05 | паритет |
| `async_task_start` | 2M | 331 | 0.94 | 352 | **лишний**: два перехода между потоками на каждый `await` |
| `async_scoped_start` | 2M | 396 | 1.63 | 243 | те же переходы + 65 нс группы (необходимо) |
| `async_fs_read_into` | 20k | 23 476 | 425 | 55 | **лишний**: dispatch_io-канал на каждую операцию |
| `array_push` | 50M | 9.70 | 0.66 | 14.6 | **лишний**: поэлементный перенос при росте, 4 уровня вызовов |
| `format_int` | 5M | 66.8 | 50.4 | 1.33 | умеренный: лишняя работа в `append_padded` |
| `own_alloc` | 20M | 22.0 | 15.6 | 1.41 | умеренный: обёртки аллокатора |

### 22.1. `async_task_start` — 330 нс на `await` без приостановки

Диагноз (профиль: `__psynch_cvwait`, `syscall_thread_switch`, `dispatch_async_f`,
`_xzm_free`). Тело `async u32 step(...)` не содержит `await`, но лоурится как
`RESUMABLE`-задача: `r_runtime_task_resumable_start_prepare` (malloc задачи + кадра),
`task_start_commit` → `task_dispatch` → `dispatch_async_f` на рабочий поток; `main`
в состоянии 10 возвращает `R_RUNTIME_TASK_STEP_SUSPENDED`; рабочий поток исполняет тело,
`task_publish_terminal` будит ожидающего через `task_wake_waiter` → второй
`task_dispatch`. Итого на итерацию: два перехода между потоками, две пары mutex
lock/unlock, cond broadcast, malloc/free кадра.

Что необходимо: двухфазный запуск, публикация исхода, кадр задачи (R-SLIB-ASYNC-0003/0004).
Что лишнее: переход на рабочий поток для тела, которое статически не может
приостановиться, и повторная постановка ожидающего в очередь.

Предложение (без изменения семантики): (1) семантика помечает async-функции, чьё тело не
содержит `await`/`task_scope`, как *eager-complete*; codegen выбирает
`r_runtime_task_start_commit_inline`, который исполняет тело на потоке вызывающего до
`commit` и публикует исход сразу — `await` попадает в ветку `task_execution_await_terminal`
без приостановки; (2) для остальных задач — «выполнить при ожидании»: если задача ещё в
состоянии `QUEUED` в момент `r_runtime_task_execution_await`, вызывающий поток снимает её с
очереди и исполняет шаг сам (нужен `dispatch_async_f` с отменяемым элементом либо
собственная очередь). Ожидаемый эффект: 330 нс → ~40–60 нс (malloc/free кадра + атомики).
Кадр можно брать из thread-local freelist.

### 22.2. `async_fs_read_into` — 23 мкс на чтение 4 КиБ

Диагноз (профиль: `dispatch_io_create_with_io` 8, `dispatch_io_close` 20,
`_dispatch_disk_perform` 47, `kevent_id` 123, `__fcntl_nocancel` 22,
`read_stream_position_barrier` 9, `__read_nocancel` всего 10). На каждую операцию
`runtime/darwin/source/io_read.c` создаёт новый `dispatch_io`-канал поверх
`root_channel` (`r_runtime_darwin_io_internal_create_operation_channel`), затем
`dispatch_io_read`, барьер позиции потока с `lseek`, `dispatch_io_close` и
`r_runtime_darwin_io_internal_operation_done`. Само чтение из page cache занимает
менее 1 мкс (мирор C: 425 нс); остальное — жизненный цикл канала, `fcntl`
внутри libdispatch, kevent-пробуждения и 3–4 перехода между очередями.

Что необходимо: подтверждение backend'а до публикации (R-SLIB-ASYNC-0012), отмена
операции по дедлайну. Что лишнее: создание/закрытие канала на операцию — оно
нужно только для `DISPATCH_IO_STOP` конкретной операции, а для обычного файла
чтение по `pread` на рабочем потоке даёт те же гарантии (операцию нельзя прервать в
середине системного вызова и в текущей схеме).

Предложение: для `R_RUNTIME_DARWIN_IO_RANDOM`/обычных файлов — прямой `pread`/`pwrite`
на исполнителе задачи (без dispatch_io), позиция потока обновляется под mutex
дескриптора; dispatch_io оставить для консоли, pipe и сокетов, где нужен
неблокирующий wait. Ожидаемый эффект: 23 мкс → ~1.5–2 мкс на чтение (доминировать
будет 22.1).

### 22.3. `array_push` — 9.7 нс против 0.66 нс

Диагноз (профиль: `_platform_memmove` 143, `r_runtime_array_reserve_valid` 68,
`memcpy`-стаб 31, `r_std_array_push` 28, `map_allocation_status` 22,
`r_runtime_array_push` 17). Три источника:

1. Рост в `r_runtime_array_reserve_valid` (`runtime/source/array.c`) выделяет новый
   блок и переносит элементы по одному через `r_runtime_array_move` (указатель на
   `move_initialize` или `memcpy` по 4 байта). Для типов без пользовательского
   переноса (`move_initialize == NULL`) достаточно одного `memcpy` блока или
   `realloc`. Это большая часть `memmove`-семплов.
2. Push — четыре уровня вызовов через границу библиотеки: генерированный код →
   `r_std_array_push` → `r_runtime_array_push` → `reserve_valid` + `move`, плюс
   `r_library_internal_array_map_allocation_status` на каждом успешном push
   (цепочка `if` ради маппинга статуса, который в 99.99 % случаев `OK`).
3. Генерированный код держит результат в `RStdArrayPushResult` и проверяет
   `status != SUCCESS` — это дёшево, но отсекает инлайнинг.

Что необходимо: проверка исчерпания памяти на каждом push (R не паникует на OOM).
Что лишнее: поэлементный перенос POD и маппинг статуса на горячем пути.

Предложение: (1) `reserve_valid`: если `element.move_initialize == NULL` — один
`memcpy` (или `realloc` через аллокатор); (2) в `r_std_array.h` объявить
`static inline` fast path `r_std_array_push_inline`: если `length < capacity` и
`move_initialize == NULL` — `memcpy` + `length += 1`, иначе вызов медленного пути;
codegen использует его для контейнеров с POD-элементом; (3) `map_allocation_status`
вызывать только при `status != OK`. Ожидаемый эффект: 9.7 нс → ~1–1.5 нс.

### 22.4. `format_int` — +16 нс на `f"{index}"`

Диагноз: обе программы делают один `malloc`/`free` на итерацию (мирор C тоже).
Разница — `r_format_append_padded` (`library/internal/text/source/format_spec.c`):
два `memset` с нулевой длиной (padding и trailing_zeros; на профиле
`_platform_memset` 10 семплов), `r_runtime_string_reserve` через маппинг статуса,
`r_std_format_create` → `r_std_string_create`, затем `r_std_format_finish` и
`r_runtime_array_destroy`. Плюс деление по `spec.radix` (переменный делитель вместо
константы 10) в цикле цифр.

Что необходимо: heap-строка (семантика `std.string::string`). Что лишнее: `memset(…, 0)`,
переменный radix для десятичной ветки, отдельный маппинг статуса на успехе.

Предложение: специализировать `radix == 10` (деление на константу), пропускать `memset`
при нулевой длине, объединить create/finish в один вызов для f-строк без спецификаторов.
Ожидаемый эффект: 66 → ~55 нс (ближе к паритету; остаток — malloc).

### 22.5. `own_alloc` — +6 нс на `new`/drop

Диагноз: `r_runtime_own_create` → `r_runtime_allocator_allocate` (проверки размера и
выравнивания, вызов через таблицу аллокатора) → `malloc`; `r_runtime_own_release` →
drop-glue (`drop == NULL` для `u32`) → `deallocate`. Пик `mach_absolute_time` (148
семплов) — внутри `_xzm_free` libmalloc, одинаково для R и C. Разница 6 нс — обёртки
и результат в структуре `RRuntimeOwn` (allocation, type, alignment = 32 байта на
каждый `own`).

Что лишнее: для `type.alignment <= _Alignof(max_align_t)` можно звать `malloc`
напрямую (пропустив ветвь выравнивания) и хранить в `own` только указатель, если
тип известен статически (codegen знает `T`). Ожидаемый эффект: 22 → ~17 нс.

### 22.6. Общие наблюдения

- В профилях всех синхронных ядер видны `__psynch_cvwait`/`__workq_kernreturn` —
  простаивающие рабочие потоки исполнителя, создаваемые при старте runtime даже для
  программ без `async`. На wall clock не влияют (база вычитается), но потоки можно
  создавать лениво при первом `task_start`.
- Проверки границ и арифметики (`index_fixed`, `checked_arith`) стоят 0.06–0.15 нс
  на операцию — это не оверхед, а контракт языка.
- Приоритет исправлений по выигрышу на типичном сервисе: 22.2 (fs/net операции),
  22.1 (eager-задачи), 22.3 (контейнеры), затем 22.4/22.5.

### 22.7. Что исправлено (2026-09-17, вторая половина дня)

- **Артефакт измерения.** Программы R в `tests/bench` линкуют runtime и std-архивы той
  конфигурации CMake, где запущена цель, а `build-debug` собран с `-O0 -g`: все ядра,
  проходящие через runtime, сравнивались с C-зеркалом на `-O2` нечестно. Теперь
  `run_benchmarks.py` получает `--build-type` и `--runtime-c-flags` от CMake, пишет их в
  таблицу «Окружение» и отказывается измерять из неоптимизированной сборки
  (`--check` для `r_bench_pairs_agree` работает везде). Замер ведётся из
  `build-release` (`-DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_RELEASE=-O2`).
- **Первый шаг задачи на потоке вызывающего (22.1).** Semantic помечает `ASYNC_START`,
  потребляемый непосредственно следующим `await` (`RHirNode.immediate_await` →
  `RMirInstruction.immediate_await`); генерируемые хелперы `r_async_launch/start_N`
  получили параметр `start_mode`: 0 — как раньше (`dispatch_async_f`), 1 —
  `r_runtime_task_start_commit_initialize_inline(…, R_STACK_ENTRY(r_async_step_N))`:
  commit, затем первый шаг исполняется на текущем потоке, если
  `r_runtime_stack_can_require` подтверждает запас стека (иначе обычный dispatch);
  2 — участник task_scope: `…_commit_initialize_deferred` без dispatch, затем после
  `r_runtime_task_scope_bind` вызывается `r_runtime_task_run_deferred`. Шаг, который
  приостанавливается, продолжает жить на исполнителе как прежде; ожидающий ничего не
  делает между commit и await, поэтому наблюдаемый порядок не меняется (модель ленивого
  запуска, как `Future::poll`). `task_worker` разбит на `task_execute(task, on_worker)`:
  inline-путь не переинициализирует границы стека и не чистит thread-local текущего
  потока. Три новых входа runtime в инвентаре (771 записей).
- **Рост массива и push (22.3).** `r_runtime_array_reserve_valid` для элементов без
  `move_initialize` переаллоцирует блок целиком через `r_runtime_allocator_reallocate`
  (`realloc`) вместо поэлементного переноса; `r_std_array_push` маппит статус только при
  ошибке.
- **Форматирование (22.4).** `r_format_append_padded` пропускает `memset` нулевой длины;
  десятичная ветка `r_library_internal_format_integer` делит на константу.
- **Не исправлено: файловый ввод-вывод (22.2).** Файл открывается как STREAM-хэндл
  (`async_open.c`), библиотека передаёт абсолютную позицию через
  `r_runtime_darwin_io_prepared_set_stream_position`, а runtime ставит
  `dispatch_io_barrier` + `lseek` и создаёт `dispatch_io`-канал на каждую операцию ради
  `DISPATCH_IO_STOP`. Дешёвый путь — RANDOM-хэндл для обычных файлов со смещением в
  запросе (`prepared_set_offset` вместо барьера) и `pread`/`pwrite` на исполнителе без
  канала — требует пересмотра отмены/дедлайна в `io_request.c`, семантики `append`
  (O_APPEND и pwrite) и детерминированных гоночных тестов lane'а; это отдельная работа
  объёмом порядка дня, вынесена в очередь.

Итог после исправлений (Release-сборка, Apple M4, медианы 7 запусков минус база):

| Ядро | Итераций | R, нс/итер | C, нс/итер | R/C | До исправлений, R нс/итер |
| --- | ---: | ---: | ---: | ---: | ---: |
| `call_overhead` | 200000000 | 1.02 | 1.01 | 1.01 | 1.04 |
| `index_fixed` | 204800000 | 0.75 | 0.74 | 1.01 | 1.08 |
| `index_slice` | 204800000 | 0.73 | 0.75 | 0.98 | 0.85 |
| `checked_arith` | 200000000 | 1.02 | 0.98 | 1.04 | 1.11 |
| `async_task_start` | 2000000 | 100.46 | 0.31 | 324.06 | 331 |
| `async_scoped_start` | 2000000 | 121.95 | 0.35 | 350.09 | 396 |
| `async_fs_read_into` | 20000 | 22360.48 | 317.64 | 70.40 | 23 476 |
| `array_push` | 50000000 | 5.32 | 0.63 | 8.40 | 9.70 |
| `format_int` | 5000000 | 38.01 | 48.11 | 0.79 | 66.8 |
| `own_alloc` | 20000000 | 21.46 | 15.57 | 1.38 | 22.0 |

Колонка «до исправлений» — замер из Debug-сборки утром того же дня, поэтому для
`format_int`/`own_alloc` часть разницы — оптимизация runtime, а не правки. Остаток
`array_push` (≈5 нс) — цепочка вызовов через границу библиотеки и структура результата на
каждый push; следующий шаг — inline fast path в заголовке `r_std_array.h` для POD-элементов.
Остаток `own_alloc` (+6 нс) — обёртки аллокатора и 32-байтовый `RRuntimeOwn`; оставлено.

### 22.8. Inline-пути контейнерных операций в сгенерированном C (2026-09-17)

Остаток `array_push` и «подобные» операции — цепочка `сгенерированный C → r_std_* →
r_runtime_*` через границу статического архива, которую компилятор C не инлайнит. Вместо
новых символов и правок инвентаря эмиттер теперь раскрывает горячие операции по полям
заголовков `RRuntimeArray`/`RRuntimeString` прямо в сгенерированном C (обе схемы: sync и
async-state-machine, `compiler/codegen/c17.c`):

- `std.array::push` для элементов без move-glue: типизированная запись
  `((T *)a->data)[a->length] = v; a->length += 1U;` пока `length < capacity`, иначе прежний
  вызов `r_std_array_push` (рост и носитель `push_error`).
- `std.array::get`/`get_mut` для любого `T`: проверка `index < length` и адрес
  `data + index * element.size`; статус статически `SUCCESS`, проверка контракта не
  эмитится.
- `std.array::pop` для элементов без move-glue: `length -= 1U` и типизированная загрузка в
  payload option.
- `std.string::len`/`capacity`/`as_str`/`as_bytes`: чтение `bytes.length`/`bytes.capacity`/
  `bytes.data` (пустая строка даёт `NULL`, как `r_runtime_string_bytes`).
- `std.string::append_str`: пока `suffix.length <= capacity - length` — один `memcpy` в
  свободную вместимость (префлайт включает `<string.h>`), иначе `r_std_string_append_str`
  с ростом и носителем `alloc_error`.
- Шимы `library/std/{array,bytes,dict,list,string}` (17 файлов) маппят статус runtime только
  при ошибке; на успехе выставляют `*_CALL_SUCCESS` напрямую.

Библиотечные символы и их ABI не изменились: медленный путь вызывается по-прежнему.
Не раскрыты (сознательно): `dict` (стоимость в хешировании), `list` (аллокация узла),
`push_scalar`/`append_utf8` (кодирование и проверка UTF-8 дороже вызова).
Тестовые обёртки, перехватывающие `r_std_array_push` для подсчёта вызовов
(`tests/codegen_generic_vector_wrapper.c`), учитывают, что элементы без move-glue больше не
доходят до библиотеки; пять символов (`r_std_array_get/get_mut`, `r_std_string_len/
capacity/as_bytes`) ушли из замкнутой поверхности генерируемого C в инвентаре
runtime-входов (766 записей).
Новые пары `tests/bench`: `array_get` (200M `get` с разбором option) и `string_append`
(20M `append_str` + `len`).

Release-замер после 22.8 (Apple M4, медианы 7 запусков минус база):

| Ядро | Итераций | R, нс/итер | C, нс/итер | R/C | До 22.8, R нс/итер |
| --- | ---: | ---: | ---: | ---: | ---: |
| `async_task_start` | 2000000 | 99.07 | 0.79 | 126.06 | 100 |
| `async_scoped_start` | 2000000 | 122.21 | 0.77 | 158.96 | 122 |
| `array_push` | 50000000 | 2.37 | 0.62 | 3.84 | 5.32 (после 22.7) |
| `format_int` | 5000000 | 38.40 | 47.94 | 0.80 | 38.0 |
| `own_alloc` | 20000000 | 21.24 | 15.30 | 1.39 | 21.5 |
| `array_get` | 200000000 | 0.34 | 0.29 | 1.16 | — |
| `string_append` | 20000000 | 0.53 | 0.49 | 1.09 | — |

`array_get` и `string_append` до 22.8 замерялись только вручную (≈2 нс и 3.9 нс на итерацию в
Release); `array_get` после раскрытия векторизуется компилятором C так же, как зеркало.

### 22.9. Эксперимент: LTO для сгенерированных программ (2026-09-17)

Остаток `array_push` (2.4 нс против 0.6) — заголовок массива живёт в памяти, потому что его
адрес уходит в невстраиваемые вызовы архива (`r_std_array_create`, медленная ветка
`r_std_array_push`). Проверено на C-модели: раскладка «три указателя» не помогает
(2.0–2.2 нс против 1.9–2.5), staged-копия внутри медленной ветки тоже. Помогает только
видимость всех использований для компилятора C, то есть LTO.

Постановка: отдельная конфигурация `build-lto` (`-DCMAKE_BUILD_TYPE=Release
"-DCMAKE_C_FLAGS_RELEASE=-O2 -flto" -DR_BENCH_GENERATED_C_EXTRA_FLAG=-flto`). Хук:
cache-переменная `R_BENCH_GENERATED_C_EXTRA_FLAG` в `tests/bench/CMakeLists.txt` передаёт
`GENERATED_C_EXTRA_FLAGS` в `tests/check_codegen_program.cmake`, где флаг добавляется к
финальной компиляции и линковке программы (в обычных сборках пусто). Рабочие
конфигурации и `docs/benchmarks.md` не тронуты; отчёт эксперимента в scratchpad.

| Ядро | Release, нс/итер | Release + LTO, нс/итер | C | R/C с LTO |
| --- | ---: | ---: | ---: | ---: |
| `array_push` | 2.37 | 0.93 | 0.62 | 1.49 |
| `format_int` | 38.4 | 26.3 | 49.5 | 0.53 |
| `array_get` | 0.34 | 0.31 | 0.30 | 1.02 |
| `string_append` | 0.53 | 0.56 | 0.54 | 1.03 |
| `async_task_start` | 99 | 97 | 1.4 | 70 |
| `async_scoped_start` | 122 | 114 | 1.0 | 117 |
| `own_alloc` | 21.2 | 1.69 | 15.3 | 0.11 (см. ниже) |

Выводы:

- LTO снимает бо́льшую часть остатка контейнерных операций: `array_push` 2.4 → 0.9 нс
  (заголовок продвинут в регистры после встраивания `create`/`push`), `format_int`
  быстрее `snprintf`. Async-ядра не меняются: их стоимость в примитивах runtime.
- `own_alloc` под LTO — артефакт: компилятор целиком удаляет пару `malloc`/`free`, как
  раньше удалял в зеркале C. Ядру R нужен наблюдаемый сток (например, `volatile`-запись
  через `core::volatile_*`), иначе метрика бессмысленна под LTO.
- Коды завершения всех 14 пар совпадают (`r_bench_pairs_agree` в `build-lto`).

Что нужно для перевода LTO в рабочий режим (решение за владельцем):

1. Согласовать с дисциплиной стека R-FUNC-0004: бюджеты входов измерены на `-O0` без
   LTO; после встраивания кадр вызывающего растёт не больше, чем на кадр вызываемого,
   который уже входит в бюджет цепочки, то есть оценка остаётся консервативной — но это
   надо зафиксировать в `check_codegen_stack_usage.cmake` и в описании инвентаря
   runtime-входов, а не полагаться на рассуждение.
2. Флаги сборки архивов и линковки программ в манифесте цели (`targets/*.json`), чтобы
   `check_target_toolchain.py` и ABI-дайджесты учитывали `-flto`; время сборки и
   `libtool`/`ld` с bitcode.
3. Сток для `own_alloc` и проверка, что ни одно ядро не «исчезает» под LTO
   (сравнение с не-LTO числами в отчёте).

### 22.10. Замер `dict`, `std.json`, `std.deflate`, TCP (2026-09-17)

Четыре новые пары в `tests/bench` (всего 18):

- `dict_lookup` — 65 536 вставок в `dict<i32, i32>` и 50 000 000 поисков по псевдослучайным
  присутствующим ключам с разбором option. Зеркало: открытая адресация с линейным
  пробингом, тот же 64-битный микс, нагрузка 2/3, отдельное хранилище записей — то же, что
  `RRuntimeDict`.
- `json_parse` — 200 000 разборов документа ~700 байт в дерево `std.json::value`, каждое
  дерево освобождается. Зеркало: рекурсивный спуск, строящий дерево с векторами детей и
  копиями строк (без экранирования, которого нет в документе).
- `deflate_roundtrip` — 200 раундов сжатия и распаковки 64 KiB псевдотекста в zlib-кадре
  на уровне 6 через `std.deflate` (модуль на R). Зеркало: `compress2`/`uncompress`
  системного zlib. Сравнение «кодек на R против zlib», контрольная сумма считается по
  восстановленным данным, а не по размеру сжатого.
- `tcp_echo` — 20 000 обменов по 4 KiB через loopback: `tcp_write_all_from` на клиенте и
  `tcp_read_into` на принятом потоке (scoped-операции в `task_scope(1)`). Зеркало:
  блокирующие `write`/`read` в одном потоке.

Сопутствующие правки: r-front для пар получает `--library-map library/r/library.map`
(R-модуль `std.deflate`); зеркало deflate линкуется с `z`.

**Исправлен дефект эмиттера.** Catch-клауза, ошибку которой тело `try` не бросает,
давала в сгенерированном C неиспользуемую метку и падала под `-Werror`. Теперь обработчик
помечается `used` при выборе (`r_c17_find_effect_handler`), а блок catch без переходов не
эмитится — остаётся только `(void)` на его payload. Фикстура
`tests/fixtures/codegen_unreachable_catch.r`.

Release-замер (Apple M4, медианы 7 запусков минус база), `r_bench_pairs_agree` зелёный на
всех 18 парах:

| Ядро | Итераций | R | C | R/C | Где время (профиль `sample`) |
| --- | ---: | ---: | ---: | ---: | --- |
| `dict_lookup` | 50M | 21.6 нс | 9.1 нс | 2.4 | `r_runtime_dict_get`: хеш и сравнение ключа через указатели на функции (`key.hash`, `key.equal`), микс с seed, смещения записей вычисляются по `entry_size`/`key_offset` во время выполнения |
| `json_parse` | 200k | 11.6 мкс | 2.2 мкс | 5.4 | `r_json_scanner_feed` ~40 %, malloc/free ~25 %, `array_reserve/push/initialize` ~20 % (массив детей на узел, рост от 4 удвоением), tree builder ~10 % |
| `deflate_roundtrip` | 200 | 1.54 мс | 0.13 мс | 12 | равномерно по функциям кодека на R (поиск совпадений, Хаффман); zlib — ручная оптимизация тех же шагов |
| `tcp_echo` | 20k | 33 мкс | 6 мкс | 5.5 | `kevent_id` ~70 %, сами `write`/`read` ~25 %: на каждую операцию dispatch-канал и ожидание готовности, тот же механизм, что в 22.2 |

Что из этого лишний оверхед, а что цена дизайна:

- **`dict` (2.4×).** Лишнее: два косвенных вызова на поиск и универсальная раскладка
  записи. Для ключей-целых (`i32`/`u64`/`usize`) codegen может передавать
  специализированные `hash`/`equal`, а runtime — держать быстрый путь «ключ помещается в
  8 байт»: сравнение по значению без вызова, хеш инлайн. Ожидание 21 → ~12 нс. Остаток —
  micro-архитектура пробинга, сопоставимая с C.
- **`std.json` (5.4×).** Лишнее: аллокация массива детей на каждый объект/массив и рост
  с 4; сканер по байту с завершением токена через несколько вызовов. Предложение:
  предразмер детей по подсчёту токенов первого прохода или арена узлов на документ,
  сканер по срезу с прямым переходом по таблице классов символов. Ожидание 11.6 → 4–5 мкс.
- **`std.deflate` (12×).** Это кодек, написанный на R, против zlib; профиль ровный.
  Следующий шаг — профиль с именами функций модуля (сопоставление `r_fN` ↔ R-функции) и
  сравнение с zlib по этапам: хеш-цепочки поиска совпадений и табличное декодирование
  Хаффмана дадут основную часть. Цель — 3–4×, паритет с zlib не ставится.
- **TCP (5.5×).** Тот же lane, что и файловый ввод-вывод: канал `dispatch_io` на операцию и
  ожидание через kqueue. Решение общее с 22.2; для сокетов `dispatch_io` остаётся, но
  канал должен жить на хэндле, а не на операции. Ожидание 16 → 4–6 мкс на операцию.

Приоритет: TCP/fs (общее решение, самый большой абсолютный выигрыш для сервисов), затем
`std.json`, затем `dict`; `std.deflate` — по потребности.


### 22.11. Мономорфизация операций `std.dict` по типу ключа (2026-09-17)

Решение владельца: мономорфизировать всё, что можно, сохраняя раскладку runtime-структур
(drop-glue, итерация, ABI и библиотечные модули продолжают работать с теми же
`RRuntimeArray`/`RRuntimeDict`/`RRuntimeList`). Для словаря сделано так:

- Раскладка индекса и записей (`RRuntimeDictIndexSlot`, `RRuntimeDictEntryHeader`,
  состояния слотов) опубликована в `runtime/include/r_runtime_dict.h` как часть контракта
  runtime; хеш-микс `r_runtime_dict_mix` зеркалится эмиттером (`r_dict_mix`), обе стороны
  помечены комментариями: записи, вставленные любым путём, должны находиться другим.
- Эмиттер по типу ключа `K` генерирует в программе `static inline` хелперы с сигнатурами
  библиотечных шимов: `r_dp_<K>` (пробинг), `r_dl_<K>` (поиск), `r_dg_<K>`/`r_dgm_<K>`/
  `r_dc_<K>` (`get`/`get_mut`/`contains`) и `r_di_<K>` (`insert`). Хеш и сравнение ключа —
  прямые вызовы уже существующих glue `r_kh_<K>`/`r_ke_<K>`, которые компилятор C инлайнит;
  значение адресуется по `value_offset` заголовка, поэтому тип значения хелперу не нужен.
  `insert` обрабатывает замену и вставку без роста; рост, удаление и итерация остаются в
  runtime (`r_std_dict_insert` как медленный путь).
- Регистрация — битами по операциям в `key_types` (get/get_mut/contains/insert), эмитятся
  только используемые обёртки: clang считает неиспользуемую `static inline` функцию в
  `.c` ошибкой под `-Werror`.
- Из замкнутой поверхности генерируемого C ушли `r_std_dict_get/get_mut/contains`
  (инвентарь runtime-входов: 763 записи).

Release-замер после мономорфизации (Apple M4, медианы 7 запусков минус база):

| Ядро | До | После | C | R/C |
| --- | ---: | ---: | ---: | ---: |
| `dict_lookup` | 21.6 нс | 9.9 нс | 9.4 нс | 1.06 |

Оставшийся кандидат в словаре — типизированный перенос ключа/значения в `insert` (сейчас
через `RRuntimeTypeInfo`), заметен только на вставке. Далее по тому же образцу: `list`
(`get`/`front`/`back`/`pop_*` — узел уже аллоцирован, ожидаемый выигрыш ≤ 10 %, так как
стоимость в аллокации узла), редкие операции `array` (`reserve`, `remove`, `clear`) и
`dict::remove` (O(n) компактификация, редкая).


### 22.12. Единый механизм мономорфизации контейнеров (2026-09-17)

Решение владельца — унификация, а не выборочный выигрыш. Все три контейнера теперь
обслуживаются одним механизмом эмиттера (`compiler/codegen/c17.c`):

- **Реестр хелперов.** При эмиссии вызова операции точка вызова регистрирует
  `(семейство, тип элемента или ключа, бит операции)` через
  `r_c17_require_container_helper` и пишет вызов по имени хелпера. После эмиссии всех
  функций `r_c17_emit_container_helpers` генерирует определения в отдельный буфер и
  вставляет их по якорю, записанному сразу после key-glue (`r_c17_mark_container_anchor`):
  хелперы стоят до функций, но их набор известен только после обхода тел. Тип элемента
  берётся из узла (`auxiliary_type` у операций array/list без эффекта, тип ошибки у
  проверяемых, `runtime_type` у dict), префлайт не задействован.
- **Именование** — общее для всех: `r_<префикс>_<kind>_<discriminator>`, как у glue
  `r_kh_/r_ke_` (`r_c17_emit_container_helper_name`); сигнатуры — с сигнатурами
  библиотечных шимов, поэтому код вокруг вызовов не меняется. Сигнатура переносится по
  правилам clang-format только когда не помещается в 100 колонок
  (`r_c17_emit_helper_signature`).
- **`array`** (`r_ap/r_apo/r_ag/r_agm`): типизированная запись и загрузка (`*(T *)dst =
  *(const T *)src` или move-glue для типов с drop), рост — через `r_std_array_push`.
- **`list`** (`r_ln`, `r_lpf/r_lpb/r_lib/r_lia`, `r_lf/r_lb/r_lfm/r_lbm/r_lg/r_lgm`,
  `r_lpof/r_lpob`, `r_lr`): раскладка узла опубликована в `r_runtime_list.h`, узел
  аллоцируется через `r_runtime_allocator_allocate` с маппингом статуса как в шиме (без
  повторной попытки через библиотеку — важно для тестов с инъекцией отказа), связывание
  через общие `r_list_link_before`/`r_list_unlink`, эмитируемые только при наличии
  соответствующих операций.
- **`dict`** (`r_dp/r_dl/r_dg/r_dgm/r_dc/r_di`): как в 22.11, но через тот же реестр.
- Эмитируются только используемые обёртки: clang считает неиспользуемую `static inline`
  функцию в `.c` ошибкой.

Из замкнутой поверхности генерируемого C ушли `r_std_array_pop` и все операции `list`,
кроме `create/clear/iter/next`; `r_runtime_allocator_allocate/deallocate` стали прямыми
входами (категория `runtime-allocator`); инвентарь runtime-входов — 750 записей.
Тестовые обёртки, перехватывавшие шимы (`generic_vector`, `container_ownership_failures`),
переведены на перехват `r_std_array_with_capacity` (сброс вместимости, чтобы каждый push
шёл через библиотеку) и `r_runtime_allocator_allocate` (отказ на аллокации узла списка);
проверка артефактов массива считает вызовы `r_ap_*`.

Release-замер после унификации (Apple M4): числа контейнерных ядер не изменились по
сравнению с 22.8 и 22.11 — `array_push` 2.4 нс, `array_get` 0.32 нс, `string_append`
0.5 нс, `dict_lookup` 9.2 нс (паритет); полный ctest (1845) зелёный. Пары для `list`
не добавлялись: стоимость его операций — аллокация узла, а механизм теперь общий.

## 23. За пределами этапа

- Linux runtime (`runtime/linux`, второй target manifest, cross-сборка).
- Shared-library экспорт с version script и SONAME.
- Независимый аудит компилятора и второй компилятор для generated C.

## 24. Базовые библиотеки (M18–M31, решение владельца от 28 сентября 2026)

Анализ пробелов после L25 и решение владельца «реализовываем все» записаны в
[дорожной карте](language-completeness-roadmap.ru.md), подраздел «Анализ пробелов и решение
владельца от 28 сентября 2026». Вехи M18–M31 продолжают нумерацию этого плана (M16 —
`std.deflate`, M17 — `std.xml`) и идут вместе с языковыми этапами L26–L37 в одной очереди:

| Веха | Содержание |
| --- | --- |
| M18 | `std.stream`, `std.bufio`, `std.console` |
| M19 | `std.text`, курсоры `std.bytes`, `std.encoding`, адаптеры `std.iter`, группы `std.regex` |
| M20 | `std.random`, `std.uuid`, HMAC |
| M21 | RFC 3339, HTTP-date, `std.time::interval` |
| M22 | `std.signal`, Unix-сокеты, опции сокетов, подключение по имени |
| M23 | `std.log`, `std.args`, `std.config` |
| M24 | `@test` и `std.test` |
| M25 | `std.tls` поверх mbedTLS |
| M26 | `std.url`, `std.mime`, `std.http` |
| M27 | `std.websocket`, DNS SRV и TXT |
| M32 | `std.jsonrpc`, `std.mcp`: Model Context Protocol ревизии 2026-07-28 (вставлена 30 сентября по запросу владельца, выполнена сразу после M27) |
| M32T | Расширение Tasks для MCP: задачи сервера и клиента `std.mcp` (подэтап сразу после M32, выполнен 1 октября) |
| M28 | `std.cbor`, `std.cose`, `std.crypto` поверх libsodium |
| M29 | `std.sqlite` поверх системной libsqlite3 |
| M30 | `std.metrics`, `std.trace`, расширения `std.service` |
| M31 | Арены, пулы, квоты поверх бюджетов L34 |

Платформенные работы раздела 23 в этот объём не входят.
