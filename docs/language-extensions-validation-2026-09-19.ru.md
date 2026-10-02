# R: функции как значения и обобщённые наборы ошибок

Дата: 19 сентября 2026. Checkout: основной R, ветка `main`.
Объём: первый блок [плана завершения языка](language-completeness-roadmap.ru.md),
L1 без явного закрытия generic-функций и L2.

## Реализовано

- Безопасная закрытая обычная функция образует номинальное Copy-callable значение.
  Его можно сохранить через `auto`, передать generic-алгоритму, вернуть через
  `opaque` и вызвать непосредственно либо через `.call`. Вызов сохраняет исходную
  функцию, её статические переменные, borrow-контракт и участие в графе вызовов.
- Поддержаны shared/mut/once-контракты синхронных функций, once-контракт async,
  checked errors, borrowed result, `@noalloc` и `@nonblocking`.
- `E: errors` задаёт параметр конечного набора checked errors. Вывод поддерживает
  пустой набор, один и несколько типов, объединение и удаление дубликатов.
  Ограничения вроде `unborrowed` и `send` проверяются для каждого члена набора.
- Вложенные адаптеры, именованный `catch`, `finally`, borrowed payload и async
  сохраняют проверки владения и очистку. При пустом наборе исчезает ненужный
  checked-error carrier. Ошибки запуска async остаются отдельными.
- Нормативные EN/RU Core обновлены до `0.1.0-draft.47`, interface schema — до 15;
  синхронизированы EBNF, inventories, target manifests и ожидаемые метаданные тестов.
  Library остаётся draft.18; `conformance_claim` остаётся `false`.

## Примеры и проверки контрактов

- `tests/fixtures/codegen_function_items.r`: хранение, копирование, прямой и generic
  вызовы, opaque, общая исходная static-переменная, borrowed result, checked и void.
- `tests/fixtures/codegen_async_function_items.r`: прямой и generic async-вызовы.
- `tests/fixtures/codegen_generic_error_sets.r`: `map`, `retry`, композиция и
  транзакционный адаптер. `retry` повторно вызывает mut-замыкание с захваченным Move
  владельцем; транзакционный пример сохраняет обновление при успехе и восстанавливает
  исходное состояние при ошибках callback-а с одним и двумя типами ошибок.
- Отдельные fixtures проверяют borrowed error payload и async-наборы ошибок.
- `tests/check_function_items.py`: межмодульные контракты, воспроизводимость interface
  и C17 при перестановке входных модулей, прямые цели вызовов, канонические наборы.
- Семантические негативные проверки отклоняют недопустимые unsafe/open/overloaded
  function items, усиление скрытого opaque-контракта, рекурсию, утечку borrow,
  наборы ошибок вместо значений, неоднозначный вывод и async без нужных гарантий.

## Результаты проверки

Сборки Debug и ASan/UBSan завершились без ошибок и предупреждений компилятора.
Сетевые проверки выполнены с разрешёнными локальными TCP/UDP-сокетами: sandbox
не разрешает даже `bind(127.0.0.1)`, что не является дефектом языка.

| Проверка | Результат |
| --- | --- |
| Полный Debug: `ctest --test-dir build-debug --output-on-failure -j6` | 1856/1856, 261.98 с |
| Debug после добавления транзакционного примера: `ctest --test-dir build-debug --output-on-failure -R generic_error_sets -j2` | 6/6, 3.04 с |
| Полный ASan/UBSan: `ctest --test-dir build-sanitize --output-on-failure -j4` | 1271/1271, 430.63 с; включает транзакционный пример |
| `specification-check` | EN/RU parity, ссылки, EBNF и манифесты прошли; 274 productions, 473 Core rules, 236 Library rules |
| `specification-render-check` | 4 документа, Asciidoctor 2.0.26 |
| clang-format 22.1.8, изменённые C-файлы | Пройдено |
| `python3 tools/check_c_style.py --root .` | Пройдено, 1042 handwritten C files |

Полный Debug был выполнен до последнего расширения fixture транзакционным примером;
после этого менялись только fixture и документация. Группа затронутых тестов повторена.
Sanitizer-конфигурация включает AddressSanitizer и UndefinedBehaviorSanitizer для
компилятора/runtime и запускаемых generated-C программ согласно её CMake-контракту.

Логи этого выполнения:

- `/private/tmp/r-language-extensions-debug-final-build.log`
- `/private/tmp/r-language-extensions-debug-final-tests.log`
- `/private/tmp/r-language-extensions-transaction-debug.log`
- `/private/tmp/r-language-extensions-sanitize-final-build.log`
- `/private/tmp/r-language-extensions-sanitize-final-tests.log`
- `/private/tmp/r-language-extensions-spec-check.log`

Общая проверка clang-format по всему репозиторию обнаружила прежние отклонения в
девяти файлах вне этого изменения: `compiler/codegen/c17.c`, `runtime/source/array.c`,
четырёх `tests/bench/*.c` (`deflate_roundtrip`, `dict_lookup`, `json_parse`, `tcp_echo`),
`tests/codegen_container_ownership_failures_wrapper.c`,
`tests/codegen_json_reader_transports_wrapper.c` и `tests/codegen_tests.c`.
Они не переформатированы; полный repository-wide format check не считается пройденным.

## Оставшиеся границы

- Open generics и семейства перегрузок требуют обёртки; явное закрытие generic
  function item относится к L8. Автоматическое связывание receiver не введено.
- В одной выводимой callable-сигнатуре допускается один параметр набора ошибок;
  независимые callable-параметры могут выводить разные наборы, объединяемые адаптером.
- У набора нет runtime-значения и операции разности. Именованный `catch` внутри
  generic-тела не сужает объявленный `throws E`; сохраняется консервативный контракт.
- L7 (`out` и контроль каждой версии результата), L8 (явные generic-аргументы),
  L4 (автоматический compile-time), L5 (динамические интерфейсы) и L9 (scoped tasks)
  остаются в принятом плане. Текущий блок не означает завершения всей работы.
- На момент этого отчёта требовался выбор [состояния `out` при checked error](out-results-design.ru.md).
  Позже в тот же день владелец подтвердил вариант A; отдельная реализация и её проверки
  записаны в [отчёте по `out`](out-a-validation-2026-09-19.ru.md).

Полной приёмки нового языка для всех target/profile, fuzz-прогона и TSan в этом
блоке нет; перечисленные проверки не заменяют эти отдельные результаты.
