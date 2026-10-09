#!/usr/bin/env python3
"""Time the R/C benchmark pairs of tests/bench and write docs/benchmarks.md.

Every pair consists of an R program, an object of r-front linked with the runtime and the library,
and a C mirror compiled by the pinned clang at -O2. Both return the same checksum modulo 109 as their exit
status (within the application range 0..111), which --check verifies without timing. The measurement mode runs each executable several
times, takes the median wall-clock time, subtracts the median of the empty ``baseline`` pair and
reports the per-iteration cost and the R/C ratio.
"""

from __future__ import annotations

import argparse
import datetime as _datetime
import hashlib
import json
import platform
import statistics
import subprocess
import sys
import time
from pathlib import Path
from typing import Any


C_MIRROR_FLAGS = (
    "-std=c17 -pedantic-errors -Wall -Wextra -Werror -Wconversion -Wsign-conversion "
    "-Wshadow -Wstrict-prototypes -Wmissing-prototypes -O2"
)

KERNEL_NOTES = {
    "baseline": "пустой `i32 main()`; медиана вычитается из остальных метрик",
    "call_overhead": (
        "200 000 000 вызовов `protected u32 step(u32, u32)`; вызовы между функциями R не несут "
        "проверок стека: статическая дисциплина R-FUNC-0004 выполняет одну проверку "
        "`r_runtime_stack_require` с границей входа `main` на входе в код R"
    ),
    "index_fixed": (
        "50 000 проходов по `u32[4096]` с индексированием `data[index]`; в R каждый доступ "
        "несёт проверку границ (`bounds` panic)"
    ),
    "index_slice": (
        "50 000 вызовов `fold(const u32[] values, u32 seed)` по срезу из 4096 элементов; "
        "проверка границ идёт по `len(values)`, длина известна только во время выполнения"
    ),
    "checked_arith": (
        "200 000 000 итераций `total = total + x - 100` на `i64`; каждое `+` и `-` несёт "
        "проверку переполнения R-EXPR-0005 (`integer_overflow` panic), в C — обычная арифметика"
    ),
    "baseline_async": (
        "пустой `async i32 main()`: старт и остановка executor; медиана вычитается из ядер "
        "с префиксом `async_`"
    ),
    "async_task_start": (
        "2 000 000 вызовов `await step(...)` пустой async-функции: двухфазный запуск, "
        "резервирование кадра задачи, публикация и потребление результата; в C — прямой вызов"
    ),
    "async_scoped_start": (
        "2 000 000 вызовов `await step(&x, ...)` функции `@scoped async` внутри одной "
        "`task_scope(1)`: резервирование слота группы, bind, займ и его освобождение; в C — прямой вызов"
    ),
    "async_fs_read_into": (
        "20 000 scoped-чтений по 4 КиБ из обычного файла `/usr/share/dict/words` через "
        "`std.fs::read_into` (адаптер файловых передач, задача на операцию); в C — блокирующий "
        "`read(2)`"
    ),
    "array_get": (
        "200 000 000 `std.array::get` по массиву из 4096 элементов с разбором option; codegen "
        "разворачивает проверку границ по полям заголовка без вызова библиотеки"
    ),
    "string_append": (
        "20 000 000 `append_str` по восемь байт с очисткой каждые 4096 добавлений и `len` после "
        "каждого; в C — буфер с удвоением и `memcpy`"
    ),
    "dict_lookup": (
        "65 536 вставок в `dict(i32, i32)` и 50 000 000 поисков по псевдослучайным ключам с разбором "
        "option; в C — открытая адресация с линейным пробингом, тем же 64-битным миксом и "
        "нагрузкой 2/3"
    ),
    "json_parse": (
        "200 000 разборов документа 492 байта в дерево `std.json::value` с освобождением; в C — "
        "рекурсивный спуск, строящий дерево с векторами детей и копиями строк"
    ),
    "deflate_roundtrip": (
        "200 раундов сжатия и распаковки 64 KiB псевдотекста (zlib-кадр, уровень 6) через "
        "`std.deflate`, написанный на R; в C — `compress2`/`uncompress` системного zlib"
    ),
    "tcp_echo": (
        "20 000 обменов по 4 KiB через loopback TCP: `tcp_write_all_from` на клиенте и "
        "`tcp_read_into` на принятом потоке; в C — блокирующие `write`/`read` в одном потоке"
    ),
    "array_push": (
        "50 000 000 `push` в `array(u32)` с последующей свёрткой среза; в C — вектор с удвоением "
        "через `realloc`"
    ),
    "format_int": (
        "5 000 000 рендеров `f\"{index}\"` в `std.string::string` с измерением длины; в C — "
        "`snprintf` в свежую heap-строку на итерацию"
    ),
    "own_alloc": (
        "20 000 000 итераций `own u32* p = new u32(...)` с уничтожением в конце тела цикла; "
        "в C — `malloc`/`free`"
    ),
}


def baseline_name_for(name: str, default: str, available: dict[str, Any]) -> str:
    """Async kernels subtract the async baseline when it was measured."""
    if name.startswith("async_") and "baseline_async" in available:
        return "baseline_async"
    return default


def run_once(executable: Path) -> tuple[int, int]:
    started = time.perf_counter_ns()
    completed = subprocess.run([str(executable)], check=False, capture_output=True)
    elapsed = time.perf_counter_ns() - started
    return completed.returncode, elapsed


def sha256_of(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def command_line(*arguments: str) -> str:
    try:
        completed = subprocess.run(
            list(arguments), check=False, capture_output=True, text=True, timeout=30.0
        )
    except (OSError, subprocess.TimeoutExpired):
        return "unavailable"
    text = (completed.stdout or completed.stderr).strip()
    return text.splitlines()[0] if text else "unavailable"


def parse_pairs(values: list[str]) -> dict[str, tuple[Path, Path]]:
    pairs: dict[str, tuple[Path, Path]] = {}
    for value in values:
        name, _, paths = value.partition("=")
        r_path, separator, c_path = paths.rpartition(":")
        if not name or separator != ":" or not r_path or not c_path:
            raise SystemExit(f"malformed --pair (expected NAME=R_EXE:C_EXE): {value}")
        pairs[name] = (Path(r_path), Path(c_path))
    return pairs


def parse_iterations(values: list[str]) -> dict[str, int]:
    iterations: dict[str, int] = {}
    for value in values:
        name, _, count = value.partition("=")
        if not name or not count.isdigit():
            raise SystemExit(f"malformed --iterations (expected NAME=COUNT): {value}")
        iterations[name] = int(count)
    return iterations


def measure(executable: Path, runs: int) -> dict[str, Any]:
    codes: set[int] = set()
    samples: list[int] = []
    for _ in range(runs):
        code, elapsed = run_once(executable)
        codes.add(code)
        samples.append(elapsed)
    if len(codes) != 1:
        raise SystemExit(f"{executable} returned different exit codes across runs: {sorted(codes)}")
    return {
        "executable": str(executable),
        "exit_code": codes.pop(),
        "runs": runs,
        "samples_ns": samples,
        "median_ns": int(statistics.median(samples)),
        "min_ns": min(samples),
    }


def format_ms(nanoseconds: float) -> str:
    return f"{nanoseconds / 1_000_000:.1f}"


def format_ns(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.2f}"


def format_ratio(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.2f}"


def classify(ratio: float | None) -> str:
    if ratio is None:
        return "нет данных"
    if ratio <= 1.10:
        return "паритет с C"
    if ratio <= 2.0:
        return "умеренная надбавка"
    return "существенная надбавка"


def render_markdown(report: dict[str, Any]) -> str:
    environment = report["environment"]
    lines = [
        "# Бенчмарки R против C (M5)",
        "",
        "Файл сгенерирован `tests/bench/run_benchmarks.py` (цель CMake `benchmarks`); правки",
        "вносятся в генератор, а не сюда.",
        "",
        "## Методика",
        "",
        "- Каждая метрика — пара программ `tests/bench/<имя>.r` и `tests/bench/<имя>.c` с одним и",
        "  тем же алгоритмом; обе возвращают одинаковую контрольную сумму кодом завершения, что",
        "  проверяет ctest `r_bench_pairs_agree` и каждый запуск измерения.",
        "- Программа R: объект `r-front --emit=object` на уровне оптимизации по умолчанию",
        "  (`--opt-level=2`) с каталогом бит-кода runtime и std-библиотек конфигурации",
        "  (`--bitcode-catalog`): их функции оптимизируются вместе с программой; границы стека",
        "  входов считаются по оптимизированному коду. Линковка с runtime и std-библиотеками как в",
        "  `tests/check_codegen_program.cmake` (`COMPILE_ONLY`), исходники runtime — с `-O2`.",
        f"- Программа C: тот же компилятор с флагами `{C_MIRROR_FLAGS}`.",
        "- Runtime и std-библиотеки берутся из той конфигурации CMake, где запущена цель;",
        "  измерение допускается только из оптимизированной сборки (`Release`, `RelWithDebInfo`,",
        "  `MinSizeRel`), иначе скрипт останавливается — Debug-архивы (`-O0`) искажают все ядра,",
        "  проходящие через runtime.",
        f"- Время — wall clock процесса (`time.perf_counter_ns` вокруг `subprocess.run`), {report['runs']}",
        "  запусков на программу, берётся медиана; из медианы вычитается медиана пары `baseline`",
        "  (пустой `main`), чтобы убрать старт процесса и инициализацию runtime. Отрицательные",
        "  разности округляются до нуля.",
        "- Отношение R/C считается по скорректированным медианам; `нс/итер` — скорректированная",
        "  медиана, делённая на число итераций ядра.",
        "",
        "## Окружение",
        "",
        "| Поле | Значение |",
        "| --- | --- |",
        f"| Дата | {environment['date']} |",
        f"| Машина | {environment['machine']} ({environment['architecture']}) |",
        f"| ОС | {environment['os']} |",
        f"| Компилятор C | {environment['c_compiler']} |",
        f"| r-front | sha256 `{environment['r_front_sha256']}` |",
        f"| Target manifest | `{environment['target_manifest']}` sha256 `{environment['target_manifest_sha256']}` |",
        f"| Python | {environment['python']} |",
        f"| Сборка runtime и std | `{environment['build_type']}`, флаги `{environment['runtime_c_flags']}` |",
        "",
        "## Результаты",
        "",
        "| Метрика | Итераций | R, мс (медиана) | C, мс (медиана) | R, нс/итер | C, нс/итер | Δ, нс/итер | R/C | Оценка |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |",
    ]
    for entry in report["pairs"]:
        lines.append(
            f"| `{entry['name']}` | {entry['iterations'] or '—'} | {format_ms(entry['r']['median_ns'])} | "
            f"{format_ms(entry['c']['median_ns'])} | {format_ns(entry['r_ns_per_iteration'])} | "
            f"{format_ns(entry['c_ns_per_iteration'])} | {format_ns(entry['delta_ns_per_iteration'])} | "
            f"{format_ratio(entry['ratio'])} | {entry['assessment']} |"
        )
    lines.extend(
        [
            "",
            "Сырые выборки, минимумы и коды завершения всех запусков лежат в JSON-отчёте рядом с",
            "целью (`benchmarks.json` в каталоге сборки).",
            "",
            "## Что измеряют ядра",
            "",
        ]
    )
    for entry in report["pairs"]:
        lines.append(f"- `{entry['name']}` — {KERNEL_NOTES.get(entry['name'], 'см. исходники пары')}.")
    lines.extend(
        [
            "",
            "## Замечания к накладным расходам",
            "",
            "- Вызов R→R компилируется в прямой вызов без проверок стека: статическая дисциплина",
            "  R-FUNC-0004 проверяет границу входа один раз на входе в код R. Колонка Δ даёт",
            "  оставшуюся абсолютную надбавку на итерацию (соглашение о вызовах, вызовы runtime,",
            "  которые не встроились).",
            "- Проверки границ и переполнения компилируются в предсказуемые ветки; отношение R/C",
            "  выше показывает, сколько из них оптимизатор смог убрать на конкретном ядре.",
            "- Решение остаётся за владельцем: строки таблицы с оценкой «существенная надбавка»",
            "  указывают, где горячие пути стоит держать в C через `extern \"C\"` (M2) либо",
            "  снижать число входов в функции R на итерацию.",
            "",
        ]
    )
    return "\n".join(lines)


OPTIMIZED_BUILD_TYPES = frozenset({"Release", "RelWithDebInfo", "MinSizeRel"})


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pair", action="append", default=[], help="NAME=R_EXE:C_EXE")
    parser.add_argument("--iterations", action="append", default=[], help="NAME=COUNT")
    parser.add_argument("--baseline", default="baseline")
    parser.add_argument("--runs", type=int, default=7)
    parser.add_argument("--check", action="store_true", help="only verify that both exit codes agree")
    parser.add_argument("--front", type=Path)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--target-manifest", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--build-type", default="", help="CMAKE_BUILD_TYPE of the runtime/library build")
    parser.add_argument("--runtime-c-flags", default="", help="C flags the runtime and std libraries were built with")
    parser.add_argument(
        "--allow-unoptimized-runtime",
        action="store_true",
        help="measure even when the runtime/library build type is not an optimized configuration",
    )
    arguments = parser.parse_args()
    if not arguments.check and arguments.build_type not in OPTIMIZED_BUILD_TYPES:
        if not arguments.allow_unoptimized_runtime:
            raise SystemExit(
                f"runtime build type {arguments.build_type or '(unset)'} is not optimized; "
                "configure a Release build (cmake -DCMAKE_BUILD_TYPE=Release) and run the benchmarks "
                "target there, or pass --allow-unoptimized-runtime"
            )

    pairs = parse_pairs(arguments.pair)
    iterations = parse_iterations(arguments.iterations)
    if not pairs:
        raise SystemExit("at least one --pair is required")
    runs = 1 if arguments.check else max(1, arguments.runs)

    results: dict[str, dict[str, Any]] = {}
    mismatches: list[str] = []
    for name, (r_path, c_path) in pairs.items():
        r_result = measure(r_path, runs)
        c_result = measure(c_path, runs)
        if r_result["exit_code"] != c_result["exit_code"]:
            mismatches.append(
                f"{name}: R exit {r_result['exit_code']} != C exit {c_result['exit_code']}"
            )
        results[name] = {"r": r_result, "c": c_result}
        print(
            f"{name}: exit R={r_result['exit_code']} C={c_result['exit_code']} "
            f"median R={format_ms(r_result['median_ns'])} ms C={format_ms(c_result['median_ns'])} ms"
        )
    if mismatches:
        for mismatch in mismatches:
            print(f"benchmark pair disagrees: {mismatch}", file=sys.stderr)
        return 1
    if arguments.check:
        print(f"benchmark pairs agree: {len(pairs)} pairs")
        return 0

    report_pairs: list[dict[str, Any]] = []
    for name, result in results.items():
        count = iterations.get(name, 0)
        baseline_key = baseline_name_for(name, arguments.baseline, results)
        is_baseline = name in (arguments.baseline, "baseline_async")
        baseline = results.get(baseline_key)
        baseline_r = baseline["r"]["median_ns"] if baseline else 0
        baseline_c = baseline["c"]["median_ns"] if baseline else 0
        if is_baseline:
            adjusted_r = result["r"]["median_ns"]
            adjusted_c = result["c"]["median_ns"]
        else:
            adjusted_r = max(0, result["r"]["median_ns"] - baseline_r)
            adjusted_c = max(0, result["c"]["median_ns"] - baseline_c)
        ratio = (adjusted_r / adjusted_c) if (adjusted_c > 0 and not is_baseline) else None
        report_pairs.append(
            {
                "name": name,
                "iterations": count,
                "r": result["r"],
                "c": result["c"],
                "adjusted_r_median_ns": adjusted_r,
                "adjusted_c_median_ns": adjusted_c,
                "r_ns_per_iteration": (adjusted_r / count) if count else None,
                "c_ns_per_iteration": (adjusted_c / count) if count else None,
                "delta_ns_per_iteration": ((adjusted_r - adjusted_c) / count) if count else None,
                "ratio": ratio,
                "assessment": "база" if is_baseline else classify(ratio),
            }
        )

    environment = {
        "date": _datetime.datetime.now(_datetime.timezone.utc).strftime("%Y-%m-%d"),
        "machine": command_line("sysctl", "-n", "machdep.cpu.brand_string"),
        "architecture": platform.machine(),
        "os": f"{platform.system()} {command_line('sw_vers', '-productVersion')}",
        "c_compiler": command_line(arguments.cc, "--version"),
        "r_front_sha256": sha256_of(arguments.front) if arguments.front else "unavailable",
        "target_manifest": arguments.target_manifest.name if arguments.target_manifest else "unavailable",
        "target_manifest_sha256": (
            sha256_of(arguments.target_manifest) if arguments.target_manifest else "unavailable"
        ),
        "python": platform.python_version(),
        "build_type": arguments.build_type or "unset",
        "runtime_c_flags": arguments.runtime_c_flags or "(none)",
    }
    report = {
        "schema": "r-benchmarks-0.1",
        "c_mirror_flags": C_MIRROR_FLAGS,
        "runs": runs,
        "baseline": arguments.baseline,
        "environment": environment,
        "pairs": report_pairs,
    }
    if arguments.json:
        arguments.json.parent.mkdir(parents=True, exist_ok=True)
        arguments.json.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    markdown = render_markdown(report)
    if arguments.output:
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(markdown, encoding="utf-8")
        print(f"wrote {arguments.output}")
    else:
        print(markdown)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
