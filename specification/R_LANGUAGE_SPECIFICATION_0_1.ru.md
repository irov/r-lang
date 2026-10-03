# R Core Language Specification 0.1

Нормативный черновик языка программирования R

| Поле | Значение |
|----|----|
| Документ | R Core Language Specification |
| Версия языка | 0.1 |
| Редакция документа | 0.1.0-draft.96 |
| Статус | Перевод нормативного черновика; не является стабильным стандартом R 1.0 |
| Язык документа | Русский перевод; ключевые нормативные термины приведены на английском |
| Целевой backend | ISO/IEC 9899:2018 (C17) |

## Предисловие

Настоящий документ определяет язык R независимо от его реализаций. Он построен по модели стандарта C: задаёт представление программы, синтаксис, ограничения, семантику, абстрактную машину, библиотечные требования и пределы реализации. Текст стандарта C не включён и не воспроизводится. Сходство с C относится к поверхностному синтаксису, модели translation unit, выражениям и системному назначению; правила безопасности, владения и вычисления принадлежат R.

Нормативные положения обозначены стабильными идентификаторами `R-...`. Слова **shall**, **shall not**, **should** и **may** имеют смысл, указанный в разделе 3. Примечания и примеры ненормативны, если явно не сказано обратное.

<a id="design-summary"></a>

### Основные конструктивные решения

| Область | R 0.1 |
|----|----|
| Разбор объявлений | `struct` и `enum` вводят имена nominal types; в последующих type uses имя записывается непосредственно, например `Packet value` |
| Композиция программы | Modules связываются через `import`; import является semantic dependency |
| Видимость | Declarations уровня module и aggregate fields экспортируются по умолчанию; `protected` оставляет declaration внутри defining module |
| Преобразование типа | Явная форма `value as Type` |
| Ссылки | Ненулевые безопасные borrows: `const T*` и `T*`; nullable-форма оканчивается `?` |
| Владение | `own T*`, shared owners `arc T`/`rc T`, явные `move`/`clone`, weak owners и детерминированный `drop` |
| Atomic types | `atomic T`; `ai8`, `ai16`, `ai32`, `ai64`, `aisize`, `au8`, `au16`, `au32`, `au64` и `ausize` являются точными shorthand spellings, а не ordinary integers |
| Dynamic containers | `array<T>` — contiguous growable sequence; predefined `bytes` является его точным transparent alias `array<u8>`; `list<T>` — stable-node sequence; `dict<K,V>` — insertion-ordered hash table с linear probing |
| Program strings | `constexpr str` обозначает immutable UTF-8 data, встроенные в program image и valid всё время execution |
| Перемещение | Для named Move place `move` обязателен; temporary передаётся неявно |
| Восстанавливаемая ошибка | Function объявляет checked set `throws E1, E2`; `throw` передаёт одну error, а typed `try`/`catch` обрабатывает её без native exception unwinding |
| Borrow regions | Origins borrow выводятся из value flow и хранятся в compiler interface metadata |
| Raw pointers | `raw T*`; разыменование и арифметика только в `unsafe` |
| Порядок вычисления | Слева направо, кроме явно короткозамкнутых операторов |
| Overflow | Unsigned — modulo; signed — panic; constant overflow — диагностика |
| Массивы и slices | Массив не decay в pointer; индекс проверяется; slice всегда несёт длину |
| Read-only byte view | `const u8[]` является единственным borrowed byte-view type; direct call arguments могут формировать его без copy из byte strings, byte owners и fixed byte arrays |
| Control flow | Каждая ветвь `if` и loop body является `{ ... }` block; conditions содержат явное comparison; `switch` clauses являются неявными scopes и завершаются explicit transfer |
| Variants и varargs | Payload хранится в tagged enum; C variadics доступны через verified imported C API |
| Внешние C libraries | `extern "C"` + logical `@link`; physical artifacts задаёт target link manifest |
| Concurrency | Ownership transfer between threads: typed `spawn`/`join`, lexical scoped threads, structural `Send`/`Sync`, synchronized shared state и MPSC channels |
| Asynchronous execution | `async` functions запускают eager `task<T throws E...>`; `await move name` / `await operation()` является explicit consuming suspension point; hosted native I/O предоставляется только асинхронно |

Нормативным оригиналом является английский файл той же редакции. При расхождении английского и русского файлов применяется английский текст.

<a id="scope"></a>

## 1. Scope

<a id="R-GEN-0001"></a>

**R-GEN-0001** — Настоящий документ shall определять форму и интерпретацию программ, написанных на R 0.1, включая синтаксис, ограничения, семантику, абстрактную машину, обязательные диагностики и минимальные пределы реализации.

<a id="R-GEN-0002"></a>

**R-GEN-0002** — R shall быть самостоятельным системным языком общего назначения без garbage collector, с детерминированным управлением ресурсами и возможностью трансляции в строго соответствующий ISO C17.

<a id="R-GEN-0003"></a>

**R-GEN-0003** — Корректная программа, в которой не нарушен safety contract ни одной выполненной unsafe operation или операции внешнего кода, shall not иметь undefined behavior. Сам факт выполнения корректно обёрнутой unsafe operation не отменяет гарантию. Каждая потенциально ошибочная операция shall приводить к определённому результату, обязательной диагностике, panic либо поведению из Annex C или D.

<a id="R-GEN-0004"></a>

**R-GEN-0004** — Настоящий документ не определяет способ запуска реализации, формат объектных файлов, отладочный формат, package manager, JIT, IDE, debugger, C++ interoperability и механизм распространения модулей.

<a id="R-GEN-0005"></a>

**R-GEN-0005** — Strict R 0.1 program shall состоять исключительно из constructs, выводимых из Annex A и удовлетворяющих normative constraints этого документа. Implementation extensions shall работать в отдельном non-strict mode.

<a id="normative-references"></a>

## 2. Normative references

Для датированных ссылок применяется только указанная редакция.

<a id="R-REF-0001"></a>

**R-REF-0001** — [ISO/IEC 9899:2018](https://www.iso.org/standard/74528.html), *Information technology — Programming languages — C*, является нормативной основой C17 mapping и C ABI терминологии, но не определяет семантику R.

<a id="R-REF-0002"></a>

**R-REF-0002** — [RFC 3629](https://www.rfc-editor.org/rfc/rfc3629), *UTF-8, a transformation format of ISO 10646*, определяет допустимую кодировку исходного текста.

<a id="R-REF-0003"></a>

**R-REF-0003** — [Unicode Standard 17.0.0](https://www.unicode.org/versions/Unicode17.0.0/) и [UAX \#31 revision 43](https://www.unicode.org/reports/tr31/tr31-43.html) определяют Unicode scalar values и классы `XID_Start`/`XID_Continue`, используемые идентификаторами.

<a id="R-REF-0004"></a>

**R-REF-0004** — [ISO/IEC 60559:2020](https://webstore.iec.ch/en/publication/66123) определяет абстрактную арифметику `f32` и `f64`.

<a id="R-REF-0005"></a>

**R-REF-0005** — [R Standard Library Specification 0.1](R_STANDARD_LIBRARY_SPECIFICATION_0_1.ru.adoc) определяет library modules, public signatures, native asynchronous I/O contracts и profile-specific facilities, на которые ссылается этот документ.

<a id="terms"></a>

## 3. Terms and definitions

<a id="R-TERM-0001"></a>

**R-TERM-0001** — **shall** обозначает обязательное требование; **shall not** — абсолютный запрет; **should** — рекомендацию с допустимым обоснованным отклонением; **may** — разрешённую возможность.

<a id="R-TERM-0002"></a>

**R-TERM-0002** — **undefined behavior** (UB) — поведение, к которому стандарт не предъявляет требований. В R оно возможно только после нарушения safety contract unsafe operation или внешнего кода.

<a id="R-TERM-0003"></a>

**R-TERM-0003** — **implementation-defined behavior** — разрешённый выбор, который реализация shall выбрать и документировать; полный каталог приведён в Annex C.

<a id="R-TERM-0004"></a>

**R-TERM-0004** — **unspecified behavior** — выбор одного из перечисленных вариантов без обязанности документировать конкретный выбор; полный каталог приведён в Annex D.

<a id="R-TERM-0005"></a>

**R-TERM-0005** — **diagnostic** — сообщение реализации о нарушении синтаксического или семантического ограничения. После обязательной диагностики реализация may продолжить анализ, но shall not исполнять или маркировать программу conforming.

<a id="R-TERM-0006"></a>

**R-TERM-0006** — **object** — область storage, имеющая тип, identity, alignment, lifetime и текущее состояние. **Subobject** — field, element или variant payload, входящий в другой object.

<a id="R-TERM-0007"></a>

**R-TERM-0007** — **value** — математическое или составное значение типа; **place** — выражение, обозначающее object или subobject; **temporary** — безымянный object, созданный вычислением.

<a id="R-TERM-0008"></a>

**R-TERM-0008** — **owner** — object, ответственный за единственный обязательный destruction принадлежащего ресурса; **move** — передача этой ответственности без копирования ресурса; **drop** — детерминированное завершение lifetime с освобождением ресурсов.

<a id="R-TERM-0009"></a>

**R-TERM-0009** — **borrow** — ограниченное lifetime право доступа к object: shared immutable (`const T*`) либо exclusive mutable (`T*`).

<a id="R-TERM-0010"></a>

**R-TERM-0010** — **raw pointer** — значение `raw T*`, для которого компилятор не доказывает lifetime, aliasing, bounds или инициализацию pointee.

<a id="R-TERM-0011"></a>

**R-TERM-0011** — **safety contract** — нормативное предусловие unsafe operation, которое shall обеспечить вызывающий код, чтобы результат оставался определённым.

<a id="R-TERM-0012"></a>

**R-TERM-0012** — **panic** — определённое аварийное событие абстрактной машины, которое запускает выбранную реализацией panic strategy, но само не является UB.

<a id="R-TERM-0013"></a>

**R-TERM-0013** — **data race** — два конфликтующих доступа из разных threads к одному memory location, не упорядоченные happens-before, если хотя бы один доступ является записью и оба не являются согласованными atomic operations.

<a id="R-TERM-0014"></a>

**R-TERM-0014** — **filesystem adapter lane** является закрытым target-runtime service ровно из четырёх threads, отдельных от каждого worker process executor, который may исполнять только семейства filesystem operations metadata, namespace, cursor и durability, перечисленные R-CMAP-0039 и выбранным target manifest. Thread lane никогда не исполняет R code, continuations, payload byte transfer, console, DNS, socket либо child-process waiting.

<a id="R-TERM-0015"></a>

**R-TERM-0015** — **пул блокирующих вызовов** является target-runtime service не более чем из числа threads, записанного выбранным target manifest, отдельным от каждого worker process executor и от filesystem adapter lane, который исполняет только entries, переданные `std.async::blocking` (Library R-SLIB-ASYNC-0017). Thread пула исполняет одну entry за раз до её return как обычный R code на собственном stack и никогда не исполняет continuations; пул добавляет thread, только пока каждый существующий занят.

<a id="R-TERM-0016"></a>

**R-TERM-0016** — **адаптер файловых передач** является закрытым target-runtime service, который исполняет только payload byte transfer обычных файлов по Library R-SLIB-ASYNC-0019, никогда не на worker process executor, не на thread filesystem adapter lane и не на thread пула блокирующих вызовов, причём одновременно начатых transfers не больше числа, записанного выбранным target manifest. Он никогда не исполняет R code, continuations, ожидание console, pipe, DNS, socket либо child process.

<a id="conformance"></a>

## 4. Conformance

<a id="R-CONF-0001"></a>

**R-CONF-0001** — Conforming implementation shall принимать и правильно исполнять каждую strictly conforming R program в пределах документированных limits.

<a id="R-CONF-0002"></a>

**R-CONF-0002** — Conforming translator shall диагностировать каждое нарушение, помеченное настоящим документом как constraint violation или required diagnostic. Кроме того, каждое statically decidable нарушение source program нормативного `shall`, `shall not` или запрета grammar, name resolution, type, initialization, ownership, lifetime, control-flow либо FFI declaration является constraint violation и shall получить наиболее специфичный code Annex B. Нормативные требования к самой implementation не превращаются этим правилом в source diagnostic.

<a id="R-CONF-0003"></a>

**R-CONF-0003** — Реализация may предоставлять extensions, если они выключены в strict-conformance mode, не меняют поведение conforming programs и диагностируются при использовании в strict-conformance mode.

<a id="R-CONF-0004"></a>

**R-CONF-0004** — Реализация shall публиковать target description, все выборы из Annex C, фактические limits, поддерживаемые profiles, panic strategy и C ABI.

<a id="R-CONF-0005"></a>

**R-CONF-0005** — Freestanding implementation shall реализовывать язык, `core` и freestanding subset Annex G. Если selected profile поддерживает allocation/new, он дополнительно shall реализовывать allocation-profile namespaces `std.arc` и `std.rc` по R-LIB-0012 R Standard Library Specification 0.1. Hosted implementation additionally shall реализовывать entry point и hosted library surface, required его selected library profile. Selection `hosted-native-async` требует language task runtime и все native asynchronous capabilities, заданные R-REF-0005.

<a id="R-CONF-0006"></a>

**R-CONF-0006** — Если generated C используется как backend, успешная компиляция C не является доказательством conformance: observable behavior generated program shall совпадать с R abstract machine.

<a id="abstract-machine"></a>

## 5. Abstract machine

### 5.1 Program execution

<a id="R-AM-0001"></a>

**R-AM-0001** — Программа состоит из конечного directed acyclic graph модулей. Каждый модуль R 0.1 состоит ровно из одной translation unit после декодирования UTF-8 и до semantic analysis.

<a id="R-AM-0002"></a>

**R-AM-0002** — Hosted execution shall выполнить в порядке: инициализация runtime; инициализация static R objects imported modules в topological order; инициализация static R objects текущего модуля; создание process executor для asynchronous entry point; вызов либо запуск `main`; ожидание synchronous entry point либо root task; выполнение quiescence drain R-AM-0013; destruction static R objects в обратном порядке; завершение процесса. Сама runtime initialization имеет две упорядоченные pre-main phases: загрузка required dynamic providers и проверка required-symbol readiness по R-FFI-0037, затем, только при успехе этой phase, native argument conversion и reservation startup snapshot по R-FUNC-0008. Failure одной phase завершает launch и предотвращает каждую последующую phase, поэтому один launch сообщает только самую раннюю failure category.

<a id="R-AM-0003"></a>

**R-AM-0003** — Observable behavior включает volatile и atomic accesses, обращения к внешней среде через standard library или C ABI, termination status и bytes, переданные внешним функциям. Оптимизация shall сохранять observable behavior.

### 5.2 Object states and lifetime

<a id="R-AM-0004"></a>

**R-AM-0004** — В каждый момент object находится ровно в одном состоянии: `uninitialized`, `initialized`, `moved` или `destroyed`.

<a id="R-AM-0005"></a>

**R-AM-0005** — Lifetime начинается после полного успешного initialization и остаётся active во время user drop body. Он заканчивается после выхода из drop body, включая выход при panic, непосредственно перед destruction первого уничтожаемого subobject. Если drop body и уничтожаемые subobjects отсутствуют, lifetime заканчивается в точке destruction object. До окончания lifetime сам object и ещё не уничтоженные fields остаются initialized; доступ через `self` подчиняется ограничениям R-INIT-0009. Successful whole-object move заканчивает lifetime source после transfer value и переводит source в `moved`; последующий scope exit переводит это storage в `destroyed` без drop.

<a id="R-AM-0006"></a>

**R-AM-0006** — Чтение value разрешено только из initialized object. Запись разрешена в initialized mutable object при наличии exclusive access либо в uninitialized storage как часть полного initialization. Запись в `moved` local storage разрешена только как полная reinitialization по R-INIT-0007 и R-OWN-0006; она начинает новый lifetime и не является доступом к прежнему object.

<a id="R-AM-0007"></a>

**R-AM-0007** — Чтение uninitialized, moved или destroyed object в safe context shall быть обязательной диагностикой. Если оно стало возможным из-за нарушения unsafe contract, результат является UB.

<a id="R-AM-0008"></a>

**R-AM-0008** — Identity object уникальна в пределах его lifetime. Новый object в том же storage получает новую identity. Borrow provenance shall identify исходный object, допустимый subobject или диапазон elements и shall not переживать lifetime.

### 5.3 Evaluation

<a id="R-AM-0009"></a>

**R-AM-0009** — Operands, function arguments, initializer elements и subexpressions shall вычисляться слева направо. `&&`, `||` и `?:` вычисляют только выбранную ветвь. Statement `try` выбирает не более одного matching `catch`, а затем свой единственный `finally`, если он присутствует. Side effect одного шага завершён до начала следующего шага.

<a id="R-AM-0010"></a>

**R-AM-0010** — Full expression заканчивается в `;`, condition controlling statement, initializer element или перед возвратом caller. Every fully initialized temporary whose ownership was not successfully transferred shall уничтожаться в обратном порядке создания в конце full expression. Temporary borrow, slice либо ordinary descriptor `str` не продлевает lifetime designated storage. Copy, selection или weakening такого descriptor value сами по себе не запрещены; R-BORROW-0020 вместо этого запрещает формировать borrow, storage root которого является temporary object. Weakening `constexpr str` в ordinary `str` designates program-image bytes, а не descriptor object. При unwind strategy, если evaluation panics, все ещё owned fully initialized temporaries текущего full expression drops в обратном порядке до cleanup окружающих automatic objects. При abort strategy R-ERR-0005 terminates без дополнительных R drops. Successful transfer из temporary в staging destination struct field, function parameter, return slot либо hidden switch-owned object передаёт также cleanup responsibility; такой source temporary не drops на границе исходного full expression.

<a id="R-AM-0011"></a>

**R-AM-0011** — Panic point является observable sequencing boundary. Проверка shall произойти до side effect самой потенциально ошибочной операции и после всех предшествующих sequenced operations.

### 5.4 Program termination

<a id="R-AM-0012"></a>

**R-AM-0012** — Нормальный return либо asynchronous completion `main`, включая
необработанный элемент её неявного набора checked-ошибок, является normal termination
со статусом процесса. Checked-ошибка не является panic: обязательный порядок —
allocation-free диагностика, затем отмена задач и полный quiescence drain R-AM-0013,
затем уничтожение thread-local объектов начального потока и static-объектов с
предписанными промежуточными drain. Payload ошибки потребляется ровно один раз до
деструкции. Вызов `abort`, uncaught panic после применимого unwind и panic во время
unwinding drop завершают процесс без дальнейших R drops.

<a id="R-AM-0013"></a>

**R-AM-0013** — Hosted normal termination является quiescence-and-destruction fixed point. Quiescence drain shall пройти только после того, как каждый R thread, существовавший либо созданный participating work этого drain, завершился; каждая созданная вследствие этого detached-outcome runtime cleanup duty рекурсивно завершилась, включая thread-local drops её cleanup context; каждая task cancellation получила terminal native acknowledgement; каждая detached task и каждая task-result cleanup duty завершились; и все active attached C callback contexts вернулись. Normal termination сначала выполняет drain, затем по одному уничтожает каждый initialized `thread_local` object initial thread и каждый static R object в точном порядке R-OBJ-0008, R-OBJ-0010 и R-MOD-0004; после каждого такого top-level object destruction выполняется новый drain до начала следующего. Threads/duties, созданные этим destruction, участвуют в следующем drain, а final drain shall пройти до завершения normal termination. После first drain новая external C-origin entry не может attach; providers уже должны удовлетворять R-FFI-0055. Abrupt termination не обязана выполнять drain и не выполняет дальнейшие R drops.

После диагностики границы, если она требуется R-AM-0012, когда asynchronous `main` commits terminal outcome, runtime requests cancellation каждой другой nonterminal task, включая detached tasks, затем начинает first drain. Никакое module/static destruction не начинается до terminal state этих tasks и их native cancellation acknowledgements.

<a id="R-AM-0014"></a>

**R-AM-0014** — Asynchronous operation имеет ровно одно execution state из reserved, running, cancellation-requested и completed. Независимо от этого observation right остаётся live, пока ровно один раз не consumed через `await`, `cancel`, `detach` либо ordinary drop; consumption этого right не означает completion. Completion commits ровно один terminal outcome: returned T, одна exact declared checked error E либо, при unwind strategy, panic report. Cancellation и ordinary completion могут race, но commits только один terminal outcome, а каждый captured value, native retain, frame и result освобождается ровно один раз. Cancellation является cooperative с target-native operation; storage остаётся live до terminal acknowledgement, даже если исходный `task<T throws E...>` уже consumed или resolved.

<a id="source-lexical"></a>

## 6. Source representation and lexical elements

### 6.1 Encoding and characters

<a id="R-LEX-0001"></a>

**R-LEX-0001** — Source file shall быть well-formed UTF-8 без surrogate code points, overlong encodings и code points выше U+10FFFF. Нарушение требует `R-DIAG-LEX-001`.

<a id="R-LEX-0002"></a>

**R-LEX-0002** — U+FEFF may присутствовать только первым code point файла и тогда не является token. Иное появление U+FEFF является недопустимым format character.

<a id="R-LEX-0003"></a>

**R-LEX-0003** — Line ending может быть LF или CRLF; lexer shall нормализовать их к одному logical newline. Lone CR shall приниматься как newline с portability warning.

### 6.2 Whitespace and comments

<a id="R-LEX-0004"></a>

**R-LEX-0004** — После normalization R-LEX-0003 whitespace состоит только из U+0009 TAB, U+000A logical newline, U+000B VT, U+000C FF и U+0020 SPACE; он разделяет tokens, но иначе незначим. Иной Unicode whitespace вне literal/comment требует `R-DIAG-LEX-004`. Поддерживаются `//` до logical newline либо end of file и ненестящиеся `/* ... */`. Незакрытый block comment требует диагностики; их tokenization задаёт следующее правило.

<a id="R-LEX-0005"></a>

**R-LEX-0005** — Комментарий shall заменяться одним whitespace для tokenization. Comment delimiters внутри string или character literal не открывают комментарий.

### 6.3 Identifiers and keywords

<a id="R-LEX-0006"></a>

**R-LEX-0006** — Identifier shall начинаться `_` или `XID_Start` и продолжаться `_` или `XID_Continue` согласно зафиксированной Unicode version. Identifier shall быть записан в NFC; ненормализованная запись требует диагностики.

<a id="R-LEX-0007"></a>

**R-LEX-0007** — Identifier equality shall сравнивать Unicode scalar sequences после проверки NFC, с учётом регистра и без locale-dependent folding.

<a id="R-LEX-0008"></a>

**R-LEX-0008** — Следующие ASCII sequences являются keywords и shall not быть identifiers:

```
alignof ai8 ai16 ai32 ai64 aisize arc array as async atomic au8 au16 au32 au64 ausize auto await bool break case catch char const constexpr continue default dict drop dyn else enum extern finally
f32 f64 false fallthrough fn for i8 i16 i32 i64 if import in isize module move never
impl list new null null_t o own panic protected raw rc return Self sizeof static str struct switch task this throw throws trait variant
thread_scope thread_local true try u8 u16 u32 u64 unsafe usize void weak while
c_char c_schar c_uchar c_short c_ushort c_int c_uint c_long c_ulong c_llong
c_ullong c_bool c_wchar c_wint c_int8 c_uint8 c_int16 c_uint16 c_int32 c_uint32
c_int64 c_uint64 c_intptr c_uintptr c_intmax c_uintmax c_float c_double
c_long_double c_size c_ptrdiff opaque
```

Только перечисленные выше sequences имеют status keyword. Все остальные identifier tokens классифицируются обычными rules разрешения имён; их spelling не активирует syntax другого языка.

### 6.4 Literals

<a id="R-LEX-0009"></a>

**R-LEX-0009** — Integer literal may быть decimal, `0b` binary, `0o` octal или `0x` hexadecimal. `_` may разделять digits, но shall not быть первым, последним или соседним radix prefix. Leading zero не меняет decimal radix.

<a id="R-LEX-0010"></a>

**R-LEX-0010** — Integer suffix shall быть одним из `i8`, `u8`, `i16`, `u16`, `i32`, `u32`, `i64`, `u64`, `isize`, `usize`. Suffixed value shall быть representable в указанном type, except signed-min formation R-EXPR-0026; иначе требуется `R-DIAG-CONST-001`. Вне контекста array bound из R-TYPE-0011 и contextual literal cases R-INIT-0002/0012, R-EXPR-0003 и R-STMT-0006 unsuffixed literal получает первый type из `i32`, `i64`, `u64`, способный представить значение; отсутствие такого type требует той же диагностики.

<a id="R-LEX-0011"></a>

**R-LEX-0011** — Floating literal shall использовать decimal или hexadecimal significand, обязательную exponent часть для hexadecimal формы и optional suffix `f32` или `f64`; decimal форма с radix point shall иметь хотя бы одну digit с каждой стороны `.`. Без suffix type равен `f64`. Exact mathematical value shall round к ближайшему representable value целевого R type с ties-to-even. Overflow выше maximum finite magnitude требует `R-DIAG-CONST-001`; underflow may дать subnormal или signed zero согласно этому rounding rule. Это ограничение сохраняет однозначную tokenization `1..2` как integer literal, `..`, integer literal.

<a id="R-LEX-0012"></a>

**R-LEX-0012** — Character literal обозначает ровно один Unicode scalar и имеет type `char`. String literal после обработки escapes и adjacent concatenation shall содержать well-formed UTF-8 bytes, имеет type `constexpr str` и не включает неявный trailing zero. Byte escape, создающий invalid UTF-8 sequence, требует `R-DIAG-LEX-002`.

<a id="R-LEX-0013"></a>

**R-LEX-0013** — Escape sequences: `\\`, `\"`, `\'`, `\n`, `\r`, `\t`, `\0`, `\xHH` (ровно два hex digits) и `\u{H...}` (1–6 hex digits, Unicode scalar). `\0` в string разрешён и является byte zero. `\xHH` in character literal shall обозначать complete single-byte Unicode scalar; in string it contributes one byte, после чего R-LEX-0012 проверяет всю resulting sequence.

<a id="R-LEX-0014"></a>

**R-LEX-0014** — После tokenization и до parsing каждая maximal sequence соседних string-literal tokens, между которыми только whitespace/comments, shall быть заменена одним logical string-literal token, concatenated после обработки escapes. Она никогда не пересекает иной token.

Для смешанной последовательности с f-литералом действует R-EXPR-0028. Проверка UTF-8 объединяет соседние текстовые части, но не пересекает слот.

### 6.5 Punctuators

<a id="R-LEX-0015"></a>

**R-LEX-0015** — Lexer shall выбирать longest token из:

```
{ } [ ] ( ) ; , . .. ... : :: ? @
+ - * / % & | ^ ~ ! = < >
++ -- -> == != <= >= && || << >> += -= *= /= %= &= |= ^= <<= >>=
```

Список punctuators закрыт; source sequence, не совпадающая ни с одним token Annex A, требует lexical diagnostic, заданной R-LEX-0016.

<a id="R-LEX-0016"></a>

**R-LEX-0016** — Lexer сначала пропускает leading whitespace/comment, затем выбирает longest sequence, matching любой один token Annex A, не пересекая следующий whitespace/comment; process repeats. При равной длине точное spelling R-LEX-0008 классифицируется как keyword, не identifier. Comment opener `//` или `/*` распознаётся до punctuator `/` и обрабатывается R-LEX-0004 и R-LEX-0005. Поэтому `ifx` является одним identifier, но `i f` не склеивается; `0x10` и `1e3` — одним numeric literal, а `.`, `..`, `...` выбираются maximal-munch. Полный character literal является одним token по этому longest-match rule. Апостроф, не завершающий character literal, не начинает token. Если ни один token не начинается в текущем code point, требуется `R-DIAG-LEX-004`.

<a id="grammar-units"></a>

## 7. Grammar and translation units

<a id="R-GRAM-0001"></a>

**R-GRAM-0001** — Annex A является нормативной grammar. Semantic constraints остальных разделов дополняют её productions. Распознавание aggregate type name — единственная context-sensitive classification, заданная R-GRAM-0003; tokenization она не изменяет.

<a id="R-GRAM-0002"></a>

**R-GRAM-0002** — Translation unit shall начинаться optional `module` declaration, после которого следуют imports, затем external declarations. Module declaration, если отсутствует, определяется implementation-defined module map.

<a id="R-GRAM-0003"></a>

**R-GRAM-0003** — `struct Name { ... };`, `enum Name { ... };` или импортированный aggregate вводит `Name` как aggregate type name. В type position aggregate записывается непосредственно как `Name` либо как qualified `module.path::Name`; `struct Name` и `enum Name` являются формами declaration, а не type-use forms. Parser may tentatively разбирать identifier как `aggregate-type-name`, но полученная declaration valid только если lookup разрешает эту token sequence ровно в один видимый aggregate type. Такая классификация type name является parser feedback после обычной tokenization и необходима для различения declaration `Node* next = value;` и expression statement. В statement или initializer `for`, если leading token sequence разрешается в видимое aggregate type name, а оставшиеся tokens могут образовать object declaration, выбирается разбор как declaration; иначе sequence разбирается как expression.

<a id="R-GRAM-0004"></a>

**R-GRAM-0004** — Каждый syntactic construct R 0.1 вводится production Annex A. Roles declaration, import, conversion, initialization, statement и expression полностью задаются соответствующими productions без контекстного заимствования иных C syntactic forms, кроме классификации aggregate type names, требуемой R-GRAM-0003.

<a id="R-GRAM-0005"></a>

**R-GRAM-0005** — Parser shall выдавать диагностику при token sequence, не выводимой из Annex A, и shall not приписывать ей semantics extension в strict mode.

<a id="R-GRAM-0006"></a>

**R-GRAM-0006** — Complete type precedes identifier, and `*`, `[]` and `?` belong to type rather than identifier. One `object-declaration` binds exactly one identifier; несколько objects записываются отдельными declarations.

<a id="R-GRAM-0007"></a>

**R-GRAM-0007** — Attribute set R 0.1 is closed: `@repr(C)` on struct/fieldless enum, `@safety` on unsafe or C-boundary function, `@deny_panic_alloc` on a module declaration (R-OBJ-0012), and FFI attributes/scopes from R-FFI-0050. Unknown argument, unknown attribute or attribute outside its allowed declaration requires `R-DIAG-SYN-002` (or more specific Annex B FFI diagnostic). `@lending` on trait methods is specified by R-TYPE-0045.

`@json` также разрешён на полях по R-JSON-0001. Атрибуты функций `@noalloc` и `@nonblocking` определены в R-FUNC-0019; `@must_use` — в R-FUNC-0020, `@discardable` — в R-FUNC-0021, `@chain` у методов — в R-FUNC-0024, `@test` у функций — в R-FUNC-0025, `@recursion` у синхронных функций — в R-FUNC-0026, `@derive` у объявлений struct, enum и error — в R-AGG-0012, `@default` у варианта enum — в R-INIT-0005. После атрибута без аргументов группа в скобках с запятой или `...` на верхнем уровне не является списком аргументов: она начинает кортежный тип объявления (R-TYPE-0052).

<a id="R-JSON-0001"></a>

**R-JSON-0001** — Поле может иметь один контракт `@json(...)`. Его параметры: `name`, `skip`, `omitempty`, `omitzero`, `string`, `case`, `embed`, `optional`, `default`, `omitnone` и `description`. Только тип поля определяет хранение в R, layout, Copy/Move и инициализацию. `optional` разрешает отсутствие входного ключа; `o<T>` допускает состояние none и JSON null, но не делает ключ необязательным. Параметры пропуска при записи не изменяют требований чтения. `case` и `default` служат именами аргументов атрибута без изменения своей лексической роли вне него. Неизвестные, повторные, неверно типизированные, неприменимые или несовместимые параметры требуют `R-DIAG-JSON-001`. `skip` допускает только дополнительный `default`; `embed` применяется отдельно. `default` требует `optional` или `skip`. Необязательное или пропускаемое поле без `default`, объявляющее инициализатор (R-INIT-0004), берёт его как default, если инициализатор — константный скаляр, строковый литерал или вызов `factory()` фабрики default; любой иной инициализатор там требует `R-DIAG-JSON-001`. Default — скалярный/строковый литерал либо безопасная синхронная фабрика без аргументов, возвращающая точный тип поля; её checked errors ограничены `std.json::error` и `std.alloc::alloc_error`. `description` принимает строковый литерал, который описывает поле в JSON Schema его типа (R-SLIB-JSON-0002) и не меняет преобразования. Полный контракт преобразования задаёт R-SLIB-JSON-0001, а схему — R-SLIB-JSON-0002. Interface schema 5 экспортирует нормализованные контракты полей и fingerprints определений без изменения физического представления aggregate. Interface schema 31 добавляет description к экспортируемому контракту поля, а hook `json_schema` — к экспортируемым hooks типа.

Associated-имена `json_marshal`, `json_unmarshal`, `json_is_zero` и `json_schema` принадлежат номинальному типу. Их безопасные синхронные определения находятся в модуле типа; точные сигнатуры и независимые направления заданы R-SLIB-JSON-0001, а для `json_schema` — R-SLIB-JSON-0002. Generic-hooks имеют те же нормализованные параметры и constraints, что схема типа.

<a id="R-GRAM-0008"></a>

**R-GRAM-0008** — Поскольку `arc`, `rc`, `array`, `list` и `dict` являются keywords, forms `std.arc::name`, `std.rc::name`, `std.array::name`, `std.list::name` и `std.dict::name` допускаются только соответствующими closed standard qualified-name productions для operations R-LIB-0012 и R-LIB-0019..R-LIB-0023. Это не ordinary module paths; program не может их declare/import, и keyword не становится identifier в другом context. Поскольку `async` также является keyword, type и operation paths `std.async`, admitted Annex A, являются closed implementation-provided forms; program не может declare `async` как component module path.

Закрытые формы `std.json::null`, `std.json::array` и соответствующие варианты `std.json::value_kind` разрешают эти ключевые компоненты только в указанных путях.

<a id="names-scopes"></a>

## 8. Names, declarations and scopes

### 8.1 Name spaces and lookup

<a id="R-NAME-0001"></a>

**R-NAME-0001** — R имеет отдельные name spaces для modules, aggregate type names, variants, labels `case` и ordinary value/function names. Field names принадлежат scope своего aggregate; variant names — scope defining enum and are referenced through `Enum::Variant`. Closed associated key-operation names `hash` и `equal` принадлежат nominal type и referenced через `Type::name` по R-FUNC-0009. Имена methods принадлежат отдельному method name space своего owner type по R-NAME-0010. Один identifier may одновременно быть aggregate type name и ordinary value/function name; type position выбирает первое name space, expression position — второе, кроме head, непосредственно за которым следует braced aggregate initializer: это constructor context, заданный R-AGG-0007.

<a id="R-NAME-0002"></a>

**R-NAME-0002** — Scope ordinary function name уровня module начинается после его declarator; scope R object name — после его complete initializer; scope imported C object name — после его declaration. Каждый из них действует до конца module. Scope parameter — всё function body. Scope local declaration начинается после complete initializer и заканчивается в конце содержащего block. Declared aggregate type name входит в scope сразу после identifier в своей `struct` либо `enum` declaration и действует до конца module; imported aggregate type name входит в scope после import и имеет тот же module extent.

<a id="R-NAME-0003"></a>

**R-NAME-0003** — Use shall resolve ровно к одной доступной declaration. Отсутствие, неоднозначность или доступ к protected name из другого module требует диагностики.

<a id="R-NAME-0004"></a>

**R-NAME-0004** — Повторная declaration одного ordinary name в одном scope запрещена, кроме совместимой function prototype и её единственного definition. Повторное aggregate type name в одном module запрещено. Function prototype и definition shall иметь одинаковую default-exported либо `protected` visibility.

### 8.2 Declarations

<a id="R-NAME-0005"></a>

**R-NAME-0005** — Object declaration shall иметь initializer; `auto` на месте его type принимает type этого initializer по R-NAME-0011. Объявление без initializer является constraint violation даже если control-flow analysis мог бы доказать последующую запись. Imported C object declaration inside `extern "C"` является единственным исключением и не создаёт R object/storage.

<a id="R-NAME-0006"></a>

**R-NAME-0006** — Module-scope initializer shall быть constant expression либо aggregate из constant expressions. Dynamic module initialization отсутствует.

<a id="R-NAME-0007"></a>

**R-NAME-0007** — Каждая module function, object, struct, enum, extern block и aggregate field экспортируется по умолчанию. Modifier `protected` may применяться к любой из этих forms и удаляет соответствующее name или field из module interface; оно остаётся доступным везде, где его declaration иным образом находится в scope внутри defining module. `protected` на local, parameter, drop definition либо individual declaration, уже governed extern block, запрещён. Это правило module visibility, а не inheritance access и не storage duration. В частности, `static` сохраняет только block-storage meaning из R-NAME-0008 и никогда не является linkage либо visibility modifier. Каждый variant имеет visibility defining enum и не может иметь независимый visibility modifier. Protected field остаётся частью aggregate layout, drop glue, derivation Copy/Send/Sync и interface fingerprint, хотя source другого module не может назвать это field.

<a id="R-NAME-0008"></a>

**R-NAME-0008** — `static` may применяться только к block object и задаёт static storage duration. `thread_local` may применяться к module или block object и задаёт отдельный instance на thread. Оба требуют constant initializer, кроме `thread_local` imported C object, storage которого определяет external library.

<a id="R-NAME-0009"></a>

**R-NAME-0009** — Shadowing outer local name may быть разрешено, но shadowing parameter, module name или imported exported name в той же translation unit требует diagnostic, чтобы исключить неустойчивое name resolution. Predeclared intrinsic name `len` shall not быть declared либо shadowed in any scope. Predeclared type name `bytes` shall not быть declared либо shadowed в aggregate type-name space. Его spelling остаётся доступным в отдельных ordinary, module, variant и field name spaces; ни одна из этих declarations не shadow predeclared type.

<a id="R-NAME-0010"></a>

**R-NAME-0010** — Каждый complete nominal, standard и built-in type имеет один плоский method name space, отдельный от ordinary, module, variant и field name spaces и общий для inherent methods этого type и для methods каждой implementation для него. Method упоминается через `Type::name` и через receiver forms R-FUNC-0014. Имя method, повторяющее другое имя method того же owner либо имя field или variant этого owner, требует diagnostic; то же относится к receiver, имя method которого предоставляют две видимые implementations. Method implementation трейта для standard или built-in type может повторять имя method, зарегистрированного для этого type (R-FUNC-0014): тогда receiver form называет зарегистрированный method, а method implementation достигается через generic constraint или dyn-интерфейс (R-TYPE-0043, R-TYPE-0051). `this` и `Self` являются keywords: `this` именует только receiver parameter, а `Self` — только owner внутри trait либо implementation.

<a id="R-NAME-0011"></a>

**R-NAME-0011** — `auto name = initializer;` объявляет automatic local, type которого — type его initializer как uncontextualized expression: unsuffixed integer literal имеет type `i32`, а initializer типа `void` либо storage specifier требуют diagnostic. `auto` — keyword, встречается только в позиции type объявления local object, включая initializer `for` и loop variable range-for (R-STMT-0014) либо comprehension clause (R-EXPR-0030), и является единственным способом хранить closure value вне имени lambda.

<a id="type-system"></a>

## 9. Type system

### 9.1 Type categories

<a id="R-TYPE-0001"></a>

**R-TYPE-0001** — Types делятся на scalar, aggregate, fixed array, dynamic container, asynchronous task, slice, borrow, unique owning pointer, reference-counted strong/weak owner, raw pointer, atomic, function, closed standard-library resource и special types `void`/`never`.

`null_t` — compile-time маркер параметра, принимающего только литерал `null` (возможно, в скобках). Это не object/value-тип: он запрещён для локальных переменных (включая `auto`), module objects, полей, enum payloads, контейнеров, целей указателей, результатов функций, variadic-элементов и generic-аргументов. Он не имеет доступных из исходника размера, выравнивания и default-значения и не является C ABI type. В списке параметров native-функции или метода используется `null_t name` либо `const null_t name`. Имя параметра не обозначает объект, который можно читать, присваивать, перемещать или заимствовать; в теле используется литерал `null`. Обычные и async-вызовы разрешают маркер статически по R-FUNC-0004. Generic value-параметр не может вывести `null_t`.

<a id="R-TYPE-0002"></a>

**R-TYPE-0002** — Boolean type `bool` имеет ровно два значения: `false` и `true`. Это ограничение value set типа, а не требование писать condition только literals. Production `condition-expression` строится из boolean literals, явных comparison operations, например `a == 1`, и их композиции `&&`/`||`. Conversion set R-EXPR-0015 не создаёт bool из integer или pointer.

<a id="R-TYPE-0003"></a>

**R-TYPE-0003** — Signed integer types `i8`, `i16`, `i32`, `i64` используют two’s-complement values от -2^(N-1) до 2^(N-1)-1. Unsigned `u8`, `u16`, `u32`, `u64` имеют values 0..2^N-1. Padding bits отсутствуют.

<a id="R-TYPE-0004"></a>

**R-TYPE-0004** — `isize` и `usize` имеют одинаковую implementation-defined width, не менее 32 bits. Для этой width N, `isize` has two’s-complement range -2<sup>(N-1)..2</sup>(N-1)-1, `usize` has range 0..2^N-1, и оба не имеют padding bits. `usize` способен представить size каждого object, а `isize` — разность indices одного object, если она представима limit реализации.

<a id="R-TYPE-0005"></a>

**R-TYPE-0005** — `char` представляет Unicode scalar U+0000..U+D7FF или U+E000..U+10FFFF и не является integer type. Explicit conversion в `u32` всегда успешна; обратная conversion panic для не-scalar value.

<a id="R-TYPE-0006"></a>

**R-TYPE-0006** — `f32` и `f64` соответствуют IEC 60559 binary32/binary64, round-to-nearest ties-to-even. Overflow даёт infinity, underflow gradual, invalid operation даёт NaN. NaN payload и sign перечислены как unspecified.

<a id="R-TYPE-0007"></a>

**R-TYPE-0007** — `void` означает отсутствие value и при direct use допустим только как return type либо target explicit discard conversion из R-EXPR-0023. Дополнительно он может быть pointee `raw void*` или `raw const void*`; ни один такой pointer нельзя dereference, index, arithmetically advance, own или converted to safe borrow before conversion to a complete object type under unsafe contract. Использование `void` как element array или slice, field, component `o`, checked-error type, owner pointee или borrow pointee является constraint violation. `void` разрешён как result marker `task<void>` по R-TYPE-0029. `never` — uninhabited type выражения, которое не завершается normally, и may coerce в любой type. Local, parameter, field или variant payload могут иметь type `never`; его инициализирует только выражение, которое не завершается, поэтому declaration или construction завершает reachable path. Struct с uninhabited field и tagged enum, каждый variant которого несёт uninhabited payload, сами uninhabited.

### 9.2 Qualified and derived types

<a id="R-TYPE-0008"></a>

**R-TYPE-0008** — `const T` object не может быть изменён после initialization. `const` является частью access type, но не меняет layout либо lifetime `T` и не подразумевает `constexpr`. В `constexpr str` слово `constexpr` не является object access или storage qualifier: оно ограничивает designated bytes и их provenance по R-TYPE-0028. Spelling `const constexpr str` применяет обычный access qualifier `const` к descriptor object; он не меняет guarantee backing storage. Outermost object `const` квалифицирует place, а не standalone copied value: R-INIT-0002 снимает этот qualifier для initializer conversion, а R-EXPR-0001 — с value, produced чтением Copy place. Assignment, increment, exclusive borrow и move из такого object являются constraint violations; ordinary destruction остаётся обязательным.

<a id="R-TYPE-0009"></a>

**R-TYPE-0009** — `const T*` — shared borrow; `T*` — exclusive borrow; `own T*` — unique owning pointer; `arc T` и `rc T` — non-null shared owning handles; `weak arc T` и `weak rc T` — non-owning weak handles; `raw const T*` и `raw T*` — raw pointers. Pointer forms non-null по умолчанию, suffix `?` добавляет одно null value. У reference-counted handles нет nullable spelling; отсутствие задаётся как `o<arc T>`, `o<rc T>`, `o<weak arc T>` или `o<weak rc T>`.

<a id="R-TYPE-0010"></a>

**R-TYPE-0010** — Borrow, slice и ordinary `str` types не имеют source-level lifetime suffix либо lifetime argument. Их borrow regions и provenance relationships выводятся по section 13 и записываются в compiler metadata, а не в type. Поэтому complete spellings имеют вид `const i32*`, `const i32[]`, `str` и `const str`. Последняя форма квалифицирует descriptor object и не меняет region, carried его value. Program-resident strings используют `constexpr str`. Parenthesized type shall использоваться для nesting, например `arc (o<Item>)` либо `raw (raw const c_char*?)*?`.

<a id="R-TYPE-0011"></a>

**R-TYPE-0011** — `T[N]` — fixed array из N elements; `T[]` — mutable exclusive slice; `const T[]` — shared immutable slice. N shall быть positive `usize` constant expression. Unsuffixed integer literal, являющийся complete expression N, contextually получает type `usize`, поэтому `u8[11]` не требует suffix; неположительный либо непредставимый bound требует `R-DIAG-CONST-001`. В `T[N][M]` leftmost N is outermost: type содержит N arrays по M elements T, а rightmost index меняется быстрее; этот порядок совпадает с C declaration `T a[N][M]`. Размещение `[N]` в type, а не после identifier, намеренно: `T[N]` — один complete type, который без изменений применим во всех type positions, включая fields, return types и parametric type arguments. Поэтому R не заимствует split array declarator из C. Slice не является unsized object: его value содержит bounds.

<a id="R-TYPE-0012"></a>

**R-TYPE-0012** — `o<T>` имеет variants `o::none` и `o::some(T)`. Checked error set записывается только как clause function `throws E1, E2` либо внутри type task/thread-handle, закреплённого R-TYPE-0029 и R-LIB-0010; он не является value type и не имеет source-level tagged wrapper. Каждый E shall быть exact named, outermost-unqualified, complete, sized, inhabited nominal type, объявленным через `error`, либо явно зарегистрированным standard error type или closed error schema с canonical type arguments. Обычные объявления `struct` и `enum` не подходят независимо от имени и полей; их использование в `throws`, `throw`, `catch` и наборах ошибок task/thread требует `R-DIAG-EFFECT-002` с пояснением, что нужно объявление `error`. `void`, scalars, pointers, borrows, slices, runtime `str` и anonymous structural types не являются error types. Throws list обозначает order-insensitive set: duplicate types ill-formed, а implementation canonicalizes его по fully qualified nominal name и canonical type arguments для type compatibility, mangling и interface fingerprints. Entry, называющий error с потомками, называет каждого exact member её family (R-AGG-0011), а `std.error::fault` называет каждую standard error R-FUNC-0008; указание error вместе с одним из её ancestors требует `R-DIAG-EFFECT-001`. `array<T>` и `list<T>` являются dynamic sequences, а `dict<K,V>` — dynamic key/value container. T, K и V shall быть outermost-unqualified, complete, sized, inhabited, non-`void` value types; K дополнительно shall иметь key contract, определённый R-FUNC-0009 и R-LIB-0020. Вместе с `arc T`, `rc T`, их weak forms, `task<T throws E...>` и closed schemas, named R Standard Library Specification 0.1, это predefined parametric constructors. Пользовательские схемы определены R-TYPE-0031 — R-TYPE-0040.

<a id="R-TYPE-0013"></a>

**R-TYPE-0013** — `atomic T` допустим для `bool`, integer type, `usize`, `isize` и nullable raw pointer. Predefined spellings `ai8`, `ai16`, `ai32`, `ai64` и `aisize` тождественны `atomic i8`, `atomic i16`, `atomic i32`, `atomic i64` и `atomic isize` в том же порядке. Spellings `au8`, `au16`, `au32`, `au64` и `ausize` тождественны `atomic u8`, `atomic u16`, `atomic u32`, `atomic u64` и `atomic usize` в том же порядке. Они не являются arithmetic integer types и не вводят отдельные identities aliases. Type/interface fingerprints, mangling, `sizeof`, `alignof` и `atomic_is_lock_free` canonicalize каждую shorthand к expanded atomic type; declaration с одним spelling совместима с definition через другое. Эти spellings не являются literal suffixes и не делают C `_Atomic` FFI type. Atomic type неявно не преобразуется в T и не является Copy.

### 9.3 Type identity and compatibility

<a id="R-TYPE-0014"></a>

**R-TYPE-0014** — Каждая `struct`/`enum` declaration создаёт nominal type, определяемый module identity и aggregate name. Одинаковые members в разных declarations не создают совместимость. Declaration keyword опускается при каждом использовании type.

<a id="R-TYPE-0015"></a>

**R-TYPE-0015** — Derived types compatible только при structural equality base types, lengths, qualifiers и nullability, а также satisfiable compiler-inferred region constraints. У region variables нет source spelling; в interface metadata они сравниваются structurally. Regions не влияют на runtime layout, но влияют на static compatibility; разрешённое shortening задаёт R-BORROW-0021.

<a id="R-TYPE-0016"></a>

**R-TYPE-0016** — Function return type shall not иметь outermost object `const`, described R-TYPE-0008; `const` shared borrow, shared slice и raw pointee не являются таким qualifier. Этот constraint применяется к declarations, definitions и raw function types. Function types compatible при одинаковых calling convention, parameter types, return type, unsafe marker, asynchronous marker, checked-error set и variadic marker. Порядок checked-error set не влияет на compatibility.

<a id="R-TYPE-0017"></a>

**R-TYPE-0017** — Implicit user conversions, inheritance, specialization и inferred module interfaces отсутствуют. Пользовательские generics следуют R-TYPE-0031 — R-TYPE-0040, traits со статической диспетчеризацией — R-TYPE-0041 — R-TYPE-0043, dyn-интерфейсы над ними и их владельцы — R-TYPE-0051 и R-TYPE-0055, кортежи — R-TYPE-0052, пакеты типов — R-TYPE-0053, а типы функций — R-TYPE-0054.

### 9.4 Copy and Move types

<a id="R-TYPE-0018"></a>

**R-TYPE-0018** — `bool`, fixed R/C ABI integers and floats, `char`, fieldless enum, shared borrows, shared immutable slices, `str`, `constexpr str`, raw pointers, raw C function pointers и their nullable forms where grammar permits nullability являются Copy.

<a id="R-TYPE-0019"></a>

**R-TYPE-0019** — Fixed array, `o`, struct или tagged enum является Copy тогда и только тогда, когда every possible element/field/variant payload type Copy и type не имеет user-defined drop. `own T*`, `arc T`, `rc T`, обе weak-owner forms, exclusive borrow `T*`, slices with exclusive access, atomic types, `array<T>`, `list<T>`, `dict<K,V>`, `task<T throws E...>`, standard synchronization/channel/guard/thread-handle, container-iterator types, а также closed standard outcome types из R-STMT-0006 Move-only.

<a id="R-TYPE-0020"></a>

**R-TYPE-0020** — Copy создаёт независимое value без изменения source state. Для Move-only type передача из named place требует `move`; bitwise copy не является операцией языка.

<a id="R-TYPE-0021"></a>

**R-TYPE-0021** — Raw pointer may become dangling when target lifetime ends. Storing, copying and equality-testing a dangling raw value remain defined; dereference, arithmetic, ordering or conversion to borrow then violates the applicable safety contract. Equality compares preserved historical allocation identity and offset.

<a id="R-TYPE-0022"></a>

**R-TYPE-0022** — C ABI integer types have value range, rank, signedness and representation of the corresponding selected C17 typedef or fundamental type. `c_char` signedness, `c_wchar` range, `c_wint` availability and, when available, its range, and all available widths shall be recorded in the target manifest. R shall check their signed overflow rather than inherit C UB.

<a id="R-TYPE-0023"></a>

**R-TYPE-0023** — `c_int8`, `c_uint8`, `c_int16`, `c_uint16`, `c_int32`, `c_uint32`, `c_int64`, `c_uint64`, `c_intptr` and `c_uintptr` are available only when the corresponding C17 typedef exists on target. `c_wint` is likewise available only when target C implementation provides compatible `wint_t` through its selected profile/headers. Use on unsupported target requires a translation diagnostic, not substitution by a merely similar type.

<a id="R-TYPE-0024"></a>

**R-TYPE-0024** — `c_bool` has C `_Bool` ABI and R values false/true; `c_intmax`/`c_uintmax`, `c_size`, `c_ptrdiff`, `c_float`, `c_double` and `c_long_double` match corresponding C types. Conversion between C ABI and fixed R numeric types is explicit and checked except where exact compatibility is proven.

<a id="R-TYPE-0025"></a>

**R-TYPE-0025** — При construction любой `arc T`/`rc T` allocation и до завершения value-bearing declaration любой strong/weak form T shall быть complete, sized, inhabited, non-`void`, non-`never` R object type с известным drop glue; borrow-bearing T передаёт owner свои regions (R-BORROW-0018). Managed-owner field may назвать R aggregate, который сейчас определяется, до его closing brace; tag shall стать complete в том же module до construction любого value containing type. Imported opaque type никогда не подходит. Handle имеет stable allocation identity, но implementation-private representation. Strong handles одного family и T могут разделять control block; families `arc` и `rc` никогда не interconvert implicit или explicit. Weak handle нельзя dereference.

<a id="R-TYPE-0026"></a>

**R-TYPE-0026** — Standard-library parametric type записывается как lowercase qualified standard name с parenthesized value type или types, например `std.thread::join_handle<i32>`, `std.thread::scoped_join_handle<i32>`, `std.sync::mutex<State>`, `std.list::iter<Item>` либо `std.dict::iter<str,Item>`. Допустимы только lowercase names, arities и constraints, закреплённые R Standard Library Specification 0.1. Разбор такой записи не объявляет user type constructor и не ослабляет R-TYPE-0017. Scoped handle, lock guard, container iterator либо entry reference carries hidden inferred region, fixed creating operation; этот region никогда не является source type argument.

<a id="R-TYPE-0027"></a>

**R-TYPE-0027** — `std.thread::join_handle<void throws E...>`, `std.thread::scoped_join_handle<void throws E...>` и `std.thread::join_result<void>` используют `void` только как compile-time marker entry без return value; это complete Move-only types, не содержащие void object. Вместе с `task<void throws E...>` это единственные исключения, разрешающие `void` как type argument. При empty `throws` part соответствующая запись handle или task становится effect-free. Любой иной standard type argument shall соответствовать R-TYPE-0007.

<a id="R-TYPE-0028"></a>

**R-TYPE-0028** — `constexpr str` является отдельным safe string type, well-formed UTF-8 bytes которого встроены в immutable program-image storage, остаются live всё время execution программы и не имеют runtime destruction. Его value — Copy descriptor, содержащий provenance-bearing start и byte length; descriptor may быть выбран, copied, передан, returned либо сохранён в runtime aggregate, даже когда его concrete value выбирается runtime. Каждый possible descriptor value shall всё равно обозначать такие program-resident bytes. Для empty value его start shall обозначать implementation-selected sentinel с lifetime всей программы, и ни один byte не является readable; это специализация R-ARRAY-0003 для `constexpr str`. Эти guarantees относятся к backing bytes. Сам Copy descriptor имеет ordinary object storage duration; завершение или, если он не `const`, перезапись его object не уничтожает и не изменяет backing bytes. `constexpr` в R 0.1 допустим только в двухтокенном string-type head `constexpr str`; поэтому access-qualified type `const constexpr str` также допустим, но любое иное placement или application `constexpr` недопустимо. Он не является storage specifier, не превращает expression в constant expression и не требует, чтобы callee знал, какую строку выбрал caller. Ни ordinary `str`, ни borrowed buffer, allocated string или external-provider string не является source этого type; safe value `constexpr str` происходит от string literal и нуля или более Copy, selection, aggregate, argument и return operations, сохраняющих type.

<a id="R-TYPE-0029"></a>

**R-TYPE-0029** — `task<T throws E1, E2>` является compiler-recognized Move-only handle с unique правом наблюдать одно eager asynchronous computation, logical return type которого T, а canonical checked completion-error set равен указанному set. Пустой spelling имеет вид `task<T>`. T и каждый E shall быть outermost-unqualified, complete, sized и inhabited и shall not recursively содержать ordinary borrow, slice либо `str`; `task<void>` — единственное fieldless-result exception и не содержит void object. Async function с logical return type `never` наблюдается только call form await (R-STMT-0012); её hidden task не является object `task<never>`. `constexpr str` допустим, поскольку его bytes имеют program lifetime. Каждый E дополнительно shall соответствовать checked-error constraints R-TYPE-0012. `task<T throws E...>` не является user generic либо standard-library nominal type, имеет implementation-private representation и не имеет nullable spelling. Task с nonempty checked-error set является must-resolve по R-FUNC-0012.

<a id="R-TYPE-0030"></a>

**R-TYPE-0030** — `bytes` является predefined non-shadowable type name и точным transparent alias `array<u8>`. Он не вводит nominal identity, wrapper, conversion, storage, invariant либо отдельный ABI. Перед проверкой type compatibility, layout, derivation capabilities, interface fingerprinting, mangling либо generic-family selection каждое occurrence `bytes` canonicalizes в `array<u8>`; поэтому value с любым из этих spellings может использоваться как другое без conversion и имеет ровно те же Move-only, Send и Sync properties. Empty initializer `{}` допускается для этого canonical type, не выполняет allocation и строит его единственное empty state с zero length и zero capacity; следовательно, `bytes value = {};` и `array<u8> value = {};` обозначают одну и ту же initialization.

`bytes` остаётся identifier token по R-LEX-0008, а не становится keyword. В type position Annex A распознаёт это exact spelling как predeclared type до classification aggregate name. Оно не может быть объявлено как aggregate type name, однако тот же token spelling остаётся допустимым в другом name space, включая ordinary function либо value и field `bytes` digest structure.

### 9.7 Пользовательские generic-схемы

<a id="R-TYPE-0031"></a>

**R-TYPE-0031** — Заголовок `@generic<T>` объявляет типовые параметры одной структуры, перечисления, ошибки, trait, обычной функции, async-функции или associated hook. `generic` является контекстным именем заголовка только после `@`; в остальных позициях это identifier, в том числе в применениях типа с именем `generic`. На объявление разрешён один заголовок. Обычные attributes могут находиться до и после него, перед модификаторами объявления; `generic` не является обычным именем attribute. Между токенами допустимы пробелы и комментарии. Заголовки запрещены на полях, параметрах, локальных и модульных объектах, на extern-блоках и внутри них. Запрет generic C exports сохраняется. Старый заголовок объявления `generic(T)` ill-formed и требует `R-DIAG-SYN-001` с сообщением `use @generic<...> to declare generic parameters`; корректные применения типов, например `generic<i32>`, сохраняются. Повторные заголовки, пустые списки параметров и повреждённые constraints являются синтаксическими ошибками. Действуют существующие правила visibility. Типовые параметры можно сочетать с compile-time параметрами `const usize` по R-TYPE-0047; defaults и специализации не вводятся, параметр-пакет следует R-TYPE-0053; trait constraint следует R-TYPE-0043, core trait constraint — R-TYPE-0046. Применение aggregate-схемы записывается `Box<i32>`; типовые аргументы shall быть complete, sized, inhabited value types без outermost object `const`. Вложенные qualifiers сохраняются.

Списки аргументов типов и generic-аргументов записываются между `<` и `>`: `array<i32>`, `dict<K, V>`, `o<T>`, `task<T throws E>`, `Box<i32>` и `@generic<T: copy>`. Token `>>` закрывает два вложенных списка. В выражении имя, за которым следует `<`, начинает тип только когда за парной `>` следует `::` или `{`, как в `Box<i32>::wrap(4)` и `Box<i32> { .value = 1 }`; иначе `<` является сравнением. Явные аргументы функции записываются `name::<arguments>` (R-TYPE-0036). Константный generic argument, содержащий `>`, заключается в скобки. Заголовок в скобках `@generic(...)` требует `R-DIAG-SYN-001` с сообщением `generic parameters are written @generic<...>`.

<a id="R-TYPE-0032"></a>

**R-TYPE-0032** — Идентичность применения определяется модулем и объявлением nominal schema и каноническими типовыми и константными аргументами. `Box<bytes>` и `Box<array<u8>>` тождественны. Одноимённые схемы разных модулей различаются. Конструкторы записываются `Box<i32> { .value = 1 }`, `Choice<i32>::Some(1)` и `Choice<i32>::Fields { .value = 1 }`. Generic error сохраняет самостоятельную категорию ошибки R-AGG-0001; generic struct и enum эту категорию не получают.

<a id="R-TYPE-0033"></a>

**R-TYPE-0033** — Закрытый набор constraints: `copy`, `pod`, `send`, `sync`, `key`, `error`, `unborrowed`, `json_encode`, `json_decode` и `clone`. Ограничения объединяются через `&`; порядок и повторы не влияют на идентичность. `|` не допускается. Copy, Send, Sync и key обозначают существующие структурные свойства и key contract. `error` доказывает допустимость во всех checked-error позициях. `unborrowed` доказывает отсутствие обычных borrows, slices и runtime str рекурсивно по правилам task; оно не доказывает Send. Guard, lock outcome с guard, scoped handle, container iterator и entry reference держат region создавшей их операции (R-TYPE-0026) и никогда не `unborrowed`. Opaque standard types используют явно зарегистрированные capabilities, а не написание или суффикс имени. Constraint вне этого набора именует trait по R-TYPE-0043, core trait как `core::Name` по R-TYPE-0046 либо записывает callable signature по R-TYPE-0044.

`json_encode` и `json_decode` доказывают доступность соответствующего направления JSON-преобразования по R-SLIB-JSON-0001. Ни одно не подразумевает другое; Copy и POD не доказывают JSON-способности. Generic-тело требует доказательства до закрытия типов. `clone` доказывает контракт клонирования R-OWN-0020; `copy` его влечёт.

<a id="R-TYPE-0034"></a>

**R-TYPE-0034** — POD означает Copy-данные без владельцев, указателей, обычных borrows, строковых представлений, atomic-компонентов и пользовательского drop. Числовые типы R и C, bool, char, fieldless enums и составные типы из POD-компонентов удовлетворяют POD. Opaque standard types требуют явной POD-записи в capability registry. Padding допускается. POD подразумевает Copy и unborrowed, но не доказывает Send, Sync или key для неизвестного параметра. POD разрешает копирование корректного представления того же типа, но не произвольные байты, нулевую инициализацию, bytewise equality или переносимую сериализацию.

<a id="R-TYPE-0035"></a>

**R-TYPE-0035** — Каждое generic-определение, включая неиспользованное, проверяется в модуле определения до инстанциации. Доступны только операции, доказанные constraints. Copy и POD не дают арифметику, произвольные поля, implicit default initialization или дополнительные методы. После `move` неизвестный T считается потреблённым, если Copy не доказан; подстановка Copy-типа не исправляет ошибочное определение. Для конкретного Copy move выполняет обычное копирование и сохраняет источник с соблюдением правил доступа. Для Move сохраняются правила передачи. Explicit drop для Copy запрещён; неизвестный T допускает обычное уничтожение при выходе из scope. Разрешения перемещать из полей или заимствованной памяти не добавляются.

<a id="R-TYPE-0036"></a>

**R-TYPE-0036** — Generic-аргументы функции указываются явно либо выводятся структурно из value arguments, включая вложенные применения схем, fixed arrays и стандартные параметризованные типы. Явный список `name::<arguments>` следует за именем функции, квалифицированным именем функции, `Owner<args>::name` либо именем метода в вызове через receiver. Он называет в порядке заголовка каждый generic-параметр, который не задан написанием owner или receiver: тип для параметра типа, проверенную константу для константного параметра и `throws(E...)`, возможно пустой, для параметра `errors`; для пакета (R-TYPE-0053) аргументы после аргументов остальных параметров, возможно ни одного, — его элементы. Список закрывает функцию до проверки value arguments, вывод не применяется. Для семейства перегрузок список, а для вызова ещё и число value arguments, shall выбирать ровно один generic-член. Список неверной длины или формы либо список после имени, не обозначающего открытую generic-функцию, требует `R-DIAG-TYPE-001`. Без списка каждый параметр выводится: префикс схемы method следует из написания owner в `Owner<args>::name(...)` (R-FUNC-0013) или из receiver, а параметры, именуемые callable constraint выведенного параметра, следуют из closure argument (R-TYPE-0044); невыведенный параметр требует `R-DIAG-TYPE-001`. Параметры, которые аргументы оставили открытыми, затем берутся из ожидаемого типа результата вызова: объявленного типа инициализируемого объекта, цели `=`, типа результата `return`, известного типа параметра, аргументом которого является вызов, и поля или элемента инициализатора агрегата; awaited async-вызов ожидает результат await, а любой другой async-вызов — свою task. Ожидаемый тип заполняет только параметры, оставленные аргументами открытыми, и не выбирает перегрузку; если тип результата ему не соответствует, он ничего не выводит, а к результату применяются обычные преобразования и диагностики. Generic-параметр не обязан встречаться в value-параметре: result-only `@generic<T> array<T> make()` корректна и вызывается как `make::<i32>()` либо как `make()`, когда у результата есть ожидаемый тип, например `array<i32>`. Подбор числовых преобразований не даёт кандидатов вывода. Каждая перегрузка проверяется независимо по R-FUNC-0004. Литералы без контекста используют обычные типы R. Противоречивые кандидаты одного параметра ill-formed. После вывода или явного закрытия применяются обычные преобразования аргументов; ослабление borrow access сохраняет выведенный pointee type. Явно закрытое имя в позиции значения является function item (R-FUNC-0007). First-class полиморфные generic-функции не поддерживаются. Распознаваемые компилятором стандартные операции с type operands используют ту же форму, например `std.array::create::<i32>()`, `std.dict::with_capacity::<Key, u32>(16usize)` и `core::enum_count::<Color>()`; type operand среди value arguments требует `R-DIAG-SYN-001`. Конструкторы `std.array::create`, `std.array::with_capacity`, `std.list::create`, `std.dict::create`, `std.dict::with_capacity`, `std.sync::channel`, `std.sync::sync_channel` и `std.sync::once_lock` могут опускать список, когда ожидаемый тип их результата — тип их конструктора, дающий операнды, как в `array<i32> values = std.array::create();`; без такого ожидаемого типа опущенный список требует `R-DIAG-TYPE-001`.

<a id="R-TYPE-0037"></a>

**R-TYPE-0037** — Generic drop, hash и equal shall использовать точные типовые параметры схемы и её нормализованные constraints. Дополнительные hook-only constraints и хуки для отдельных закрытых применений ill-formed. `drop(Box<T>* self)` следует R-OWN-0006; после тела поля или активный payload уничтожаются в существующем порядке. Hash/equal остаются парой с сигнатурами и effect contract R-FUNC-0009. Constraint key даёт доступ через `core::hash` и `core::key_equal`, а не неявный оператор равенства.

<a id="R-TYPE-0038"></a>

**R-TYPE-0038** — Generic async-результаты и completion errors требуют доказанных Send и unborrowed; ошибки дополнительно требуют error. Captures и значения через suspension сохраняют все правила task, владения и заимствований. Условный throw, throw/else, rethrow, условные выражения, catch и finally сохраняют обычную семантику после подстановки, включая уничтожение выбранного активного payload ровно один раз.

<a id="R-TYPE-0039"></a>

**R-TYPE-0039** — Каждое закрытое применение получает конкретные типы, layout, cleanup и код. Инстанциация подставляет типы в проверенное семантическое представление, а не в исходный текст. Type erasure, boxing и runtime generic metadata не вводятся. Единый детерминированный cache компиляции использует идентичность схемы и канонические аргументы; обычная рекурсия переиспользует запись. Static-объекты принадлежат закрытой инстанциации функции и разделяются её вызовами. Растущие инстанциации ограничиваются compiler limits и shall давать диагностику с цепочкой инстанциаций. Зависимые размеры, выравнивания, layouts и значения констант определяются после закрытия типов.

<a id="R-TYPE-0040"></a>

**R-TYPE-0040** — Межмодульная инстанциация требует исходники определения. Interface schema version 5 экспортирует параметры, нормализованные constraints, схемы полей и вариантов, категорию error, хуки, fingerprints определений и зависимости от исходников. Имена разрешаются в модуле определения; порядок обнаружения или исходных файлов shall не менять идентичность инстанциаций, код или interface metadata. Загрузка generic-тел из бинарной библиотеки и generic C exports не входят в эту версию. C-символы и контракты стандартной библиотеки сохраняются; внутренние хеши компилятора и core хеширование ключей контейнеров остаются независимыми от пользовательских generics.

<a id="R-TYPE-0041"></a>

**R-TYPE-0041** — `trait Name { members };` объявляет статический trait в пространстве имён типов. `@generic<T: copy> trait Name ...` добавляет типовые параметры; константные параметры traits не допускаются. Членом служит прототип метода, проверяемое тело метода по умолчанию объявление связанного типа (R-TYPE-0045) или связанной константы (R-TYPE-0050). `Self` обозначает реализующий тип. Схема проверки включает Self, явные параметры и связанные типы. Сигнатуры и тела по умолчанию проверяются при определении, в том числе для неиспользованных объявлений; доступны только операции, доказанные ограничениями. Имена разрешаются в модуле определения. К методам применяются обычные атрибуты ресурсных эффектов и must-use.

`trait Child : Parent<T> & copy ...` требует указанные nominal supertraits и capabilities для Self. Зависимости supertraits должны быть ациклическими. Унаследованные методы и ограничения связанных типов доступны при проверке тел и вызывающего generic-кода. Capabilities Self независимы от capabilities явных параметров. Trait objects и runtime witnesses не вводятся; динамическая диспетчеризация существует только через dyn-интерфейсы R-TYPE-0051.

<a id="R-TYPE-0042"></a>

**R-TYPE-0042** — `impl Name<Arguments> for Type { definitions };` реализует приложение trait для канонического типа; у негeneric trait скобки отсутствуют. Реализация принадлежит модулю trait либо модулю целевого типа. Для пары канонического приложения trait и типа допустима одна реализация. Различные приложения, например Read(i32) и Read(u64), могут иметь реализации для одного типа.

Реализация связывает все связанные типы и все связанные константы без значения по умолчанию (R-TYPE-0050) и удовлетворяет ограничениям связанных типов, capabilities Self и отдельно реализованным supertraits. Метод без тела по умолчанию требует определения. Определение с точной сигнатурой заменяет default; иначе проверенное тело инстанцируется с целевым и связанными типами. Лишние или несовпадающие члены запрещены. Должны совпадать параметры, результат, receiver, точный набор checked errors, unsafe и async. Обещания ресурсных эффектов и must-use действуют и без повторения атрибутов в реализации. Неиспользованные реализации также проверяются. Методы protected-типа сохраняют его видимость. Generic-реализация повторяет параметры и нормализованные ограничения схемы целевого типа и может добавлять к ним ограничения; она применяется только к инстансам, аргументы которых их доказывают (R-TYPE-0043). Целью generic-реализации может быть и стандартный конструктор типа от параметров реализации, например `o<T>`, `array<T>`, `T[N]`, `T[]` или `const T[]`: реализация применяется к каждому инстансу этой формы, аргументы которого доказывают её ограничения, причём аргументы следуют из инстанса, а поиск метода у инстанса находит её методы. Выбор даёт обычные конкретные функции и прямые вызовы.

<a id="R-TYPE-0043"></a>

**R-TYPE-0043** — Nominal generic constraint обозначает видимый trait или его приложение, например `Read<i32>` либо `m.n::Read<T>`. Арность, аргументы и их ограничения проверяются при определении и после подстановки. Тип доказывает constraint согласованной реализацией либо своими объявленными ограничениями, включая подставленные supertraits. Поиск метода учитывает конкретное приложение trait. Метод из разных приложений неоднозначен даже при общем исходном объявлении. Повторный унаследованный путь к тому же методу того же приложения не создаёт второго кандидата. Ожидаемый результат не разрешает неоднозначность прямого вызова. До генерации кода каждый trait-вызов связывается с конкретной реализацией, кроме вызова через dyn-интерфейс, который выбирает среди реализаций его членов (R-TYPE-0051).

В заголовке generic-функции запись `P::Name: constraints` ограничивает ассоциированный тип `Name` параметра `P` того же заголовка, как в `@generic<I: core::Iterator, I::Item: std.cmp::Ordered>`. Ровно одно trait-ограничение `P`, напрямую или через supertrait, shall объявлять `Name`, иначе требуется `R-DIAG-TRAIT-001`; ограничения — capabilities и номинальные traits, а callable constraint требует `R-DIAG-TRAIT-001`. Такая запись в другом заголовке требует `R-DIAG-TYPE-001`. Запись не добавляет параметра в схему. Тело может применять к значениям `P::Name` операции, которые обосновывают её ограничения, а каждая инстанциация shall связывать с `P::Name` тип, доказывающий их; иначе требуется `R-DIAG-TYPE-001`. Interface schema 31 записывает такие записи как `associated_constraints=((associated=P::Name constraints=(...)))`.

<a id="R-TYPE-0044"></a>

**R-TYPE-0044** — Generic constraint `fn(P...) -> R` описывает синхронный callable с параметрами `P...` и результатом `R`. Типы могут зависеть от той же схемы. Синхронные параметры и результаты следуют обычным правилам заимствований, включая aggregate с заимствованиями, slices и runtime `str`; `void` допустим только как результат, `never` исключён. Результат и checked payload ограничены происхождением входов по R-BORROW-0009. Режим после `fn` — `shared` (по умолчанию), `mut` или `once` — задаёт receiver `const Self*`, `Self*` или по значению. Неявных адаптеров между режимами нет. Последним типом параметра может быть раскрытие пакета `P...`, обозначающее элементы пакета `P` как последние параметры (R-TYPE-0053).

Атрибуты без аргументов `@noalloc` и `@nonblocking` разрешены после `fn`, перед режимом. Необязательное `throws(E1, E2)` после результата задаёт точный набор ошибок; скобки отделяют его от следующих generic-параметров. Начальное `async` задаёт async callable и требует `once` (также значение по умолчанию). Его логический результат, completion-errors, параметры и передаваемая среда требуют Send и unborrowed; ошибки дополнительно требуют категорию error. Вызов возвращает обычный task и сохраняет обычную транзакцию start-error. Ресурсные гарантии можно забыть, но нельзя получить без доказательства; остальные типы параметров/результата, режим, sync/async и checked errors должны совпадать точно.

Каждой сигнатуре соответствует неявный callable trait, реализуемый подходящим замыканием или function item (R-FUNC-0007). Generic-тело использует только доказанные сигнатурой операции и ресурсные гарантии. Структурный вывод использует callable-сигнатуру и value-аргументы; инстанциация подставляет зависимые типы и привязывает вызовы непосредственно к проверенному lambda-телу или исходной функции. Runtime dispatch, boxing и callable metadata не вводятся.

<a id="R-TYPE-0045"></a>

**R-TYPE-0045** — `type Name: copy & Read<T>;` объявляет связанный тип с необязательными ограничениями closed capabilities и nominal traits. Self::Name обозначает его в trait и телах по умолчанию. Доступно также однозначное унаследованное имя. Ограничения дают ровно заявленные операции и проверяются для каждой реализации, в том числе неиспользованной. Реализация связывает каждый связанный тип своего trait ровно один раз записью `type Name = T;`, где T — полный value-тип. Отсутствующее, неизвестное или несовместимое связывание требует `R-DIAG-TRAIT-001`, повторное — `R-DIAG-NAME-002`.

`P::Name` проецирует связанный тип одного однозначного приложения trait параметра P. До инстанциации операции определяются только ограничениями связанного типа; после неё используется тип из выбранной реализации. Связанные типы не выводятся независимо из аргументов вызова. Opaque-результат может явно зафиксировать их по R-TYPE-0048.

Связанный тип связывается один раз для реализации и не зависит от региона вызова. Метод, реализующий prototype с заимствованным receiver, результат которого упоминает связанный тип этого trait, в том числе унаследованный от supertrait и в любом приложении generic trait, например `next` у `core::Iterator`, не должен возвращать view хранилища, которое обозначает его receiver, в том числе view, достигнутый из этого хранилища через удерживаемые там исключительное заимствование или срез, owner или контейнер; такой возврат требует `R-DIAG-BORROW-002`. Допустимы view, которые удерживает это хранилище, и view, достигнутые через удерживаемые там разделяемые заимствования, разделяемые срезы и обычные `str`. Вызов такого метода зависит от заимствованного receiver только через то, что receiver удерживает (R-BORROW-0009): пока результат жив, receiver можно снова заимствовать, перемещать или уничтожать, а удерживаемое им остаётся заимствованным. То, что результат берёт из других аргументов или из receiver, переданного по значению, зависит от этих аргументов, как при любом вызове.

Атрибут `@lending` помечает prototype метода trait, не async, с receiver `Self*` или `const Self*`, результат которого упоминает связанный тип этого trait; на любой другой функции он требует `R-DIAG-TRAIT-001`. Предыдущий абзац к lending-методу не применяется: его реализации могут возвращать view хранилища, которое обозначает receiver, а вызов такого метода — непосредственно, через generic-параметр, dyn-интерфейс или opaque-результат — зависит от всего receiver, как от заимствованного аргумента, из которого сформирован результат (R-BORROW-0009), поэтому receiver остаётся заимствованным, пока результат жив. Реализация наследует атрибут и может его повторить; атрибут на методе, реализующем prototype без него, требует `R-DIAG-TRAIT-001`. Interface schema 31 записывает `lending=true`.

Связанный тип может объявлять типовые параметры, например `type Name<T: copy, U>: bounds;`, каждый с ограничениями, записанными после него (R-TYPE-0033, R-TYPE-0043). Их имена отличаются друг от друга и от параметров объемлющего generic trait или реализации; иначе требуется `R-DIAG-NAME-002`. Такой связанный тип используется только применённым к одному типовому аргументу на каждый параметр, как `Self::Name<i32>` или `P::Name<T>`, и каждый аргумент доказывает ограничения своего параметра (R-TYPE-0031). Недостающий или лишний аргумент, константный аргумент, аргументы связанного типа без параметров и недоказанное ограничение требуют `R-DIAG-TYPE-001`. Реализация связывает его записью `type Name<T, U> = Type;`, повторяющей параметры по порядку без ограничений; `Type` может их упоминать и shall доказывать ограничения связанного типа для параметров, у которых есть только объявленные ограничения. Связывание с другими параметрами требует `R-DIAG-TRAIT-001`, ограничения, записанные на его параметрах, — `R-DIAG-SYN-001`. До инстанциации применение имеет только операции, которые дают ограничения его связанного типа; после неё это выбранное связывание с аргументами, подставленными вместо параметров. Generic-параметр не выводится из применения. Связанный тип с параметрами не фиксируется равенством opaque-результата или dyn-интерфейса (R-TYPE-0048, R-TYPE-0051). Interface schema 31 записывает параметры связанного типа, а связывание с параметрами — как `(type_function parameters=(...) body=...)`.

<a id="R-TYPE-0046"></a>

**R-TYPE-0046** — Язык объявляет шесть core traits, не принадлежащих ни одному module и записываемых `core::Iterator`, `core::LendingIterator`, `core::Contains`, `core::CaseMatcher`, `core::Format` и `core::AsyncIterator` везде, где допустимо trait name, включая generic constraints и интерфейсы (R-TYPE-0051). `core::Iterator` объявляет associated type `Item` и prototype `o<Self::Item> next(Self* this)`; `core::LendingIterator` объявляет `Item` и lending prototype `@lending o<Self::Item> next(Self* this)` (R-TYPE-0045); `core::Contains` объявляет `Item` и `bool contains(const Self* this, const Self::Item* value)`; `core::CaseMatcher` объявляет `Label` и `bool matches(const Self* this, Self::Label label)`; `core::Format` не объявляет associated type и объявляет prototype `void format(const Self* this, std.format::builder* out) throws std.alloc::alloc_error`, который дописывает текст значения в `out`; `core::AsyncIterator`, объявленный только в профиле `hosted-native-async`, объявляет `Item` и prototype `@scoped async o<Self::Item> next(Self* this) throws std.error::fault` (R-STMT-0017), где `Self` — Send, а `Item` — Send и не содержит borrow (R-TYPE-0029). Core trait реализуется только для nominal type реализующего module с associated type, связанным по R-TYPE-0045; любой другой target требует `R-DIAG-TRAIT-001`. Range-for (R-STMT-0014) продвигает каждую implementation `core::Iterator` и, для типа без неё, implementation `core::LendingIterator`, асинхронный for (R-STMT-0021) продвигает каждую implementation `core::AsyncIterator` типа без обеих, membership (R-EXPR-0029) проверяет каждую implementation `core::Contains`, а метки `switch` и `match` (R-STMT-0006, R-EXPR-0031) — каждую implementation `core::CaseMatcher` в дополнение к built-in forms, перечисленным в этих rules. Standard cursors `std.list::iter` и `std.dict::iter` продвигаются range-for через собственные operations `next` и не реализуют trait в этой редакции.

`core::Format` называет `std.format::builder` и доступен там, где доступен `std.format` (R-SLIB-PROFILE-0001). Кроме типов с implementation, ему удовлетворяют типы со стандартным форматированием: целые, плавающие, Boolean и символьные типы R и C, `str`, `constexpr str`, `std.string::string`, `std.net::ip_address`, `std.net::socket_address`, а также `o<T>`, `T[N]`, срезы, `array<T>` и кортежи, типы элементов которых ему удовлетворяют. Стандартное форматирование пишет целые в десятичной записи, плавающие значения в канонической записи `std.format`, Boolean как `true` или `false`, символы и текст точно, адрес так же, как `std.net::format_ip`, адрес сокета как `a.b.c.d:port`, `[v6]:port` или, при ненулевом scope, `[v6%scope]:port`, option как `none` или `some(v)`, массивы и срезы как `[a, b]`, а кортежи как `(a, b)`, каждый элемент — его собственным форматированием. Место, тип которого удовлетворяет trait, имеет метод с разделяемым receiver (R-FUNC-0014): `pointer->format(out)` и `place.format(out)`, который для места, не являющегося шаблоном, — этот метод, а не шаблонная форма R-EXPR-0028. Форматированные литералы (R-EXPR-0028), эти вызовы и стандартное форматирование объемлющего значения вызывают implementation; неудачное дописывание бросает `alloc_error` и оставляет в builder текст, дописанный до него. Форматирование входит в статический граф вызовов (R-FUNC-0004) каждой достижимой implementation, поэтому implementation, рекурсивно форматирующая собственный тип, требует `R-DIAG-STACK-001`, и у форматирования нет доказательства `@noalloc` или `@nonblocking` (R-FUNC-0019).

<a id="R-TYPE-0047"></a>

**R-TYPE-0047** — `@generic<T, const usize N>` объявляет типовые параметры и неизменяемые compile-time константные параметры в одном упорядоченном заголовке. Константный параметр имеет целочисленный тип R (`i8`, `i16`, `i32`, `i64`, `isize`, `u8`, `u16`, `u32`, `u64`, `usize`) или `bool`, без constraints, defaults, packs и специализации. Поддерживаются те же объявления и hooks, что в R-TYPE-0031; hooks должны совпадать по видам, типам и именам параметров и нормализованным constraints. Константный параметр является значением своего объявленного типа, а не типом значения и не runtime-параметром. Его нельзя присваивать, заимствовать как storage или скрывать локальной переменной либо value-параметром.

Применения вроде `Buffer<u8, 64usize>` требуют аргументов объявленного вида. Константный аргумент — проверенное constant expression типа параметра: литерал, доступная локальная или модульная константа, константный параметр, связанная константа (R-TYPE-0050), `sizeof`/`alignof`, reflection constant, translation-time call (R-EXPR-0032) либо целочисленная арифметика. Неявные преобразования не применяются: `Buffer<u8, 4u32>` отклоняется для параметра `usize`, а литерал без суффикса получает тип параметра. Действуют обычные правила доступа и lexical scope; runtime-значения не подходят. Формулы размера отклоняют отрицательный результат, переполнение сложения, вычитания и умножения, переполняющий сдвиг влево, недопустимые сдвиги и деление на ноль. Уже вычисленная именованная константа `usize` предоставляет своё значение. Ноль допустим как generic argument, но граница фиксированного массива остаётся положительной по R-TYPE-0011. Закрытый layout подчиняется обычным ограничениям размера, alignment и compiler limits. Зависимые формулы проверяются при определении и вычисляются после закрытия параметров; ошибочные неиспользованные тела также отклоняются. Зависимая формула может вызывать функцию, вычислимую при трансляции (R-EXPR-0032), аргументы или generic arguments которой зависят от параметров, как в `u8[size_of::<T>()]`. Такая вычисляемая формула проверяется при определении как зависимое статическое условие (R-META-0003); вычислимость вызываемых функций и значение определяются для каждого закрытого инстанса, а panic, превышение лимита, вызываемая функция, невычислимая для этого инстанса, или измерение определяемого инстанса требуют диагностик R-EXPR-0032 с именем инстанса. Вычисляемые формулы с одинаковыми токенами в одном модуле при сопоставлении параметров по позиции — одна формула; формула в теле функции, читающая её локальные переменные, отдельна. Инстанс, закрытый до проверки тел функций, например поле или сигнатура уровня модуля, получает значение, вычисленное разведочным проходом (R-EXPR-0032).

Каноническая идентичность включает тип и значение каждой константы. `Buffer<u8, 2usize + 2usize>` и `Buffer<u8, 4usize>` тождественны. Значения подставляются в проверенное семантическое представление, не в исходный текст. Использования в теле становятся обычными константами своего типа в каждой закрытой функции; скрытых runtime-параметров, таблиц metadata или аллокаций нет. Статические объекты инстанциаций и пределы рекурсии следуют R-TYPE-0038..0040.

Вывод получает константный параметр из непосредственной размерности `T[N]` или вложенного generic argument, рекурсивно по поддерживаемым формам параметров. Уравнения не решаются: одного `T[N + 1usize]` недостаточно для вывода `N`. Если другой аргумент определил `N`, размерность проверяется после подстановки. Противоречивые кандидаты отклоняются; невыводимый константный параметр называется в явном списке, например `fill::<u8, 4usize>` (R-TYPE-0036). Ожидаемый результат не участвует. Copy/Move, заимствования, checked errors, запуск async и cleanup сохраняют обычные правила.

Interface schema 31 записывает константные параметры с `constant_type=usize`, закрытые аргументы с типом и значением, зависимые выражения и fingerprints определений. Callable constraints schema 11 остаются структурными. Импорт определений по-прежнему требует исходники; сериализация generic bodies не вводится.

<a id="R-TYPE-0048"></a>

**R-TYPE-0048** — `opaque(contracts)` задаёт полный контракт результата функции, скрывающий один конкретный тип реализации. В контракт входят generic capabilities, приложения статических traits, callable-сигнатуры и равенства связанных типов, например `Item = i32`. Для каждого объявленного связанного типа требуется одно однозначное равенство. У связанного типа с параметрами (R-TYPE-0045) равенства нет, поэтому контракт, traits которого объявляют такой тип, требует `R-DIAG-TRAIT-001`. Например, `opaque(core::Iterator & Item = i32)` предоставляет итерацию по i32, а `opaque(fn(i32) -> i32 & copy)` — Copy callable. Поддерживаются обычные и async-функции, включая generic. Требуется определение; синтаксис запрещён в параметрах, объектах, полях, вложенных конструкторах типов и сигнатурах C ABI. Внешний object const запрещён.

Все return должны устанавливать один канонический конкретный тип при проверке определения, в том числе неиспользованного generic. Уточнения статических ветвей не меняют идентичность типа. Тело без конкретного return не позволяет вывести opaque-результат. Реализация должна доказать все заявленные capabilities, traits, связанные равенства и callable-контракты. Объявляющая функция и её канонические generic-аргументы задают самостоятельную opaque идентичность. Вывод generic-аргументов использует её; ожидаемый результат не раскрывает и не выбирает реализацию. Разные opaque-идентичности сохраняют раздельное generic static-состояние.

Вызывающий код использует auto либо обычный generic inference и только заявленные контракты. Скрытые поля, необъявленные операции и неявное преобразование к реализации недоступны. Универсальный базовый тип и runtime type erasure не вводятся. Provenance следует конкретным captures и компонентам результата; opaque не продлевает заимствование. Для async сохраняются требования Send и unborrowed.

После проверки и мономорфизации исполняемое представление совпадает с конкретным типом, его layout, прямыми вызовами и cleanup. Opaque Move-контракт может иметь Copy-представление: источник потребляется семантически, а физическое копирование не требует деструктора. Конкретные Move-payload сохраняют однократный cleanup. Анализы стека, аллокаций и блокировок проверяют конкретный исполняемый граф, включая скрытые drop-тела. Interface 16 записывает opaque-контракты, связанные равенства, идентичность объявления и fingerprints исходников; физический carrier layout остаётся ABI metadata. Для импортированных определений нужны исходники.

<a id="R-TYPE-0049"></a>

**R-TYPE-0049** — Generic-параметр `E: errors` обозначает конечный набор checked errors, включая пустой. Он отличается от `E: error`, обозначающего один номинальный тип ошибки. Параметр набора не является типом значения и не может использоваться как value-параметр, результат, локальная переменная, payload или catch-binding. Члены сохраняют номинальную идентичность. Дополнительные capability-ограничения действуют на каждый член; в частности, набор completion-errors async-функции требует `send & unborrowed`.

Throws-список может включать параметры наборов: `throws E, Failure` обозначает их объединение с перечисленными номинальными ошибками. Подстановка раскрывает объединение, канонически упорядочивает его и удаляет пересечения; пустое объединение не имеет checked carrier и exceptional exit. Повтор явно записанного throws-entry остаётся ошибкой. `fn(P...) -> R throws(E)` выводит E из точного набора callable-аргумента, в том числе пустого набора у функции без throws. Callable-ограничения другого generic-параметра также предоставляют сигнатуру. Каждое callable-ограничение может выводить не более одного параметра набора: явно перечисленные фиксированные ошибки проверяются и удаляются перед связыванием оставшегося набора. Противоречивый вывод одного параметра запрещён. Два независимо выведенных набора можно объединить во внешнем throws-списке. Ожидаемый тип результата не участвует в выводе.

Generic-тело проверяется до инстанцирования. Вызов с абстрактными эффектами требует их наличия во внешнем throws-контракте; catch именованной ошибки не доказывает обработку всего абстрактного набора и не сужает объявленный параметр набора. При инстанцировании каждый подходящий именованный catch выбирает обычный путь cleanup, а каждый необработанный член передаётся наружу. Именованные catch, bare rethrow и finally сохраняют обычные правила. Происхождение заимствований консервативно сохраняется через абстрактные вызовы и именованные catch; подстановка набора не продлевает жизнь payload. Ошибки async-start остаются отдельными от выводимого набора completion-errors. Interface schema 15 записывает вид параметра `errors`, структурные callable-эффекты и канонические аргументы наборов. Определения по-прежнему импортируются из исходников.

<a id="R-TYPE-0050"></a>

**R-TYPE-0050** — В trait `const T NAME;` объявляет связанную константу, а `const T NAME = value;` — связанную константу со значением по умолчанию; `T` — целочисленный тип R или `bool`, как у константных параметров (R-TYPE-0047). Реализация связывает константу записью `const T NAME = value;`, повторяя объявленный тип. Она связывает каждую константу без значения по умолчанию ровно один раз и может переопределить константу со значением по умолчанию. Отсутствующее, неизвестное или связывание с другим типом требует `R-DIAG-TRAIT-001`; повтор в одном trait или одной реализации — `R-DIAG-NAME-002`. Значение — constant expression ровно объявленного типа, как константный аргумент по R-TYPE-0047, включая translation-time calls (R-EXPR-0032); значение другого типа требует `R-DIAG-TYPE-001`. Каждая закрытая реализация вычисляет свои константы, даже если их никто не использует; неконстантное значение, panic или зависимость значения от самого себя требуют диагностик R-EXPR-0032.

`Self::NAME` обозначает константу в значениях по умолчанию и телах методов её trait и в реализации. `P::NAME` обозначает константу единственного применения trait к `P`, объявляющего `NAME`: через ограничения generic-параметра, включая supertraits, или через реализации закрытого типа, как в `Point::SIZE` или `Buffer<4usize>::SIZE`. Значение по умолчанию вычисляется для каждой реализации с подстановкой `Self` и аргументов trait. Имя, которое не объявляет ни один такой trait или объявляют несколько, требует `R-DIAG-NAME-001`. Связанная константа — значение, а не объект: у неё нет storage, её нельзя присваивать и заимствовать.

Над generic-параметром связанная константа зависима. Она допустима везде, где R-TYPE-0047 принимает константный параметр: в границах фиксированных массивов вроде `u8[T::SIZE]`, как константный аргумент вроде `Buffer<T::SIZE>`, в зависимых статических условиях (R-META-0003) и в выражениях. Каждый закрытый инстанс подставляет значение выбранной реализации. Закрытая связанная константа — значение своего типа, вычисленное при трансляции; объявление уровня модуля, использующее её до проверки реализаций, получает значение, вычисленное разведочным проходом (R-EXPR-0032).

Interface schema 31 перечисляет константы каждого trait с их типом и признаком значения по умолчанию и записывает зависимую константу как её trait, имя и владельца; fingerprint определения реализации покрывает её значения. Импорт определений по-прежнему требует исходники.

<a id="R-TYPE-0051"></a>

**R-TYPE-0051** — `dyn(Contracts)` обозначает dyn-интерфейс. Его контракты записываются как у opaque-результата (R-TYPE-0048): один или несколько traits либо их приложений, закрытые capabilities вроде `send` или `sync` и равенства связанных типов вроде `Item = i32`. Контракты в другом порядке или с повторами обозначают тот же интерфейс в любом модуле. Контракт без trait, с callable-сигнатурой или зависящий от generic-параметров требует `R-DIAG-TYPE-001`. Каждый связанный тип traits контракта, включая supertraits, фиксируется равенством, а каждый метод этих traits принимает receiver как `const Self*` или `Self*`, не имеет собственных generic-параметров и не упоминает `Self` в сигнатуре иначе как через фиксированные связанные типы; иначе упоминание интерфейса требует `R-DIAG-TRAIT-001`. Связанный тип с параметрами (R-TYPE-0045) не фиксируется, поэтому trait, объявляющий его, не может быть контрактом интерфейса. Связанные константы traits через интерфейс недоступны.

Dyn-интерфейс не является типом значения: он используется только как referent заимствования — `dyn(C)*`, `const dyn(C)*` или их nullable-формы — и как член владельца (R-TYPE-0055). Значение, поле, параметр или результат типа `dyn(C)`, `sizeof` от него, разыменование или доступ к полю через его заимствование, сравнение адресов и упорядочение заимствований интерфейса требуют `R-DIAG-TYPE-001`; nullable-заимствование интерфейса можно сравнивать с `null`. Интерфейс доказывает capabilities, которые перечисляет его контракт, и те, которые его трейты, включая их супертрейты, требуют от `Self` (R-TYPE-0041); контракт, называющий такую capability, и контракт, опускающий её, обозначают один интерфейс. Заимствование интерфейса Send или Sync соответственно (R-TYPE-0029).

Там, где ожидается заимствование интерфейса, заимствование типа, доказывающего каждый trait, capability и равенство связанных типов контракта, неявно преобразуется в него с сохранением квалификации либо с ослаблением исключительного заимствования до разделяемого интерфейса; `dyn(C)*` ослабляется до `const dyn(C)*`. Тип, не доказывающий контракт, требует `R-DIAG-TRAIT-001`; интерфейс преобразуется в другой только сужением R-TYPE-0055, а любое другое преобразование между интерфейсами требует `R-DIAG-TYPE-001`. Заимствование интерфейса сохраняет origin, исключительность и время жизни исходного заимствования, а преобразование ничего не аллоцирует.

Члены интерфейса — закрытые типы, которые программа к нему преобразует, в том числе в каждой инстанциации generic-функции; набор конечен и известен при сборке программы. Вызов метода через заимствование интерфейса вызывает реализацию члена, из которого оно было преобразовано, включая методы по умолчанию и унаследованные. Его сигнатура, checked errors, borrow-контракт, must-use и ресурсные атрибуты — атрибуты прототипа trait, которым удовлетворяет каждая реализация (R-TYPE-0042). Статический граф вызовов (R-FUNC-0004) и ресурсные доказательства R-FUNC-0019 следуют от вызова к реализации каждого члена: рекурсивная цепочка через интерфейс требует `R-DIAG-STACK-001`, а аллоцирующая реализация члена нарушает `@noalloc` вызывающей функции. `@scoped async`-метод запускается через интерфейс внутри task group так же, как его реализации (R-STMT-0017).

Заимствование интерфейса представлено заимствованным адресом и тегом, выбирающим член; члены упорядочены по каноническим именам, а вызов — выбор по тегу с прямым вызовом каждой реализации. Членом интерфейса над `core::Format` может быть тип со стандартным форматированием (R-TYPE-0046); его выбор форматирует значение этим форматированием. Загрузка новых реализаций во время работы программы, восстановление типа члена и сам интерфейс как generic-аргумент в эту редакцию не входят; владельцы интерфейсов следуют R-TYPE-0055. Interface schema 31 записывает тип как `(dyn "dyn(...)")` с каноническим контрактом интерфейса.

<a id="R-TYPE-0052"></a>

**R-TYPE-0052** — `(T1, T2, ..., Tn)` из двух и более элементов — тип кортежа. Тип каждого элемента shall быть complete, sized, inhabited value type без outermost object `const`; иначе требуется `R-DIAG-TYPE-001`. Кортежи с одинаковыми типами элементов в одном порядке — один тип во всех модулях, а `(T)` остаётся типом `T` в скобках. Кортеж имеет layout, классификацию Copy и Move, порядок очистки и свойства Send, Sync, `pod` и `unborrowed` структуры, поля которой с именами от `0` до `n - 1` — его элементы по порядку; у него нет hooks, он не `key`, не имеет JSON-формы и не является C ABI type.

`(e1, e2, ..., en)` из двух и более элементов строит кортеж: элементы вычисляются слева направо и каждый перемещается или копируется в свой элемент. Ожидаемый тип кортежа с тем же числом элементов задаёт каждому элементу ожидаемый тип; иначе тип кортежа образуется из значений элементов. `value.N` выбирает элемент `N` как поле (R-AGG-0002), а шаблон match разбирает кортеж полями `.0`, `.1`, ... (R-EXPR-0031). Элементы держат views как поля; последующее использование `value.N` сохраняет живыми views этого элемента. `core::type_name` записывает кортеж как `(A, B)`, а interface schema 31 — как `(tuple A B)`.

<a id="R-TYPE-0053"></a>

**R-TYPE-0053** — В заголовке generic-функции `T...` объявляет пакет: параметр, обозначающий упорядоченный список из нуля или более типов, аргумент которого — кортеж этих типов (R-TYPE-0052), возможно без элементов. Функция объявляет не более одного пакета, последним параметром заголовка; пакет в заголовке другого объявления, второй пакет и пакет перед другим параметром требуют `R-DIAG-TYPE-001`. Ограничения, записанные после `T...`, относятся к каждому элементу. Как тип пакет доказывает `copy`, `pod`, `send`, `sync` и `unborrowed` ровно тогда, когда их доказывает каждый элемент, и не доказывает других свойств и трейтов, поэтому операции элементов требуют значения элемента (R-TYPE-0035).

Пакет используется только раскрытым: `T... name` последним value-параметром объявляет один параметр типа `(T...)`; `(T...)` — кортеж пакета, а `(A, B, T...)` — кортеж из `A`, `B` и элементов; `fn(X, T...) -> R` в callable constraint принимает элементы последними параметрами (R-TYPE-0044); `len(T...)` — число элементов, константа `usize` каждой инстанциации. Любое другое использование имени пакета требует `R-DIAG-TYPE-001`.

Вызов функции с пакетом передаёт сначала фиксированные параметры; остальные аргументы, ноль или больше, вычисляются слева направо и инициализируют по порядку кортеж, передаваемый для пакета, а вывод связывает пакет с кортежем их типов. В явном списке аргументы после аргументов остальных параметров — элементы пакета (R-TYPE-0036).

Spread `...operand` последним аргументом вызова или последним элементом выражения кортежа обозначает элементы операнда-кортежа или пакета. Аргументы перед ним сначала вычисляются в скрытые объекты; операнд вычисляется один раз в скрытый объект, элементы которого по порядку перемещаются или копируются в скрытые части, как шаблон разбивает значение (R-EXPR-0031), и части передаются по порядку. Spread может задавать фиксированные параметры, пакет или и то и другое; пакет callable-сигнатуры `fn(..., P...)` задаётся spread значения типа `P`. Spread не последним аргументом или элементом требует `R-DIAG-SYN-001`; spread, операнд которого не кортеж и не пакет, или дающий иное число аргументов, чем принимают параметры, требует `R-DIAG-TYPE-001`. Async-старт резервирует части как именованные Move-операнды (R-FUNC-0010), поэтому отклонённый старт уничтожает каждую часть один раз.

В определении generic длина пакета неизвестна: его элементы достижимы только через spread в параметры, а `value.N` для пакета требует `R-DIAG-TYPE-001`. Spread пакета может задавать `k` фиксированных параметров только там, где охватывающий `@if` доказывает, что в пакете не меньше `k` элементов, и ровно `k`, когда у вызываемой функции нет пакета (R-META-0003); тогда фиксированные параметры получают первые элементы, доказывающие ограничения элементов пакета, а пакет вызываемой функции — остальные. Рекурсия по пакету, как `show(...move tail)` под `@if (len(T...) != 0usize)`, инстанцирует одну функцию на каждую длину и завершается там, где ветвь не выбрана.

Инстанциация заменяет каждый пакет его кортежем, поэтому у инстанса только обычные параметры и прямые вызовы. Interface schema 31 записывает `pack=true` с ограничениями каждого элемента, пакет из префикса и элементов другого пакета — как `(pack prefix=(...) base=... from=k)`, а раскрытие в callable-сигнатуре — как `(expand P)`.

<a id="R-TYPE-0054"></a>

**R-TYPE-0054** — `fn [@noalloc] [@nonblocking] (P1, ..., Pn) -> R [throws(E1, ...)]` — тип функции, а `async fn (P...) -> R [throws(E...)]` — async-тип функции. Их параметры, результат и checked errors — как у callable-сигнатуры (R-TYPE-0044) без режима вызова и без раскрытия пакета; тип функции с режимом, с `P...`, с результатом `never` или с параметром набора ошибок требует `R-DIAG-TYPE-001`, а async-тип функции, параметры, результат или ошибки которого не Send и не unborrowed, требует `R-DIAG-ASYNC-001`. Типы функций с одинаковыми параметрами, результатом, checked errors, формой и гарантиями — один тип в любом модуле. Значение-функция — Copy, Send и Sync, никогда не null и не имеет значения по умолчанию, поэтому отсутствие выражает `o<fn(...) -> R>`. Оно может быть локальным объектом, полем, элементом массива или контейнера, параметром, результатом, объектом модуля и generic-аргументом. Тип функции в начале инструкции пишется без скобок; функция, результат которой — async-тип функции, пишет результат в скобках, например `(async fn(i32) -> i32) handler()`, потому что `async` перед объявлением функции делает функцию асинхронной.

Там, где ожидается тип функции, function item (R-FUNC-0007), в том числе закрытая generic-функция и метод, преобразуется к нему, когда его параметры, результат, checked errors и синхронная или async-форма в точности совпадают с типом, а функция доказывает каждую ресурсную гарантию, указанную в типе. В теле generic-функции generic-функция, названная с аргументами типов, которые являются параметрами этого тела, например `handler::<S>`, закрыта в каждом instantiation и преобразуется там. Значение типа функции преобразуется к типу, который отличается только меньшим набором гарантий; гарантия никогда не добавляется. Любое другое преобразование, в том числе из лямбды, открытого generic-имени, unsafe- или C-функции либо raw-указателя на функцию, требует `R-DIAG-TYPE-001`.

`f(args)`, `(expression)(args)`, `object.field(args)` и `sequence[index](args)` вызывают функцию, которую обозначает значение-функция: аргументы вычисляются после значения, а вызов имеет параметры, результат, checked errors, borrow-контракт, cleanup и транзакцию async-start этой функции; его результат заимствует только из аргументов и никогда из самого значения. Вызов async-значения-функции — async-запуск, как прямой вызов (R-FUNC-0010). Значение-функция удовлетворяет callable-ограничению (R-TYPE-0044) своей сигнатуры в любом режиме, если ограничение не требует гарантии, которой нет у типа. `==` и `!=` сравнивают два значения одного типа функции по обозначаемой функции; любой другой оператор требует `R-DIAG-TYPE-001`. Значение-функция, полученное из function item, — константа, поэтому оно может инициализировать объект модуля (R-OBJ-0010).

Цели типа функции — функции, которые программа преобразует к нему и к каждому типу функции, значения которого преобразуются к нему, в том числе в каждом инстанцировании generic-функции; это множество конечно и известно при сборке программы. Статический граф вызовов (R-FUNC-0004) ведёт от вызова через значение-функцию к каждой цели, поэтому рекурсивная цепочка через значение-функцию требует `R-DIAG-STACK-001` у преобразования, замыкающего цикл. Вызов через значение-функцию доказывает `@noalloc` или `@nonblocking` (R-FUNC-0019) ровно тогда, когда тип указывает эту гарантию, поскольку каждое преобразование к типу её доказало.

Значение-функция представлено номером обозначаемой функции; вызов — выбор по целям его типа с прямым вызовом каждой либо async-запуск выбранной функции. Interface schema 31 записывает тип функции как `(fn parameters=(...) return=R throws=(...))`, добавляя `async=true`, `noalloc=true` и `nonblocking=true` для имеющихся формы и гарантий. Значения-функции, захватывающие состояние, упорядочение значений-функций и типы функций в C-объявлениях не входят в эту редакцию.

<a id="R-TYPE-0055"></a>

**R-TYPE-0055** — `own dyn(C)*`, `arc dyn(C)` и `rc dyn(C)` — владельцы dyn-интерфейса (R-TYPE-0051), а `weak arc dyn(C)` и `weak rc dyn(C)` — их слабые формы. Каждый владеет одним членом интерфейса, а слабая форма наблюдает его, и подчиняется правилам того же владельца типа: `own dyn(C)*` — Move-only и имеет ровно одного владельца (R-OWN-0001), а сильные и слабые handles — Move-only, клонируются, понижаются, повышаются и считаются как в R-OWN-0011 — R-OWN-0017. Уничтожение `own dyn(C)*` или последнего сильного владельца `arc dyn(C)` либо `rc dyn(C)` один раз уничтожает член так, как уничтожается его тип, и освобождает его память. Владелец интерфейса никогда не равен null, поэтому его отсутствие выражает `o<own dyn(C)*>`, а nullable-форма требует `R-DIAG-TYPE-001`. Он может быть локальным объектом, полем, элементом массива или контейнера, полезной нагрузкой `o<...>`, параметром, результатом и generic-аргументом; он не является `key`-типом. Поскольку интерфейс доказывает capabilities своего контракта (R-TYPE-0051), по R-MEM-0003 `own dyn(C)*` — Send, если интерфейс доказывает `send`, `arc dyn(C)` — Send и Sync, если он доказывает и `send`, и `sync`, а `rc dyn(C)` — ни то ни другое.

Там, где ожидается владелец интерфейса, тот же владелец закрытого типа преобразуется в него: `own T*` в `own dyn(C)*`, `arc T` в `arc dyn(C)`, `rc T` в `rc dyn(C)`, а слабый handle — в соответствующую слабую форму. Тип shall доказывать каждый trait, capability и равенство связанных типов контракта, иначе требуется `R-DIAG-TRAIT-001`, и быть `unborrowed` (R-TYPE-0033), иначе требуется `R-DIAG-TYPE-001`; владелец другого вида или заимствование требует `R-DIAG-TYPE-001`. Преобразование перемещает владельца, сохраняет его счётчики и ничего не аллоцирует, а тип становится членом интерфейса.

Заимствование или владелец `dyn(C1)` преобразуется в ту же форму `dyn(C2)`, если `C1` доказывает каждый trait `C2`, непосредственно или через supertrait, а также каждую capability и каждое равенство связанных типов `C2`. Такое сужение сохраняет член, выбирает его тегом среди членов более узкого интерфейса и ничего не аллоцирует; заимствование сохраняет origin, исключительность и время жизни. Любое другое преобразование между интерфейсами требует `R-DIAG-TYPE-001`.

`owner->method(args)` вызывает метод члена так же, как через заимствование интерфейса (R-TYPE-0051). `own dyn(C)*` достигает receivers `const Self*` и, при исключительном доступе к владельцу, `Self*`; сильный разделяемый владелец достигает только receivers `const Self*` (R-OWN-0010), а другой receiver требует `R-DIAG-BORROW-001`; вызов через слабого владельца требует `R-DIAG-TYPE-001`, поскольку слабого владельца сначала повышают. Там, где ожидается заимствование интерфейса, `&owner` одалживает член: `own dyn(C)*` — как `dyn(C)*` или `const dyn(C)*`, сильный разделяемый владелец — только как `const dyn(C)*` (`R-DIAG-BORROW-001`), и заимствование ограничено заимствованием владельца. Разыменование владельца интерфейса, а также `try_unwrap`, `into_raw` и `get_mut` от него требуют `R-DIAG-TYPE-001`, так как программа не называет тип его члена. `clone`, `clone_weak`, `downgrade`, `upgrade`, `strong_count`, `weak_count` и `ptr_eq` применяются как к другим разделяемым владельцам, а `==` и `!=` сравнивают владельцев `own dyn(C)*` по идентичности (R-EXPR-0011).

Владелец интерфейса представлен владельцем своего члена и тегом члена; его уничтожение выбирает уничтожение члена по тегу, а сужение перенумеровывает тег по таблице двух интерфейсов. Interface schema 31 записывает его как владельца типа интерфейса, например `(own (dyn "dyn(...)"))` или `(weak_rc (dyn "dyn(...)"))`. Исключительный доступ к члену разделяемого владельца, восстановление типа члена, сам интерфейс как generic-аргумент и владельцы интерфейсов в C-объявлениях в эту редакцию не входят.

<a id="objects-storage"></a>

## 10. Objects, storage and alignment

<a id="R-OBJ-0001"></a>

**R-OBJ-0001** — Storage duration бывает automatic, static, thread и allocated. Block object automatic, module object static, `thread_local` object thread, а object, созданный `new`, allocated.

<a id="R-OBJ-0002"></a>

**R-OBJ-0002** — Каждый complete object имеет positive size и alignment. `sizeof(T)` и `alignof(T)` имеют type `usize`, являются constant expressions и shall быть не меньше 1 для inhabited complete T.

<a id="R-OBJ-0003"></a>

**R-OBJ-0003** — Elements array расположены contiguously без inter-element padding. Struct fields имеют declaration order и различные non-overlapping storage regions; реализация may вставлять padding. Safe code shall not наблюдать padding bytes.

<a id="R-OBJ-0004"></a>

**R-OBJ-0004** — Default R layout aggregate является implementation-defined для каждого target и не является ABI. `@repr(C)` заменяет его C-compatible layout по разделу 23; смешивание declarations разных repr требует диагностики.

<a id="R-OBJ-0005"></a>

**R-OBJ-0005** — Address comparison `==`/`!=` для borrows определяет, обозначают ли они один и тот же subobject. Relational address comparison отсутствует в safe R.

<a id="R-OBJ-0006"></a>

**R-OBJ-0006** — В profile с allocation expression-initialized allocation пишется `new T(expression)`, а aggregate-initialized allocation — непосредственно `new T { initializer-items }`. Для ordinary complete sized T обе forms shall выделить suitably aligned storage, полностью initialize T и вернуть `own T*`. Префикс `arc` либо `rc` перед T, например `new arc T { initializer-items }`, вместо этого возвращает corresponding strong handle и выделяет implementation-private control block вместе с T либо отдельно. `new weak arc` и `new weak rc` ill-formed. Allocation failure вызывает panic `allocation_failure`. При unwind strategy, если initialization panics, initialized subobjects shall быть dropped и весь acquired storage shall быть released; abort strategy вместо этого следует R-ERR-0005 и не обещает дальнейшую cleanup. Freestanding profile без allocation диагностирует любую из этих forms.

<a id="R-OBJ-0007"></a>

**R-OBJ-0007** — `own T*?` может быть `null`; non-null owning pointer owns ровно один T. Литерал `null` соответствует параметру `null_t`. В контексте borrow, own, raw object или raw function pointer он разрешён только при явном `?` и создаёт соответствующее null pointer value. Объект nullable-указателя, даже со значением null, не соответствует параметру `null_t`. `o` представляет отсутствие только вариантом `o::none`. Уничтожение null owning pointer — no-op; non-null форма следует R-INIT-0010 ровно один раз.

<a id="R-OBJ-0008"></a>

**R-OBJ-0008** — Каждый R `thread_local` instance constant-initialized до его первого access в соответствующем R thread. Его first access registers этот instance как Live на вершине drop stack данного thread. Normal thread exit многократно переводит верхний Live instance в Destroying, drops его и переводит в Destroyed; поэтому instances dropped в reverse order first access. First access к другому never-accessed instance во время этого teardown registers его на вершине, и такой instance dropped следующим после завершения текущего destruction. Access к instance уже в Destroying либо Destroyed вызывает panic `thread_local_lifetime` до exposing его storage; во время active unwind это second panic по R-ERR-0008. При unwind strategy тот же stack algorithm выполняет thread-root cleanup. Если first panic начинается в thread-local drop после normal return entry, cleanup сначала завершает remaining subobject/resource duties failing instance по R-ERR-0008, затем ровно один раз destroys любой staged return value, затем drops remaining Live instances по этому stack algorithm и только после этого forms и commits panic outcome. Panic в любой из этих subsequent duties является second panic и aborts. Abrupt thread/process termination и external C thread, не завершённый через R runtime, не обязаны выполнять такие drops.

<a id="R-OBJ-0009"></a>

**R-OBJ-0009** — Safe access к mutable object со static storage допускается только через atomic operation или safe synchronized abstraction; прямое чтение, запись или borrow требует `unsafe`. Direct access к immutable static object из possibly concurrent context требует, чтобы его type был Sync. Access к текущему R `thread_local` instance safe, поскольку он не shared, если type access сам не требует unsafe. Поэтому static `rc` или `weak rc` handle недоступен safe spawned- thread code.

<a id="R-OBJ-0010"></a>

**R-OBJ-0010** — Module object и block `static` object constant-initialized во время initialization owning module в textual source order, даже если control никогда не войдёт в содержащий block. При normal module destruction они dropped в обратном порядке. Их initialization не является lazy и не может panic.

<a id="R-OBJ-0011"></a>

**R-OBJ-0011** — Reference-counted allocation содержит storage ровно для одного T, один strong count и один weak-liveness count. Construction initializes T и оба count значением one до появления любого handle; сама по себе она не выполняет inter-thread publication. T initialized, пока strong count nonzero. Irrevocable transition one-to-zero reserves sole destruction либо `try_unwrap` move-out duty; T остаётся initialized на время этой operation по R-AM-0005, после чего может оставаться только его uninitialized storage. Weak-liveness count учитывает все explicit weak handles и одну implicit control-block duty, пока T alive или pending его final cleanup. Counter width не меньше `usize`; ни один counter не может wrap.

<a id="R-OBJ-0012"></a>

**R-OBJ-0012** — Module declaration может нести attribute `@deny_panic_alloc`, записанный перед `module` и не принимающий argument. В translation unit, module declaration которого несёт его, а также в каждом translation unit при выбранном translation option deny-panic-allocation implementation, каждое expression `new`, `new arc` и `new rc` и каждый call standard library operation, для которой Library inventory объявляет panic при allocation failure, требует `R-DIAG-ALLOC-001`. Library R-LIB-0007 оставляет `new` единственной такой allocation в R 0.1, поэтому policy не диагностирует ни один library call, пока inventory не объявит его. Policy не меняет ни profile, ни type system, ни generated code, ни meaning accepted program; recoverable allocation через `std.alloc::try_new` и checked operations `throws std.alloc::alloc_error` остаётся доступной. Attribute на любой другой declaration требует `R-DIAG-SYN-002`.

<a id="initialization"></a>

## 11. Initialization and destruction

### 11.1 Initialization

<a id="R-INIT-0001"></a>

**R-INIT-0001** — Каждая object declaration shall выполнить exactly one complete initialization до входа name в scope. Safe uninitialized declaration отсутствует.

<a id="R-INIT-0002"></a>

**R-INIT-0002** — Scalar initializer вычисляется и value-converts к declared destination value type. Если declared object type имеет outermost object `const`, destination — corresponding unqualified T; successful initialization затем establishes object `const T`. Nested access kinds, включая `const` shared borrow/slice, не снимаются. Narrowing, sign-changing и float-to-integer conversions требуют explicit `as`. Когда unsuffixed integer literal либо непосредственный unary `-`, применённый к нему, является complete source expression для statically known R integer destination, он contextually получает этот destination type, если mathematical value представимо. Это применяется к initialization object или subobject, включая aggregate, array, enum/outcome payload и atomic initialization; к right operand simple assignment; by-value argument и ordinary return operand. Правило не распространяется внутрь larger expression и не выбирает между несколькими candidate destinations по R-STMT-0011. Unrepresentable literal требует `R-DIAG-CONST-001`; compiler shall not сначала присваивать ему default type из R-LEX-0010, а затем выполнять narrowing conversion.

<a id="R-INIT-0003"></a>

**R-INIT-0003** — Array initializer `{ e1, ... }` сопоставляет elements слева направо. Число elements shall not превышать length. Missing elements получают default value только если element type default-initializable. Каждый explicit element имеет array element type как initialization context, поэтому, например, `u8[3] bytes = { 0xcb, 0x48, 0xcd };` использует R-TYPE-0011/R-INIT-0002 и не требует integer suffixes.

<a id="R-INIT-0004"></a>

**R-INIT-0004** — Struct initializer использует `{ .field = expression, ... }`. Field may появиться не более одного раза; evaluation идёт в source order, а пропущенное поле требует объявленного инициализатора или типа со значением по умолчанию (R-INIT-0005). Указание неизвестного field или пропуск иного поля требует диагностики. Все explicit expressions сначала вычисляются в fully initialized temporaries в source order; затем в порядке объявления вычисляются значения пропущенных полей, и fields initialize в declaration order из соответствующего temporary либо значения.

Последним элементом struct initializer может быть обновление структуры `...base`, как `Point { .x = 0, ...origin }`. `base` — выражение типа структуры, вычисляемое один раз после явных выражений полей, и каждое пропущенное поле берёт соответствующее поле `base` вместо объявленного инициализатора. Copy-поле копируется, Move-поле перемещается; после инициализации нового значения поля `base`, заменённые явными значениями, уничтожаются, а остаток `base` потребляется без уничтожения целого. Именованная Move-база требует `move` (R-OWN-0003), а Copy-место базы остаётся пригодным. База другого типа требует `R-DIAG-TYPE-001`; база типа с пользовательским drop и `...base` в инициализаторе, не являющемся struct initializer, требуют `R-DIAG-INIT-002`.

Объявление поля может заканчиваться `= initializer`, как `u32 retries = 3;`. Инициализатор — значение типа поля в модуле его агрегата: он называет элементы модуля так же, как тело функции этого модуля, и не называет локальных объектов кода, который инициализирует агрегат. Он проверяется один раз при объявлении; для generic-агрегата, тип поля которого зависит от параметра, — в каждом инстансе, инициализация которого его использует. Он вычисляется заново при каждой инициализации, пропускающей поле, включая `Type {}` и прочие значения по умолчанию R-INIT-0005; его checked errors — ошибки этой инициализации, распространяющиеся или перехватываемые в ней (R-ERR-0001), а диагностика такого использования, например необъявленная checked error, сообщается у инициализации. Отказ уничтожает temporaries и поля, инициализированные до него, так же, как R-INIT-0006 уничтожает их при panic, в обратном порядке. Инициализатор не делает поле необязательным во входном JSON, если этого не говорит `@json(optional)` (R-JSON-0001). Инициализатор, тип которого не совпадает с типом поля, требует `R-DIAG-TYPE-001`.

<a id="R-INIT-0005"></a>

**R-INIT-0005** — Default-initializable: numeric zero, `false`, U+0000, nullable pointer `null`, `o::none`, array/struct только при рекурсивной default-initializability, а struct — только если ни одно поле не объявляет инициализатор. Non-null pointer, user-declared enum без варианта `@default` и owning resource не default-initializable. Canonical type `array<u8>`/`bytes` является единственным owning-resource exception: его empty initializer и resulting state закреплены R-TYPE-0030.

Тип имеет значение по умолчанию, если он default-initializable, если это struct, каждое поле которого объявляет инициализатор или имеет тип со значением по умолчанию, если это enum с вариантом `@default`, полезная нагрузка которого, если она есть, имеет значение по умолчанию, или если это fixed array такого типа элементов. Значение по умолчанию такого struct — значение `Type {}`: инициализаторы и значения по умолчанию его полей, вычисленные в порядке объявления (R-INIT-0004); такого enum — его вариант `@default` со значением по умолчанию полезной нагрузки; такого массива — значение по умолчанию каждого элемента в порядке индексов. Его используют пропущенные поле или элемент массива, `core::take` (R-OWN-0019) и необязательное поле JSON (R-JSON-0001); его вычисление может бросать checked errors вычисляемых им инициализаторов. Атрибут `@default` стоит перед одним вариантом enum или error enum; второй `@default` в одном enum требует `R-DIAG-NAME-002`, а иной атрибут у варианта — `R-DIAG-SYN-002`. Interface schema 31 записывает `initializer=true` у поля с инициализатором и `default=true` у варианта `@default`.

<a id="R-INIT-0006"></a>

**R-INIT-0006** — При unwind strategy, если aggregate initialization panics после инициализации части subobjects, уже initialized subobjects shall быть dropped в обратном порядке; containing object никогда не начинает lifetime. Если panic возникает при evaluation explicit struct field expression, ранее созданные expression temporaries drops в reverse source order и ни один field ещё не initialized. Если panic возникает в declaration-order field initialization, initialized fields drops в reverse declaration order, затем ещё не consumed temporaries — в reverse source order. При abort strategy R-ERR-0005 terminates без дополнительных R drops.

### 11.2 Assignment and destruction

<a id="R-INIT-0007"></a>

**R-INIT-0007** — Simple assignment `=` сначала вычисляет destination place ровно один раз (base, indices и required checks слева направо), не читая и не изменяя stored value, и создаёт internal non-owning destination capability с storage slot, provenance, subobject path и, для initialized destination, его current object identity; затем полностью вычисляет RHS. Capability не является ordinary competing borrow: owner или exclusive borrow, использованный ровно для destination designator, may быть использован им в commit. До commit storage slot и все capabilities, от которых зависит доступ, shall оставаться valid и не быть moved, dropped, reassigned, reused или deallocated; initialized destination identity shall оставаться live. Overlapping write, move или drop destination во время RHS запрещены. Обычное чтение initialized destination и shared reborrow для вычисления RHS разрешены, если они заканчиваются до commit. В commit point не должно оставаться иного usable overlapping borrow; иначе требуется diagnostic. После этого прежнее initialized destination value drops, если оно было initialized, и новое value copy/move-initializes; moved destination тем самым reinitialized без drop. Panic/неудача RHS не меняет initialized/moved state или stored destination value, но уже произошедшие side effects destination/RHS evaluation сохраняются.

<a id="R-INIT-0008"></a>

**R-INIT-0008** — Objects shall уничтожаться в обратном порядке завершения их успешной initialization в каждом scope. `return`, `throw`, `break`, `continue`, checked-error propagation и unwinding shall drop все exited automatic scopes. Transfer, пересекающий `finally`, сначала stages payload, drops exited try/catch scope, выполняет этот finally и только затем продолжает через outer scopes по R-ERR-0002.

<a id="R-INIT-0009"></a>

**R-INIT-0009** — Special declaration `drop(T* self) { ... }` may быть определена ровно один раз в module, defining complete struct или payload enum T. Imported, opaque и fieldless enum type shall not иметь user drop. Parameter shall быть named `self` and have exact non-null, non-const exclusive borrow type `T*`; его region выводится из call. Body implicitly returns `void`. It shall not move self, сохранять derived borrow за lifetime call или execute destruction более одного раза.

<a id="R-INIT-0010"></a>

**R-INIT-0010** — Destruction fully initialized array destroys каждый element ровно один раз в reverse index order. Если element destruction даёт first panic при unwind strategy, destruction продолжается для remaining lower-index elements; second panic aborts по R-ERR-0008. При abort strategy first panic не выполняет дальнейших drops по R-ERR-0005. Destruction struct выполняет user drop body, затем fields в обратном declaration order. Destruction tagged enum после user body destroys payload active variant. Unique owning pointer drops pointee, затем освобождает allocation. Destruction strong `arc` или `rc` handle освобождает одну strong reference и следует R-OWN-0011 для last reference; destruction weak handle освобождает только его weak control-block reference. Destruction `o` либо любой closed standard outcome schema, named R-STMT-0006, destroys payload active alternative ровно один раз через его ordinary recursive drop glue; fieldless active alternative не destroys payload.

<a id="R-INIT-0011"></a>

**R-INIT-0011** — Statement `drop expression;` may досрочно destroy named initialized Move value; place становится moved и shall быть reinitialized до повторного использования. Explicit drop любого Copy value запрещён как бессмысленный.

<a id="R-INIT-0012"></a>

**R-INIT-0012** — Initialization `atomic T`, включая любой spelling `ai8`, `ai16`, `ai32`, `ai64`, `aisize`, `au8`, `au16`, `au32`, `au64` или `ausize`, принимает ровно одно non-atomic T value без implicit narrowing и устанавливает initial modification-order value до publication. Complete unsuffixed integer literal либо его непосредственная unary-minus form получает contextual base type T по R-INIT-0002, поэтому `au32 flags = 0;` является valid. Это initialization-only conversion, а не ordinary conversion T/atomic T. После этого atomic object shall быть доступен только через `core` atomic operations; ordinary assignment, increment и compound assignment запрещены. Move-initialization atomic value является отдельным исключением R-INIT-0013.

<a id="R-INIT-0013"></a>

**R-INIT-0013** — `move` of `atomic T` or aggregate containing it permitted only когда в move linearization point compiler proves exclusive access, отсутствие active borrow и current concurrent reachability source. Prior publication сама по себе не disqualifies после того, как standardized acquire operation exclusively claimed item, completion outcome либо allocation и устранила каждый concurrent access path. Это допускает exact acquire-and-move paths receive, explicit/implicit join и successful `try_unwrap`; move при сохраняющемся concurrent path не допускается. Каждый atomic component snapshots through a representation-safe atomic load with `relaxed` ordering; backend shall not replace it with a non-atomic access. Relaxed snapshot creates no synchronizes-with edge. Destination component is initialized with that value as its first modification and therefore has a fresh modification order. Для containing aggregate components transfer recursively in declaration/element order, после чего source whole-object lifetime ends. Если proof отсутствует, требуется `R-DIAG-MOVE-003`; ordinary atomic load/store shall использовать `core` operation and ordering.

<a id="R-INIT-0014"></a>
**R-INIT-0014** — Простое присваивание в исходном коде не должно безусловно заменять
инициализированную автоматическую локальную переменную целиком или параметр-значение.
Требуется `R-DIAG-USE-002`, даже если прежнее значение прочитано, передано или явно
отброшено; новое значение в последовательном коде должно получить новое локальное имя.

Ветка runtime-условия, ветка switch, тело/шаг цикла или обработчик catch разрешают
замену переменной, объявленной вне этой управляющей области. Переменная, объявленная
внутри области, не получает разрешения от её внешнего условия. Обычный вложенный блок,
unsafe-блок, тело try, тело finally, compile-time выбор и константное условие `if`
такого разрешения не дают. Составное присваивание, инкремент и декремент остаются
явными обновлениями по R-EXPR-0013. Правило не запрещает запись в поля, элементы или
через указатели, полную повторную инициализацию moved-хранилища, инициализацию и публикацию
выходных параметров по R-FUNC-0022. Второе присваивание в исходном коде уже
инициализированному частному выходу подчиняется этому же правилу. Оно не заменяет const
объекта и проверку исключительного доступа.


<a id="ownership-moves"></a>

## 12. Ownership and moves

<a id="R-OWN-0001"></a>

**R-OWN-0001** — В каждый момент non-null `own T*` shall иметь ровно одного owner. Owner ответственен за drop pointee и deallocation, если ownership не перемещён.

<a id="R-OWN-0002"></a>

**R-OWN-0002** — `move place` требует initialized именованный whole-object place. Для Copy-типа операция выполняет обычное чтение значения с текущими проверками доступа и снятием внешнего object `const`; источник остаётся initialized и доступным. Для Move-типа place должен быть mutable без active borrow. Операция создаёт value того же типа и переводит source object в `moved` без drop; source lifetime заканчивается после successful value transfer по R-AM-0005.

<a id="R-OWN-0003"></a>

**R-OWN-0003** — Named Move value в by-value initializer, assignment, argument, return или variant payload shall быть написан с `move`. Temporary Move value may передаваться без keyword, поскольку у него нет повторно используемого name.

<a id="R-OWN-0004"></a>

**R-OWN-0004** — Использование moved place, второй move, move borrowed place и покидание scope с partially initialized object требуют диагностики.

<a id="R-OWN-0005"></a>

**R-OWN-0005** — R 0.1 разрешает move только whole object. Для извлечения field shall использоваться method-like free function, принимающая whole owner и возвращающая требуемые values. Единственное payload-pattern exception — извлечение active payload по R-STMT-0010. Когда selected alternative имеет payload, соответствующая extraction сначала consumes whole payload-bearing value и transfers этот payload ровно один раз; fieldless alternative не transfers payload.

<a id="R-OWN-0006"></a>

**R-OWN-0006** — Reinitialization moved local полной assignment начинает новый lifetime с новой identity. Reinitialization destroyed static/thread object запрещена.

<a id="R-OWN-0007"></a>

**R-OWN-0007** — Conversion `own T*` в borrow не передаёт ownership. Conversion этого borrow через `borrow as raw T*` либо `borrow as raw const T*` является exact non-owning raw-exposure form R-EXPR-0019 и R-SAFETY-BORROW-RAW; исходный owner shall оставаться alive и неподвижным в течение всех accesses через raw pointer. `core::release(move owner)` по R-UNSAFE-0008 является отдельной consuming operation, которая передаёт allocation и deallocation duty возвращённому raw pointer.

<a id="R-OWN-0008"></a>

**R-OWN-0008** — Unique `own` links остаются acyclic by construction в safe code. Strong `arc` или `rc` links могут образовать cycle; cycle удерживает каждый соответствующий strong count выше zero и поэтому сохраняет values/control blocks. Такая leak является defined behavior, не UB. Back edge, который не должен удерживать value alive, shall использовать соответствующую weak form.

<a id="R-OWN-0009"></a>

**R-OWN-0009** — Dereference non-null unique owning pointer и `->` дают exclusive access к его pointee без consumption owner только при usable exclusive access к самому owner value. Shared/const access к owner attenuates это право до shared pointee access и не разрешает mutation или move pointee. Nullable owner требует non-null fact по R-BORROW-0005 и R-BORROW-0015. Borrow, созданный из owner либо его subobject, shall not переживать pointee; owner shall not быть moved, dropped или reassigned, пока такой borrow usable. Обычное чтение через owner не перемещает pointee.

<a id="R-OWN-0010"></a>

**R-OWN-0010** — Dereference или `->` через `arc T` или `rc T` создаёт только shared access к T, никогда exclusive. Pointee shall not быть moved out. Ordinary assignment через такой access ill-formed даже при observed strong count one. Mutation shared state shall использовать exclusive borrow из uniqueness API либо standardized interior-mutable synchronization type. Каждый borrow T либо subobject, derived через strong shared owner, bounded borrow exact owner handle, использованного для derivation. Этот handle shall not быть moved, dropped либо reassigned, пока derived borrow usable; создание или retention другого clone не продлевает borrow и не подменяет identity handle.

<a id="R-OWN-0011"></a>

**R-OWN-0011** — `std.arc::clone(&owner)` и `std.rc::clone(&owner)` — единственные safe operations, создающие дополнительный strong handle из существующего strong handle; R-OWN-0012 отдельно определяет создание из weak handle. Clone increments matching strong count и оставляет `owner` usable. Drop последнего strong handle выполняет irrevocable transition one-to-zero до того, как ровно один раз drops T, затем releases implicit weak-liveness duty. Control block deallocated только после release последней explicit weak reference и этой implicit duty. Если drop T начинает first unwind, cleanup всё равно releases implicit duty один раз до propagation. Second panic во время active unwind следует immediate-abort rule R-ERR-0008; после начала этого abort дальнейший cleanup не обещается.

<a id="R-OWN-0012"></a>

**R-OWN-0012** — `std.arc::downgrade(&owner)` и `std.rc::downgrade(&owner)` создают matching weak handle. `std.arc::upgrade(&weak)` atomically, а `std.rc::upgrade(&weak)` non-atomically возвращает `o::some` с новым strong handle ровно тогда, когда strong count nonzero; иначе возвращается `o::none`. Last-release или successful-unwrap transition to zero linearizes до начала T destruction либо move-out, поэтому concurrent upgrade не может resurrect T. Clone weak handle также explicit через `std.arc::clone_weak` или `std.rc::clone_weak`.

<a id="R-OWN-0013"></a>

**R-OWN-0013** — Strong и weak handles Move-only. Assignment, argument passing, return и aggregate initialization следуют ordinary explicit `move` rules и не изменяют reference count только из-за move source. Reference-count increment никогда не является implicit copy, conversion, parameter action или return action.

<a id="R-OWN-0014"></a>

**R-OWN-0014** — `std.arc::get_mut(&owner)` или `std.rc::get_mut(&owner)` возвращает nullable exclusive borrow T только когда caller exclusively borrows strong handle и не существует другого strong или explicit weak handle. Test shall быть linearizable относительно clone, downgrade, upgrade и drop. Failure возвращает null и не меняет counts или T.

<a id="R-OWN-0015"></a>

**R-OWN-0015** — `std.arc::try_unwrap(move owner)` или `std.rc::try_unwrap(move owner)` возвращает alternative `unwrapped(T value)` своего module `try_unwrap_result<T>` только когда consumed handle является sole strong handle; при failure возвращает `shared(owner)` без изменения strong count. Success transition shall быть linearizable относительно weak upgrade. Explicit weak handles могут остаться; после этого их upgrade fails и они удерживают alive только control block. T moved out и не dropped в прежнем allocation. После завершения move operation ровно один раз releases implicit weak-liveness duty, retires T storage и deallocates block тогда и только тогда, когда explicit weak handles не осталось. Staging successful outcome, включая unwind во время staging, shall not потерять или duplicate эту cleanup duty.

<a id="R-OWN-0016"></a>

**R-OWN-0016** — Operations `strong_count`, `weak_count` и `ptr_eq` модулей `std.arc`/`std.rc` не передают ownership. Count results — instantaneous observations; они shall not использоваться как synchronization или proof успеха последующего uniqueness operation. `weak_count` сообщает число explicit weak handles без implicit weak-liveness duty. Он shall retry или mask любой internal locked sentinel и никогда не expose его как count. `ptr_eq` true ровно для strong handles с одинаковой control-block allocation identity.

<a id="R-OWN-0017"></a>

**R-OWN-0017** — Strong count и число explicit weak handles shall not превышать половину максимального значения, представимого типом `usize`, rounded down. Physical weak-liveness counter может дополнительно содержать одну implicit duty из R-OBJ-0011; internal locked sentinel не является count. До того как strong clone, weak clone, downgrade или upgrade превысит applicable limit, operation shall вызвать panic `reference_count_overflow`, не изменив count и не создав handle. Destruction никогда не должна underflow; underflow возможен только из-за unsafe/external contract violation и является UB.

<a id="R-OWN-0018"></a>

**R-OWN-0018** — `arc`, `rc`, обе weak forms, `array`, `list`, `dict`, `task` и каждый standard resource не имеют C ABI representation. Они shall not встречаться на любой nesting depth `extern "C"` signature, включая raw-pointer pointee, либо recursively в `@repr(C)` aggregate. Managed-token adapter exposes только `raw const void*`, никогда managed layout. Separately verified imported opaque C handle остаётся допустимым для своего external C object, но не является managed-owner token. Exposing borrowed raw pointer к T не передаёт strong reference и требует сохранять original strong owner alive; external retained reference требует unsafe balanced adapter R-FFI-0060.

<a id="R-OWN-0019"></a>

**R-OWN-0019** — `core::replace(T* destination, T replacement) -> T` безопасно заменяет инициализированное место подготовленным значением. T должен быть поддерживаемым Copy- или Move-значением без внешнего квалификатора и с доказанным unborrowed по R-GEN-0002; void, never и атомарное хранилище исключаются. Назначение — ненулевая эксклюзивная ссылка на изменяемое хранилище. Обычные правила доступа, пересечения заимствований, порядка аргументов и запрета вызовов внутри аргументов сохраняются. Именованная Move-замена требует `move`; Copy-замена сохраняет источник. Операция вычисляет операнды один раз, подготавливает замену до изменения назначения, возвращает прежнее значение и оставляет назначение инициализированным. Она не открывает неинициализированное промежуточное место, не вызывает пользовательский drop и не выделяет heap-память. Последующее уничтожение обоих значений подчиняется обычным правилам cleanup и ресурсных эффектов. Ошибка предшествующего checked-вызова, подготавливающего замену, сохраняет назначение. Операции не предоставляют атомарности или синхронизации.

`core::take(T* destination) -> T` выполняет такую же замену значением R по умолчанию для T (R-INIT-0005). Наличие default должно быть доказано при проверке определения; новый default-конструктор или обнулённое представление владельца не вводится. Значение по умолчанию, вычисляющее инициализаторы полей, готовится до замены, поэтому брошенная им checked error оставляет назначение неизменным и является ошибкой вызова `take`. Например, извлечение `o<T>` оставляет `o::none`, числа — ноль, а `bytes` — пустого владельца байтов. Ненулевой владелец или владеющая строка требуют `replace` с явно подготовленной заменой. Операции не разрешают извлечение сохранённых обычных заимствований, срезов и runtime- строковых представлений либо перемещение произвольного поля без замены. Содержащий агрегат всё время остаётся полностью инициализированным.

`core::swap(T* first, T* second) -> void` обменивает значения двух инициализированных мест одного типа T при требованиях к типу, как у `core::replace`. Оба операнда — ненулевые исключительные заимствования изменяемого хранения; они активны до обмена, поэтому двойное заимствование одного места — обычное нарушение алиасинга `R-DIAG-BORROW-001`. Операция вычисляет операнды один раз, ничего не уничтожает, не вызывает пользовательский код и не выделяет heap-память; оба места остаются инициализированными. Два элемента одного среза обменивает `std.slice::swap` (Library R-SLIB-SLICE-0002).

<a id="R-OWN-0020"></a>

**R-OWN-0020** — `core::clone(const T* value) -> T` создаёт независимое значение, равное значению, которое обозначает `value`, и не изменяет его. Аргумент — ненулевое разделяемое или исключительное заимствование, а T доказывает способность `clone` из R-TYPE-0033:

- Copy-значение копируется; копия сохраняет происхождение каждого своего view, у вызова нет checked-ошибок;
- `std.string::string`, `std.fs::path`, `array<U>`, `list<U>`, `o<U>`, `own U*`, фиксированный массив и кортеж копируют содержимое поэлементно по порядку, а `dict<K, V>` — записи в порядке вставки, если каждая компонента клонируема;
- `arc U`, `rc U` и их слабые формы дают ещё один handle той же аллокации по R-OWN-0011 и R-OWN-0012;
- номинальный struct, enum или error клонируем, если модуль, объявивший тип, определяет ассоциированный хук `T T::clone(const T* value)`: безопасное, синхронное, не protected определение со списком ошибок, пустым или равным ровно `std.alloc::alloc_error`; generic-тип объявляет хук с параметрами схемы, как в R-FUNC-0009.

Тип, который не Copy, должен быть unborrowed; generic-параметр объявляет `clone & unborrowed`, а `copy` влечёт `clone`. Остальные типы, включая task, atomic, dyn-интерфейсы, стандартные ресурсы и номинальный тип без хука, требуют `R-DIAG-TYPE-001`, а второй хук — `R-DIAG-NAME-002`. Клонирование значения, которое не Copy, может бросить только `std.alloc::alloc_error`: при отказе каждая уже построенная компонента уничтожается ровно один раз в обратном порядке, результата нет, источник не изменён. Структурное клонирование самовложенного типа рекурсировало бы вместе с данными и требует `R-DIAG-STACK-001`; статический граф вызовов (R-FUNC-0004) и ресурсные контракты (R-FUNC-0019) включают каждый хук клонирования, который вызывает копируемая структура. Клонирование значения, которое не Copy, выделяет память и не имеет доказательства `@noalloc` и `@nonblocking`. Interface schema 31 записывает хук `clone` типа и ограничение `clone` generic-параметра.

<a id="borrowing-lifetimes"></a>

## 13. Borrowing and lifetimes

### 13.1 Borrow creation

<a id="R-BORROW-0001"></a>

**R-BORROW-0001** — Address expression `&place` является context-dependent: target `const T*` создаёт shared borrow, target `T*` создаёт exclusive borrow. Если target type не определяет kind однозначно, требуется диагностика; implicit mutable borrow не выбирается по умолчанию. Receiver вызова method получает borrow kind из объявленной формы receiver по R-FUNC-0014.

<a id="R-BORROW-0002"></a>

**R-BORROW-0002** — Shared borrow разрешает чтение и дальнейшие shared reborrows, но запрещает mutation и move исходного overlapping place. Exclusive borrow разрешает чтение и mutation и запрещает любой иной overlapping access. Если value exclusive borrow или exclusive slice само доступно только через shared/const path, его право attenuates до shared pointee/element access; mutation, move и создание exclusive reborrow через такой path запрещены.

<a id="R-BORROW-0003"></a>

**R-BORROW-0003** — Число одновременных shared borrows не ограничено. В каждый момент для memory location допускается либо любое число shared borrows без записи, либо ровно один usable exclusive borrow.

<a id="R-BORROW-0004"></a>

**R-BORROW-0004** — Reborrow не может иметь lifetime длиннее исходного borrow и на время exclusive reborrow приостанавливает использование parent borrow.

<a id="R-BORROW-0005"></a>

**R-BORROW-0005** — Dereference nullable safe indirection (`borrow` или `own`) требует предшествующего flow-sensitive доказательства `p != null`; иначе constraint violation. `null` dereference через raw pointer нарушает unsafe contract и является UB.

### 13.2 Lifetime inference

<a id="R-BORROW-0006"></a>

**R-BORROW-0006** — Lifetime каждого borrow начинается при evaluation `&` или reborrow и заканчивается после последнего потенциального использования borrow, но не позднее конца scope binding. Реализация shall применять non-lexical lifetime analysis по control-flow graph.

<a id="R-BORROW-0007"></a>

**R-BORROW-0007** — Borrow shall not outlive source object identity. Возврат borrow на automatic local, сохранение его в более долгоживущий object или захват thread, который может пережить source, требуют диагностики.

<a id="R-BORROW-0008"></a>

**R-BORROW-0008** — Каждое borrow-bearing input occurrence вводит fresh hidden region variable. Для каждого borrow, slice, ordinary `str`, guard, scoped handle либо compound, содержащего один из них и flowing to output, compiler shall derive origin set из всех reachable return paths. Output usable только пока каждый source этого set valid и все applicable alias restrictions выполняются. Program-storage root имеет distinguished program region и не добавляет caller-owned source. Automatic local в escaping origin set требует `R-DIAG-BORROW-002`.

<a id="R-BORROW-0009"></a>

**R-BORROW-0009** — Source signatures никогда не записывают region variables. Exported function definition с borrow-bearing output shall публиковать inferred input-origin mapping в interface fingerprint. Declaration без body shall получить и точно match этот mapping из resolved definition либо imported interface; source-only declaration без такого definition не может объявлять borrow-bearing output. Например, первый output связан с `values`, а второй conservatively bounded одновременно `left` и `right`:

```r
const i32* first(const i32[] values) {
    return &values[0];
}

const i32* choose(const i32* left, const i32* right, bool take_left) {
    if (take_left == true) {
        return left;
    } else {
        return right;
    }
}
```

Output зависит от borrow- или slice-входа либо через хранилище, которое этот вход обозначает, — как заимствование этого хранилища или view, достигнутый из него через удерживаемые там исключительное заимствование или срез, owner или контейнер, — либо только через то, что это хранилище удерживает: view, прочитанный из него, или элемент либо поддиапазон удерживаемых там разделяемого среза или обычного `str` (R-BORROW-0021). Вызов связывает зависимость второго вида с тем, что удерживает аргумент, а не с самим аргументом: пока output жив, хранилище, которое обозначает аргумент, можно снова заимствовать, изменять, перемещать или уничтожать, а удерживаемое им остаётся заимствованным. Например, `take` возвращает поддиапазон среза, который удерживает его декодер, поэтому `head` и `rest` живы одновременно:

```r
struct Decoder { const u8[] input; usize at; };

const u8[] take(Decoder* this, usize n) {
    const u8[] part = this->input[this->at..this->at + n];
    this->at += n;
    return part;
}

usize both(const u8[] data) {
    Decoder d = Decoder {.input = data, .at = 0usize};
    const u8[] head = take(&d, 1usize);
    const u8[] rest = take(&d, 2usize);
    return len(head) + len(rest);
}
```

Interface schema 31 экспортирует `borrow_contract` для обычного результата и каждой номинальной checked error, включая generic-определения и закрытые инстанциации. Происхождение — `none` (заимствований нет), `static`, точный набор нулевых индексов входных параметров, где `held=(...)` перечисляет входы, от которых оно зависит только через удерживаемое их хранилищем, либо `conservative_inputs`, если символический контракт требует все потенциально заимствующие входы. `stores=(...)` перечисляет для каждого параметра-цели входы, сохраняемые в обозначаемое им хранилище, с `held=(...)`, как выше. `projections=union` ограничивает каждое вложенное поле, элемент массива и активный payload этим объединением; отдельное время жизни и непересечение разных projections из него не следуют. Копирование descriptor не делает его хранилище владельцем referent. Generic-подстановка сохраняет границы и проверки projected alias; unborrowed-инстанциация удаляет ограничения заимствования, но не проверки владения. Объединение с неизвестным источником остаётся консервативным. Rethrow и распространение checked errors сохраняют собственную границу каждой ошибки. Provenance является compile-time metadata и не меняет layout или carrier ABI. Для импорта generic-тел по-прежнему нужны исходные определения; одной interface-записи недостаточно для доказательства происхождения результата объявления без тела.

<a id="R-BORROW-0010"></a>

**R-BORROW-0010** — U+0027 apostrophe не является standalone punctuator и никогда не вводит type relation, region name либо identifier. Он встречается только как opening и closing delimiter одного complete character literal.

### 13.3 Provenance and aliasing

<a id="R-BORROW-0011"></a>

**R-BORROW-0011** — Borrow provenance содержит source identity и допустимый range. Field borrow ограничен field; slice borrow — half-open element range `[begin,end)`. Disjoint fields или доказанно disjoint slice ranges may иметь exclusive borrows одновременно.

<a id="R-BORROW-0012"></a>

**R-BORROW-0012** — Safe pointer arithmetic над borrows отсутствует. Indexing или subslice создаёт проверенный derived borrow, сохраняющий provenance.

<a id="R-BORROW-0013"></a>

**R-BORROW-0013** — Converting exclusive borrow to shared borrow is allowed and freezes mutation for shared lifetime. Обратная conversion запрещена.

<a id="R-BORROW-0014"></a>

**R-BORROW-0014** — Borrow и raw pointer shall not быть использованы для доступа к object после move, destruction или storage reuse, даже если numeric address совпал.

<a id="R-BORROW-0015"></a>

**R-BORROW-0015** — Condition `p != null` establishes non-null fact only on its true edge; `p == null` — on false edge. Assignment to p, passing exclusive borrow of p or control-flow merge with an unproven path invalidates the fact; move или drop p также прекращает возможность использовать fact для исходного place. Реализация shall perform this minimum flow-sensitive refinement for nullable borrow и owning pointer dereference.

<a id="R-BORROW-0016"></a>

**R-BORROW-0016** — Hidden input region variables universally quantified function interface. Output origin expressions выводятся из value flow, а не textual type equality. Region variables являются compiler metadata: это не identifiers, они не объявляют runtime objects и не могут referenced source code.

<a id="R-BORROW-0017"></a>

**R-BORROW-0017** — Distinguished program region охватывает всё program execution. Он may быть proven для borrow только когда storage root является static thread-independent immutable object, immutable backing data, designated `constexpr str`, либо external object, verified safety contract которого proves immutability, program-long provider loading и storage duration. Каждый borrow-bearing component compound value proves independently; это не продлевает duration containing object. Automatic local либо thread-local instance никогда не имеет program region.

<a id="R-BORROW-0018"></a>

**R-BORROW-0018** — Every compound value, включая named struct/enum, fixed array, `array`, `list`, `dict`, `o` либо standard schema, содержащее borrow, slice, ordinary `str`, guard, scoped handle или recursively borrow-bearing component, получает hidden region parameters в compiler metadata. Construction выводит их из component origins; copy, move, assignment, argument и return сохраняют либо shorten их по R-BORROW-0021. Ни nominal aggregate, ни built-in/standard type constructor не пишет эти parameters в source. Component `constexpr str` не carries caller-owned region. Owning pointer `own T*`, strong owner `arc T`/`rc T` и weak owner получают hidden region parameters своего payload T, как compound, содержащий T; `new` и `std.alloc::try_new` выводят их из payload. Payload owner является хранилищем, которое достигает этот owner: место, достигнутое через него, как `node->word`, — проекция owner, и заимствование этого места заимствует owner. Handles разделяемого owner, strong и weak, разделяют regions своей allocation, фиксированные при её construction: `clone`, `downgrade`, `upgrade` и `try_unwrap` их сохраняют, а view, сохраняемый в payload через `get_mut`, shall иметь только эти regions, иначе требуется `R-DIAG-BORROW-002`. Payload, adopted через `core::adopt` или reconstructed через `from_raw`, не имеет известных regions, поэтому его T не может быть borrow-bearing: он не содержит borrow, slice, ordinary `str`, guard, lock outcome с guard, scoped handle, container iterator либо entry reference, а T, зависящий от generic parameter, shall быть доказан `unborrowed` constraints своего определения (R-TYPE-0033); иначе требуется diagnostic.

<a id="R-BORROW-0019"></a>

**R-BORROW-0019** — Каждое borrow-bearing input occurrence независимо, если value flow либо containing value не связывает его с другим occurrence. Local region constraints выводятся из initializer и uses и shall not превышать ни один source region. Exported и local source types используют одинаковое region-free spelling; отличается только interface metadata.

Функция может сохранить view, который вносит один вход, в хранилище, которое обозначает borrow- или slice-параметр, как `feed` ниже сохраняет `chunk` в tokens своего парсера. Тогда её контракт записывает сохранение: параметр-цель и каждый сохраняемый туда вход, отмечая, сохраняется ли только удерживаемое хранилищем этого входа (R-BORROW-0009). Вызов применяет сохранение: хранилище, которое обозначает аргумент-цель, и хранилища, которые обозначают удерживаемые в нём исключительные view, с момента вызова удерживают view сохраняемых аргументов, как после сохранения в самой вызывающей функции; вызывающая функция, передающая целью собственный параметр, записывает сохранение в свой контракт. Аргумент-цель с неизвестным обозначаемым хранилищем требует `R-DIAG-BORROW-002`. До возврата из функции хранилище, которое обозначает вход, остаётся заимствованным после такого сохранения его view: исключительный view исключает любой другой доступ к этому хранилищу, разделяемый — запись в него. View автоматической памяти, неизвестного хранилища или хранилища, которое обозначает сама цель, так не сохраняется, а holder, переданный по значению, целью не является.

```r
struct Parser { array<str> tokens; };

void feed(Parser* this, str chunk) throws std.array::push_error<str> {
    std.array::push(&this->tokens, chunk);
}
```

Заимствование хранилища, которое обозначает parameter, не должно сохраняться в это хранилище: объект вызывающей стороны нёс бы region, называющий его собственное хранилище, что удлиняет этот region (R-BORROW-0021); требуется `R-DIAG-BORROW-002`.

<a id="R-BORROW-0020"></a>

**R-BORROW-0020** — Address-of and safe borrow construction shall designate an existing non-temporary object or its live subobject. Borrowing a value temporary, либо member/index whose storage root is such temporary, is a constraint violation; R 0.1 не имеет temporary lifetime extension. String-literal expression по R-ARRAY-0006 produces non-place value descriptor `constexpr str`; descriptor не имеет address identity, и direct address-of для него является constraint violation. Для byte index любого ordinary либо `constexpr str` descriptor, включая descriptor value temporary, storage root является designated backing byte, а не descriptor object. Его region — carried region ordinary `str` либо program region для `constexpr str`. Такой byte place может быть shared-borrowed по R-EXPR-0021, но не exclusively borrowed.

<a id="R-BORROW-0021"></a>

**R-BORROW-0021** — Если compiler proves, что source region outlives required use region, shared borrow, shared slice либо ordinary `str` may быть coerced/reborrowed к этому shorter use, preserving provenance. Shortening exclusive borrow/slice creates exclusive reborrow, suspends parent по R-BORROW-0004 и не copies value. Region may never be lengthened. `o`/`array`/`list`/`dict` allow this coercion recursively only through contained shared borrow/slice/`str` positions; они remain invariant через exclusive positions, а other type constructors remain invariant unless another rule states otherwise.

Shared borrow, shared slice либо ordinary `str`, удерживаемые в storage, при чтении сохраняют свой region, поэтому element или subrange, достигнутый через них, как `this->items[i]`, лежит в этом region, а не в region storage, из которого они прочитаны; place, достигнутое через удерживаемые в storage exclusive borrow или slice, остаётся в region этого storage.

<a id="R-BORROW-0022"></a>

**R-BORROW-0022** — Module-scope, `static` либо `thread_local` object, содержащий borrow или slice, valid только когда initializer proves program region для каждого такого component; иначе требуется `R-DIAG-BORROW-002`. Ordinary `str` там остаётся запрещён; для program-image text используется `constexpr str`. Module либо block-static descriptor для direct safe immutable access shall использовать `const constexpr str` по R-OBJ-0009; non-`const` static descriptor требует той же synchronization либо `unsafe` access, что любой другой mutable static object.

<a id="R-BORROW-0023"></a>

**R-BORROW-0023** — Каждый `thread_scope` создаёт один fresh hidden scope region. Каждый `std.thread::scoped_join_handle<R throws E...>`, созданный непосредственно в этом block, carries этот region в compiler metadata. Nested scopes создают distinct regions. Ни scoped handle, ни borrow-bearing argument, ни returned value, связанные со scope region, не могут escape из block.

<a id="R-BORROW-0025"></a>

**R-BORROW-0025** — Контракты заимствований сохраняют отдельные зависимости от входов для статически различимых выходных компонентов: полей структур, элементов фиксированных массивов и активных payload. Возвращаемые значения и checked-payload переносят зависимости через вызовы, rethrow, generic-подстановку и слияние потока управления. Выбор компонента выбирает только его доказанные зависимости. Непересекающиеся изменяемые проекции сохраняют обычные проверки алиасов. Слияние объединяет возможные источники каждого компонента; неполные или динамические проекции консервативно сохраняют зависимости охватывающего значения. Параметры времени жизни в исходнике и runtime-таблицы provenance не вводятся. Interface 16 сериализует канонические пути компонентов и входные зависимости независимо от порядка обнаружения исходников.

<a id="R-BORROW-0024"></a>

**R-BORROW-0024** — Обычный async-frame не захватывает обычные заимствования, срезы и runtime `str`, включая рекурсивно содержащиеся представления, и не сохраняет их живыми через await. Представления можно использовать полностью между приостановками. Параметр `args` асинхронного main — вход запуска; его последнее использование предшествует первой приостановке, если данные не скопированы во владельцев. Стандартная async-операция с заимствованием на время вызова устанавливает независимый retain или копию до успешного возврата запущенной задачи, если библиотека не объявляет её scoped: такая операция удерживает представление как займ охватывающей группы задач по R-STMT-0017.

Исключение `@scoped async` регулируется R-STMT-0017: Send-представления можно захватывать и сохранять через приостановку, поскольку группа удерживает их хранение до подтверждения cleanup ребёнка. Это не предоставляет Send неподходящему типу, не разрешает заимствованный результат/ошибку завершения и не ослабляет обычные проверки доступа и алиасов. Результаты и ошибки завершения требуют Send и unborrowed; generic-сигнатуры доказывают эти свойства при определении.

<a id="expressions-conversions"></a>

## 14. Expressions and conversions

### 14.1 Value categories

<a id="R-EXPR-0001"></a>

**R-EXPR-0001** — Expressions имеют type и category `place`, `value` или `never`. Чтение Copy place produces value. Когда только place имеет outermost object `const`, produced value имеет corresponding unqualified T; nested access kinds сохраняются. Чтение Move place by value требует `move`, а R-TYPE-0008 запрещает move из object с outermost `const`.

<a id="R-EXPR-0002"></a>

**R-EXPR-0002** — Parentheses не меняют type/category. Member `.` требует aggregate place/value; `->` является shorthand `(*p).field` и требует usable non-null borrow, unique owner или strong reference-counted owner, либо raw dereference внутри unsafe. Access through owner подчиняется R-OWN-0009..R-OWN-0010 и не consumes owner.

### 14.2 Arithmetic

<a id="R-EXPR-0003"></a>

**R-EXPR-0003** — Integer promotions преобразуют `i8`, `u8`, `i16`, `u16` в `i32`. Для остальных mixed integer operands применяются usual arithmetic conversions: выбирается больший rank при одинаковом signedness; при разном signedness — signed тип, если он представляет все values unsigned, иначе unsigned type с не меньшим rank. Fixed ranks increase with width. `isize`/`usize` share one rank placed above fixed integer of the same width and below any wider fixed integer; this is the tie-break when widths coincide. До promotions для binary integer arithmetic, bitwise либо comparison operator, кроме shift, если ровно один complete operand является unsuffixed integer literal или его immediate unary-minus form, а другой operand имеет R integer type, literal contextually получает этот другой type, когда representable. Иначе применяется default type R-LEX-0010. Shift count никогда не определяет type или width left operand; поэтому `1u64 << shift` сохраняет suffix, когда требуется 64-bit left operand. Unsuffixed literal right shift-count converts в `usize` по R-EXPR-0007.

<a id="R-EXPR-0004"></a>

**R-EXPR-0004** — Mixed floating arithmetic converts integer to the floating operand type. Если любой floating operand is `f64`, common type is `f64`; otherwise it is `f32`. Simple assignment, argument и return conversion не разрешают narrowing без `as`, даже если arithmetic conversion её выполняет.

<a id="R-EXPR-0005"></a>

**R-EXPR-0005** — Unsigned `+`, `-`, `*` и unary `-` вычисляются modulo 2^N. Signed операция с результатом вне representable range вызывает `integer_overflow` panic. В constant expression тот же случай требует compile-time diagnostic.

<a id="R-EXPR-0006"></a>

**R-EXPR-0006** — Integer `/` или `%` с zero divisor вызывает `division_by_zero` panic. Signed `MIN / -1` вызывает `integer_overflow`; иначе quotient truncates toward zero, remainder имеет sign dividend и удовлетворяет `a == q*b + r`.

<a id="R-EXPR-0007"></a>

**R-EXPR-0007** — Shift count converted to `usize`; count \>= bit width вызывает `invalid_shift` panic. Unsigned left shift modulo 2^N; signed left shift panic, если математический результат непредставим. Right shift unsigned zero-fills; right shift signed является arithmetic и округляет к negative infinity.

<a id="R-EXPR-0008"></a>

**R-EXPR-0008** — Bitwise operators for all R integer types, including `isize` and `usize`, разрешены после integer promotions. Their two’s-complement/unsigned binary representation определяет `~`, `&`, `|` и `^` без padding/trap representations. C ABI integer operands follow the separate target-dependent restriction R-EXPR-0024.

### 14.3 Comparison and logic

<a id="R-EXPR-0009"></a>

**R-EXPR-0009** — `==`, `!=`, `<`, `<=`, `>`, `>=` numeric operands используют usual arithmetic conversions и возвращают bool. Float comparison с NaN: `!=` true, все остальные false, включая `==`. `char` поддерживает все comparisons по scalar value; `bool` и fieldless values одного enum type — только `==`/`!=`.

<a id="R-EXPR-0010"></a>

**R-EXPR-0010** — `&&`, `||`, `!` принимают только bool. `&&` не вычисляет RHS при false LHS; `||` не вычисляет RHS при true LHS.

<a id="R-EXPR-0011"></a>

**R-EXPR-0011** — Pointer equality разрешена между compatible values одной pointer category (`borrow`, `own` или `raw`) с одинаковой nullability либо после explicit safe widening to nullable. Nullable pointer may сравниваться с `null`. Equality owning pointers observes address/identity without copy, move или ownership transfer. Ordering, subtraction и arithmetic borrows/owners запрещены; raw analogues требуют unsafe.

### 14.4 Assignment, increment and conditional expression

<a id="R-EXPR-0012"></a>

**R-EXPR-0012** — Assignment operators имеют type `void` и допустимы только как expression statement либо first/iteration clause `for`. Chained assignment и использование assignment value запрещены.

<a id="R-EXPR-0013"></a>

**R-EXPR-0013** — Compound assignment `x op= y` вычисляет destination place x один раз, читает его current Copy value, затем вычисляет y, выполняет arithmetic/checks 14.2 и в commit point записывает converted result. Для fixed R integer либо non-Boolean C ABI integer destination эта final conversion является неотъемлемой частью compound operator и не требует `as`: unsigned destination приводит математический результат modulo 2^N, а signed destination вызывает `integer_overflow` panic, если результат непредставим. Все arithmetic, division и shift checks выполняются до commit. Для C ABI destination N, signedness и range берутся из target manifest. Для остальных destination types требуется non-narrowing simple-assignment conversion. Это исключение compound operator не ослабляет правила initialization, simple assignment, argument или return conversion. RHS mutation of x, если она вообще разрешена borrow rules, не меняет уже прочитанный left value. `++`/`--` доступны только mutable integer place, evaluate that place once, use the same signed-panic or unsigned-modulo `+/- 1` semantics и имеют type `void`. Compound assignment holds the storage-slot/provenance portion of R-INIT-0007 capability: required root storage cannot be moved, dropped, reused or deallocated during RHS, and conflicting borrows shall end before commit, but an otherwise legal overlapping RHS write is permitted and is overwritten by the result based on the saved left value.

<a id="R-EXPR-0014"></a>

**R-EXPR-0014** — Conditional expression `condition ? a : b` использует explicit `condition-expression` Annex A и всегда produces value, никогда place. Copy place arm читается по R-EXPR-0001 до common-type determination, поэтому outermost object `const` снимается. Затем arms shall иметь identical value type либо единую value-preserving common type. Named Move place arm требует explicit `move`; только выбранный Move arm вычисляется и transfers.

Условие вычисляется ровно один раз. При истинном условии вычисляется только `a`, при ложном — только `b`. Обе ветки проходят проверку типов и учитываются в checked effects даже при константном условии. Состояния владения достижимых веток объединяются по тем же правилам, что у `if` (R-OWN-0004). Оператор ассоциативен справа; его ветви используют обычные правила выражений.

Conditional expression является развилкой. Она shall not встречаться на любой глубине внутри аргумента вызова либо операнда return: аргумента вызова функции, метода, `format` или awaited call, операнда `panic`, value operand standard type call и операнда `return` в statement либо switch clause. Выбранное значение сначала инициализирует именованный объект, либо развилка записывается через `if`. Поэтому `return (count == 0) ? 1 : 2;` требует `R-DIAG-FLOW-001`, а `i32 status = (count == 0) ? 1 : 2;` с последующим `return status;` допускается.

```r
constexpr str message = (exists == true)
    ? "output archive already exists"
    : "cannot atomically publish output archive";
```

### 14.5 Conversions

<a id="R-EXPR-0015"></a>

**R-EXPR-0015** — Implicit conversions ограничены: integer promotion; exact integer widening без sign change; `f32` to `f64`; mutable-to-shared borrow; non-null-to-nullable; `never` to any; exact array-to-slice borrow при явном `&`; `constexpr str` to ordinary `str`, carrying program region; `str` to `const u8[]` с сохранением inferred region и забыванием UTF-8 invariant; и exact error либо значение error с потомками to ancestor error как значение family ancestor (R-AGG-0011). Conversion set в `constexpr str` содержит только identity; поэтому ordinary `str`, byte-slice, allocated-text и foreign-storage values сохраняют собственные types. Reverse byte-slice-to-str conversion follows checked R-ARRAY-0007 and is not implicit. Два string conversion edges этого rule shall not быть chained в любой implicit conversion sequence или common-type determination; каждый operand may traverse не более одного edge. Поэтому и `const u8[] bytes = "abc";`, и selection common byte-slice type между literal и byte slice требуют `R-DIAG-TYPE-001`. Source shall сначала initialize named ordinary string, затем initialize byte slice из этого name.

<a id="R-EXPR-0016"></a>

**R-EXPR-0016** — Для numeric и character cases, разрешённых этим разделом, `value as Type` performs checked conversion. Out-of-range integer destination, NaN-to-integer, infinity-to-integer и invalid Unicode scalar вызывают `invalid_conversion` panic; в constant expression — diagnostic. Floating destination follows R-EXPR-0017 and does not use integer range failure.

<a id="R-EXPR-0017"></a>

**R-EXPR-0017** — Float-to-integer `as` truncates toward zero после range check. Integer-to-float may round ties-to-even и никогда не panic; loss of precision разрешена только потому, что conversion explicit. `f32 as f64` is exact; `f64 as f32` rounds ties-to-even, yields signed infinity on finite overflow and uses gradual underflow, while NaN remains NaN with unspecified payload/sign.

<a id="R-EXPR-0018"></a>

**R-EXPR-0018** — Enum-to-integer и integer-to-enum conversions доступны через `as` только fieldless `@repr(C)` enum; invalid discriminant при обратной conversion вызывает panic. Tagged enum conversions отсутствуют.

<a id="R-EXPR-0019"></a>

**R-EXPR-0019** — Cast между raw pointers, borrow/raw, pointer/integer, изменение raw constness и reinterpretation representation являются unsafe operations. Raw pointer conversion сохраняет или добавляет nullability: nullable raw pointer converts только в nullable raw pointer type, null converts в null, а nullable source с non-null target требует `R-DIAG-TYPE-001`.

### 14.6 Calls, indexing and constant expressions

<a id="R-EXPR-0020"></a>

**R-EXPR-0020** — Вызов функции или метода разрешён в выражении, в том числе в аргументе другого вызова, инициализаторе, операнде return или throw. Вызываемое значение и аргументы вычисляются один раз в порядке исходника; вложенное выражение завершается до следующего аргумента. Вычисляется только выбранная условная ветвь или ветвь сокращённого логического вычисления; conditional expression shall not встречаться внутри аргумента (R-EXPR-0014). Временные владельцы отслеживаются при вызовах, checked-ошибках и отмене и уничтожаются ровно один раз. Именованный Move-аргумент по-прежнему требует `move`.

Вызов async-функции немедленно запускает `task<T throws E...>` и может сразу бросить `std.async::start_error`. По R-FUNC-0010 именованный Move-аргумент резервируется и потребляется только после успешного запуска. Завершённые вложенные вызовы не откатываются; непереданное временное значение уничтожается. Вложенный `move` через синхронный вызов потребляет источник по контракту этого вызова.

<a id="R-EXPR-0021"></a>

**R-EXPR-0021** — `a[i]` для fixed array, `array<T>`, slice, ordinary `str` или `constexpr str` converts `i` to `usize` с range check, затем проверяет `i < len(a)`. Failure вызывает panic `bounds`; compile-time provably invalid index требует diagnostic. Index array либо slice designates его element с учётом access kind source. Index `array<T>` designates соответствующий live T в owned buffer и создаёт ту же inferred borrow relationship, что `std.array::get` либо `get_mut`; last use каждого element либо slice borrow предшествует всем structural mutations, перечисленным R-LIB-0019. String index designates immutable byte `u8` в backing storage; его чтение produces этот byte, и разрешён только shared borrow этого place. Для array либо slice, включая `array<T>`, `a[lo..hi]` additionally проверяет `lo <= hi <= len(a)` и produces corresponding slice. String subscript с range не определён в R 0.1; source shall сначала weaken либо convert string в byte slice по R-EXPR-0015.

<a id="R-EXPR-0022"></a>

**R-EXPR-0022** — Constant expression may содержать literals, visible const objects whose initializer is recursively a constant expression, fieldless enum values, variants tagged enumerations и options с constant payloads, связанные константы (R-TYPE-0050), reflection constants, `sizeof`, `alignof`, pure operators без allocation, borrow, mutable access или panic, и translation-time calls (R-EXPR-0032). Cyclic const reference, potential panic оператора либо panic при вычислении translation-time call делает expression ill-formed.

<a id="R-EXPR-0032"></a>

**R-EXPR-0032** — Translation-time call — вызов translation-time evaluable функции (R-FUNC-0023), аргументы которого — constant expressions либо shared borrows const-объектов со значениями constant expressions. Он вычисляется при трансляции по правилам абстрактной машины с ширинами, layout и conversions выбранной цели (R-TYPE-0004, R-OBJ-0004), независимо от host и окружения трансляции, и его значение — значение вызова; строковый результат обозначает байты строкового литерала в образе программы (R-TYPE-0028). Где требуется constant expression (граница массива, константный generic argument, значение enumerator, case label, initializer объекта модуля, `static` или `thread_local` и любой другой операнд, который этот документ требует константным), panic при вычислении или checked error, которую не поймал ни один `try` вычисляемых вызовов, требует `R-DIAG-CONST-003`, превышение документированных лимитов вычисления при трансляции (R-IDB-010) требует `R-DIAG-LIMIT-001`, а вызов невычислимой функции либо с неконстантным аргументом требует `R-DIAG-CONST-002` с первой причиной вдоль цепочки вызовов. Checked errors такого вызова не являются checked effects охватывающей функции (R-ERR-0001): вычисление либо завершается, либо трансляция отклоняется. В остальных местах вызов заменяется своим значением, когда вычисление завершается в пределах этих лимитов, не создавая и не изменяя owner, и значение укладывается в документированный лимит подстановки; иначе вызов выполняется во время выполнения с обычным поведением, потому что panic, брошенная ошибка, allocation или стоимость там наблюдаемы. Вызов функции `void` не заменяется. `const`-объект модуля или `static`-объект, не являющийся `thread_local`, может хранить owners, которые его initializer вычисляет при трансляции: они входят в образ программы, который даёт им storage, поэтому во время выполнения они не выделяются, не растут и не освобождаются, а dictionary среди них сохраняет index Library R-LIB-0021 по predefined contract ключа — целого, `bool`, `char`, fieldless enumeration или строки; любой другой объект, который хранил бы такой owner, требует `R-DIAG-INIT-001`. Объявлению уровня модуля может понадобиться translation-time значение из функций, проверяемых позже; реализация вычисляет такие значения до проверки использующих их объявлений, а цикл между ними требует `R-DIAG-CONST-002`.

<a id="R-EXPR-0023"></a>

**R-EXPR-0023** — `expression as void` явно отбрасывает значение после однократного вычисления операнда. Copy-операнд читается обычным образом и сохраняет источник. Move-операнд потребляется по обычным правилам: именованный владелец требует `move`, а временное значение уничтожается ровно один раз, включая user drop и вложенный cleanup. Лишнего копирования или выделения памяти нет. Эффекты операнда, drop, panic и checked errors сохраняют обычные обязательства; преобразование не обходит правила разрешения task. Оно подтверждает `@must_use`, не ослабляя владение и доступ. Явный `drop` Copy остаётся запрещённым. API без значимого результата следует возвращать `void`; результат, который вызывающий вправе игнорировать, объявляется `@discardable` (R-FUNC-0021).

<a id="R-EXPR-0024"></a>

**R-EXPR-0024** — Arithmetic between C ABI integer types uses promotions, ranks and usual arithmetic conversions of selected C17 ABI, but R overflow, division and shift checks apply before generated C operation. Compound assignment and increment/decrement final conversion use manifest width, signedness and range under R-EXPR-0013. Arithmetic on `c_float`, `c_double` or `c_long_double` is available only when target manifest records an IEC operation mapping; otherwise it requires checked `std.c` helper or explicit conversion. Mixing any C ABI numeric type with fixed R numeric type requires explicit `as` conversion. Bitwise operation on unsigned C ABI integer is defined; on signed C ABI integer it is available only when manifest proves two’s-complement without padding/trap representation compatible with R-EXPR-0008, otherwise a checked `std.c` helper or explicit unsigned conversion is required.

<a id="R-EXPR-0025"></a>

**R-EXPR-0025** — Floating unary `+`/`-` and binary `+`, `-`, `*`, `/` follow R-TYPE-0006 at common type selected by R-EXPR-0004. Floating division by zero produces IEC infinity or NaN and does not use integer `division_by_zero` panic; `%`, bitwise and shift operators do not accept floating operands.

<a id="R-EXPR-0026"></a>

**R-EXPR-0026** — Immediate expression `-L`, where L is a signed-suffixed integer literal whose mathematical magnitude is exactly one greater than positive maximum of that suffix type, directly forms its minimum value and is well-formed. This exception does not apply through parentheses, macro (none exist), constant name or any other operator; larger magnitude remains `R-DIAG-CONST-001`. Unsuffixed immediate unary-minus initializer вместо этого непосредственно получает destination type по R-INIT-0002 и проверяется по своему mathematical signed value.

<a id="R-EXPR-0027"></a>

**R-EXPR-0027** — Когда exact parameter type direct function call равен `const u8[]`, argument initialization дополнительно допускает одну contextual read-only byte-view conversion из любого следующего source: identity из `const u8[]`; ordinary mutable-to-shared conversion из `u8[]`; `str`; `constexpr str`; place типа `array<u8>` либо его exact alias `bytes`; или place fixed-array type `u8[N]`. Direct случай `constexpr str` является одним contextual edge и не образует chain из двух ordinary string conversions, ограниченных R-EXPR-0015.

Результат имеет существующий type `const u8[]`, а не новый wrapper либо owner. Он обозначает ровно bytes и length source: UTF-8 byte length для каждого string type, live element count для `array<u8>`/`bytes` и N для `u8[N]`. Формирование view не выполняет allocation, copy, move и не consumes source. Поэтому owner либо fixed-array source указывается без `move` и shared-borrowed на время call. Temporary slice descriptor существует только для argument initialization и call; его byte provenance и inferred borrow origin остаются provenance и origin исходного storage. Поэтому returned или иной derived borrow, объявленный interface callee, остаётся связанным с этим первоначальным source и продлевает соответствующий shared borrow до своего last use.

Эта contextual conversion рассматривается только после resolution callee и его exact parameter type. Она не создаёт overload resolution, не выбирает между callees и не применяется к object initialization, assignment, return, conditional common-type selection либо element type, отличному от `u8`. Независимая ordinary conversion `str` в `const u8[]` из R-EXPR-0015 остаётся доступной в указанных там contexts.

<a id="R-EXPR-0028"></a>

**R-EXPR-0028** — Форматированный литерал имеет слитный контекстный префикс `f`: `f"hello {name}"`. В остальных позициях `f` остаётся идентификатором. Обычные строки сохраняют тип и правила escape. Соседние обычные и форматированные литералы образуют одно выражение форматирования, если хотя бы один литерал форматирован. Только форматированные части интерпретируют слоты; слот не пересекает границу литерала. `{{` и `}}` дают буквальные скобки. Скобка из escape — текст, не синтаксис.

Именованный слот содержит имя локальной переменной или параметра, включая `this`, и необязательные проекции полей через `.` либо `->`. Вызовы, индексация, произвольные выражения и `move` внутри слота запрещены. Позиционный слот — десятичный индекс начиная с 1. Порядок и повторения произвольны. Позиционные проекции и пустые слоты запрещены. `receiver.format(arguments)` — примитивное выражение компилятора: receiver должен быть последовательностью форматированных литералов или локальным `std.format::format`; на другом месте, тип которого удовлетворяет `core::Format`, этот суффикс — метод R-TYPE-0046. Результат — владеющий `std.string::string`; для каждого индекса нужен аргумент, каждый переданный аргумент должен использоваться. Аргументы следуют R-EXPR-0020. Сам примитив допустим в runtime-выражениях, включая return, условные ветви и aggregate payload. Общие методы и вариативные R-функции не вводятся. Владельцы заимствуются для форматирования; аргументы с `move` запрещены.

При явном объявлении локального `std.format::format` f-литерал создаёт шаблон; в остальных контекстах — `std.string::string`, требуя заполнить позиционные слоты сразу. Без позиционных слотов допустим `.format()`. Шаблон разрешён только как автоматическая локальная переменная: параметры, результаты, поля, контейнеры, generic-аргументы и static/module объекты запрещены. Компилятор выводит схему из литерала либо перемещённого локального шаблона. Тип имеет свойство Move; перемещение сохраняет схему, несовместимые присваивания запрещены. Форматирование не потребляет и не изменяет шаблон. Повторные вызовы могут передавать разные типы при совместимости с каждым спецификатором.

При создании шаблона каждый уникальный именованный путь захватывается один раз, в порядке первого появления. Числа, bool, char и constexpr str копируются. Runtime `str` и `std.string::string` копируются в собственное UTF-8 хранилище. Исходные владельцы не перемещаются; захватывается конечное поле, а не содержащий его объект. Последующее изменение или уничтожение источника не влияет на шаблон. При прямом вызове `.format` сначала завершаются снимки, затем аргументы вычисляются однократно слева направо, затем части выводятся в порядке шаблона. Повторный индекс использует готовое значение; вставленный текст не разбирается повторно. Снимки unborrowed; Send/Sync и сохранение через await следуют конкретным типам захватов.

Создание и форматирование вводят checked `std.alloc::alloc_error`, даже если оптимизация устраняет выделение памяти. Отказ освобождает частичные снимки, результат и владеющие временные значения ровно один раз, сохраняя исходных владельцев и ранее созданный шаблон. Неявного преобразования результата в заимствованный `str` или `constexpr str` и продления жизни временного значения нет. Сохраняются обычные правила staged async Move.

Встроенное форматирование поддерживает runtime/constexpr `str`, `std.string::string`, `char`, R/C Boolean, целые и плавающие R/C типы. Текст вставляется точно, char кодируется UTF-8, Boolean даёт `true`/`false`. Целые по умолчанию десятичные, плавающие используют каноническую запись std.format. Значение любого другого типа, удовлетворяющего `core::Format` (R-TYPE-0046), включая параметр-тип, ограниченный им, форматируется его implementation или стандартным форматированием в текст, который вставляет слот, а заимствование форматирует значение, на которое указывает. Aggregates, enum и error без implementation, bytes, указатели и недоказанные generic-параметры не имеют контракта форматирования; одних constraints Copy/POD для него недостаточно.

После `:` допустимы целочисленные `d`, `x`, `X`, `b`, `o`; фиксированная дробная точность `.N` для плавающих; необязательная положительная десятичная минимальная ширина с начальным `0` для заполнения нулями. Примеры: `8`, `8x`, `08X`, `10.2`, `010.2`. Ширина и точность — константы usize; точность допускает 0. Динамические спецификаторы запрещены. Одна ширина применима к любому значению: она дополняет вставленный текст пробелами слева до стольких Unicode scalar values. Целочисленные формы, дробная точность и заполнение нулями — числовые спецификаторы: они неприменимы к тексту, bool, char и другим операндам `core::Format`; целочисленные формы неприменимы к float, дробная точность — к integer. Ширина учитывает знак и точку, не обрезает результат, по умолчанию дополняет пробелами слева. Нули следуют после минуса. Префикса основания нет; `X` использует верхний регистр; отрицательные целые выводятся как знак и модуль. Фиксированная точность округляет точное бинарное значение к ближайшему десятичному с ties-to-even, сохраняет завершающие нули и отрицательный ноль; `.0` не выводит точку. Форматирование не зависит от locale и сохраняет floating environment. `inf`, `-inf`, `nan` сохраняют запись и дополняются только пробелами. Переполнение длины результата вызывает `std.alloc::alloc_error::size_overflow`. Ошибочные слоты, типы, константы и хранение требуют `R-DIAG-FORMAT-001`, с существующими синтаксическими, именными, ownership и checked-effect диагностиками в соответствующих случаях.

<a id="R-EXPR-0029"></a>

**R-EXPR-0029** — `a in b` и `a not in b` — membership tests типа `bool`; `in` связывает как relational operator и non-associative, а `not` — identifier, распознаваемый только непосредственно перед `in`. Membership test — condition leaf по R-STMT-0002 и обычный операнд `bool` в остальных позициях. Когда `b` — range `lo..hi`, test равен `a >= lo && a < hi` над одним integer либо `char` type, а `a` вычисляется один раз до нижней границы. Верхняя граница вычисляется только при успехе нижнего сравнения. Иначе `b` именует place либо non-null borrow place, type которого — `dict<K, V>` либо реализует `core::Contains` (R-TYPE-0046); тогда `a` именует place key либо item type, а test равен `std.dict::contains(&b, &a)` либо `b.contains(&a)` соответственно, оба операнда borrowed shared на время test. Любой другой right operand, left operand не place и type mismatch требуют `R-DIAG-TYPE-001`.

<a id="R-EXPR-0031"></a>

**R-EXPR-0031** — `match (value) { case pattern if (condition): expression; ... }` вычисляет вход один раз и выдаёт значение выбранной ветви. Guard необязателен. Ветвь может завершаться безусловным оператором `throw` вместо выражения; тогда ветвь имеет тип `never`. Шаблоны: константы, `_`, именованные привязки только для чтения, `move name`, вложенные `variant Type::Name(pattern)` и поля `{ .field = pattern }`, где поле кортежа называется индексом элемента, как в `{ .0 = a, .1 = move b }` (R-TYPE-0052); пропущенные поля — wildcard. Простое имя создаёт привязку; именованная константа в скобках является константным шаблоном. Необязательная ветвь `default:` — wildcard. Контекстное слово `match` остаётся обычным допустимым именем. Вход типа `str` или `constexpr str` принимает шаблоны — строковые литералы, сравниваемые побайтно, а вход реализации `core::CaseMatcher` — константы её типа `Label`, проверяемые её `matches` (R-TYPE-0046); такие шаблоны никогда не покрывают свой домен, ветви проверяются по порядку, а повторная метка требует `R-DIAG-SWITCH-001`.

Покрытие исчерпывает комбинации конструкторов. Guard не доказывает покрытие; открытый скалярный домен требует wildcard. Проверки и guard выполняются до переноса payload и читают только активный payload. Move-привязка требует временного владельца или явно перемещённого входа. Декомпозиция передаёт каждый активный компонент в отслеживаемое хранение; безымянные владельцы также уничтожаются. Значение с пользовательским drop нельзя декомпозировать, но целый конечный компонент с drop можно переместить. Запрет произвольного перемещения через поля и заимствования сохраняется. Ветви имеют единый контекстный или выведенный тип; `never` не добавляет продолжающееся состояние владения. Заимствования локального хранения ветви не выходят наружу. Выбранные ветви допускают вызовы и await; невыбранные не имеют runtime-эффектов. Разворачивание покрытия ограничено compiler limits вложенности и специализации. Variant-шаблоны поддерживают номинальные объявления enum/error и встроенные option/result. Для непрозрачных native-outcome стандартной библиотеки сохраняется существующая проекция typed switch; адресуемый номинальный layout payload для match у них не предоставляется.

<a id="R-EXPR-0030"></a>

**R-EXPR-0030** — Collection expression строит standard container из своих elements. `[e1, e2, ...]` и `[element for (T name in iterable) ...]` — array expressions; `{k1: v1, k2: v2, ...}` и `{key: value for (T name in iterable) ...}` — dict expressions. Collection expression не имеет собственного type: оно встречается только там, где известен contextual type, и этот type shall быть `array<T>` для array expression либо `dict<K, V>` для dict expression; каждый element, key и value вычисляется с element, key либо value type в качестве context и shall иметь ровно этот type. `[]` — пустое array expression; `{}` остаётся aggregate initializer, а dict expression содержит хотя бы одну entry. Expression вычисляется так, как если бы hidden local был инициализирован `std.array::create::<T>()` либо `std.dict::create::<K, V>()`, каждый element добавлен `std.array::push`, каждая entry вставлена `std.dict::insert` в source order (поздняя entry с равным key замещает раннее value, которое dropped), а hidden local перемещён в result; checked effects этих standard calls (`std.array::push_error<T>`, `std.dict::insert_error<K, V>`) — effects expression и подчиняются R-ERR-0001. Comprehension состоит из одного element либо entry, за которым следуют одна clause `for` и любое число дальнейших clauses `for` и `if`; каждая clause `for` имеет формы header и семантику range-for (R-STMT-0014), её loop variable видима в последующих clauses и в element, каждая clause `if` — condition по R-STMT-0002, пропускающее текущую iteration при false, а element вычисляется один раз на каждую уцелевшую iteration. Collection expression допускается как argument или operand `return`, когда доступен полный контекстный тип коллекции (R-EXPR-0020, R-STMT-0005); отсутствующий либо mismatching destination type, mismatching element type и element type без value representation требуют `R-DIAG-TYPE-001`.

<a id="static-reflection"></a>

<a id="R-REFL-0001"></a>

**R-REFL-0001** — Static reflection fieldless enumeration `T` обеспечивают compiler-recognized формы `core`: `core::enum_count::<T>()`, `core::enum_min::<T>()`, `core::enum_max::<T>()` и `core::enum_variants::<T>()` — reflection constants, сворачиваемые во время translation, — и `core::enum_name(value)`, `core::enum_ordinal(value)`, `core::enum_at::<T>(index)` и `core::enum_from_name::<T>(name)`, выбирающие во время выполнения. `enum_count` — число объявленных variants как `usize`; `enum_min` и `enum_max` — variants с наименьшим и наибольшим discriminant в signedness underlying type; `enum_variants` — array `T[N]` всех variants в порядке объявления. `enum_name(value)` — объявленное имя variant, который хранит value типа `T`, как `constexpr str`, а `enum_ordinal(value)` — его declaration index от нуля как `usize`; `enum_at(T, index)` принимает `usize` и даёт `o::some` variant с этим declaration index либо `o::none` на count и далее; `enum_from_name(T, name)` принимает `str` и даёт `o::some` variant, объявленное имя которого побайтно равно name, либо `o::none`. Reflection constant — constant expression (R-EXPR-0022); четыре selections — calls, а `core::enum_name` и `core::enum_ordinal` могут вычисляться при трансляции (R-FUNC-0023). Форма enumeration, type operand которой не fieldless enumeration либо value operand которой не имеет такого типа, требует `R-DIAG-TYPE-001`. Когда type operand либо тип operand именует generic parameter, требование проверяется, а constant сворачивается при каждой инстанциации (R-TYPE-0043); `enum_variants` требует конкретного enumeration.

<a id="R-REFL-0002"></a>

**R-REFL-0002** — `core::variant_count::<T>()` — reflection constant число variants tagged union `T` (enumeration с payload variants, включая `error` declaration) как `usize`, а `core::variant_name(const T* value)` выбирает во время выполнения объявленное имя активного variant tagged union, который обозначает shared borrow, как `constexpr str`. Operand, не являющийся tagged union, требует `R-DIAG-TYPE-001`; generic parameter проверяется при инстанциации, как в R-REFL-0001.

<a id="R-REFL-0003"></a>

**R-REFL-0003** — `core::type_name::<T>()` — reflection constant canonical spelling complete type как `constexpr str`: primitive types по их keyword; `str`, `constexpr str`, `void` и `never` как записаны; struct либо enumeration как `module.path::Name`, а generic instance как `module.path::Name<arguments>` с arguments, записанными рекурсивно через `", "`; `const T` и `atomic T` как записаны; borrows как `const T*` и `T*` с завершающим `?`, когда nullable; slices как `const T[]` и `T[]`; fixed arrays как `T[N]`; `own T*`, `raw T*`, `raw const T*`, `arc T`, `rc T`, `weak arc T` и `weak rc T`; containers как `array<T>`, `list<T>` и `dict<K, V>`; `o<T>` и `task<T>`; standard type — его qualified name с type arguments; raw function type как `raw fn(P, ...) -> R`. Type, который нельзя записать в source, записывается его implementation-defined именем. `core::field_count::<T>()` — reflection constant число fields struct `T` как `usize`, а `core::field_name::<T>(index)` — объявленное имя field с declaration `index` от нуля, integer constant expression, как `constexpr str`; operand, не являющийся complete struct, требует `R-DIAG-TYPE-001`, а index на field count и далее требует `R-DIAG-CONST-001`. Generic parameter проверяется при инстанциации, как в R-REFL-0001.

<a id="R-REFL-0004"></a>

**R-REFL-0004** — `core::target_name()` — target triple выбранного target manifest, а `core::profile_name()` — имя выбранного library profile (R-CONF-G005); обе — `constexpr str`, фиксированные во время translation; ни одна форма не принимает argument. Каждая форма R-REFL-0001..R-REFL-0004 доступна в каждом profile, ничего не выделяет, паникует только через Core checks своих operands и не вводит run-time type metadata (R-TYPE-0039): selections понижаются в прямые сравнения значения enumeration, активного tag либо байтов имени с таблицами времени translation.

<a id="static-conditions"></a>

### 14.3 Статические условия

<a id="R-META-0001"></a>

**R-META-0001** — `@if (predicate) { ... }` с необязательным продолжением `@else { ... }` или `@else @if (predicate) { ... }` выбирает ветвь при трансляции. Это оператор в теле функции либо внешнее объявление, содержащее внешние объявления. Это не выражение и не атрибут поля, параметра или объявления. Обе ветви должны быть синтаксически корректны. Неактивная закрытая ветвь не создаёт объявлений, проверяемых эффектов, cleanup, исполняемого кода или зависимости линковки. Выбранный блок оператора сохраняет обычную лексическую область. Импорты остаются в преамбуле единицы трансляции. Статические условия не меняют сигнатуру функции и не вводят перегрузку или специализацию.

<a id="R-META-0002"></a>

**R-META-0002** — Предикаты используют `&&`, `||`, `!` и скобки с обычным приоритетом Boolean-операторов, но отделены от runtime-выражений. Закрытый словарь: `Type is Type` для канонической идентичности, `Type is Trait`, `Type is fn(P...) -> R` и `Type is capability` для свойства из R-TYPE-0033. Для имени трейта действуют обычные правила видимости и квалификации. `core::profile is profile-name` сравнивает выбранный профиль с `freestanding`, `allocation`, `hosted`, `hosted-thread` или `hosted-native-async`; неизвестное имя профиля является ошибкой. `core::target is "target-triple"` точно сравнивает выбранный target triple. Константное условие — один лист условия из R-STMT-0002 (`true`, `false`, явное сравнение или проверка принадлежности), операнды которого — constant expressions (R-EXPR-0022), включая translation-time calls (R-EXPR-0032); атом является предикатом типа ровно тогда, когда за типом следует `is`. Условие — обязательная константа, вычисляемая при трансляции: panic требует `R-DIAG-CONST-003`, превышение лимитов вычисления при трансляции — `R-DIAG-LIMIT-001`, неконстантный операнд — `R-DIAG-CONST-002`. На уровне модуля разрешены предикаты профиля и цели, константные условия и их Boolean-комбинации. Константное условие модуля вычисляется до появления выбираемых им объявлений: оно может использовать любое объявление вне своих ветвей, в том числе выбранное другим условием модуля, а имя, объявленное только в его собственных ветвях, не разрешается. Runtime-вычисление, поиск произвольных членов и пользовательское расширение словаря предикатов не вводятся. Неразрешимый предикат или несовместимые предположения ветви требуют `R-DIAG-META-001`; для операндов также действуют обычные диагностики имён, типов и видимости.

<a id="R-META-0003"></a>

**R-META-0003** — Определение generic проверяет каждую ветвь, предикат которой остаётся зависимым. Предикат, прямо называющий типовой параметр, уточняет его внутри ветви: положительная идентичность подставляет точный тип, положительное свойство или трейт предоставляет существующий контракт. Отрицательные факты разрешают вложенные предикаты, но не предоставляют операций. Конъюнкция объединяет факты; дизъюнкция предоставляет только факты, доказанные для каждой альтернативы, включая следствие `pod` ⇒ `copy & unborrowed`. Дизъюнкция и отсутствие свойства не предоставляют несвязанных свойств. Ограничения составного зависимого типа не выводятся обратно для его аргументов. Константное условие, называющее константный generic-параметр или layout типового параметра либо вызывающее generic-функцию с зависящими от них generic arguments, остаётся зависимым и ничего не уточняет. Сравнение `len(T...)` для пакета `T` с целой константой, в том числе в сочетании через `&&`, `||` и `!`, уточняет длину `T` внутри выбранной ветви, включая отрицание одиночного сравнения в другой ветви (R-TYPE-0053). Его операнды проверяются в определении; вычислимость такого вызова при трансляции (R-FUNC-0023) проверяется для каждой инстанциации с закрытой вызываемой функцией. Факты и уточнённые типы изолированы от других ветвей, определений и инстанциаций. Продолжающиеся ветви подчиняются обычному объединению состояний владения: потреблённое на достигающей ветви значение недоступно после объединения. Инстанциация вычисляет проверенный предикат по каноническим аргументам, включая зависимое константное условие с диагностиками R-META-0002 для этой инстанциации, и клонирует только выбранную ветвь и её локальное хранение. Неактивная ветвь не инстанцируется. Контракты ресурсов проверяют все зависимые ветви определения и выбранные закрытые операции.

<a id="statements-flow"></a>

## 15. Statements and control flow

<a id="R-STMT-0001"></a>

**R-STMT-0001** — Блок `{ ... }` создаёт scope. Объявления и операторы могут чередоваться. Пустой оператор `;` разрешён. Expression statement требует `void` или `never` (завершающий достижимый путь), явное discard-преобразование по R-EXPR-0023 либо прямой вызов `@discardable`-функции по R-FUNC-0021. Значимые результаты дополнительно следуют R-FUNC-0020.

<a id="R-STMT-0002"></a>

**R-STMT-0002** — `if` и `while` принимают `condition-expression` Annex A. Каждая ветвь `if`, optional `else` и body `while` shall быть explicit block `{ ... }`. Condition leaves являются `true`, `false`, explicit comparison либо membership test (R-EXPR-0029); combined conditions используют `&&`/`||`. Loop body may выполняться zero or more times.

Условием `if` или `while` может быть проверка образца `value is pattern`, где `is` — контекстное слово, а образец — образец `match` (R-EXPR-0031). Проверка образца — всё условие целиком; внутри другого условия или в любом другом месте она требует `R-DIAG-SYN-001`. Значение вычисляется один раз на проверку. Место проверяется там, где оно находится, а любое другое значение, включая `move place`, инициализирует скрытый объект оператора. Если образец совпал, его привязки существуют только в блоке `if` или в теле цикла: привязка без `move` обозначает совпавшую часть только для чтения, пока её используют, а `move name` перемещает часть из скрытого объекта и требует его (R-EXPR-0031). Скрытый объект уничтожается в конце выбранного блока либо перед блоком `else` или концом цикла, если образец не совпал. `while` заново вычисляет и проверяет значение перед каждой итерацией и заканчивается на первом несовпадении, как `while (queue.next() is variant o::some(move item)) { ... }`.

<a id="R-STMT-0003"></a>

**R-STMT-0003** — `for (init; condition; iteration)` вычисляет init один раз, затем explicit `condition-expression` перед каждой iteration, body block и iteration expression. Condition shall присутствовать; init declaration scope включает condition, iteration и body.

<a id="R-STMT-0004"></a>

**R-STMT-0004** — `break` допустим только в loop/switch и exits innermost construct; `continue` — только в loop и переходит к iteration clause `for` либо condition. Все exited automatic objects dropped согласно 11.2.

Оператор `while`, `for` или range-for может нести метку, как `outer: for (...) { ... }` (Annex A). В его теле `break name;` покидает, а `continue name;` продолжает цикл с меткой `name` из любой вложенности циклов и операторов `switch`: переход покидает каждую область между ними с её уничтожениями и проходит каждый finally, защищённую область которого покидает (R-STMT-0009), а клауза `switch` может заканчиваться `break name;`. Метки образуют собственное пространство имён и видны только в теле своего цикла. Имя, не являющееся меткой объемлющего цикла, требует `R-DIAG-FLOW-001`, метка, повторяющая метку объемлющего цикла, — `R-DIAG-NAME-002`, а переход по метке из finally недопустим, как определяет R-ERR-0003.

<a id="R-STMT-0005"></a>

**R-STMT-0005** — `return;` разрешён в `void`-функции. Функция с результатом возвращает выражение или контекстный aggregate-инициализатор в фигурных скобках. Вызовы, конструкторы и await допускают прямую композицию в операнде по R-EXPR-0020 и R-STMT-0012; conditional expression shall not встречаться в нём (R-EXPR-0014). Значение преобразуется в объявленный тип результата по R-EXPR-0015; сужение требует явного `as`. Именованный Move-результат требует `move`. Результат полностью инициализируется и копируется или перемещается до уничтожения локальных значений функции. R-STMT-0013 определяет эквивалентный неявный выход в конце void-функции.

<a id="R-STMT-0006"></a>

**R-STMT-0006** — `switch` expression shall быть integer, char, fieldless enum, tagged enum, `o` либо closed standard outcome type. Он также может иметь тип `str` или `constexpr str`, метки которого — строковые литералы, сравниваемые побайтно, либо тип реализации `core::CaseMatcher`, метки которого — константы её типа `Label`, проверяемые её `matches` (R-TYPE-0046); такой switch вычисляет значение один раз, проверяет метки в порядке clauses и выполняет clause первой подошедшей метки, его метки shall быть различными значениями, и он shall иметь `default`, а метка другого типа требует `R-DIAG-TYPE-001`. Closed standard outcomes — ровно те public schemas, finite variant sets которых normative R Standard Library Specification 0.1 явно фиксирует как ordinary ownership-preserving outcomes по R-SLIB-GEN-0005. Library rule каждой такой schema фиксирует её qualified path, generic arguments, variants, payloads и capabilities; implementation shall не добавлять другую schema или variant. Case labels shall быть unique; допускается не более одной `default` clause. Integer/char switch shall иметь `default`; каждый fieldless/tagged enum, o или closed-outcome switch shall перечислить все possible variants либо иметь `default`. Unsuffixed integer literal, являющийся complete case pattern, contextually получает integer switch type, когда representable; иначе требуется `R-DIAG-CONST-001`.

<a id="R-STMT-0007"></a>

**R-STMT-0007** — После каждого `case` и `default` следует sequence statements без обязательных `{}`. Clause сама задаёт неявный lexical scope. Clause может завершаться explicit transfer: `break;`, допустимым в данном context `continue;`, `return ...;`, `throw ...;`/`throw;` либо, только для non-final clause, `fallthrough;`. Normal completion clause при достижении следующего label или закрывающей `}` покидает switch, как `break;`. `fallthrough;` shall быть последним top-level statement clause; target clause не binds payload, и ни один live local/borrow не пересекает edge. Перед transfer уничтожаются initialized locals покидаемой clause.

<a id="R-STMT-0008"></a>

**R-STMT-0008** — Tagged case pattern `case variant Type::Variant(binding):` проверяет variant и создаёт borrow binding payload по умолчанию. Keyword `variant` shall использоваться также для fieldless enum/o либо closed standard outcome pattern, чтобы pattern не был неоднозначен с constant-expression. Standard- outcome pattern использует qualified schema path без type arguments, например `variant std.thread::join_result::returned(value)`; arguments задаёт scrutinee. Fieldless alternative не имеет binding, payload alternative имеет один binding. Для standard variant с несколькими named payload fields этот единственный binding designates aggregate payload, а fields R Standard Library Specification 0.1 выбираются через него. `move binding` разрешён только при switch над whole payload-bearing value, записанным с outer `move`.

<a id="R-STMT-0009"></a>

**R-STMT-0009** — Structured control transfer выполняется forms `break;`, `continue;`, `return ...;`, `throw ...;`, `throw;` и `fallthrough;` в contexts R-STMT-0004..0007 и R-ERR-0001..0003. Каждый такой transfer покидает scopes только через их boundaries, traverses каждый active `finally`, protected region которого этот transfer покидает, и выполняет required drops по R-INIT-0008. Если target transfer остаётся внутри protected region, finally этого region не выполняется.

<a id="R-STMT-0010"></a>

**R-STMT-0010** — Switch scrutinee вычисляется ровно один раз. Here outer `move` means the whole scrutinee, ignoring parentheses, is unary `move place`; a nested move in a larger expression does not qualify. Без outer `move` tagged enum, `o` либо closed standard outcome place не consumes: switch удерживает shared borrow до выхода из switch, а payload binding без `move` имеет соответствующий shared borrow type. Любой non-place payload-bearing scrutinee materializes в hidden switch-owned object: Move result transfers ownership, Copy result copy-initializes его. Lifetime hidden object covers all cases; payload bindings borrow that object, not the original value temporary, and it drops on switch exit. Для Move scrutinee форма `switch (move place)` требует named initialized whole value tagged-enum, o либо closed standard outcome type; она consumes place в hidden temporary. Для Copy scrutinee `move` выполняет обычное копирование по R-OWN-0002 и сохраняет источник. `move binding` для Copy payload копирует его, сохраняя scrutinee. Для Move payload `move binding` передаёт активный payload из temporary внешнего Move switch. Успешный transfer сначала initializes binding как единственного cleanup owner payload, затем переводит whole hidden switched object в moved state; его storage позднее освобождается без user drop и без повторного payload drop. Это единственное whole-object pattern exception к запрету partial move R-OWN-0005; partially initialized shell не остаётся. Binding follows ordinary Move rules: if it remains initialized, it drops once on normal/unwinding exit; if ownership is transferred, cleanup responsibility transfers by R-AM-0010 and no local drop occurs. `move binding` запрещён, если scrutinee type имеет user drop, потому что drop body должен видеть целый active payload. Outer `move` для non-place или already moved place является constraint violation.

<a id="R-STMT-0011"></a>

**R-STMT-0011** — Statement `try` состоит из одного try block, одного или нескольких typed catch clauses с optional последующим finally clause либо одного finally clause без catches. Catch types внутри statement shall быть distinct. Catch binding является новым initialized local, owning exact selected error payload. Finally clause не имеет доступа к locals, объявленным внутри try block или sibling catch; он может обращаться к objects enclosing scope. Подробные правила selection, propagation, rethrow, cleanup и effects задают R-ERR-0001..0003.

<a id="R-STMT-0012"></a>

**R-STMT-0012** — `await move task_name` и `await function(arguments)` — выражения приостановки внутри async-функции. Они сочетаются с вызовами, операторами, условиями, операндами return и автоматическими инициализаторами. Отдельный await-оператор требует логический результат `void` либо форму вызова `@discardable` async-функции, результат которой затем отбрасывается по R-FUNC-0021. Именованная форма потребляет инициализированное наблюдение задачи и требует `move`. Форма вызова требует прямой именованный, квалифицированный вызов или вызов метода с результатом `task<T throws E...>`, включая стандартные и выводимые generic-вызовы. Она материализует одну скрытую задачу и автоматически потребляет её. Аргументы вычисляются один раз; немедленные ошибки вызова предшествуют await, ошибки завершения наблюдаются в await. Именованные Move-источники сохраняются при отказе запуска по R-FUNC-0010. Результат инициализируется только после успешного завершения. Скрытое хранение подчиняется обычным правилам заимствований, Send и cleanup. Формы `await task_name` и `await move function(arguments)` остаются запрещёнными. Await запрещён в синхронных функциях, модульных/static-инициализаторах и внутри `finally`. Выражения готовности группы определены в R-STMT-0017 и R-STMT-0018.

<a id="R-STMT-0013"></a>

**R-STMT-0013** — Достижение closing brace function body на reachable path допускается, когда declared logical return type равен `void`. Оно точно equivalent выполнению `return;`; обе forms одинаково покидают и уничтожают scopes. Rule использует также logical return type `async` function. Explicit final `return;` остаётся valid, но является semantically redundant. Каждый reachable path любого другого declared return type shall завершаться explicit value return либо иметь type `never`; достижение его closing brace требует `R-DIAG-FLOW-001`.

<a id="R-STMT-0017"></a>

**R-STMT-0017** — `task_scope(capacity) name { statements }` создаёт ограниченную группу задач в async-функции вне finally. Capacity — compile-time целое в диапазоне 1..65536. В generic-функции её можно записать через constant generic parameters (R-TYPE-0047), прямо или через constant locals; тогда она — константа каждого instance, и instance с capacity вне этого диапазона требует `R-DIAG-ASYNC-001` с именем instance. Имя обозначает локальное хранение компилятора, а не first-class значение. Вложенные группы имеют независимые регионы. Каждая задача, непосредственно запущенная в теле, контролируется ближайшей группой; перенос наблюдения в другую группу запрещён. Заполненная группа выдаёт `std.async::start_error::scope_full` до подготовки задачи и commit именованных Move-аргументов. Завершение ребёнка не освобождает слот, пока наблюдение не потреблено; отсоединённый участник освобождает слот, когда он завершён и его исход уничтожен.

Атрибут без аргументов `@scoped` разрешён один раз у async-функции и запрещён у extern. Такую функцию можно вызвать только при активной группе. Аргументы с заимствованиями требуют Send и хранения, охватывающего тело группы; локальное хранение тела, неизвестные источники и выходящие наблюдения отклоняются. Потребляющий `await move member`, наблюдающий терминальный исход участника, завершает займы этого участника; после потребляющей отмены или detach либо пока наблюдение не потреблено, займы группы сохраняются до барьера всех участников или выхода, подтверждающего cleanup ребёнка. Общие займы допускают общее чтение; эксклюзивные запрещают доступ родителя. Отказ запуска восстанавливает состояние займов и владения до запуска.

Стандартная async-операция, которую Спецификация стандартной библиотеки R объявляет scoped, вызывается как функция `@scoped async`: только при активной группе задач, а её аргументы-представления являются займами этой группы с теми же правилами источника, Send, общих и эксклюзивных займов. Её займ завершается только тогда, когда native backend подтвердил завершение или отмену и больше не может обращаться к хранению представления; этот момент — публикация исхода операции, которую наблюдает потребляющий await и ждут барьер всех участников и выход из группы. Хранение представления операция не копирует и не удерживает, а неудачный запуск не держит займа.

`await name.first(&a, &b, ...)` заимствует инициализированные именованные наблюдения группы и возвращает индекс с нуля первого завершённого участника в порядке аргументов. `await name.all()` ждёт cleanup всех участников. Барьеры готовности не потребляют результаты и не наблюдают ошибки завершения; это делает отдельный `await move member`. Наблюдения группы нельзя вынести через return, aggregate, heap-аллокацию, замыкание или аргумент обычного вызова. Потребляющие cancel и `std.async::detach(move member)` разрешены и не прекращают контроль группы: отсоединённый участник продолжает выполняться под своей группой, `await name.all()` и выход из группы его ждут, выход из группы отменяет его, если он ещё выполняется, его возвращённое значение или checked-ошибка уничтожаются один раз без наблюдения, а ненаблюдаемый panic попадает в настроенный обработчик panic (R-FUNC-0012). Ожидания с дедлайном, `name.cancel_all()` и оператор select определены в R-STMT-0018.

Любой выход, включая return, checked-ошибку, break, continue и отмену родителя, отменяет непотреблённых участников и ждёт подтверждения завершения до закрытия группы и уничтожения охватывающего заимствованного хранения. Ненаблюдаемый payload уничтожается один раз по обычной политике отмены task. Обязательный drain компилятора может приостанавливаться и игнорирует повторную отмену родителя; исходный await внутри finally по-прежнему запрещён. Контроль группы независим от единственного наблюдения. Новый исходный ABI task и boxing не вводятся; ограниченное хранение слотов принадлежит frame родителя.

<a id="R-STMT-0018"></a>

**R-STMT-0018** — Внутри группы задач (R-STMT-0017) `await name.first_until(deadline, &a, &b, ...)` и `await name.all_until(deadline)` ограничивают барьеры готовности `first` и `all` дедлайном типа `std.time::instant` монотонных часов; дедлайн другого типа требует `R-DIAG-TYPE-001`. `first_until` имеет тип `o<usize>`: индекс с нуля первого завершённого участника в порядке аргументов либо `o::none`, если дедлайн наступил раньше. `all_until` имеет тип `bool`: `true`, если все участники завершили cleanup до дедлайна. Обе формы требуют контекста значения (`R-DIAG-USE-001`) и проверяют готовность раньше дедлайна, поэтому готовая группа никогда не сообщает об уже прошедшем дедлайне. Как `first` и `all`, они не потребляют наблюдений, не наблюдают ошибок завершения и не завершают займов; дедлайн прекращает только ожидание. Из нескольких завершённых участников `first`, `first_until` и select выбирают наименьший индекс аргумента или ветви.

`await name.vacancy()` ждёт, пока в группе не появится свободный слот, и имеет тип `void`; `await name.vacancy_until(deadline)` ограничивает это ожидание дедлайном типа `std.time::instant`, имеет тип `bool` — `true`, если слот освободился до дедлайна, — и требует контекста значения (`R-DIAG-USE-001`). Слот свободен, когда его не занимает ни один участник: участник занимает слот, пока не завершится и его наблюдение не будет потреблено или отсоединено (R-STMT-0017), поэтому в заполненной группе, все участники которой работают или остаются ненаблюдёнными, свободного слота нет. Готовность проверяется раньше дедлайна, других аргументов ожидание свободного слота не принимает (`R-DIAG-ASYNC-001`) и, как барьеры готовности, не потребляет наблюдений, не наблюдает ошибок завершения и не завершает займов.

`name.cancel_all()` — вызов типа `void`, запрашивающий отмену каждого участника группы с непотреблённым наблюдением, включая select-pending участников, и потребляющий эти наблюдения как `std.async::cancel(move member)`. Он не ждёт: контроль, слоты и займы сохраняются до подтверждения завершения, которое наблюдают `await name.all()` или выход из группы; повторный вызов больше ничего не потребляет. Другое имя операции или аргумент требуют `R-DIAG-ASYNC-001`. Payload завершения отменённого участника, включая checked-ошибку, уничтожается один раз без наблюдения. Отмена — запрошенная или выполненная выходом из группы — не доказывает, что внешние эффекты участника не произошли.

`select (name) { clauses }` ждёт в названной охватывающей группе, пока один из перечисленных участников не завершится или не наступит дедлайн, затем выполняет ровно одну ветвь. Ветвь участника `case T binding = await move member:`, `case auto binding = await move member:` или, для участника с результатом `void`, `case await move member:` называет инициализированного участника этой группы один раз. Ветвь `case until (deadline):` встречается не более одного раза и принимает `std.time::instant`. Нужна хотя бы одна ветвь участника. Операторы и терминаторы ветвей следуют ветвям switch по R-STMT-0006: `break` покидает select, а `fallthrough` требует `R-DIAG-FLOW-001`. Любая другая форма ветви, участник другой группы, повтор участника или отсутствие ветви участника требуют `R-DIAG-ASYNC-001`; участник, который может быть уже потреблён, требует `R-DIAG-MOVE-002`; дедлайн другого типа требует `R-DIAG-TYPE-001`. `select` и `until` контекстны: `select` начинает оператор, только если за ним следуют имя в скобках и `{`.

Дедлайн вычисляется один раз; затем select ждёт как `first_until` по участникам в порядке ветвей либо как `first` без ветви until. Ветвь выбранного участника потребляет его через `await move` по R-STMT-0012, поэтому ошибка завершения этого участника распространяется из ветви; ветвь until выполняется, если дедлайн наступил раньше, и ничего не потребляет. Select никогда не отменяет. Каждый перечисленный участник, которого выполненная ветвь не потребила, продолжает работу и удерживает слот и займы. После select перечисленный участник, потреблённый только на части путей, становится select-pending: эти пути не считаются несогласованными по R-OWN-0004, упоминание участника требует `R-DIAG-MOVE-002`, и только `name.cancel_all()` или выход из группы, отменяющий его по R-STMT-0017, разрешает его и уничтожает исход без наблюдения. Программа, которой нужны ошибки завершения остальных участников, ожидает их внутри ветвей.

Ограниченные ожидания, ожидания свободного слота, `cancel_all` и select не запускают задач и не занимают слотов; ограниченное ожидание и ветвь until заканчиваются не позже дедлайна ожидающей задачи (R-STMT-0019). Наступивший дедлайн не меняет состояния участников, займов и Move, а отмена родителя во время такого ожидания идёт по пути отмены R-STMT-0017.

<a id="R-STMT-0019"></a>

**R-STMT-0019** — `deadline (value) block` задаёт операторам блока дедлайн в async-функции вне finally; в другом месте он требует `R-DIAG-ASYNC-001`. Значение имеет тип `std.time::instant` монотонных часов или `o<std.time::instant>`, где `o::none` не добавляет дедлайна; другой тип требует `R-DIAG-TYPE-001`. `deadline` контекстно: оно начинает оператор, только если за ним следуют выражение в скобках и `{`.

У каждой задачи есть дедлайн, возможно отсутствующий. Значение вычисляется один раз до блока; пока блок выполняется, дедлайн выполняющей задачи — более ранний из её прежнего дедлайна и значения, поэтому вложенный блок может сократить дедлайн, но не продлить его. Любой выход из блока — завершение, `break`, `continue`, `return`, checked-ошибка, panic или отмена — восстанавливает прежний дедлайн, как это сделал бы finally (R-STMT-0009). Задача при запуске получает дедлайн запускающей её задачи и сохраняет его: так наследуют async-вызовы, участники групп задач и стандартные асинхронные операции, запущенные во время выполнения блока, а задача, запущенная до блока, сохраняет собственный дедлайн, даже если её ожидают внутри блока.

Дедлайн ограничивает операции, а не вычисление: блок не прерывает операторов и не отменяет задач. Стандартная асинхронная операция берёт более ранний из собственного аргумента дедлайна, который вызов может опустить, и дедлайна запускающей её задачи (Library R-SLIB-ASYNC-0008); поэтому истёкший дедлайн даёт ошибку операции `timed_out`, а не `cancelled`. Ожидания `first_until`, `all_until` и `vacancy_until` и select с ветвью until (R-STMT-0018) заканчиваются не позже дедлайна ожидающей задачи; ожидания без дедлайна, сам `await`, таймеры `sleep_for` и `sleep_until` и асинхронный приём из канала (Library R-LIB-0016) им не ограничиваются. Блок дедлайна не запускает задач и не занимает слотов.

<a id="R-STMT-0020"></a>

**R-STMT-0020** — `budget (value) block` задаёт операторам блока бюджет в async-функции вне finally; в другом месте он требует `R-DIAG-ASYNC-001`. Значение имеет тип `std.alloc::limits` (Library R-SLIB-ALLOC-0003), поля которого `bytes` и `tasks` хранят пределы бюджета, а `o::none` не задаёт предела этого вида; другой тип требует `R-DIAG-TYPE-001`. `budget` контекстно: оно начинает оператор, только если за ним следуют выражение в скобках и `{`.

У каждой задачи есть бюджет, возможно отсутствующий. Значение вычисляется один раз до блока; пока блок выполняется, бюджет выполняющей задачи — новый бюджет с пределами значения, вложенный в её прежний бюджет, и любой выход из блока восстанавливает прежний бюджет, как для дедлайна (R-STMT-0019). Задача при запуске получает бюджет запускающей её задачи и сохраняет его.

Каждое выделение runtime, которое делает код, выполняющий задачу, через `new` или стандартную операцию, учитывается в бюджете задачи и во всех бюджетах, в которые он вложен, и остаётся учтённым до своего освобождения, где и когда бы оно ни произошло; перераспределение учитывает или возвращает разницу в бюджетах выделения, а выделение, сделанное без бюджета, остаётся без него. Байты, учтённые в бюджете, никогда не превышают его предела байтов: выделение, которое превысило бы его, завершается неудачей `std.alloc::alloc_error::budget_exhausted` (Library R-LIB-0004), а `new`, не выполненный по этой причине, вызывает panic `allocation_failure` (R-OBJ-0006). Так же каждая задача, выполняющая async-функцию — вызов или участника группы задач, — учитывается от запуска до завершения в бюджете запускающей её задачи и во всех бюджетах, в которые он вложен; запуск сверх предела задач бросает `std.async::start_error::budget_exhausted` до подготовки задачи. Стандартные асинхронные операции задач не учитывают, а работа, которую `std.async::blocking` выполняет на своих потоках, ни в каком бюджете не учитывается. Если реализация не может записать бюджет, каждое выделение и каждый запуск блока завершаются неудачей, как если бы бюджет был исчерпан.

<a id="R-STMT-0021"></a>

**R-STMT-0021** — В async-функции вне finally `for (T name in &source) block` — асинхронный for, когда `source` называет локальный объект или параметр типа `std.sync::receiver<U>` либо типа, который реализует `core::AsyncIterator` и не реализует ни `core::Iterator`, ни `core::LendingIterator` (R-TYPE-0046). Каждая итерация заново заимствует `source` и ожидает следующий item: для receiver — асинхронный receive из Library R-LIB-0016 с item type `U`; иначе scoped `next` реализации, запущенный членом ближайшей task group с заимствованием как её loan, который завершает await (R-STMT-0017), с item type `Item`. Цикл заканчивается на первом `o::none`; иначе payload перемещается в `name`, тип которого shall быть item type, если не написано `auto`, и выполняется block. `break`, `continue` и `return` следуют R-STMT-0004, цикл не создаёт скрытых объектов дольше одной итерации, а ошибка запуска или `next` покидает цикл, как из любого await. Форма вне async-функции или внутри finally требует `R-DIAG-ASYNC-001`, как и источник `core::AsyncIterator` без объемлющей task group (R-STMT-0017); источник, не являющийся именованным локальным объектом или параметром, требует `R-DIAG-TYPE-001`, как и несовпадающий type loop variable (R-STMT-0014).

<a id="R-STMT-0022"></a>

**R-STMT-0022** — Деструктурирующее объявление `auto (first, second) = pair;` вычисляет инициализатор один раз. Инициализатор shall быть кортежем (R-TYPE-0052) с таким же числом элементов, сколько имён в объявлении; иначе требуется `R-DIAG-TYPE-001`. Объявление объявляет по локальному объекту на имя, по порядку, в объемлющем блоке или клаузе `switch`; тип каждого — тип его элемента, и он инициализируется из него: Copy-элемент копируется, Move-элемент перемещается. Место кортежа Move-типа требует `move` (R-OWN-0003) и потребляется без уничтожения опустевшего кортежа; место Copy-кортежа копируется и остаётся пригодным. Имена следуют R-NAME-0004 в этой области. Объявление более чем 64 имён требует `R-DIAG-LIMIT-001`, а объявление, не являющееся непосредственно оператором блока или клаузы, — `R-DIAG-TYPE-001`.

<a id="R-STMT-0014"></a>

**R-STMT-0014** — `for (T name in iterable) block` объявляет loop variable `name` типа `T` либо item type при написании `auto` и выполняет block один раз на каждый item; `break` и `continue` следуют R-STMT-0004, а hidden objects, создаваемые loop, dropped при его завершении; clause `for` comprehension (R-EXPR-0030) имеет те же формы header. Iterable — одно из следующего. Range `lo..hi` одного integer type: `name` принимает каждое value от `lo` до `hi` исключительно, обе bounds вычисляются один раз до loop, `T` — этот integer type, а присваивание `name` не влияет на iteration. Borrowed sequence `&place` типа `[T; N]`, `T[]`, `const T[]` либо `array<T>`: sequence borrowed на весь loop и индексируется по порядку; loop variable — `const T*` (также для `auto`), `T*` через exclusive borrow sequence либо копируемый `T` Copy element type, и каждый element borrow следует borrow rules. Borrowed `list<T>` либо `dict<K, V>`: container обходится через его standard cursor, loop variable — `const T*` либо `std.dict::entry_ref<K, V>`. Standard cursor либо implementation `core::Iterator` (R-TYPE-0046), переданная по значению как Move value с `move` или результат call либо продвигаемая на месте через exclusive borrow: каждая iteration вызывает `next`, loop завершается на первом `o::none`, а loop variable получает каждый payload по значению, поэтому её type shall быть item type. Implementation `core::LendingIterator` (R-TYPE-0046) типа, не реализующего `core::Iterator`, продвигается в тех же формах, и каждый возвращённый ею item заимствует iterator до следующего продвижения (R-TYPE-0045), поэтому удержание item дольше его iteration требует `R-DIAG-BORROW-001`. В async-функции заимствованный `std.sync::receiver<T>` или implementation `core::AsyncIterator` продвигается асинхронным for из R-STMT-0021. Любой другой iterable и несовпадающий type loop variable требуют `R-DIAG-TYPE-001`; named Move iterator без `move` либо `&` требует `R-DIAG-MOVE-001`.

<a id="functions"></a>

## 16. Functions and calling semantics

<a id="R-FUNC-0001"></a>

**R-FUNC-0001** — Function declaration содержит return type, name, typed parameter list, optional marker `async`, optional clause `throws` и optional body. Throws clause является частью function type, compatibility declarations/definitions и interface fingerprint. Empty `()` обозначает ровно zero parameters; последний parameter может быть variadic по R-FUNC-0018. `(void)` не является native R spelling parameter list; old-style unspecified parameter list отсутствует.

<a id="R-FUNC-0002"></a>

**R-FUNC-0002** — Parameter передаётся by value. Copy argument копируется; named Move argument требует `move`. Borrow/slice parameter передаёт ограниченное право доступа без ownership transfer.

<a id="R-FUNC-0003"></a>

**R-FUNC-0003** — Function body с non-void return type shall возвращать value на каждом reachable path, если этот path не имеет type `never`. Достижение конца `void` function имеет ровно semantics R-STMT-0013. Checked throw является terminating path только если его type caught снаружи этой точки либо входит в declared throws set function.

<a id="R-FUNC-0004"></a>

**R-FUNC-0004** — Функции R и методы могут разделять имя в модуле или имя члена типа, если их канонические списки типов параметров или число параметров различаются. Внешний object const, имена параметров, результат, checked errors, visibility, unsafe и async не различают перегрузки. Псевдонимы обозначают тот же тип. Generic-параметры сравниваются по позиции; constraints не различают одинаковые списки параметров. Совместимые прототип и определение по-прежнему обозначают один член. Объявления C, callback entry points и замкнутые key/JSON hooks не образуют наборы перегрузок. Default arguments не вводятся; variadic-параметры следуют R-FUNC-0018.

Выбор использует типы и число аргументов, без ожидаемого результата. Литералы имеют обычные типы R. Предпочтителен единственный точный набор типов параметров; иначе требуется единственный кандидат, допускающий существующие преобразования аргументов. Несколько оставшихся кандидатов или отсутствие кандидатов требуют R-DIAG-TYPE-001. Variadic-кандидат проверяет каждый упакованный аргумент по типу элемента либо весь передаваемый срез для spread. Кандидат с пакетом (R-TYPE-0053) принимает любое число аргументов после фиксированных параметров и связывает пакет с кортежем их типов; spread кортежа или пакета не выбирает перегрузку. Приоритеты числовых преобразований и constraints не вводятся. Receiver метода следует R-FUNC-0014. Выбор не вычисляет операнды и не фиксирует moves, borrows или checked effects: понижается только выбранный вызов, один раз, с его ошибками. Generic-тела выбирают объявление при проверке определения по constraints; инстанциация сохраняет этот выбор. Имена из разных импортируемых модулей остаются неоднозначными. Члены перегрузки имеют детерминированную каноническую идентичность в interface schema 31 и в порядке генерируемых символов функций. Static call graph функций R программы, включая drop hooks, достижимые через owned storage, initializers `std.sync::call_once`/`get_or_init` и asynchronous starts, shall быть acyclic, кроме циклов функций с `@recursion`, которые допускает R-FUNC-0026; вызов через dyn-интерфейс достигает реализации каждого члена интерфейса (R-TYPE-0051), а любая другая recursive call chain требует `R-DIAG-STACK-001`. Aggregate type may достигать себя через owned storage вдоль owners `own`, `arc` и `rc`, options `o`, fixed arrays, elements `array` и `list`, values `dict` и by-value members struct или enum, вложенных в любом порядке; self-reaching route через key `dict`, standard type, `task` или atomic требует `R-DIAG-STACK-001`, как и derived JSON encoder или decoder self-nesting aggregate. Destruction self-nesting aggregate следует R-INIT-0010 (user drop body сначала, затем members в reverse declaration order, elements с последнего, каждый pointee или element до release его owner) с глубиной stack, не зависящей от числа и вложенности owned values. Из измеренных frames target implementation shall вывести static worst-case stack bound для каждого entry в код R (hosted entry point, entry каждого spawned thread, каждый asynchronous step или helper gate, каждый initializer `call_once` и каждый entry из C) и shall отвергнуть программу, bound которой превышает entry stack budget target. Во время выполнения этот bound проверяется один раз на каждом таком entry относительно remaining stack текущего thread; неуспешная проверка вызывает panic `stack_exhaustion` либо documented termination, никогда не memory UB, а calls между функциями R не несут дальнейшей проверки.

Для литерала `null` параметр `null_t` является точным совпадением, а nullable pointer parameters — совместимыми преобразованиями. Типизированный nullable-указатель выбирает указательную перегрузку, даже если содержит null. Параметр `null_t` отвергает другие типы и вычисляемые выражения. Без точного совпадения два nullable pointer candidates остаются неоднозначными. Литерал `null` не предоставляет кандидата для вывода generic-типа. Другие аргументы могут определить тип параметра с nullable-указателем; после этого применяются обычные правила преобразования null и передачи аргументов. Кандидат с невыведенным типовым параметром не применим. Generic-функция может явно объявить параметр `null_t` вместе с независимо выводимыми value-параметрами.

<a id="R-FUNC-0005"></a>

**R-FUNC-0005** — `unsafe` function may содержать unsafe operations без вложенного unsafe block, но вызов такой function сам является unsafe operation. Её signature shall документировать safety contract.

<a id="R-FUNC-0006"></a>

**R-FUNC-0006** — Function returning Move value transfers owner to caller. Function returning borrow shall satisfy lifetime relationship section 13; returning borrow derived from local or by-value parameter storage запрещено.

<a id="R-FUNC-0007"></a>

**R-FUNC-0007** — Однозначное имя закрытой безопасной R-функции в позиции значения создаёт function item: Copy-значение номинального типа, идентифицирующего эту функцию, без захватов и runtime-указателя на функцию. Его можно сохранить через `auto`, преобразовать к типу функции (R-TYPE-0054), передать в generic callable-параметр или вернуть через подходящий `opaque`-контракт. Generic-функция, закрытая `name::<arguments>` (R-TYPE-0036), и метод либо associated function, названные `Owner::name` или `Owner<args>::name`, являются закрытыми именами; function item метода принимает receiver первым параметром, поэтому `auto get = Point::get;` вызывается как `get(&point)`. Открытое generic-имя, неразрешённое семейство перегрузок, unsafe- или C-функция, variadic-функция либо метод, выбранный через receiver без вызова, не образуют function item. Синхронное значение удовлетворяет подходящим режимам `shared`, `mut` и `once`; асинхронное — только `async fn once`. Типы параметров и результата, checked errors и ресурсные гарантии следуют R-TYPE-0044. `item(args)` и `item.call(args)` вычисляют значение функции перед аргументами и вызывают исходную функцию. Сохраняются её идентичность статического хранения, borrow-контракт, checked errors, cleanup, транзакция async-start и ребро статического графа вызовов. Создание и копирование значения не вызывают функцию и не выделяют среду в памяти.

Указатели `raw fn(P...) -> R` существуют только для C FFI; nullable-форма — `raw fn?(P...) -> R`. Вызов через обе формы unsafe; R-FUNC-0004 включает каждую подходящую `@callback`-функцию как возможную цель (R-FFI-0025). Function item не преобразуется неявно в raw-указатель. Распознаваемые компилятором designator-contexts `std.thread::spawn`, `std.thread::spawn_scoped`, `std.sync::call_once`, `std.sync::call_once_force` и `std.sync::get_or_init` сохраняют точные контракты R-LIB-0010/R-LIB-0015. Тип raw C-функции никогда не содержит checked errors.

<a id="R-FUNC-0008"></a>

**R-FUNC-0008** — Hosted program shall define exactly one exported entry point одной из forms `i32 main()`, `i32 main(const str[] args)`, `async i32 main()` либо `async i32 main(const str[] args)`. Asynchronous forms требуют profile `hosted-native-async`. В тестовом режиме точку входа добавляет реализация (R-FUNC-0025). Runtime создаёт process executor и предоставляет owned immutable argument snapshot до запуска asynchronous root. После успешной проверки required dynamic-provider readiness и до initialization любого R static object каждый hosted launch shall преобразовать каждый предоставленный native process argument согласно target mapping R-IDB-022 в well-formed UTF-8 без replacement, truncation либо byte substitution и зарезервировать один owned immutable startup snapshot независимо от того, получает ли выбранная форма main параметр `args`. Snapshot никогда не пуст. Его element zero является converted native argument-zero spelling, когда host предоставляет его; иначе runtime синтезирует target-documented well-formed UTF-8 executable spelling. Каждый последующий element соответствует по порядку и byte-for-byte одному remaining converted native argument. Runtime сначала validates representability и computes required size полного vector, включая synthesized element zero, без materialization R `str`; только после прохождения всех elements он reserves и populates единую backing allocation snapshot. Если argument непредставим, runtime сообщает allocation-free pre-main category `argument_encoding_failure`; если backing snapshot невозможно зарезервировать, он сообщает `allocation_failure`. В обоих случаях runtime использует emergency diagnostic path, abruptly terminates с distinct category-specific status из R-IDB-022 и не shall инициализировать invalid `str`, инициализировать R static object либо вызывать `main`. Source slice `args` может использоваться во время initial execution, но по R-BORROW-0024 shall быть copied в owned storage до того, как value останется required через `await`. `str` is a distinct UTF-8-invariant refinement type with the same runtime representation and read-only byte access as `const u8[]`, not the same type identity; conversions follow R-EXPR-0015 and R-ARRAY-0007. Returned i32 maps to environment status according to target documentation. Если runtime не может reserve executor либо root frame, ни одна R main-body instruction не выполняется; runtime сообщает exact category start-error `allocation_failed` либо `runtime_stopping` через configured panic handler или его allocation-free emergency path, затем abruptly terminates с target-documented nonzero status. Поскольку root task не стал live, этот path не выполняет R static/module destruction. Ни одна из четырёх форм `main` не может явно объявлять `throws`. Каждая имеет следующий неявный закрытый набор checked-ошибок с точным номинальным совпадением типов:

`core::utf8_error`, `std.alloc::alloc_error`, `std.async::start_error`, `std.bits::read_error`, `std.bytes::bytes_error`, `std.c::runtime_error`, `std.c::string_error`, `std.convert::parse_error`, `std.convert::range_error`, `std.env::env_error`, `std.error::error`, `std.format::format_error`, `std.fs::fs_error`, `std.fs::path_error`, `std.io::io_error`, `std.math::math_error`, `std.net::address_error`, `std.net::net_error`, `std.process::process_error`, `std.string::boundary_error`, `std.string::string_error`, `std.sync::barrier_error`, `std.thread::thread_error`, `std.time::duration_error`, `std.time::time_error`.

Только эти Copy-семейства без владеющих payload могут достигать границы программы. Их скалярные диагностические поля не несут владение. Замкнутые владеющие семейства `std.alloc::new_error<T>`, `std.array::push_error<T>`, `std.dict::insert_error<K,V>` и `std.list::push_error<T>`, все пользовательские ошибки и любые другие типы ошибок по-прежнему требуют matching catch до этой границы. Генерируемая компилятором точка входа C17 shall обрабатывать каждый допущенный тип отдельной именованной, статически проверенной веткой и потреблять payload ровно один раз; уничтожение Copy-payload не имеет наблюдаемого drop-действия. Она shall без аллокаций преобразовать ошибку в portable domain/code representation R-SLIB-ERR-0002/0003, получить имя через `std.error::name` и вывести domain, name, code и native_code через emergency diagnostic sink. Вызывать `std.error::diagnostic` запрещено. Неизвестный carrier tag либо тип ошибки является внутренним нарушением целостности кодогенерации, а не документированным статусом завершения. Panic обходит эту checked-error boundary.

R-IDB-023 резервирует документированный таргетом диапазон статусов этой границы, отдельный от разрешённых статусов приложения R-IDB-007. Сохраняются четыре i32-формы; void-форма точки входа не вводится. Это позволяет отличать допустимый возврат приложения от ошибки подложки по одному статусу процесса. Возвращённое значение вне диапазона shall приводить к allocation-free диагностике `invalid_main_status` и специальному зарезервированному статусу, затем к normal termination; запрещено молча усекать его до успеха либо другого статуса приложения. Таргет shall документировать этот статус и диапазон приложения. Программы, возвращающие разрешённые статусы, сохраняют поведение.

<a id="R-FUNC-0009"></a>

**R-FUNC-0009** — Defining module complete nominal K может предоставить dictionary key contract через ровно две associated definitions: `u64 K::hash(const K* value)` и `bool K::equal(const K* left, const K* right)`. Pair неделима: definition только одной operation, `unsafe` либо `extern`, другая signature, `protected` или definition вне module, определяющего K, являются constraint violation. Qualified names являются associated operations и methods с shared receiver по R-FUNC-0013, а не overloads, virtual methods или implicit operators; `K::equal` не разрешает `left == right`. Pair наследует visibility K без отдельного source marker. Для exported K обе definitions и их verified effect summary входят в module interface fingerprint.

Обе operations shall terminate для каждого valid input и shall зависеть только от logical values, reachable через их shared-borrow parameters. Их полный permitted transitive effect set состоит из: чтения literals, constants и parameter-reachable immutable subobjects; local computation; calls `core::hash`, `core::key_equal` либо соответствующих associated key operations component types. Compiler shall диагностировать любой statically present effect вне этого closed set, включая checked throw или call с nonempty throws set. `K::equal` shall быть equivalence relation, а когда она возвращает true, оба operands shall produce одинаковое value `K::hash`. Эти semantic requirements делают repeated lookup deterministic; program, associated definitions которого нарушают их, non-conforming.

Хуки generic-типа K повторяют параметры его схемы и их ограничения; пара может добавить к параметрам ограничения-способности, например `key`. Инстанс K, аргументы которого их не доказывают, не имеет ни одного из хуков и не имеет key contract. Хук clone из R-OWN-0020 может добавлять ограничения так же. Пару определяет `@derive(key)` (R-AGG-0012).

<a id="R-FUNC-0010"></a>

**R-FUNC-0010** — В `async T operation(P...) throws E...` T является logical return type, а E…​ — canonical checked completion-error set для type-check body; source return и throw statements ведут себя точно как в synchronous function. Declaration shall not иметь `extern` linkage. Call начинает two-phase start transaction, produces `task<T throws E...>` и сам имеет дополнительный immediate checked effect `std.async::start_error`. В таком start named Move operand permitted только когда полный argument expression является direct form `move place`; nesting named Move в aggregate, constructor либо другом argument expression требует `R-DIAG-MOVE-003`. Callee и arguments evaluated слева направо по R-EXPR-0020; каждый ordinary effect происходит в specified point, но evaluation direct named argument `move place` stages source без изменения его initialized ownership state и устанавливает exclusive staged-move reservation для source и каждого overlapping place до конца transaction. Последующий argument shall not читать, писать, borrow, move либо повторно stage overlapping storage; нарушение требует `R-DIAG-BORROW-001`. При unwind strategy panic во время argument evaluation освобождает каждую staged-move reservation, оставляет каждый такой named source initialized и выполняет ordinary temporary cleanup. При abort strategy он terminates по R-ERR-0005 без дополнительного R cleanup. После successful evaluation implementation validates и reserves все frame/executor resources без commit named Move operand. Затем она либо throws `std.async::start_error::allocation_failed` либо `std.async::start_error::runtime_stopping`; тогда body не запускалось, а каждый named Move argument остаётся initialized и unchanged; либо atomically converts каждую staged-move reservation в её move, commits все argument transfers, publishes и returns task. Каждый failed start освобождает все staged-move reservations до exposure error. Committed task является eager: body становится independently eligible to execute до следующего наблюдения caller. Каждый temporary, созданный при preparation failed call, остаётся owned своим ordinary full expression и уничтожается по R-AM-0010.

<a id="R-FUNC-0011"></a>

**R-FUNC-0011** — Asynchronous frame owns каждый committed by-value parameter и каждый local, lifetime которого пересекает suspension. Каждое такое value уничтожается ровно один раз в ordinary reverse-scope order после окончания его live range, после terminal result transfer либо во время cancellation cleanup. Parameter types user `async` function, каждый checked error payload и каждый local, live через `await`, shall соответствовать R-BORROW-0024. Value, которое `hosted-native-async` executor может transfer между threads, shall быть Send; compiler-generated frame metadata records и verifies это requirement. Guard блокировки `std.sync` не является ни Send, ни unborrowed, поэтому shall not оставаться live через `await` (`R-DIAG-ASYNC-001`); диагностика называет `std.async::mutex` или `std.async::rw_lock` из R-SLIB-ASYNC-0013, чьи guards могут. Runtime shall not expose partially initialized frame либо выполнять body до successful start commit.

<a id="R-FUNC-0012"></a>

**R-FUNC-0012** — `await move task_name` или `await function(arguments)` logically ждёт task без blocking executor thread. Если task incomplete, operation suspends current async frame и arranges его continuation; если completed либо после resume, она consumes unique observation right. Для `task<T throws E...>` с non-void T returned T напрямую initializes destination local; completion с E transfers exact error в await point, поэтому E shall быть caught либо входить в throws set enclosing function. Для `task<void throws E...>` successful statement не имеет value. При unwind observed task panic re-raised в await point после required cleanup completed frame; при abort originating panic уже завершил process.

`std.async::cancel(move operation)` consumes любой task, explicitly requests target-native cancellation и relinquishes observation. `std.async::detach(move operation)` consumes любой task и relinquishes observation без cancellation request. Ordinary implicit drop разрешён только для effect-free `task<T>` и requests cancellation. Effectful task является must-resolve value: каждый normal, return, break, continue и checked-throw path shall consume его через await либо explicit cancel либо explicit detach, включая enclosing finally. Panic unwind и cancellation containing frame могут выполнить forced cleanup и не являются successful source-level resolution. Во всех случаях runtime ownership сохраняет frame, native resources, buffers и handles live до single terminal completion R-AM-0014. Returned value либо checked-error payload detached или cancellation-race result drops ровно один раз; unobserved panic доставляется configured panic handler. Cancellation containing async frame сначала получает terminal/native acknowledgement от каждой current native operation или awaited child, для которой runtime запросил cancellation. Только после этого acknowledgement runtime выполняет каждый active finally ровно один раз inner-to-outer и frame drops до terminal publication. Cancellation во время finally latches и не может interrupt либо повторить его. `await` запрещён в finally, explicit cancel разрешён.

<a id="R-FUNC-0013"></a>

**R-FUNC-0013** — Method объявляется как `Ret Owner::name(receiver, parameters)` в module, определяющем complete nominal Owner, либо как член trait или implementation. Receiver — первый parameter с именем `this` и ровно одной из форм `const Owner*`, `Owner*` и `Owner`; внутри trait либо implementation owner записывается как `Self`. Declaration без receiver является associated function своего owner. Для generic owner заголовок `@generic` method повторяет параметры и нормализованные constraints owner как свой префикс по R-TYPE-0037, а дополнительные параметры method следуют за ними; `Owner<args>::name(arguments)` задаёт этот префикс написанием owner, поэтому associated function generic owner не нуждается в value-параметре, упоминающем его. Имя method shall отличаться от каждого имени field и variant своего owner и от предобъявленных intrinsic names. Method не имеет C linkage, не является целью `@callback` и может быть `unsafe` либо `async`. `protected` применяется к inherent method и не допускается для члена trait либо implementation; в остальном inherent method наследует visibility своего owner. Associated key и JSON operations R-FUNC-0009 и R-JSON-0001 являются methods со своими существующими закрытыми контрактами.

<a id="R-FUNC-0014"></a>

**R-FUNC-0014** — `receiver.name(arguments)` и `receiver->name(arguments)` вызывают method `name` owner type получателя, передавая receiver первым argument. Shared receiver заимствует place как shared, exclusive receiver заимствует его exclusively и требует mutable place, а consuming receiver принимает value, поэтому именованный Move receiver требует `move`. Форма `->` требует non-null borrow owner и не принимает consuming receiver. Если имя выбирает field owner, выражение является member access и последующим вызовом по R-FUNC-0007. Method-call suffix является последним postfix suffix своего выражения, если метод не объявлен `@chain` (R-FUNC-0024); его arguments и результат допускают композицию по R-EXPR-0020 и R-STMT-0005. Associated function вызывается только через qualified name. На заимствовании dyn-интерфейса метод — прототип, предоставленный его контрактом, а вызов достигает реализации члена, из которого заимствование было преобразовано (R-TYPE-0051). Иначе вызов является прямым вызовом разрешённого definition и входит в static call graph R-FUNC-0004 одним ребром.

Зарегистрированные стандартные методы являются альтернативной записью стандартных операций с теми же правилами доступа, checked errors и владения. И стандартные, и обычные методы допускаются во вложенных runtime-выражениях по существующим правилам receiver и доступа. Свободные функции не становятся extension methods неявно. Потребляющий именованный receiver записывается `(move owner).method(…​)`; скобки относятся к синтаксису метода и не меняют транзакции запуска R-FUNC-0010. Прямой вызов метода допускается после await по R-STMT-0012.

<a id="R-FUNC-0015"></a>

**R-FUNC-0015** — `fn [attributes] [shared|mut|once] Ret name(parameters) [move(names)] [throws E...] block` внутри блока объявляет именованную lambda с уникальным nominal closure type и методом `call`. Синхронная сигнатура следует R-TYPE-0044, включая заимствованные параметры, результаты и checked errors. Lambda не является выражением, аргументом или модульным объявлением и не может быть unsafe. Имя входит в scope после объявления и не может вызывать себя. Обычный static call graph включает её тело. Closure type нельзя записать в исходнике; значение хранится в имени lambda, `auto` local или ограниченном generic параметре. Замыкание является Copy ровно тогда, когда все captures являются Copy.

Lambda разрешена внутри обычной, async или generic-функции. Generic lambda-тело проверяется при определении, даже если не используется; captures и private environment schema подставляются вместе со схемой внешней функции. Идентичность, cleanup и генерируемые функции детерминированы. Синхронное замыкание может заимствовать данные для локальной обработки внутри async, но живое заимствование не пересекает suspension.

`async fn` объявляет async lambda. Разрешён только `once`, также используемый по умолчанию. Среда передаётся транзакционно и требует Send и unborrowed, как параметры, результат и completion-error payloads. Не включённый в move capture является заимствованием и не удовлетворяет этим требованиям. Start-error сохраняет именованные Move-аргументы, включая consuming receiver; успех передаёт их задаче. Завершение, отмена и cleanup среды следуют обычным async-правилам. Контекстные слова режимов остаются допустимыми именами типов; режим распознаётся перед полным типом результата и именем lambda.

<a id="R-FUNC-0016"></a>

**R-FUNC-0016** — Body lambda обращается к locals и parameters enclosing body по имени; каждое такое free name является capture и становится field closure environment. Имя, перечисленное в `move(names)`, захватывается by value: declaration consumes названный local либо parameter по R-OWN-0003, а body получает доступ к захваченному value согласно режиму вызова. Copy captures сохраняют инициализированный источник. Захват borrow по значению сохраняет его pointee type и provenance; переменная указателя не заимствуется и не разыменовывается. Любой другой capture — borrow, формируемый в declaration: exclusive, когда body присваивает имени, инкрементирует либо декрементирует его, берёт его адрес или вызывает на нём method, и shared в остальных случаях. Явный `shared` создаёт только shared capture borrows; изменение через const окружение отклоняется. Environment удерживает эти borrows, пока имя lambda live, поэтому правила borrow раздела 13 действуют между declaration и последним использованием имени. Имя, объявленное внутри body lambda, скрывает enclosing name во всём своём block; capture скрывает каждое module-level name того же написания внутри body; имя, которое enclosing body не может разрешить, capture не является. Lambda может захватить другую lambda и enclosing `this`.

<a id="R-FUNC-0017"></a>

**R-FUNC-0017** — `name(arguments)`, где `name` обозначает closure local, parameter либо capture или parameter type, ограниченного по R-TYPE-0044, через value либо borrow, вызывает method `call` closure type с environment в качестве receiver: shared для `shared`, exclusive для `mut`, по значению для `once`. Без явного режима lambda выбирается mutable, если body изменяет захваченное значение или capture exclusive, иначе shared. Именованный Move closure с consuming receiver требует `(move name).call(arguments)`: окружение потребляется и уничтожается ровно один раз, а последующее использование отклоняется. Copy closure сохраняет обычные правила Copy даже с `once`: `move` копирует его и сохраняет источник. Потребление closure не разрешает произвольное перемещение captured fields; R-OWN-0019 даёт replace и optional take. Generic thread entry выводится структурно из value arguments по обычным правилам. Запуск task/thread резервирует именованные Move arguments и передаёт их только после успешного start по R-FUNC-0010. Вызов прямой по R-FUNC-0014; arguments допускают композицию по R-EXPR-0020. Создание и синхронный вызов closure не вводят indirect call, function pointer или heap allocation; captures и body сохраняют собственные эффекты. Async-вызов имеет обычные эффекты выделения task и cleanup. Interface schema 31 записывает callable constraints структурно: режим, типы параметров и результат; synthetic callable traits и implementations не экспортируются как trait records.

<a id="R-FUNC-0018"></a>

**R-FUNC-0018** — `T... name` в качестве последнего parameter ordinary R function объявляет variadic parameter типа `const T[]`; `T` shall быть complete value type, не `void` и без exclusive borrow и exclusive slice, поэтому упаковывать можно runtime `str`, shared borrows, shared slices и aggregates, содержащие их, а function shall не быть generic, `async`, `extern "C"` либо `@callback`; в generic-функции `T... name`, где `T` — пакет, объявляет параметр-пакет R-TYPE-0053. Declaration и каждая compatible redeclaration записывают parameter одинаково, а variadic form является частью function type и interface fingerprint. Call передаёт fixed parameters первыми; остальные arguments, zero или больше, — packed arguments: каждый вычисляется с `T` в качестве context и shall иметь type `T`, named Move argument записывается с `move` по R-OWN-0003, а packed arguments инициализируют по порядку hidden fixed array `[T; n]` caller, shared slice которого передаётся для `name`; `n = 0` передаёт empty slice. Hidden array живёт до возврата из call и затем dropped, поэтому result call shall не borrow storage `name` (`R-DIAG-BORROW-002`); result, несущий только то, на что указывают elements (R-TYPE-0045), заимствует packed arguments, а для spread — elements его operand. Иначе последний argument — spread `...operand`, где `operand` — выражение, обозначающее slice `const T[]` либо `T[]`, `array<T>` либо fixed array `T`; spread передаёт эту sequence как shared slice без copying, следует ровно за fixed arguments и не допускается ни в каком другом call; spread кортежа или пакета следует R-TYPE-0053. Spread slice, array либо fixed array для non-variadic parameter, spread, не являющийся единственным packed argument, packed argument другого type, variadic parameter function исключённого вида и element type с exclusive borrow или slice требуют `R-DIAG-TYPE-001`. Правило не вводит C variadic functions: R-FUNC-0004 и R-FFI-0005 не меняются.

<a id="R-FUNC-0019"></a>

**R-FUNC-0019** — Атрибуты функций без аргументов `@noalloc` и `@nonblocking` задают независимые транзитивные контракты ресурсов. Они допустимы на обычных и async-функциях, методах, generic-функциях, drop-хуках и импортируемых C-функциях. Каждый атрибут встречается не более одного раза; совместимые повторные объявления имеют одинаковые контракты. Calling convention, layout и владение не меняются.

`@noalloc` требует доказательства отсутствия heap-аллокаций при исполнении. `@nonblocking` требует доказательства отсутствия ожидания на OS thread, включая потенциально блокирующие операции allocator, синхронизации и непрозрачного runtime. Контракты не следуют друг из друга и не гарантируют чистоту, ограниченное время исполнения, real-time scheduling или отсутствие panic. Они охватывают нормальное исполнение и восстанавливаемые ошибки; терминальная диагностика panic и завершение процесса находятся вне этих контрактов.

Проверка распространяется на вызываемые функции (через dyn-интерфейс — на реализацию каждого члена, R-TYPE-0051), associated-хуки, явное и неявное уничтожение, cleanup при замене и checked-error exits, включая catch/finally. Неаннотированная R-функция проверяется по телу. Generic-определение проверяется и без инстанциации; constraint Copy исключает пользовательское уничтожение, но не обещает свойства ресурсов других операций. Компилятор вправе консервативно учитывать все runtime-ветви и cleanup всех владеемых типов; `if (false)` не снимает обязательство. Неизвестный эффект не считается отсутствующим. Стандартная операция допускается только по проверенному compiler resource registry; новая или незарегистрированная операция остаётся неизвестной.

Для async-функции `@noalloc` проверяет тело и его cleanup, но не launch wrapper вызывающей стороны. Аллокация запуска проверяется в точке вызова независимо от атрибута async-функции. Регистрация в executor, await и отмена требуют отдельного доказательства. Текущие executor с mutex и cleanup frame не обеспечивают `@nonblocking`; само свойство async такой гарантии не даёт. Atomic-операции также не гарантируют отсутствие блокировок на каждой целевой платформе.

Атрибут ресурса на C-импорте задаёт обязательство доверенной FFI-границы, а не доказательство свойств невидимого C-кода. Проверки safety и ABI сохраняются. Прямой вызов именованной C-функции использует этот контракт. Значение raw-функции с сигнатурой без resource contract остаётся неизвестным, даже если отдельное присваивание указывало на аннотированную функцию. Проверяются тела R callback и их C-entry wrappers. Hosted attachment и detachment могут брать блокировки и не доказывают неблокирующее исполнение; возможный thread-local cleanup включается в доказательство отсутствия аллокаций.

Неудачное доказательство требует `R-DIAG-RESOURCE-001` для `@noalloc` либо `R-DIAG-RESOURCE-002` для `@nonblocking` с цепочкой вызова или cleanup. Interface schema 31 экспортирует `noalloc=true/false` и `nonblocking=true/false` у функций и generic-схем и включает их в fingerprints. Флаги описывают объявленные контракты, а не выведенные обещания неаннотированного кода. Бинарный ABI и C-символы стандартной библиотеки не меняются.

Callable и raw C signatures могут содержать те же атрибуты без аргументов сразу после `fn`. Гарантии охватывают вызов и уничтожение closure environment. Совместимая сигнатура может забыть любую гарантию, но не усилить её; остальные правила сигнатуры и доступа сохраняются. Generic callable constraints используют обещания при проверке определения. Атрибуты lambda проверяются по телу и cleanup captures, включая checked выходы. Raw imports остаются доверенной границей, а доказательство для R callbacks включает boundary wrappers.

<a id="R-FUNC-0020"></a>
**R-FUNC-0020** — Атрибут без аргументов `@must_use` помечает значимым результат именованной
функции либо nominal struct, enum или error type. Он разрешён на generic-объявлениях,
associated-функциях, async-функциях и C imports, но не на полях, параметрах, переменных,
drop hooks или lambda-заголовке. Функция должна выдавать значение (async выдаёт task);
синхронная void-функция требует хотя бы один выходной параметр по R-FUNC-0022,
а never-функция не может иметь этот атрибут. Повторы атрибута и
несогласованные переобъявления требуют диагностики.

Сохранённый в локальную переменную non-void результат вызова, результат await и
значение типа `@must_use` значимы. Каждая успешная инициализация или разрешённое
присваивание создаёт отдельную обязанность использования. На каждом достижимом
обычном пути результат должен быть использован до перекрывающей замены или конца
области его хранилища; иначе требуется `R-DIAG-USE-001`. Использование последующего
значения не подтверждает использование предыдущего. Чтение, в том числе константным
выражением при трансляции, например границей массива или статическим условием (R-EXPR-0022,
R-META-0002), projection, заимствование, move, передача, return, явное уничтожение или
`as void` подтверждают использование.
Обновление компонента использует обычный результат-агрегат как хранилище; замена
локальной переменной целиком по-прежнему требует подтверждения использования.
Неявная очистка при обычном выходе из области — нет. Ветвления, циклы и finally
сохраняют это различие. Непосредственное игнорирование вызова по-прежнему регулируют
R-STMT-0001 и R-FUNC-0021.

Исключительное разворачивание и отмена задачи могут уничтожить хранилище до
использования результата; дополнительное подтверждение на таких путях не требуется.
Panic завершает путь по существующему контракту завершения. Обычные ранние return,
break и continue не снимают обязанность. Ошибка, перехваченная внутри области переменной,
не снимает обязанность для остающегося
живым хранилища. Неудачное получение не создаёт результата. Результаты, созданные
телом finally, проверяются обычным образом в собственной области этого тела.
Await подтверждает использование task и создаёт независимую обязанность для своего
сохранённого non-void результата. Generic-определения проверяются и без инстанциации.
Значимость сохраняется при преобразовании и условном выборе результата; она не задаёт
прикладной протокол обработки или линейную обязанность через произвольные копии и
обёртки. Атрибут не переносится через стёртую raw function signature. Copy/Move,
проверка borrow и правила разрешения task остаются обязательными. R-INIT-0014
независимо запрещает безусловную замену локального значения.
Interface schema 31 записывает `must_use=true`; отсутствие означает false.

<a id="R-FUNC-0021"></a>

**R-FUNC-0021** — Атрибут без аргументов `@discardable` помечает результат именованной функции как допускающий игнорирование в точке вызова. Он разрешён там же, где `@must_use` разрешён на функции: на generic-объявлениях, associated-функциях и методах, прототипах трейтов, async-функциях и C imports; он не разрешён на nominal types, полях, параметрах, переменных, drop hooks, callable-ограничениях и lambda-заголовке. Функция должна выдавать логический результат, отличный от void, либо быть синхронной void-функцией хотя бы с одним выходным параметром; для async-функции логический результат — результат await, а не task. `@discardable` вместе с `@must_use` на одной функции, `@discardable`-результат (включая выходной параметр) типа с `@must_use`, повторы атрибута и несогласованные переобъявления требуют диагностики. Реализация наследует атрибут от прототипа трейта.

Expression statement, являющийся прямым именованным, квалифицированным вызовом или вызовом метода `@discardable`-функции, и await-оператор, форма вызова которого именует `@discardable` async-функцию, вычисляют вызов один раз и уничтожают результат ровно так, как `as void` по R-EXPR-0023. Ничего иного не ослабляется: checked errors, panic, borrow checking, Copy/Move и разрешение effectful task сохраняют свои обязательства, и атрибут не отбрасывает ни одной task. Разрешение не следует за значением в local, операнд оператора, условное выражение, callable-ограничение или raw function signature; они остаются под R-STMT-0001 и R-EXPR-0023, а local, инициализированный из `@discardable`-вызова, — обычный local. Interface schema 31 записывает `discardable=true`; отсутствие означает false.

<a id="R-FUNC-0022"></a>
**R-FUNC-0022** — `out T name` объявляет выходной параметр синхронной функции R.
Соответствующий аргумент записывается `out place`. `out` контекстен в этих двух
позициях; это не общий конструктор типа и не обычный входной borrow. `T` должен быть
полным, населённым, изменяемым типом значения без заимствований (`unborrowed`). Режим
входит в сигнатуру функции, трейта и callable и сохраняется при generic-подстановке
и в интерфейсах модулей. Async-функции и границы C этот режим параметра не принимают;
выходной параметр не может быть variadic. В списке аргументов контекстный маркер
предшествует назначению, начинающемуся с имени или `*`; внутри назначения допустимы
подвыражения в скобках.

Функция начинает работу с частной неинициализированной локальной переменной `T`.
Чтение и проекция до инициализации целого значения некорректны. Каждый успешный возврат,
включая достижение конца void-тела, обязан инициализировать все выходы. Переприсваивание
и Move подчиняются обычным правилам владения. Выход остаётся частным до завершения всех
активных finally. Успешный возврат передаёт все выходы вызывающей стороне, заменяя и
обычно уничтожая прежние значения назначений. Выражения назначений вычисляются один
раз, в порядке аргументов; перекрывающиеся выходы и конфликтующие входные borrow запрещены.

Выход с checked error уничтожает все инициализированные частные выходы и не публикует
ни одного из них. Назначения вызывающей стороны сохраняют прежние состояния
инициализации и владения. Это правило не откатывает прочие внешние эффекты. Паника
следует существующему контракту завершения и не вводит unwinding. Каждый успешно
переданный выход создаёт обязательство использования значимого результата. Для выходов
в локальное хранилище компилятор обязан отслеживать каждую успешную запись отдельно
через ветвления, циклы, catch и finally. До перекрывающей записи или выхода из области
каждый достижимый обычный путь обязан подтвердить использование чтением, borrow, move, передачей
дальше, явным drop или `as void`. Неявного уничтожения при обычном выходе недостаточно.
Исключительное разворачивание, panic и отмена подчиняются исключениям из R-FUNC-0020. Публикация частного
выхода при успешном возврате передаёт значение дальше. Запись через входной изменяемый
borrow или в нелокальное хранилище передаёт значение владельцу этого хранилища. Анализ
консервативен: он не обязан доказывать корреляции независимых условий и равенство
runtime-индексов; чтение или явное отбрасывание содержащего значения подтверждает его
выходы. Неудачный вызов не создаёт нового обязательства и не снимает прежнее.

При именованном вызове `@discardable` разрешает игнорировать выходы; это разрешение
не переносится через function item, opaque callable или callable constraint. Оно не
может отменить `@must_use` типа выхода. Interface schema 31 представляет режим как
`(out T)`. Обычный изменяемый указатель не имеет контракта out. R-ERR-0003 по-прежнему
запрещает ошибку, покидающую finally.

<a id="R-FUNC-0023"></a>

**R-FUNC-0023** — Функция translation-time evaluable, когда её проверенное тело доказывает, что вызов с известными аргументами не имеет эффекта вне вызова; атрибут для этого не нужен. Такая функция — синхронная безопасная R-функция с телом, не import, не C export, не callback, не тело lambda, не trait prototype и не тело, созданное компилятором, кроме преобразования `std.string`; у неё нет output-параметров; каждая её checked error имеет constant value type либо является стандартной ошибкой операций owners ниже (`std.alloc::alloc_error`, `std.array::push_error<T>`, `std.list::push_error<T>`, `std.dict::insert_error<K,V>`, `std.string::string_error`, `std.string::boundary_error` или `std.math::math_error`); её параметры имеют constant value types либо являются shared или exclusive borrows или slices таких типов, а результат имеет constant value type или `void`. Constant value types — целочисленные типы, включая целочисленные C ABI types, типы с плавающей точкой формата IEC 60559 binary32 или binary64 (R-TYPE-0006), `bool`, `char`, fieldless enumerations, `str` и `constexpr str`, фиксированные массивы constant value types, structs и tagged enumerations без функции `drop`, поля и payloads которых имеют constant value types, включая family ошибки (R-AGG-0011), `o<T>` от constant value type и стандартные owners `array<T>`, `list<T>`, `dict<K,V>` и `std.string::string`, элементы, ключи и значения которых имеют constant value types, а K — целочисленный тип, тип с плавающей точкой, `bool`, `char`, fieldless enumeration, строка или `std.string::string`. После generic closing тело содержит только: локальные объекты этих типов, borrows или slices на них и options таких borrows; blocks, `if`, `while`, `for`, range `for` по целым и по owners, `switch` с константными или variant labels, `match`, `break`, `continue` и `return`; `throw` и `try` с `catch` и `finally`; literals, enumerators, variants, `sizeof`, `alignof` и `len`; чтение своих параметров и локалов, констант модуля и `const`-объектов модуля constant value types; element, field, payload и dereference places, borrows и slices своего storage; операторы, conversions между целочисленными, floating, `char`, `bool` и enumeration types и views строки как `str` или как её байтов; assignments и compound assignments своего storage; aggregate и array initializers; `panic`; `core::wrapping_*`, `core::saturating_*`, `core::enum_name` и `core::enum_ordinal`; точные операции `abs`, `floor`, `ceil`, `trunc`, `round`, `copy_sign`, `min`, `max`, `next_after`, `is_finite`, `is_infinite`, `is_nan`, `is_normal` и `sign_bit` из `std.math` над этими типами с плавающей точкой и их корректно округлённые `sqrt` и `remainder`, ошибки области которых бросаются как `std.math::math_error`; операции owners из Library R-LIB-0019 — R-LIB-0023 и R-SLIB-STRING-0001 — R-SLIB-STRING-0003, кроме `capacity` и `std.string::from_bytes`; и вызовы evaluable функций. Любая другая операция оставляет функцию run-time функцией, включая доступ к объекту со static или thread storage duration, кроме константы, иную allocation, owners `own`, `arc` и `rc`, значения с плавающей точкой другого формата, остальные операции `std.math`, форматирование, `unsafe`, raw pointers, atomic и volatile access, callable values, tasks, `await`, threads, I/O и другие стандартные операции; вызов run-time функции действует так же. При трансляции allocation не отказывает: её ограничивают лимиты R-IDB-010, поэтому ветвь catch ошибки allocation там не выполняется, а capacity owner, которая следует политике роста во время выполнения, не наблюдается. Evaluability — свойство всей цепочки вызовов; interface schema 31 экспортирует его как `consteval=true` (R-MOD-0006).

<a id="R-FUNC-0024"></a>

**R-FUNC-0024** — Атрибут без аргументов `@chain` помечает метод, вызов которого может продолжаться следующими postfix suffixes того же выражения, как в `Options::create().with_port(9000u16).with_capacity(128usize)`. Он допустим у синхронного метода с receiver, результат которого не `void` и не `never`, включая generic-метод, prototype trait и член реализации; у любой другой функции, а также повторный, он требует `R-DIAG-SYN-002`. Реализация наследует атрибут от prototype своего trait, а у реализации prototype без атрибута он требует `R-DIAG-TRAIT-001`; расходящиеся prototype и definition требуют `R-DIAG-NAME-002`.

Suffix после вызова любого другого метода, включая стандартный метод, callable-поле и function item, требует `R-DIAG-SYN-001`; receiver в скобках `(a.f()).g()` остаётся доступным для любого метода. Продолженный вызов означает ровно свою форму со скобками: вызов завершается вместе со своими checked errors и panic до вычисления следующего suffix; его результат — временное значение, которое consuming receiver принимает по значению, заимствующий receiver по-прежнему требует place (R-BORROW-0020), а результат-заимствование продолжается через `->`. Через ограничение trait или dyn-интерфейс действует разрешение prototype. Атрибут не меняет ни сигнатуру, checked errors, borrow contract и владение метода, ни обязательства R-FUNC-0020. В асинхронном кадре временный consuming receiver вычисляется в скрытый объект кадра, как любой argument, а последний вызов цепочки может быть awaited (R-STMT-0012). Interface schema 31 записывает `chain=true`; отсутствие означает false.

<a id="R-FUNC-0025"></a>

**R-FUNC-0025** — Атрибут `@test` отмечает тестовую функцию: функцию области module без receiver, параметров, generic-параметров и C-связывания с типом результата `void`, синхронную или асинхронную. `@test(expect = E)` называет тип ошибки `E`, catch которого (R-ERR-0003) может принять ошибку, объявленную функцией, а `@test(allocations)` допускается у синхронной тестовой функции. Атрибут на любом другом объявлении, повторённый или с другим аргументом, требует `R-DIAG-SYN-002`. В остальном тестовая функция — обычная функция своего module.

Тестовый режим транслирует программу для запуска её тестов. Реализация выбирает его по запросу, например `r-front --test`, и он требует profile `hosted-native-async`. В тестовом режиме module точки входа не объявляет функцию с именем `main` (`R-DIAG-FLOW-001`); реализация добавляет в него точку входа `async i32 main()` R-FUNC-0008, которая импортирует `std.test` и `std.console` и выполняет тестовые функции module точки входа одну за другой в порядке объявления — синхронную вызовом, асинхронную через `await` — и печатает для каждой строки Library R-SLIB-TEST-0002: `test NAME ... ` и итог. Тест проходит, когда возвращается, а с `expect = E` — когда бросает ошибку, которую принимает catch `E`. Тест проваливается, когда бросает `std.test::failure` (её сообщение), член `std.error::fault` (его переносимое имя) или другую объявленную ошибку (имя её типа), а с `expect` — когда возвращается. Прошедший тест с `allocations` затем выполняется по разу для каждой попытки выделения N своего успешного прогона с отказом попытки N (Library R-SLIB-TEST-0003) и проваливается, если такой прогон завершается иначе, чем броском `std.alloc::alloc_error`. После последнего теста точка входа печатает итог и возвращает ноль, если прошли все тесты, и единицу иначе. Panic в тесте завершает программу так же, как любую программу. Диагностика кода, добавленного для теста, сообщается в атрибуте этого теста, а остальной части точки входа — в начале module.

<a id="R-FUNC-0026"></a>

**R-FUNC-0026** — Атрибут `@recursion(depth = N)`, где `N` — десятичный целый литерал без суффикса от 1 до 65535, ограничивает активации синхронной R-функции, метода или generic-функции с телом. Повторный или с другим аргументом он требует `R-DIAG-SYN-002`, на объявлении с C-связыванием — диагностику из R-FFI-0050, а на `async`-функции — `R-DIAG-ASYNC-001`; прототип и определение функции несут одинаковый атрибут (R-NAME-0004). Функция shall перечислять `core::recursion_error` в списке throws (`R-DIAG-EFFECT-001`). `core::recursion_error` — Copy, Send+Sync стандартная структура-ошибка (R-TYPE-0012) с единственным публичным полем `usize depth`; она не входит в неявный набор ошибок `main` (R-FUNC-0008).

На каждом потоке одновременно существует не более N активаций функции, и каждый экземпляр generic-функции считает свои. Вызов, который застаёт на своём потоке N активаций функции, бросает `core::recursion_error` с `depth` N до выполнения тела: полученные вызовом аргументы уничтожаются, как при любом throw, и ни один оператор тела не выполняется. Каждый выход из активации — return или throw — завершает её. Граница не входит в тип функции: вызов через значение функции или член dyn достигает той же границы. Функция с атрибутом не вычисляется при трансляции (R-FUNC-0023), потому что `core::recursion_error` не является константным типом значения.

Цикл статического графа вызовов (R-FUNC-0004) допустим, когда каждая функция цикла имеет атрибут. Цикл через любую другую функцию по-прежнему требует `R-DIAG-STACK-001`; диспетчер значения функции или dyn-интерфейса, drop-, clone- и format-хуки, initializer `call_once` и asynchronous start никогда его не несут, поэтому асинхронная рекурсия недоступна. В static stack bound из R-FUNC-0004 сильно связное множество C таких функций учитывает N(m) кадров каждого члена m плюс большее из наибольшего кадра C (отклонённая активация) и наибольшей границы вызываемой функции вне C; граница каждого entry, достигающего C, включает это. Ресурсные контракты R-FUNC-0019 выполняются для функций такого цикла, когда они выполняются для тела каждой из них.

<a id="aggregates"></a>

## 17. Structures, enumerations and tagged unions

### 17.1 Structures

<a id="R-AGG-0001"></a>

**R-AGG-0001** — `struct Tag { fields };` defines complete nominal type. `Tag` входит в aggregate type-name space сразу после declaration identifier и до field body, поэтому разрешённые self-references записывают bare name. Каждое field имеет unique name и к closing brace shall иметь complete non-void type. Direct recursive value fields forbidden; recursion through pointer либо managed-owner indirection разрешена с учётом R-TYPE-0025.

`error Name { members };` определяет номинальный тип ошибки. Тело из полей с завершающим `;` имеет форму структуры; тело из вариантов enum, разделённых запятыми, имеет форму перечисления. Смешивание форм запрещено. Пустое тело имеет форму структуры. Значения дискриминантов, `Variant<T>`, `Variant { fields }` и необязательное `: underlying-type` подчиняются правилам enum; underlying type требует непустого тела с вариантами. Форма определяется грамматикой членов независимо от разрешения имён типов. `error` — контекстное слово объявления только перед именем объявления, за которым следует `{` или `:`; в остальных позициях оно остаётся идентификатором, включая имена типов, переменных и `std.error::error`. Формы объявлений `error struct` и `error enum` не вводятся. Регистрация имён, imports, visibility, attributes, доступ к полям, representation, Copy/Move, `drop`, borrowing и Send/Sync следуют правилам соответствующей структуры или перечисления. Кроме наследования R-AGG-0011, ошибки не вводят базового типа, неявных преобразований и дополнительных ограничений POD/payload. Generic error следует R-TYPE-0031 — R-TYPE-0040. Catch выбирает ошибку или её ближайшего ancestor (R-ERR-0003). Имена объявлений error регистрируются до разбора тел и доступны для локальных и квалифицированных ссылок до своего объявления.

<a id="R-AGG-0002"></a>

**R-AGG-0002** — Member access для aggregate place produces corresponding field place и сохраняет effective outermost object `const` из aggregate path либо field declaration. Member access для aggregate value допустим by value только для Copy field и produces field value со снятым outermost object `const` по R-TYPE-0008/R-EXPR-0001. Move field нельзя extract из aggregate value по R-OWN-0005. Protected fields may быть accessed только в defining module. Borrow field carries field provenance и не может использоваться после whole-object move либо drop. `value.N` и `pointer->N`, где `N` — десятичный integer literal без знака, суффикса и ведущего нуля, выбирают элемент `N` кортежа (R-TYPE-0052); индекс вне кортежа требует `R-DIAG-NAME-001`.

<a id="R-AGG-0003"></a>

**R-AGG-0003** — Struct equality is not implicit. Program shall compare fields explicitly or call library function; padding never participates in R value.

### 17.2 Enumerations

<a id="R-AGG-0004"></a>

**R-AGG-0004** — Fieldless enum syntax may specify fixed integer representation: `enum Color : u8 { RED = 1, GREEN = 2 };`. `Color` входит в aggregate type-name space сразу после declaration identifier и до variant body. Discriminant constant shall be unique and representable; first omitted discriminant is zero, each later omitted value is predecessor plus one. Without `: type`, semantic underlying type is `i32` and every discriminant shall be representable in it. Non-representable implicit/explicit value requires `R-DIAG-CONST-001`.

<a id="R-AGG-0005"></a>

**R-AGG-0005** — Without `@repr(C)`, fieldless enum layout opaque, but discriminant values stable. With `@repr(C)`, underlying type shall be a supported `c_*` integer type and every value representable by corresponding C enum contract.

<a id="R-AGG-0006"></a>

**R-AGG-0006** — Payload enum is a tagged union:

```r
error ParseError {
    i32 code;
};

enum ParseResult {
    OK(i32),
    ERROR(ParseError)
};
```

It stores exactly one active variant and optional payload. Untagged unions and reading inactive payload are absent from safe R.

<a id="R-AGG-0007"></a>

**R-AGG-0007** — Head, непосредственно за которым следует braced aggregate initializer, является constructor context, а не ordinary expression-name context. Он shall resolve ровно в один видимый complete struct type в bare либо module-qualified form или ровно в один видимый struct-like payload variant, qualified через его defining enum. Первый initializes struct по R-INIT-0004; второй atomically initializes tag и named payload fields с rollback drops. Name, разрешающееся только в ordinary value/function, opaque type, fieldless variant либо parenthesized-payload variant, не может быть head braced constructor. R-AGG-0010 отдельно допускает unheaded braced initializer только как complete operand `throw`. Unresolved либо ambiguous constructor head требует `R-DIAG-NAME-001`; uniquely resolved, но inadmissible kind требует `R-DIAG-TYPE-001`. Parenthesized constructor `ParseResult::OK(value)` atomically initializes один payload; fieldless variant называется без parentheses и braces.

<a id="R-AGG-0008"></a>

**R-AGG-0008** — Tagged enum layout is opaque unless a separately specified `@repr(C, tag=Type)` form is used. R 0.1 does not standardize that form; therefore payload enums shall not cross C ABI by value.

<a id="R-AGG-0009"></a>

**R-AGG-0009** — Enum declaration is exactly one class. It is fieldless iff every variant is bare or has `= constant`; only this class may have `: underlying-type` or numeric discriminants. If any variant has parenthesized or struct-like payload, the declaration is a payload enum: `: underlying-type` and every `= constant` are forbidden, while a bare variant is a zero-payload variant. Mixing classes requires `R-DIAG-TYPE-001`. Built-in constructors `o::none` и `o::some` получают missing type parameter только из одного expected `o<T>` type. Каждый payload expression initializes selected component subobject по R-INIT-0002; outermost object `const` этого component устанавливается только после conversion к его unqualified destination value type. Without a unique expected type, construction requires `R-DIAG-TYPE-001`.

<a id="R-AGG-0010"></a>

**R-AGG-0010** — Complete unheaded braced operand `throw { ... };` разрешён только когда statically permitted outgoing checked-error set в текущей effect context содержит ровно один type. Этот единственный candidate shall быть complete error type в форме структуры, а operand checked против него по R-INIT-0004. Empty set либо set из нескольких types требует `R-DIAG-TYPE-001`, даже если field shape operand позволил бы initialize только один из candidates. Exact members одной family считаются одной error в её корне, а корень стандартных ошибок не считается вовсе (R-AGG-0011). Selected error object строится directly в hidden propagation storage, поэтому intermediate payload object, copy или move не существует. Field expressions вычисляются в порядке исходника по R-EXPR-0020 и имеют rollback-drop behavior R-INIT-0004. Explicitly headed `throw Error { ... };` использует ordinary aggregate constructor и shall назвать один permitted outgoing error type. Fieldless либо parenthesized enum error использует ordinary named expression/constructor как `throw` operand. Named Move error operand требует `throw move error;`; temporary передаётся неявно по R-AM-0010. Construction завершается до drop вышедших locals либо входа в любой finally.

<a id="R-AGG-0011"></a>

**R-AGG-0011** — `error Name : Parent { fields };` объявляет error, parent которой — `Parent`, видимая non-generic error, объявленная со списком полей; generic error не имеет parent и не является parent. Имя parent, не обозначающее видимую error, требует `R-DIAG-NAME-001`, а parent, не являющийся такой error, error, являющаяся собственным ancestor, и generic error с parent требуют `R-DIAG-TYPE-001`. Поля parent по порядку предшествуют полям, перечисленным в объявлении, их имена shall различаться (R-NAME-0004), а initializer называет их все плоско: `net_error {.code = 5, .port = 80u16}`. Error с потомками и её потомки образуют её family, закрытую при сборке программы. Object, parameter, field, element или result, объявленный тип которого называет error с потомками, хранит любого member family: tag и inline storage наибольшего member без heap storage; он Copy, Send или Sync ровно тогда, когда таков каждый member, а его drop уничтожает хранимого member. Literal `Name { ... }` и object `auto` сохраняют exact error. Поля error читаются через такое значение; через него они не присваиваются и не перемещаются. Members family не содержат borrow, slice, runtime `str` или type, содержащий их; такой member требует `R-DIAG-TYPE-001`. Library error `std.error::fault` — корень стандартных ошибок (Library R-SLIB-ERR-0004): её family не имеет собственного member, и у неё нет полей. Interface schema 31 записывает `parent=` для error с parent и пишет type такого значения как `(error_family T)`.

<a id="R-AGG-0012"></a>

**R-AGG-0012** — `@derive(capability, ...)` перед объявлением struct, enum или error на уровне модуля добавляет перечисленные способности из закрытого набора `clone`, `equal`, `ordered`, `key` и `format`, каждую по одному разу:

- `equal` реализует `std.cmp::Equal`: два значения равны, когда содержат один вариант и их поля или полезные нагрузки равны в порядке объявления;
- `ordered` реализует `std.cmp::Ordered`: поля сравниваются лексикографически в порядке объявления, варианты — по индексу объявления, а не по значению перечислителя, затем по полезной нагрузке;
- `key` определяет хуки hash и equal из R-FUNC-0009 через `core::hash` и `core::key_equal` полей либо индекса варианта и его полезной нагрузки;
- `clone` делает доступным структурное клонирование R-OWN-0020: поля и полезные нагрузки клонируются в порядке объявления;
- `format` реализует `core::Format` (R-TYPE-0046): struct или error записывается как `Name { field: value, ... }`, а без полей как `Name {}`, enum — как имя варианта, `name(payload)` или `name { field: value, ... }`, каждое поле и полезная нагрузка — их собственным форматированием.

Реализации и хуки — объявления модуля, объявившего тип: они проверяются и транслируются как написанные и записываются в его интерфейс, а interface schema 31 дополнительно перечисляет способности как `derived=`. Generic-тип добавляет способность к каждому своему параметру-типу — как ограничение реализации (R-TYPE-0042) или хуков (R-FUNC-0009), поэтому инстанс обладает ею ровно тогда, когда её доказывают его аргументы. Error с parent выводит реализацию сначала по полям parent (R-AGG-0011). Модуль, выводящий equal или ordered, импортирует `std.cmp` так же, как `import std.cmp;`. Каждое поле и каждая полезная нагрузка, тип которых не зависит от параметра-типа, shall реализовывать trait, удовлетворять `core::Format`, доказывать `key` либо быть клонируемыми и, если тип не Copy, unborrowed; иначе `R-DIAG-TRAIT-001` называет первый такой член. Другое имя, повтор имени, пустой список и второй `@derive` требуют `R-DIAG-SYN-002`, как и вывод equal, ordered, key или format внутри статического условия или вне уровня модуля и вывод для C-агрегата. Реализация или хук, которые программа объявляет также для выводимой способности, требуют диагностики повтора этого объявления, а хук clone типа, выводящего clone, требует `R-DIAG-NAME-002`. Error с потомками хранит любого member своей family и не выводит реализации, что требует `R-DIAG-TYPE-001`; её members могут выводить. Диагностика внутри выведенной реализации или хука сообщается у имени способности, не более одной на способность.

<a id="arrays-slices-strings"></a>

## 18. Arrays, slices and strings

<a id="R-ARRAY-0001"></a>

**R-ARRAY-0001** — Array value owns exactly N elements. Array never implicitly decays to pointer. Copy/move/drop applies to whole array in increasing order for initialization and reverse order for rollback/destruction.

<a id="R-ARRAY-0002"></a>

**R-ARRAY-0002** — `&array` in slice context creates slice of all N elements; `array[lo..hi]` creates slice of selected range. Mutable slice requires exclusive borrow; immutable slice shared borrow.

<a id="R-ARRAY-0003"></a>

**R-ARRAY-0003** — Slice value consists abstractly of provenance-bearing start, length and access kind. Empty slice is valid and may have implementation-selected internal start; safe code cannot observe or dereference it.

<a id="R-ARRAY-0004"></a>

**R-ARRAY-0004** — `len` is a predeclared non-shadowable compiler intrinsic. `len(x)` returns `usize`: N для fixed array, число live elements для `array<T>`, число live nodes для `list<T>`, число live entries для `dict<K,V>`, stored length для slice либо UTF-8 byte length для string. It accepts no other type and evaluates x exactly once. Bounds are half-open; one-past index is permitted only as empty subslice endpoint, never for element access. Place operand observes через shared borrow на duration intrinsic и не copy/move.

<a id="R-ARRAY-0005"></a>

**R-ARRAY-0005** — `str` is a distinct immutable refinement of `const u8[]` whose bytes shall be well-formed UTF-8. It has the same representation and byte access, but not type identity. Boundary-safe iteration decodes Unicode scalars; byte indexing returns u8 and shall not claim character boundary semantics. `constexpr str` имеет те же representation, UTF-8 и access properties, а также guarantees program storage из R-TYPE-0028.

<a id="R-ARRAY-0006"></a>

**R-ARRAY-0006** — После обработки escapes и concatenation соседних literals string literal создаёт одно value типа `constexpr str`, immutable UTF-8 bytes которого встроены в program image. Для каждой complete R program emission implementation shall collect literal sequences в declarations, selected for emission из её reachable module closure, и create ровно одну application-global canonical immutable program-image backing-storage region для каждой unique exact complete post-escape byte sequence в этом set. Equality sequences включает как length, так и каждый byte. Каждое emitted occurrence literal с такой же sequence shall designate эту же canonical region, включая occurrences, lowered через synchronous и asynchronous functions. Mapping sequence-to-region shall be deterministic для одинаковых compiler inputs; из этого не следует требование стабильности numeric load address между executions. Sequence shall not be represented как suffix либо другой subrange region, associated с другой sequence. Empty sequence shall designate canonical program-lifetime sentinel, required by R-TYPE-0028. Runtime selection между values, происходящими из literals, сохраняет type `constexpr str`. Embedded zero bytes разрешены. C NUL termination shall be requested explicitly through `std.c` conversion, которая может allocate либо validate storage.

<a id="R-ARRAY-0007"></a>

**R-ARRAY-0007** — `core::utf8_error` является Copy, Send+Sync struct с единственным public field `usize index`. Exact checked conversion operation имеет signature `core::validate_utf8(const u8[] source) -> str throws core::utf8_error`. Она ничего не allocates, не вызывает panic, validates полный slice по RFC 3629 и сообщает byte index, с которого начинается первая invalid sequence. При success inferred origin возвращённого `str` является source slice, а значение представляет shared view ровно validated bytes. Mutable `u8[]` не преобразуется в `str` автоматически; его передача в эту operation создаёт shared reborrow и запрещает mutation этих bytes до last use возвращённого `str`. Failure не создаёт `str` и не mutates source.

<a id="errors-panic"></a>

## 19. Error handling and panic behavior

<a id="R-ERR-0001"></a>

**R-ERR-0001** — Recoverable failures shall использовать checked errors, declared после parameter list как `throws E1, E2`; optional absence shall использовать `o<T>`. Ни null, ни panic не shall кодировать expected error. Call с nonempty throws set valid только когда каждая possible error выбрана enclosing typed catch либо входит в declared throws set enclosing function. Outgoing set statement является union uncaught call, await, join, explicit throw и rethrow effects, canonicalized по R-TYPE-0012. Propagation не выполняет implicit error conversion, allocation или user code.

Для strict C17 mapping каждое nonempty normalized checked-error set имеет один flat carrier, содержащий tag типа `uint32_t` и один union, общий для logical success value и всех error payloads. Success имеет tag zero; error tags начинаются с one в ascending canonical error-type-key order. Generated throwing R-to-R function возвращает результат через explicit output-carrier parameter. Она полностью initializes selected payload до publication tag и не создаёт nested carriers. Compiler interface record содержит logical value type, normalized error set, complete tag table и layout hash, bound к selected target manifest. Async declaration записывает immediate start carrier отдельно от task-completion carrier. Checked propagation использует ordinary branches; C++ exceptions, `setjmp` и `longjmp` не используются. Errors одного call, которые достигают одного destination через одинаковый cleanup (caller или один catch, family которого их содержит), могут share одну branch: relay moves payload active member в destination carrier или family value до publication destination tag, а drops, finally bodies и moves происходят так же, как на отдельных branches.

<a id="R-ERR-0002"></a>

**R-ERR-0002** — `throw operand;` вычисляет operand ровно один раз и transfers resulting exact named error type в hidden propagation storage до drops exited locals. Когда operand — значение error с потомками (R-AGG-0011), передаётся хранимый в нём exact member, поэтому `throw;` и `throw move failure;` сохраняют конкретный type. Named Move operand требует `move`; temporary transfers implicitly. Direct braced construction следует R-AGG-0010. `throw;` valid только внутри lexical body catch и rethrows current value nearest catch binding. Для Move error он consumes binding; use после prior move/drop либо path, где binding не definitely initialized, требует diagnostic. Он bypasses все later sibling catches того же try и может быть выбран только nested либо outer try.

`throw (condition) operand;` является сокращением для `if (condition) { throw operand; }`. Скобки обязательны; condition следует тем же правилам syntax, typing и evaluation, что и условие `if`, включая short-circuit evaluation. Условие вычисляется ровно один раз. При false выполнение продолжается без evaluation или move operand и без создания error payload. При true применяются обычные правила throw выше. Условная форма не создаёт дополнительный lexical scope. Operand сохраняет правила выражений, contextual aggregate typing и checked-effect требования обычного throw, в том числе при constant false condition. Parenthesized operand без второго operand, `throw (error);`, остаётся unconditional throw. Существующие postfix projections и casts этого operand остаются частью обычного operand. Conditional throw без `else` в switch clause является case-body statement, поэтому clause по-прежнему требует explicit terminator для false path.

`throw (condition) first else second;` является сокращением для `if (condition) { throw first; } else { throw second; }`. Условие вычисляется ровно один раз; вычисляется и передаётся только выбранный operand. Оба operands независимо следуют обычным правилам выражений, ownership, contextual aggregate и checked effects, в том числе при constant condition. Они могут иметь разные complete nominal error types; каждый type должен быть обработан или объявлен как обычно. Unheaded aggregate в каждой ветке следует существующим правилам inference; type не выводится из противоположной ветки. Единственная точка с запятой ставится после второго operand. Оба пути передают управление, поэтому эта форма также служит switch clause terminator и завершает путь функции. Ветка `else` разрешена только при explicit throw condition и требует operand; она не является bare rethrow.

Checked transfer, пересекающий try/finally boundary, сначала owns один fully initialized pending error payload, drops locals, exited до boundary, выполняет каждый applicable finally inner-to-outer и затем transfers тот же payload в selected catch binding либо caller. В каждой точке существует ровно один cleanup owner; propagation никогда не copies Move error и не drops его дважды.

<a id="R-ERR-0003"></a>

**R-ERR-0003** — Try statement выполняет try block один раз. При checked error самый внутренний try, имеющий catch для этой error или одного из её ancestors (R-AGG-0011), выбирает среди этих catches catch ближайшего ancestor, начиная с собственного type error; catch types distinct, а sibling source order не влияет на выбор и не вводит conversion. Selected payload move-initializes catch binding как значение family catch type, когда у этого type есть потомки. Errors, produced catch, не рассматриваются sibling catches. Try без matching catch propagates original error.

Optional finally выполняется ровно один раз после normal try completion, после selected catch и до continuation любого pending return, break, continue, unmatched error, rethrow или panic unwind. Try/finally без catches не intercepts error. Nested finally clauses execute inner-to-outer. Finally может покинуться только через closing brace: `return`, `throw`, rethrow либо loop/switch transfer с target вне него invalid, а residual checked-error set shall быть empty. Nested try целиком внутри finally может обработать все собственные checked errors; loop transfer с target целиком внутри finally допустим. `await` запрещён везде внутри finally; explicit non-throwing task cancellation допустим. Поэтому finally не может replace либо suppress pending completion. Return/error payloads staged до finally и остаются live для borrow/move checking до resume original transfer.

Strict C17 lowering хранит ровно одну логическую hidden pending-completion record для каждого active structured transfer. Её reason есть normal completion, return, checked error, break, continue, fallthrough, cancellation либо panic unwind вместе с соответствующим initialized payload или target, когда он существует. Поэтому function либо async frame может содержать bounded stack этих records, capacity которого выводится из статически известной lexical finally nesting. Начало отдельного transfer во вложенном try/finally во время выполнения finally body помещает одну record в stack. Проведение того же transfer через последовательные outer finally clauses не добавляет и не дублирует record. Terminal dispatch потребляет и удаляет эту record ровно один раз. Move payload имеет одну owning record и не копируется либо дублируется.

Каждое edge, покидающее protected region, сначала выбирает reason своего transfer, выполняет drops, необходимые для достижения следующей protected-region boundary, проходит через одно shared finally body этого region и затем продолжает original transfer. Объект, объявленный снаружи этого protected region, не уничтожается неявно до выполнения его finally; он входит в finally в текущем состоянии и остаётся initialized, если сама finally явно не move либо drop его. Если transfer также покидает scope объекта, любой всё ещё initialized объект уничтожается только после этой finally и до следующей внешней boundary либо terminal transfer. Поэтому nested exit чередует boundary-local drops и finally bodies изнутри наружу, не предоставляя finally уже уничтоженный внешний объект, имя которого ей доступно. Async frame хранит pending stack и active finally stack во время runtime cancellation cleanup; terminal completion публикуется только после завершения applicable finally bodies и frame drops.

<a id="R-ERR-0004"></a>

**R-ERR-0004** — `panic(message)` accepts str, never returns and begins panic with category `explicit`. Runtime checks use stable categories: `bounds`, `integer_overflow`, `division_by_zero`, `invalid_shift`, `invalid_conversion`, `allocation_failure`, `reference_count_overflow`, `scoped_thread_panic`, `once_poisoned`, `thread_local_lifetime`, `stack_exhaustion` and `contract_violation`. `link_load_failure` is a pre-main runtime termination category governed by R-FFI-0037 rather than a catchable R panic. `argument_encoding_failure` аналогично является pre-main runtime termination category R-FUNC-0008, а не catchable R panic. `allocation_failure`, сообщённый при reservation startup snapshot R-FUNC-0008, также является non-catchable pre-main category; то же spelling остаётся catchable panic category для последующих R runtime checks.

<a id="R-ERR-0005"></a>

**R-ERR-0005** — Panic strategy is implementation-defined as `abort` или `unwind`. Abort не выполняет additional R drops или finally. Unwind drops initialized automatic objects в reverse order и executes active finally clauses inner-to-outer до program boundary; typed catch clauses выбирают только checked errors и никогда не catch panic. Panic, начавшийся в finally при любой pending non-panic completion, replaces эту completion; любой initialized return или checked-error payload drops ровно один раз как unwind cleanup. Эта panic-channel replacement не является outgoing checked или structured-control edge из finally clause. Panic, начавшийся в finally при уже active panic unwind, является second panic и aborts по R-ERR-0008.

<a id="R-ERR-0006"></a>

**R-ERR-0006** — Panic escaping `extern "C"` boundary, panic during active panic drop, or failure of panic runtime shall call abort. This behavior is defined and shall not unwind through C frames lacking explicit interoperability support.

<a id="R-ERR-0007"></a>

**R-ERR-0007** — Diagnostic text may vary, but code, primary source span and normative rule identifier shall be emitted for every required diagnostic.

<a id="R-ERR-0008"></a>

**R-ERR-0008** — Если first panic начинается во время ordinary destruction при unwind strategy, каждый remaining initialized subobject и каждая mandatory non-throwing resource-release duty этого destruction продолжаются ровно один раз в prescribed order как unwind cleanup. Это включает fields после user drop, active variant payload, remaining array elements и release allocation, control block либо runtime state. Уже начатое destruction не повторяется. Любой second panic вызывает abort. При abort strategy first panic aborts и не выполняет дальнейших R drops.

<a id="R-ERR-0009"></a>

**R-ERR-0009** — Panic, достигший initial thread, остаётся uncaught и завершает process после unwind этого thread. При unwind strategy panic, достигший root spawned R thread, завершает этот thread после drops его automatic и thread-local objects; owned report становится `panicked` в `std.thread::join_result`. Если thread detached, report передаётся runtime panic hook и затем destroyed без завершения других threads. Если первый panic начинается в thread-local drop после normal return entry, текущее failing TLS destruction завершает свои duties R-ERR-0008, staged return value dropped ровно один раз, remaining TLS stack dropped по R-OBJ-0008, и только после этого completion outcome становится этим panic report. Если первый panic вместо этого начинается при destruction detached unobserved returned payload, его report передаётся panic hook и join result не существует. При abort strategy любой panic aborts whole process и join result не создаётся. Second panic while unwinding всё так же aborts по R-ERR-0008. Delivery в runtime panic hook — mandatory non-throwing runtime duty; failure hook является panic-runtime failure и aborts по R-ERR-0006.

<a id="unsafe"></a>

## 20. Unsafe operations and safety contracts

<a id="R-UNSAFE-0001"></a>

**R-UNSAFE-0001** — Unsafe operation is permitted only lexically inside `unsafe { ... }` or body of `unsafe` function. Unsafe context does not disable type checking, move checking, initialization, scope or automatic drops.

<a id="R-UNSAFE-0002"></a>

**R-UNSAFE-0002** — Полный закрытый перечень unsafe operations R 0.1: raw pointer deref/index/arithmetic; raw pointer ordering/subtraction; call unsafe/C function; pointer/integer conversion; borrow-to/from-raw conversion; owner-to-raw exposure; cast between distinct raw pointer types или изменение raw constness; managed-owner raw-token exposure/reconstruction; construction borrow/slice from raw parts; access any imported external object; access mutable R static storage; representation transmute; volatile access; manual allocation adoption/release; assume assertion.

<a id="R-UNSAFE-0003"></a>

**R-UNSAFE-0003** — Каждая unsafe function declaration shall непосредственно перед declaration иметь `@safety("CONTRACT-ID", "preconditions")` с machine-readable identifier и human-readable preconditions. Пустой contract запрещён.

<a id="R-UNSAFE-0004"></a>

**R-UNSAFE-0004** — При выполнении safety contract unsafe operation shall иметь семантику Annex E и shall not создавать UB в safe caller. Нарушение contract may немедленно или позже вызвать UB; implementation не обязана обнаруживать нарушение.

<a id="R-UNSAFE-0005"></a>

**R-UNSAFE-0005** — `unsafe` shall not использоваться для подавления required diagnostic, не относящейся к закрытому перечню операций, включая use-after-move, duplicate name, invalid grammar или incompatible by-value ABI.

<a id="R-UNSAFE-0006"></a>

**R-UNSAFE-0006** — Safe abstraction, внутри использующая unsafe, shall обеспечивать contract для всех inputs, разрешённых её safe signature. Иначе implementation этой abstraction non-conforming независимо от caller behavior.

<a id="R-UNSAFE-0007"></a>

**R-UNSAFE-0007** — `source as D` является единственной source form standard representation transmute в R 0.1. Она выбирается только тогда, когда не применяется ни одна numeric, fieldless-enum, pointer, borrow или raw conversion, определённая R-EXPR-0016..R-EXPR-0019. Source и target types shall быть Copy и recursively не содержать owner, exclusive borrow/slice, atomic value или user drop. Target type shall not быть `constexpr str`, его `const`-qualified form либо recursively содержать любую из этих форм; R-TYPE-0028 не допускает transmute origin такого value. Source и target sizes shall быть statically equal. Нарушение этих type restrictions requires `R-DIAG-TYPE-003`; их выполнение не отменяет target-validity safety contract R-SAFETY-TRANSMUTE в Annex E.

<a id="R-UNSAFE-0008"></a>

**R-UNSAFE-0008** — Каждый profile предоставляет следующие exact compiler-recognized unsafe operations `core` как closed intrinsic families:

- `unsafe core::slice_from_raw_parts(raw const T*? pointer, usize length) -> const T[]`;

- `unsafe core::slice_from_raw_parts_mut(raw T*? pointer, usize length) -> T[]`;

- `unsafe core::slice_from_raw_parts_in(A anchor, raw const T*? pointer, usize length) -> const T[]`;

- `unsafe core::slice_from_raw_parts_in_mut(A anchor, raw T*? pointer, usize length) -> T[]`;

- `unsafe core::volatile_load(raw const T* address) -> T`;

- `unsafe core::volatile_store(raw T* address, T value) -> void`;

- `unsafe core::assume(bool condition) -> void`.

Каждый allocation-supporting profile дополнительно предоставляет:

- `unsafe core::adopt(raw T* pointer) -> own T*`;

- `unsafe core::release(own T* owner) -> raw T*`.

Для каждой slice operation T выбирается из pointee raw pointer и shall быть outermost-unqualified, complete, sized, inhabited non-`void` value type. Result является descriptor ровно над length последовательными elements T; он не allocates и не copies ни одного element. Он carries fresh hidden region, constrained всеми uses этого result. Unsafe caller shall установить storage lifetime, initialization и shared либо exclusive alias permission R-SAFETY-SLICE-PARTS для всего этого inferred region; raw pointer не supplies borrow origin и не extends storage lifetime. Нулевая length создаёт ordinary empty-slice value R-ARRAY-0003 и является единственным случаем, допускающим null pointer.

Anchored forms `core::slice_from_raw_parts_in` и `core::slice_from_raw_parts_in_mut` создают тот же descriptor по тем же правилам pointer, length и element, но carry region своего anchor вместо fresh region. Anchor A shared form является borrow `const U*` или `U*`, slice `const U[]` или `U[]`, либо `str`; anchor mutable form является exclusive borrow `U*` или mutable slice `U[]`. Записанный anchor `&place` borrows place shared для shared form и exclusively для mutable form. Anchor не evaluated и supplies только свой region, поэтому shall быть place, field либо dereference place или borrow place; call, index или любое другое вычисление в качестве anchor требует `R-DIAG-TYPE-001`. Result carries borrow origin anchor: его можно хранить, возвращать (R-BORROW-0007) и удерживать везде, где мог бы borrow, derived от anchor, а borrow rules сохраняют referent anchor live, unmoved и, для mutable form, иначе unused, пока result live. Unsafe caller shall установить R-SAFETY-SLICE-PARTS для этого anchored region: elements остаются live, initialized и correctly aliased, пока referent anchor live и unmoved, что object, хранящий address, обычно гарантирует, владея storage и освобождая его только в своём drop.

Для каждой volatile operation T выбирается из pointee raw pointer и shall быть outermost-unqualified, complete, sized, inhabited Copy value type, который recursively не содержит safe borrow, slice, `str`, `constexpr str`, managed owner, atomic value, standard resource или user drop. Load возвращает единственное прочитанное value; store copies value и выполняет единственную write. Они не создают atomic либо synchronization edge.

Для `core::adopt` и `core::release` T выбирается из pointer либо owner и shall быть outermost-unqualified, complete, sized, inhabited non-`void` value type. Adopt принимает non-null exact base pointer одной allocation, содержащей один initialized T, и transfers её существующие drop и compatible-deallocation duty возвращённому unique owner по R-SAFETY-ADOPT; он не allocates и не constructs другой T. Release consumes unique owner, suppresses его R drop и возвращает тот же base pointer с duty, transferred caller по R-SAFETY-RELEASE. Поэтому named owner argument требует `move` по R-FUNC-0002.

`core::assume` имеет effect R-SAFETY-ASSUME. Ни одна из этих девяти operations не allocates и не panics; нарушение применимого safety contract Annex E является UB, а не recoverable result.

<a id="concurrency"></a>

## 21. Concurrency and memory model

### 21.1 Threads and data-race freedom

<a id="R-MEM-0001"></a>

**R-MEM-0001** — Execution may содержать multiple threads only through hosted standard library or C FFI. Each thread has sequenced-before order; inter-thread happens-before is the transitive closure of sequenced-before and synchronizes-with edges defined in this section and standardized synchronization APIs.

<a id="R-MEM-0002"></a>

**R-MEM-0002** — Safe R program shall be data-race-free. Potential transfer or sharing, для которого compiler cannot establish required capabilities, requires diagnostic; data race can arise only from unsafe contract violation.

<a id="R-MEM-0003"></a>

**R-MEM-0003** — Type является `Send`, если его value может transfer ownership в другой thread; он является `Sync`, если shared borrow value можно использовать concurrently. `bool`, `char`, fixed R numeric types `i8`, `u8`, `i16`, `u16`, `i32`, `u32`, `i64`, `u64`, `isize`, `usize`, `f32`, `f64`, and every target-supported C ABI scalar numeric type listed by R-FFI-0002 are Send+Sync. Fieldless enums, ordinary `str`, `constexpr str` и `atomic T` также Send+Sync. `own T*` is Send iff T Send and Sync iff T Sync. Shared borrow `const T*` and shared slice `const T[]` are Send+Sync iff T Sync. Exclusive borrow `T*` и exclusive slice `T[]` являются Send iff T Send и Sync iff T Sync с shared-path attenuation по R-BORROW-0002. Instance, содержащий borrow вне distinguished program region, может transfer только через R-MEM-0015, который statically joins его до source reuse. `arc T` и `weak arc T` являются Send+Sync iff T одновременно Send и Sync. `rc T` и `weak rc T` никогда не являются Send или Sync независимо от T.

<a id="R-MEM-0004"></a>

**R-MEM-0004** — Fixed array, `array<T>`, `list<T>`, `dict<K,V>`, `o`, struct и tagged enum derive Send/Sync iff all possible elements, keys, fields and variant payloads do. Raw pointers and raw C function pointers are neither Send nor Sync in safe code, несмотря на их Copy status. Standard thread, synchronization и channel types имеют capabilities из R Standard Library Specification 0.1. R 0.1 не имеет user-written capability implementation; FFI wrapper, asserting capability, является unsafe и shall документировать lifetime, ownership и synchronization.

### 21.2 Atomics and ordering

<a id="R-MEM-0005"></a>

**R-MEM-0005** — Initialization is the first modification of an atomic object. All later stores and read-modify-write operations on that object form one total modification order consistent with happens-before. RMW reads the immediately preceding modification and appends exactly one modification atomically. Available orderings: relaxed, acquire, release, acq_rel, seq_cst.

<a id="R-MEM-0006"></a>

**R-MEM-0006** — Release operation synchronizes-with acquire operation that reads its value or a value from its release sequence. Release sequence is the maximal contiguous modification-order subsequence headed by release store/RMW, followed by modifications from the same thread and any atomic RMW modifications. Seq_cst operations additionally participate in one total order consistent with happens-before and every affected modification order. Relaxed alone creates no synchronizes-with edge.

<a id="R-MEM-0007"></a>

**R-MEM-0007** — Allowed ordering is: load — relaxed/acquire/seq_cst; store — relaxed/release/seq_cst; exchange/other RMW — any of five. Compare-exchange failure may be relaxed/acquire/seq_cst, never release/acq_rel, and shall not be stronger than success: relaxed→relaxed, acquire→relaxed\|acquire, release→relaxed, acq_rel→relaxed\|acquire, seq_cst→relaxed\|acquire\|seq_cst. Invalid constant ordering requires `R-DIAG-ATOMIC-001`; runtime-selected invalid ordering panics before any atomic read or modification.

<a id="R-MEM-0008"></a>

**R-MEM-0008** — Atomic operations are indivisible for the atomic object and shall not cause data race. Whether they are lock-free is implementation-defined and queryable. Signal-handler semantics are outside R 0.1.

<a id="R-MEM-0009"></a>

**R-MEM-0009** — Compiler shall not introduce a data race, tear atomic access or remove observable synchronization. Mapping to C17 atomics shall preserve R order.

<a id="R-MEM-0010"></a>

**R-MEM-0010** — Memory location is storage of one scalar object or one distinct non-overlapping scalar subobject. Two accesses conflict when their byte ranges overlap and at least one writes; atomic and non-atomic access to the same location also conflict. Conflicting accesses by different threads shall be happens-before ordered unless both are compatible atomic operations. Otherwise behavior can arise only from unsafe/external contract violation and is a data race per R-TERM-0013.

<a id="R-MEM-0011"></a>

**R-MEM-0011** — Successful `std.thread::spawn` или `std.thread::spawn_scoped` contains an abstract publication event sequenced after evaluation of its caller-side arguments and before the first action of the new thread; that publication synchronizes-with the first child action. Child execution may begin before `spawn` returns. Когда spawn throws `std.thread::thread_error`, publication не происходит, child не существует и entry не вызывается. После thread-local drops target publication его committed completion outcome является release operation. Каждый explicit/implicit join и каждая detached outcome-cleanup duty выполняет acquire operation, observing эту publication, до inspect, move либо destruction outcome; publication synchronizes-with этот acquire. Каждый successful spawn создаёт один private completion/runtime-handle state и ровно одну last-reference release duty. State остаётся live через target runtime reference, sole outcome observation/cleanup right и каждый scoped revoked-handle tombstone reference и scoped supervisor-registration liveness reference, required R-MEM-0016. Только после завершения target reference, после того как это право moved outcome через explicit join, staged его через implicit scoped join либо завершило detached outcome destruction/hook delivery, и после завершения каждого такого tombstone/registration reference duty releases completion storage и underlying runtime/OS thread resource ровно один раз. Раньше они не releases; unwind cleanup сохраняет эту mandatory non-throwing duty по R-ERR-0008. Completion/runtime-reference transitions и `std.thread::thread` descriptor identity/runtime-reference transitions linearizable. Каждый earlier release reference на любое из этих states synchronizes-with last-reference acquire step, который предшествует release соответствующего storage либо runtime resource. Last action abstract thread-completion path, включая normal thread-local drops и required detached-outcome destruction/hook delivery, synchronizes-with successful return from explicit или implicit `join` or runtime’s normal-termination completion wait for a detached thread. C-origin thread edges exist only through verified C synchronization and attachment contract.

<a id="R-MEM-0012"></a>

**R-MEM-0012** — Atomic load shall read a value written by initialization or an actual modification of the same object. Coherence shall satisfy all four cases: (a) modification W happens-before modification X only if W precedes X in modification order; (b) if load L reads W and L happens-before modification X, W precedes X; (c) if modification W happens-before load L, L reads W or a later modification; and (d) if load A happens-before load B, B shall not read a modification earlier than A read. Для seq_cst load B let A be the last seq_cst modification of that object preceding B in the single seq_cst order. B reads A or a non-`seq_cst` modification X for which X does not happen-before A; if no A exists, B reads some non-`seq_cst` modification. This choice shall also satisfy (a)..(d). A non-atomic read in a data-race-free execution observes the unique latest happens-before write to its location. The directed causality relation formed by sequenced-before, synchronizes-with and reads-from shall not justify a value through a cycle without an originating initialization/modification; out-of-thin-air values are forbidden.

### 21.3 Создание thread, transfer и lexical scope

<a id="R-MEM-0013"></a>

**R-MEM-0013** — Safe `spawn(entry, arguments...)` и `spawn_scoped(entry, arguments...)` принимают только direct module-level safe R function designator и одно выражение аргумента точного типа для каждого entry parameter. Каждый by-value argument shall быть Send. Кроме R-MEM-0015, каждый direct/nested borrow-bearing component, transferred в borrow, slice, `str` или scoped handle, shall иметь program region; это ограничение не относится к `constexpr str`, который не содержит lifetime runtime storage. Shared borrow/slice требует Sync pointee/element. Named Move argument permitted только как complete direct form `move place`; nested named Move в aggregate, constructor либо другом argument expression требует `R-DIAG-MOVE-003`. Каждая форма spawn является одной двухфазной creation transaction: evaluation direct named argument `move place` подготавливает source, не изменяя его initialized ownership state, и устанавливает exclusive staged-move reservation для source и каждого overlapping place до конца transaction. Последующий argument shall not читать, писать, borrow, move либо повторно stage overlapping storage; нарушение требует `R-DIAG-BORROW-001`. Implementation резервирует полное состояние child и completion; successful creation атомарно converts каждую reservation в её move, commits все подготовленные transfers и затем publishes child. При creation failure child отсутствует, entry не вызывается, каждая staged-move reservation освобождается, каждый именованный Move source остаётся initialized и неизменным, а каждый временный объект, подготовленный для operation, уничтожается ровно один раз своим обычным full expression. При unwind strategy panic во время argument evaluation аналогично освобождает каждую reservation и выполняет ordinary temporary cleanup; при abort strategy R-ERR-0005 terminates без дополнительного R cleanup.

<a id="R-MEM-0014"></a>

**R-MEM-0014** — Successful unscoped `std.thread::spawn` transfers staged arguments child и возвращает sole `std.thread::join_handle<R throws E...>`, где R — entry return type, а E…​ — его canonical checked-error set. R и каждый E shall быть Send, либо R may быть `void`. Child вызывает entry ровно один раз. Returned R либо thrown E moved в handle completion state. `join(move handle)` ожидает: checked E transfers из state и throws в join point, а normal completion возвращает ровно один `std.thread::join_result<R>`: `completed` для normal void return, `returned(R)` для normal value return или `panicked(report)` при unwind strategy. Join consumes handle; ни один result нельзя наблюдать дважды.

<a id="R-MEM-0015"></a>

**R-MEM-0015** — Statement `thread_scope { body }` создаёт fresh hidden thread region. `spawn_scoped`, непосредственно используемый в region, may передавать arguments с direct/nested borrow, slice или ordinary `str` components, inferred regions которых включают thread region, а также scoped handles, bound ровно к тому же region; все остальные staging, exact-parameter, Send и failure rules R-MEM-0013 остаются применимы. Shared borrow/slice либо ordinary `str` требует Sync referenced pointee/element; exclusive borrow/slice требует Send pointee/element и suspends parent access до region completion, кроме early-release proof ниже. R и каждый checked error shall быть Send, либо R may быть `void`; returned handle имеет type `std.thread::scoped_join_handle<R throws E...>`. Ни scoped handle, argument component, ни returned value, bound thread region, не может escape его. Successful explicit join may снять suspension от exclusive input borrow/slice раньше только когда flow-sensitive move analysis tracks exact originating scoped handle через local whole-handle moves до этого join. Передача handle другой function/child, embedding либо drop теряет early-release proof; handle, позднее returned из такой передачи, не восстанавливает identity proof в R 0.1, поэтому suspension продолжается до region completion. При successful creation compiler attaches borrow-origin identity непосредственно к returned handle. Direct local whole-handle moves сохраняют identity и не считаются embedding. Passing либо embedding handle через любую другую function либо child теряет proof. Когда spawn throws, handle не существует, и suspension от staged exclusive argument заканчивается, когда failed two-phase transaction освобождает staged access, до transfer error caller; именованные Move sources остаются неизменными по R-MEM-0013. Даже с exact-handle proof suspension продолжается, пока returned value содержит производный lifetime-bearing component. Любой такой direct/nested borrow, slice или ordinary `str`, включая находящийся в aggregate, сохраняет ordinary access suspension до своего last use.

<a id="R-MEM-0016"></a>

**R-MEM-0016** — Каждый successful `spawn_scoped` немедленно registers child у scope supervisor. Registration owns один non-observing liveness reference на completion storage независимо от handle, пока scope-close sweep не release его exactly как ниже. Каждый scoped child joined до любого exit из `thread_scope`, включая `return`, `break`, `continue`, checked throw и unwind. Explicitly joined child возвращает свой `join_result`; drop scoped handle transfers его observation right scope supervisor для automatic join и никогда не detaches child. Implicit exit выполняет три ordered phases до ordinary destruction remaining automatic objects region. Сначала scope supervisor ждёт завершения target execution каждого successfully spawned child, который ещё не explicitly joined, в reverse order successful `spawn_scoped`, не обращаясь к unconsumed outcome и не destroying его. Затем до staging любого outcome supervisor устанавливает sole ownership каждого still-unconsumed scoped outcome observation/cleanup right: он retains право, уже transferred drop handle, и claims каждое остальное право из его live handle, включая handle на любой глубине automatic object либо committed child outcome. Каждый live handle value, право которого claimed, становится revoked и retains один non-observing tombstone reference на completion storage: его subsequent destruction только releases этот reference ровно один раз, и он не может снова observe, join либо register target. Tombstone сохраняет revocation state readable до destruction handle shell и не может сохранять target execution либо outcome live. После этого supervisor visits все registrations в reverse successful-spawn order. Для already Consumed outcome он releases только registration reference. Для каждого claimed right он выполняет acquire R-MEM-0011, stages outcome и затем releases этот registration reference. Поэтому каждый registration reference released ровно один раз только после phase-one target waiting и required state inspection либо outcome staging. В третьей phase staged outcomes consumed в том же reverse order. Unobserved payload `returned(R)` dropped ровно один раз. Scoped handle с nonempty checked-error set является must-resolve и shall быть explicitly joined на каждом normal либо checked-transfer path; supervisor observes такое completion только при forced panic/cancellation cleanup, где unobservable error payload destroyed ровно один раз после acquire edge. Panic reports retained в encounter order до consumption всех staged outcomes; ни один report не delivered и не destroyed во время этого traversal. Если parent уже unwinding либо first unwind начинается при destruction Returned payload в третьей phase, каждый remaining outcome consumed в fixed order как unwind cleanup; после этого каждый retained child report передаётся panic hook и destroyed в encounter order, а original panic продолжается. Second panic aborts по R-ERR-0008. Только если cleanup третьей phase завершён без parent unwind, retained reports могут вызвать ровно один `scoped_thread_panic`: первый report, encountered в reverse successful-spawn order, используется для escalation, каждый later report передаётся hook в encounter order, и все reports destroyed до начала escalation. Таким образом, все child target executions и scope claims complete до начала implicit payload-drop, ordinary region-local destruction либо scoped-child escalation panic. Panic behavior abort strategy остаётся по R-ERR-0005/R-ERR-0009.

<a id="R-MEM-0017"></a>

**R-MEM-0017** — Unscoped join handle Move-only. Каждый unscoped handle может быть explicitly detached. Drop effect-free handle имеет тот же non-blocking effect, что detach: каждая operation transfers sole current-or-future outcome-cleanup duty runtime, а target продолжается. Когда outcome существует, duty сначала выполняет acquire, required R-MEM-0011, затем выполняется как один root task в attached runtime cleanup context с fresh live R thread-local set; она никогда не выполняется в context, thread-local teardown которого уже начался. Concrete cleanup-context identity/scheduling unspecified по R-USB-011. Unobserved returned payload либо checked- error payload destroyed ровно один раз, а unobserved panic report передаётся panic hook и destroyed ровно один раз. Handle с nonempty checked-error set является must-resolve и shall быть consumed через join либо explicit detach на каждом normal либо checked- transfer path; implicit normal drop invalid. При unwind strategy любой first panic в этом cleanup root task, включая payload destruction или его собственный thread-local teardown, рассматривается как detached thread-root panic по R-ERR-0009: remaining initialized thread-local objects dropped по R-OBJ-0008, а owned report передаётся hook и destroyed ровно один раз; second panic aborts по R-ERR-0008. При abort strategy первый panic немедленно aborts по R-ERR-0005/R-ERR-0009 без report и дальнейших R drops. Last action context является частью abstract completion path R-MEM-0011 и предшествует прохождению quiescence drain R-AM-0013, включающего эту duty. Detach никогда не cancels thread и не ослабляет normal-termination draining. Thread scheduling, relative start order и fairness в остальном unspecified; ownership и happens-before rules не зависят от конкретного schedule.

<a id="R-MEM-0018"></a>

**R-MEM-0018** — Spawn publication и explicit/implicit join либо detached normal-termination completion edges R-MEM-0011 — единственные implicit memory edges thread lifecycle. Move value в thread не clone его; return value из thread не делает его Copy. `arc` clone изменяет lifetime ownership, но сам по себе не publishes последующие writes в T. Concurrent mutation T legal только через atomics или standardized synchronized interior-mutable object.

### 21.4 Locks, notification и message passing

<a id="R-MEM-0019"></a>

**R-MEM-0019** — Successful acquisition `mutex<T>` synchronizes-with preceding unlock этого mutex и предоставляет один Move-only exclusive guard. Guard даёт единственный safe mutable access к T, пока удерживается. Explicit unlock или guard destruction releases mutex ровно один раз. Portable mutex guard не Send и не transferable в другой thread; recursive acquisition владельцем возвращает guardless alternative `would_deadlock`, а не UB.

<a id="R-MEM-0020"></a>

**R-MEM-0020** — Если thread начинает unwinding, удерживая exclusive mutex либо write guard, poison marked до либо atomically with release этого guard и release-published unlock operation. Для каждого mutex acquisition, release и poison transitions linearizable в одной per-lock history, consistent with happens-before; poison transition rw_lock участвует в history R-MEM-0021. Каждая acquisition, которая получает guard после poisoned release, observes mark и возвращает `poisoned(G)`; same-thread conflict и nonblocking contention сохраняют precedence R-LIB-0014. Poison advisory, и outcome всё ещё owns guard, поэтому caller может inspect либо repair T. Panic при наличии только read guard не poisons `rw_lock`. Poison state никогда не разрешает unsynchronized access.

<a id="R-MEM-0021"></a>

**R-MEM-0021** — `rw_lock<T>` разрешает либо один exclusive write guard, либо любое число shared read guards. Все acquisition/release transitions одного rw_lock linearizable в единой state history, consistent with happens-before. Writer unlock synchronizes-with каждой subsequent successful reader/writer acquisition, ordered после него в этой history. Successful writer acquisition после completed reader epoch synchronizes-with каждым reader-guard release этого epoch; release последнего reader закрывает epoch. Любая acquisition thread, уже holding любой guard того же rw_lock, возвращает `would_deadlock` до blocking. В остальном reader/writer preference и fairness unspecified.

<a id="R-MEM-0022"></a>

**R-MEM-0022** — `condvar` wait consumes mutex guard, atomically releases mutex и enqueues caller, затем blocks и reacquires тот же mutex до return нового guard. Для каждой condition variable все atomic release-and-enqueue, notify operation и transition waiter из enqueued в eligible-to-wake участвуют в одном total order, consistent with happens-before. `notify_one` делает eligible ровно одного waiter, enqueued в его order point, если такой существует: он выбирает earliest still-enqueued waiter в этом total order. `notify_all` делает eligible всех waiters, enqueued в его order point. Notification не remembered, когда никто не enqueued. Wait также может стать eligible spuriously, поэтому protected predicate shall проверяться в explicit loop. Eligibility предшествует mutex reacquisition; notification сама по себе не создаёт synchronizes-with edge, а protected-state visibility обеспечивается этим mutex reacquisition. Использование condition variable с другим mutex после первого wait вызывает `contract_violation` до release supplied guard.

<a id="R-MEM-0023"></a>

**R-MEM-0023** — Все arrivals одного reusable `barrier` linearizable в per-barrier history, partitioned на generations configured participant count. Arrival, являющийся last в generation, closes и releases этот generation и возвращает `leader`; каждый earlier arrival в нём возвращает `follower`. Каждое pre-arrival action этого generation happens-before каждый successful return из generation. `once` имеет ровно abstract states Uninitialized, Running, Completed и Poisoned. Invocation из Uninitialized переводит его в Running; каждый concurrent `call_once` или `call_once_force` ждёт этот attempt. Normal completion переводит Running в Completed, после чего обе operations возвращаются без invocation designator; completion synchronizes-with каждый такой return. Если initializer throws declared checked E, operation moves exact payload caller, не publishes completion, release-restores Uninitialized и не sets poison; waiter acquires restoration до retry. Это также применяется к forced attempt из Poisoned: checked error restores Uninitialized, а не сохраняет panic poison. При unwind strategy failed attempt остаётся Running, пока его panic не покинет initializer invocation после automatic cleanup, затем release-publishes Poisoned; successful-completion state не публикуется, а initializer side effects не roll back. Каждая operation, которая observes, re-evaluates либо claims из этого Poisoned state, сначала выполняет acquire. При abort strategy первый panic aborts до того, как failure state станет observable. `call_once` на Poisoned panics с `once_poisoned` без invocation. На Poisoned competing `call_once_force` callers shall claim serially: winner переводит Poisoned в Running и invokes once, остальные wait и затем снова evaluate resulting state. Следующий failed unwind attempt следует тому же Running-to-Poisoned publication protocol, а normal completion устанавливает Completed. `once_lock<T>` имеет ровно states Uninitialized, Running и Initialized(T). `get` никогда не waits и возвращает absence в первых двух states, а shared borrow только в Initialized. `get_or_init` в Initialized возвращает existing borrow без invocation; из Uninitialized переводит state в Running и invokes один раз. Normal initializer return publishes Initialized(T). Declared checked E не publishes T, release-restores Uninitialized, не sets poison и transfers E caller после restoration; waiter acquires его до retry. При unwind strategy panic сохраняет Running, пока panic не покинет initializer invocation после automatic cleanup, затем release-publishes Uninitialized; каждая operation, которая observes, re-evaluates либо claims этот release-restored state, сначала выполняет acquire. Implementation may выполнять тот же acquire для initial Uninitialized state. При abort strategy panic aborts до того, как restored Uninitialized станет observable. Competing `get_or_init` или `set`, встретившие Running, waits и затем снова evaluates resulting state. `set` из Uninitialized publishes staged T и возвращает `stored`, а из Initialized возвращает `occupied(T)` с этим staged T. Эти competing transitions linearizable. `once_lock` не poisoned, а successful publication synchronizes-with каждой operation, observing initialized T.

<a id="R-MEM-0024"></a>

**R-MEM-0024** — Asynchronous channel и bounded synchronous channel являются multi-producer, single-consumer queues. sender(T) explicitly cloneable; sole receiver(T) Move-only и не Sync. Send передаёт T by value по R-FUNC-0002: named Move T требует `move`, тогда как named Copy T копируется. Failure возвращает staged T в error outcome; для Move T это тот же transferred owner, а source Copy T остаётся неизменным. Successful receipt transfers staged value один раз, а successful send publishes этот item release operation. Receive operation либо queued-value destruction duty, claiming item, выполняет acquire до move либо destruction T; send synchronizes-with этот acquire.

<a id="R-MEM-0025"></a>

**R-MEM-0025** — Creation, cloning и destruction sender, creation/destruction receiver и каждая send/receive operation linearizable в одной per-channel abstract history, consistent with happens-before; каждый result отражает channel state в linearization point своей operation. Successful capacity-zero send и matching receive имеют один paired transfer point. Values одного sender received в successful-send linearization order; ordering между distinct senders до linearization unspecified. Asynchronous form conceptually unbounded, но may вернуть allocation failure с unsent value. Bounded form blocks when full; capacity zero — rendezvous, требующий matching receive. После destruction всех senders receiver drains queued items и затем observes disconnection. При empty queue и хотя бы одном live sender `recv` waits, а `try_recv` возвращает `empty`; при empty queue и zero live senders обе возвращают `disconnected`. Когда value available, любая receive operation возвращает `received(T)` до любого disconnect result. Пока receiver side live, `sync_send` waits на full bounded queue, а `try_send` возвращает `full(T)`; capacity zero считается full, если matching receive не может принять value. Connected asynchronous send, который enqueues, возвращает `sent`, если он не возвратил `allocation_failed(T)` до enqueue; connected bounded send, который enqueues либо completes rendezvous, возвращает `sent`. Destruction receiver либо его creation right wakes blocked senders, и каждый later/awakened send возвращает `disconnected(T)`. Destruction last sender wakes blocked receiver. Destruction receiver либо unconsumed receiver-creation right synchronously owns одну drop duty для каждого queued value. Duties являются sub-duties этого destruction, выполняются в его current attached R context и все attempted в FIFO order abstract queue до normal return destruction либо propagation его first panic; background channel-cleanup duty не остаётся. При unwind strategy first drop panic продолжает все later queued duties в том же FIFO order, затем propagates после их попытки, а second panic aborts. Abort strategy следует R-ERR-0005, и ни одно value, drop которого уже начался, не drops снова. Failure initial queue/control-block allocation не создаёт channel и возвращается channel constructor. После disconnect cleanup и только когда не осталось factory, receiver, sender endpoint, blocked operation, queued value либо queued-value drop duty, одна last-reference duty releases queue/control-block storage ровно один раз; ни один endpoint destruction не releases это storage раньше.

<a id="R-MEM-0026"></a>

**R-MEM-0026** — `unpark(thread)` делает доступным не более одного token target thread и выполняет release operation. Все `unpark` operations и token-consuming `park` operations для одного target образуют единый total token order, consistent with happens-before. Consuming `park` выполняет acquire operation и synchronizes-with каждый `unpark` после previous consuming `park` либо от начала, если previous отсутствует, и до себя в этом token order, включая releases, coalesced в тот же available token. Calls may return spuriously; spurious return отсутствует в token order, не consumes token и не выполняет acquire. Multiple unparks до consumption coalesce в один token. Sleep и yield не создают synchronizes-with edge и не гарантируют fairness.

<a id="R-MEM-0027"></a>

**R-MEM-0027** — Deadlock, livelock, starvation и priority inversion не являются data races и не создают UB. Кроме operations, contract которых возвращает `would_deadlock`, implementation не обязана их detect. Destruction locked synchronization object, live channel endpoint с invalid internal state или live thread runtime object возможна в safe code только после ownership proof отсутствия outstanding use.

<a id="R-MEM-0028"></a>

**R-MEM-0028** — Strong/weak counters `arc` — atomic lifetime metadata. Clone использует checked relaxed increment. Strong/weak release используют release decrement и acquire fence до того, как thread, observing last reference, drops T или deallocates control block. Weak upgrade использует checked compare-exchange из nonzero strong count с acquire success/relaxed failure. `try_unwrap` использует acq_rel transition one-to-zero. Каждый earlier strong release allocation synchronizes-with и acquire step operation, которая позднее reserves либо observes его last-strong transition до move/drop T, и later successful uniqueness acquire `arc get_mut`. Каждый earlier explicit либо implicit weak release synchronizes-with acquire step, observing last weak-liveness reference до control-block deallocation; каждый earlier explicit weak release также synchronizes-with later successful uniqueness acquire `arc get_mut`. Operations shall быть linearizable и никогда не могут transiently wrap или resurrect zero strong count.

<a id="R-MEM-0029"></a>

**R-MEM-0029** — Reference-count atomics защищают только allocation lifetime. Они не предоставляют mutable access к T и не заменяют publication последующих non-atomic writes T. `rc` выполняет те же abstract state transitions ordinary sequenced non-atomic counters; unconditional non-Send/non-Sync исключает safe concurrent reachability.

<a id="R-MEM-0030"></a>

**R-MEM-0030** — Standard synchronization operations создают только edges, явно перечисленные в этом section. Failed try-lock/channel operation, count observation, sleep и yield не создают synchronizes-with edge. Condition-variable notification, независимо от того, делает ли она waiter eligible, сама по себе не создаёт такой edge; associated mutex release/acquisition обеспечивает protected-state visibility. Implementation may использовать stronger internal ordering, но shall not expose weaker result.

<a id="R-MEM-0031"></a>

**R-MEM-0031** — `task<T throws E...>` является Send iff T и каждый E являются Send и никогда не является Sync. Successful start publishes каждый committed argument с release semantics, а first task execution observes их с acquire semantics. Terminal completion publishes returned value, one exact declared checked error либо panic report с release semantics; successful `await` observes completion с acquire semantics до move result, transfer error либо re-raise panic. Detached-result cleanup выполняет такое же acquire observation до доступа либо destruction outcome. Cancellation request сам по себе не создаёт synchronizes-with edge; terminal acknowledgement использует completion edge.

<a id="modules"></a>

## 22. Modules and visibility

<a id="R-MOD-0001"></a>

**R-MOD-0001** — `module a.b;` assigns canonical module path. Path components are identifiers. Module path and source file mapping is implementation-defined and shall be documented by build system. Top-level module paths `core` and `std`, а также все их descendants зарезервированы за implementation. Program может импортировать эти modules, но не должна объявлять или определять их.

<a id="R-MOD-0002"></a>

**R-MOD-0002** — `import a.b;` makes exported names available only by qualified path `a.b::name`. `import a.b::{x, y};` imports selected names unqualified. Wildcard imports отсутствуют. Modules selected profile под reserved roots `core` и `std` predeclared, и их exact qualified names may использоваться без import. Selective import остаётся доступным для их ordinary exported identifiers, но compiler-recognized standard-owner operations, standard parametric type constructors и standard type-operand calls shall сохранять exact qualified spelling Annex A и не могут imported unqualified. Модуль под корнем `std` может поставляться implementation как R source, названный в её library map (Library раздел 21): program импортирует его через `import std.name;` как любой другой модуль, import из profile ниже least profile, названного map, требует `R-DIAG-PROFILE-001`, его exported traits, types, functions и methods разрешаются как у любого imported module, а его generic aggregates записываются `std.name::Type<arguments>` по Annex A.4.

<a id="R-MOD-0003"></a>

**R-MOD-0003** — Import is semantic dependency, not textual inclusion; comments, protected names, macros (которых нет) и lexical state не пересекают boundary.

<a id="R-MOD-0004"></a>

**R-MOD-0004** — Module dependency cycle requires diagnostic. Imported modules initialise in deterministic topological order; ties break by Unicode code-point order canonical module path. Destruction uses exact reverse order.

<a id="R-MOD-0005"></a>

**R-MOD-0005** — Exported signature shall use only exported complete types и shall содержать complete compiler-inferred region metadata, требуемую section 13, кроме `void` в positions, permitted R-TYPE-0007. Exported opaque type, declared non-protected `extern "C"` block, may occur recursively only behind raw pointer or raw C function pointer boundary; it is never complete by value. Changing exported type, layout contract, discriminants, exported const value, asynchronous marker, drop/Copy/Send/Sync property or safety contract is ABI/API change.

<a id="R-MOD-0006"></a>

**R-MOD-0006** — Separate translation shall validate imported interface fingerprint including language version и document revision, declaration visibility, canonicalized target-independent type identity, exported constant values, drop/Copy/Send/Sync properties, asynchronous declarations, translation-time evaluability, исходные определения, от которых зависят generic instances и translation-time значения, и exported rule set; mismatch requires diagnostic, not best-effort linkage.

<a id="R-MOD-0007"></a>

**R-MOD-0007** — Canonical module path names exactly one module node in the selected build graph. Duplicate definitions, missing imported path or resolution of one path to multiple source/interface identities require `R-DIAG-MOD-001`; search-path order shall not choose silently. Every import edge records the unique resolved interface identity in module fingerprint.

<a id="c-interoperability"></a>

## 23. C interoperability

### 23.1 `extern "C"` blocks and ABI types

<a id="R-FFI-0001"></a>

**R-FFI-0001** — `extern "C" { declarations }` declares symbols, types and compile-time constants supplied by an external C ABI. It does not define storage, execute C preprocessor or import names not explicitly declared in the block.

<a id="R-FFI-0013"></a>

**R-FFI-0013** — Language string following `extern` shall contain exactly `C`. Every function/object declaration inside block is an import. Function body and external object initializer are forbidden; only `@c_constant` has R initializer. Contained names входят в R module interface по умолчанию; `protected extern "C" { ... }` оставляет их внутри defining module. Ни одна form не defines и не re-exports external C symbols.

<a id="R-FFI-0002"></a>

**R-FFI-0002** — R predeclares distinct ABI types `c_char`, `c_schar`, `c_uchar`, `c_short`, `c_ushort`, `c_int`, `c_uint`, `c_long`, `c_ulong`, `c_llong`, `c_ullong`, `c_bool`, `c_wchar`, `c_wint`, `c_int8`, `c_uint8`, `c_int16`, `c_uint16`, `c_int32`, `c_uint32`, `c_int64`, `c_uint64`, `c_intptr`, `c_uintptr`, `c_intmax`, `c_uintmax`, `c_float`, `c_double`, `c_long_double`, `c_size` and `c_ptrdiff`; `std.c` provides conversions/utilities. Fixed R integer shall not be assumed C-compatible without target proof. Availability of optional typedef-backed types, including `c_wint`, follows R-TYPE-0023 and target manifest.

<a id="R-FFI-0014"></a>

**R-FFI-0014** — C `void` maps to R `void`; object pointer `void*` maps to `raw void*?` или non-null `raw void*` according to contract, а C `const void*` аналогично maps to `raw const void*?` или `raw const void*`. C `_Bool` maps to `c_bool`, never directly to R `bool` at ABI boundary. Empty parameter list `()` в R declaration `extern "C"` означает ровно zero C parameters и verified либо emitted как C prototype form `(void)`; R никогда не exposes C old-style unspecified-parameter form. R result `never` у imported function, callback либо raw C function type является C result `void` функции, которая не возвращается; return из такой C function нарушает её safety contract.

<a id="R-FFI-0003"></a>

**R-FFI-0003** — Function declaration `extern "C"` и каждый raw C function type shall не иметь marker `async` или checked-error set; `throws` не является C ABI mechanism. C object pointer parameter shall быть represented как compatible raw pointer с explicit constness/nullability. Safe borrow или slice, ordinary `str`, `constexpr str`, unique/shared/weak owner, `atomic`, `array`, `list`, `dict`, `task`, `o`, payload enum или standard resource type shall not встречаться на любой nesting depth C signature, включая pointee raw pointer. Managed-token adapter exposes только `raw const void*`; spellings `raw (arc T)*`/`raw (rc T)*` invalid. Ordinary separately verified imported opaque C handle may представлять только свой external C object и не является managed-owner token. Каждый legitimate C pointer level shall быть explicit; например C `T **` may map to `raw (raw T*?)*`.

<a id="R-FFI-0004"></a>

**R-FFI-0004** — Allowed by-value C ABI types: C ABI scalars, raw pointers, raw C function pointers, `@repr(C)` fieldless enums and `@repr(C)` structs recursively composed only of allowed types or fixed C ABI arrays. Every aggregate/enum passed by value shall be trivially Copy, have no user drop and recursively contain no R destruction obligation. Top-level array parameter or return is forbidden because C adjusts/forbids it. Slice, ordinary `str`, `constexpr str`, own, arc, rc, weak owner, borrow, atomic, array, list, dict, task, standard synchronization/thread/channel type, o и payload enum forbidden by value и как recursive field `@repr(C)` aggregate.

<a id="R-FFI-0005"></a>

**R-FFI-0005** — C variadic `...` may occur only after at least one fixed parameter in imported function declaration. Call is unsafe; each variadic argument shall be an allowed promoted C ABI scalar or raw pointer after explicit conversion. `c_float` shall become `c_double`; `c_bool` and every C integer/fieldless-enum type whose target rank does not exceed `c_int` shall become `c_int` if it represents all source values, otherwise `c_uint`. Fixed R types, aggregates and any unpromoted scalar are rejected. R function shall not define `...`.

### 23.2 External types and constants

<a id="R-FFI-0015"></a>

**R-FFI-0015** — `opaque struct Name;` inside `extern "C"` declares an incomplete nominal C type и вводит bare `Name` в aggregate type-name space сразу после declaration identifier. It may occur only behind raw pointer/function-pointer boundaries. `sizeof`, `alignof`, field access, construction, by-value passing, `new`, safe borrow, unique или managed owning form и drop opaque type требуют `R-DIAG-FFI-003`.

<a id="R-FFI-0016"></a>

**R-FFI-0016** — `@c_type(name = "CName", kind = "typedef")` maps R aggregate type name to C typedef. Kinds `"struct"`, `"union"` and `"enum"` map to corresponding C tag name. CName shall be one ASCII C identifier, shall not be a C17 keyword and is additionally governed by R-FFI-0057. `"union"` is allowed only for opaque R declaration; a complete C union requires wrapper functions.

<a id="R-FFI-0017"></a>

**R-FFI-0017** — Complete struct/fieldless enum declared inside `extern "C"` shall carry `@repr(C)` and `@c_type`, use only types permitted by R-FFI-0004 and match verified C declaration. Struct field name, count, order, type, qualifiers, size, alignment and offset shall match a complete normalized member inventory; layout assertions alone are insufficient because an omitted C member may occupy apparent R padding. For fieldless enum, underlying representation and every R-declared variant name and value shall correspond one-for-one to a complete normalized C enumerator inventory; C declaration order need not match, but an omitted, additional or aliased C enumerator is a mismatch. A C enum that cannot satisfy R’s unique-discriminant rule shall instead be mediated as its compatible C integer plus verified constants or by a C shim. Different signedness, qualifier or ABI-significant property is mismatch.

<a id="R-FFI-0018"></a>

**R-FFI-0018** — A declaration with `@c_constant(name = "C_NAME") const CTYPE NAME = constant-expression;` mirrors a C integer enum constant or object-like integer macro without importing a symbol. Top-level `const` is mandatory; CTYPE shall be a supported C ABI integer or fieldless enum type, C_NAME a non-keyword ASCII C identifier, and initializer a representable integer constant-expression; reserved spellings follow R-FFI-0057. It has no address or storage. Header/ABI verification shall match both mathematical value and normalized type of the C expanded expression, including integer rank, signedness and corresponding enum identity when expression itself has enum type. In C17 a bare enumerator has type `int`; a macro cast to an enum retains that enum type. A merely representable value of a different C type is a mismatch. Floating, string, pointer and aggregate C constants require target-generated ABI accessor support or an external C function shim and are not `@c_constant` forms in R 0.1.

<a id="R-FFI-0019"></a>

**R-FFI-0019** — C bit-fields, flexible array members, packed/anonymous aggregate members, vector/complex types, variable-length arrays, complete unions, C `_Atomic`, variadic function-pointer types, `restrict` and `volatile`-qualified ABI types are not directly expressible. Library integration shall expose opaque type plus C wrapper/verified bridge or a separately standardized ABI type rather than guess layout or erase qualifier.

<a id="R-FFI-0020"></a>

**R-FFI-0020** — Function-like macros, `_Generic`, inline-only functions and preprocessor conditionals are not importable symbols. Bindings shall use a C shim with an external C function or an independently verified R constant.

### 23.3 Imported functions, objects and callbacks

<a id="R-FFI-0021"></a>

**R-FFI-0021** — Imported function prototype shall exactly match C return type, parameter count/order/types, variadic status and default C calling convention. Parameter names are documentary. C array parameters shall be declared as raw pointers because C adjusts them to pointer types. `extern "C"` R 0.1 supports only target default C calling convention. Header-specific non-default convention shall be exposed by an externally supplied C shim with default C ABI; otherwise требуется `R-DIAG-FFI-001`.

<a id="R-FFI-0022"></a>

**R-FFI-0022** — `TYPE name;` inside `extern "C"` imports a C object symbol. `thread_local TYPE name;` imports C `_Thread_local` object. External object never starts R ownership and is never automatically dropped.

<a id="R-FFI-0023"></a>

**R-FFI-0023** — Reading, writing, taking address of or borrowing an imported C object is unsafe. Even `const` object access requires contract that library is loaded, initialization completed, representation valid and concurrent mutation excluded. `thread_local` additionally refers only to current attached thread.

<a id="R-FFI-0024"></a>

**R-FFI-0024** — `extern "C"` function definition exports one C-callable symbol. It shall have only R-FFI-0004 signature types, no captures or general R calling convention, and shall have `@safety` describing obligations of foreign caller. It shall carry `@callback` if its name is converted to raw function pointer.

<a id="R-FFI-0012"></a>

**R-FFI-0012** — C callback into R shall use a compatible exported `extern "C"` function. C caller shall ensure R runtime is initialized and current thread is either already attached or eligible for temporary attachment by the entry trampoline. Trampoline per R-FFI-0055 attaches before R body. Caller shall also satisfy lifetime, aliasing and representation contracts for all raw arguments. Panic at this boundary aborts per R-ERR-0006.

<a id="R-FFI-0025"></a>

**R-FFI-0025** — In expected matching `raw fn(...) -> R` context, name of exported C function bearing `@callback` may convert to raw function pointer without unsafe operation. Missing `@callback`, mismatched prototype or calling convention requires `R-DIAG-FFI-005`; calling through resulting pointer remains unsafe.

<a id="R-FFI-0026"></a>

**R-FFI-0026** — If C may retain a pointer, callback or userdata after call returns, the declaration safety contract shall state retention duration and release event. R borrow with shorter lifetime shall not be passed; owner transfer requires explicit release/adoption protocol.

<a id="R-FFI-0027"></a>

**R-FFI-0027** — Weak imports, symbol interposition promises, versioned symbol syntax and runtime `dlopen`/`dlsym` are not part of `extern "C"` R 0.1. Missing required symbol shall not be represented by null function pointer; optional APIs need a separately specified dynamic-loading wrapper.

### 23.4 External library binding and link manifest

<a id="R-FFI-0028"></a>

**R-FFI-0028** — Optional singleton block attribute `@link(name = "logical-name", kind = "kind")` associates all symbol declarations in the block with exactly one provider library. Logical name identifies a target-manifest entry and shall not be interpreted as path, filename or linker option. Additional link dependencies belong only to that manifest entry. Логическое имя библиотеки состоит из одного или нескольких непустых сегментов, разделённых ровно одним ASCII U+002E FULL STOP, U+005F LOW LINE или U+002D HYPHEN-MINUS. Каждый сегмент начинается со строчной ASCII-буквы и продолжается нулём или более строчными ASCII-буквами либо десятичными цифрами. Длина кодирования составляет от одного до 255 байтов. Это синтаксис логического имени R-FFI, используемый запросами к манифесту библиотек.

<a id="R-FFI-0029"></a>

**R-FFI-0029** — Link kind shall be one of: `"static"` (archive incorporated at link), `"dynamic"` (load-time shared library plus any import library), `"framework"` (target framework exporting C ABI) or `"system"` (toolchain-owned stable system library). Unsupported kind/target combination requires diagnostic.

<a id="R-FFI-0030"></a>

**R-FFI-0030** — Absence of `@link` selects exactly the target manifest’s implicit C runtime link entry as provider for symbol inventory, header roots and ABI-record roots. Such block may import only symbols and use only evidence owned by that entry. Third-party header/ABI evidence requires explicit `@link` even when block declares only types or constants and imports no symbol. Реализация shall not search arbitrary host libraries to satisfy an undeclared symbol, choose evidence roots or infer provider from link success.

<a id="R-FFI-0031"></a>

**R-FFI-0031** — Link manifest entry shall contain at least: logical name; kind; target triple and C ABI identity; compile/link artifact identities; header/ABI record roots; library version; content digest for non-system artifacts; dependency logical names; runtime deployment identity for dynamic/framework artifact; and all feature-test definitions used by ABI verification. It shall also contain the normalized export/re-export symbol inventory claimed for direct imports. Each inventory entry records C source identifier, exact target linkage spelling, function/data/TLS kind, strong/weak status, defining artifact/provider identity and explicit re-export chain. Target identity incorporates floating-environment choice R-IDB-020 and every extra control affecting values, flags or traps.

<a id="R-FFI-0032"></a>

**R-FFI-0032** — Physical paths, search directories, import-library names, sonames, install names, DLL names, framework roots and linker options belong only to build manifest. `@link` and `@abi` accept logical identifiers, while `@header` accepts only relative include spelling constrained by R-FFI-0039. Physical/absolute source value is a constraint violation. Relative manifest paths resolve from manifest location.

<a id="R-FFI-0033"></a>

**R-FFI-0033** — Library resolution shall use selected target triple, sysroot and C ABI; host fallback is forbidden during cross-compilation. Artifact architecture, object format and ABI mismatch require `R-DIAG-LINK-001` before executable emission.

<a id="R-FFI-0034"></a>

**R-FFI-0034** — Same logical library/kind selected by multiple extern blocks with identical manifest identity coalesces. Same logical name resolving to different value of any normalized identity field — including kind, version, digest, target, ABI, feature macros, roots, dependency/runtime identity or symbol inventory — in one program requires `R-DIAG-LINK-002`.

<a id="R-FFI-0035"></a>

**R-FFI-0035** — Manifest dependency graph determines deterministic link order. Dependency shall follow its dependent where target static linker requires it. Unresolvable static cycle requires diagnostic unless manifest describes a target link-group mechanism whose result is deterministic.

<a id="R-FFI-0036"></a>

**R-FFI-0036** — Each imported symbol shall occur in selected provider’s declared export/re-export inventory (or documented implicit runtime inventory) and resolve to that provider identity. Same external spelling in multiple imports is permitted only for the same normalized defining provider or one explicit re-export chain, unless target manifest proves a provider-specific binding mechanism. Missing or kind-mismatched inventory entry, multiple provider claims or unresolved symbol requires `R-DIAG-LINK-003`; dependency/host search shall not silently choose a different same-spelled symbol.

<a id="R-FFI-0037"></a>

**R-FFI-0037** — Dynamic/framework dependency shall remain loaded from before module initialization through completion of all R drops. Required-provider loading и required-symbol readiness являются первой pre-main phase R-AM-0002. Их failure shall terminate до native argument conversion, `main` либо module initializers с category `link_load_failure`; process status is implementation-defined and documented.

### 23.5 Header and ABI verification

<a id="R-FFI-0038"></a>

**R-FFI-0038** — Every non-empty `extern "C"` block shall carry at least one `@header("logical/header.h")` or `@abi("logical-abi-record")`. Absence requires `R-DIAG-FFI-006`; linking alone is not proof of prototype/layout compatibility.

<a id="R-FFI-0039"></a>

**R-FFI-0039** — `@header` names a header relative to roots of the provider selected by `@link` or R-FFI-0030. It shall not be absolute, contain `.`/`..` path component, backslash, NUL or platform drive prefix. Header is compiled only by ABI verifier with exact target compiler, sysroot, language mode and feature-test definitions; it does not inject tokens/names into R.

<a id="R-FFI-0040"></a>

**R-FFI-0040** — `@abi` names, relative to ABI-record roots of the provider selected by `@link` or R-FFI-0030, an immutable generated ABI record containing target, C implementation identity/options, sysroot, feature macros, complete normalized manifest/provider/artifact identity, header digests, declaration/member inventories, layouts, constants and typed symbol inventory. Any identity/digest mismatch requires fresh generation, not best-effort reuse. If one block supplies both `@header` and `@abi`, fresh header-derived declarations/layout/constants shall agree with record, and manifest-derived provider/symbol facts shall agree separately; neither source overrides the other.

<a id="R-FFI-0041"></a>

**R-FFI-0041** — ABI verifier shall check every imported function prototype and calling convention, global object type/TLS status, `@repr(C)` size/alignment/field offset and complete member inventory, complete one-to-one imported enum enumerator inventory and representation, `@c_constant` mathematical value plus normalized expanded-expression type/rank/ signedness, C type-name mapping and typed required symbol/provider. Complete by-value aggregate or enum requires an immutable normalized inventory produced by the selected compiler AST/ABI generator. `@header` alone is sufficient only if the verifier produces and records an equivalent complete inventory; `sizeof`, `_Alignof`, `offsetof` and type assignments alone are not sufficient proof.

<a id="R-FFI-0042"></a>

**R-FFI-0042** — Header verification shall use a generated C17 translation unit with the declared headers, `_Static_assert` for values/layout and warnings-as-errors type assignments for functions/objects, plus compiler-derived normalized declaration inventory whenever R-FFI-0041 requires it. Inventory extraction is data generation, not permission to accept a language extension in generated bridge. Any mismatch or inability to prove a required property requires `R-DIAG-FFI-004`.

<a id="R-FFI-0043"></a>

**R-FFI-0043** — Header order is source `@header` order; repeated identical header coalesces. Required feature macros shall come from manifest and be part of its identity; R source shall not define preprocessor macros.

<a id="R-FFI-0044"></a>

**R-FFI-0044** — Library manifest identity, headers/ABI record digests, verified declarations, `@fenv` assertions and every exported C function shall enter module interface/build fingerprint. Change requires re-verification and relink of affected dependency graph.

<a id="R-FFI-0009"></a>

**R-FFI-0009** — C headers are not parsed as R and `#include` is absent. They are verification evidence only. Binding generator may produce explicit R declarations, but generated output is reviewed/versioned source subject to the same rules.

### 23.6 Layout, symbol names and exports

<a id="R-FFI-0006"></a>

**R-FFI-0006** — `@repr(C)` struct shall match field order, size, alignment and padding of equivalent C17 struct on selected target. Bit-fields, flexible array members, anonymous members and packed layout are not expressible in R 0.1.

<a id="R-FFI-0007"></a>

**R-FFI-0007** — `@link_name("symbol")` selects imported C identifier; `@export_name("symbol")` selects exported C identifier. String shall match ASCII C identifier pattern `[A-Za-z_][A-Za-z0-9_]*`, shall not be a C17 keyword and is additionally governed by R-FFI-0057. Multiple compatible imports may name the same external symbol only under single-provider rule R-FFI-0036; every exported definition name shall be unique in linked program. Arbitrary linker spellings require an external C shim.

<a id="R-FFI-0045"></a>

**R-FFI-0045** — Without `@link_name`/`@export_name`, imported/exported C symbol name is its R identifier encoded as ASCII and shall itself be a valid C identifier; it shall also satisfy R-FFI-0007 and R-FFI-0057. Non-ASCII R identifier therefore requires the corresponding explicit valid C-identifier name. Backend emits/calls that actual C identifier. No C++ mangling, assembler label alias, prefix/suffix or Windows decoration guessing is performed.

<a id="R-FFI-0046"></a>

**R-FFI-0046** — R 0.1 exports C functions only. Exported global objects, TLS, linker aliases and symbol-version scripts require a later ABI extension or C shim.

<a id="R-FFI-0008"></a>

**R-FFI-0008** — Exported function shall not let panic cross C boundary; runtime shall abort per R-ERR-0006. It shall not expose R-owned lifetime unless API uses paired raw handle functions with documented ownership contract.

### 23.7 Unsafe boundary and safe wrappers

<a id="R-FFI-0010"></a>

**R-FFI-0010** — Every imported C function is unsafe and shall have `@safety("CONTRACT-ID", "preconditions")`. `unsafe` keyword in extern block is optional documentary redundancy; call requires unsafe context. A reviewed R wrapper may be safe only when its signature/checks establish every precondition.

<a id="R-FFI-0011"></a>

**R-FFI-0011** — `core::adopt(pointer)` и `core::release(move owner)` по R-UNSAFE-0008 являются exact operations для transfer одной compatible single-T allocation duty через эту boundary. Adopting C allocation как `own T*` требует proof unique ownership, initialized valid T, compatible allocator/deallocator и alignment. Releasing R owner в C consumes owner и suppresses automatic drop только один раз.

<a id="R-FFI-0047"></a>

**R-FFI-0047** — NUL-terminated C string is raw byte sequence, а не ordinary R `str` или `constexpr str`. Conversion shall validate non-null/range/terminator and chosen encoding; passing R string to C shall provide stable NUL-terminated storage for complete retention time.

<a id="R-FFI-0048"></a>

**R-FFI-0048** — C `longjmp`, foreign exception, signal callback, process termination, allocator mismatch, callback after unload and C data race across live R frame are outside ordinary call contract and shall be explicitly excluded or mediated by a C shim. They shall not cross R scopes containing live Move objects.

<a id="R-FFI-0049"></a>

**R-FFI-0049** — Safe wrapper shall validate return discriminants, lengths, error codes, pointer nullability, ownership, library lifetime and every API-specific invariant before constructing safe R value. C scalar machine-representation limits follow R-FFI-0056. Link/ABI verification does not replace these runtime safety checks. Address of an existing safe R object may be passed for C output only if contract guarantees a valid initialized representation on every return path; otherwise output shall use unchecked ABI storage and R-FFI-0056 gate.

<a id="R-FFI-0050"></a>

**R-FFI-0050** — FFI attribute scopes are closed: `@link`, `@header`, `@abi` on extern block; `@link_name` on imported symbol; `@safety` on imported or exported C function; `@fenv` on imported C function; `@repr`, `@c_type` on C type; `@c_constant` on verified constant; `@export_name`, `@callback` on exported C function. `@header` is repeatable; `@link` and `@abi` are singleton per block, and every declaration-scoped FFI attribute is singleton on its declaration. Misplaced, duplicated-singleton or unknown FFI attribute requires `R-DIAG-FFI-003`.

<a id="R-FFI-0051"></a>

**R-FFI-0051** — Named attribute argument may occur once; required names and value types shall match attribute definition. Positional forms are allowed only where shown (`@header("...")`, `@abi("...")`, `@link_name("...")`, `@export_name("...")`, `@safety("...", "...")`, `@fenv("preserve")`, `@repr(C)`).

<a id="R-FFI-0052"></a>

**R-FFI-0052** — Logical library and ABI-record identifier shall match ASCII pattern `[A-Za-z0-9][A-Za-z0-9_.+-]{0,254}`. Header spelling is a non-empty portable ASCII relative include spelling whose non-empty `/`-separated components each match `[A-Za-z0-9_.+-]+`; it is resolved only against manifest header roots and a filename suffix is not required. Empty, `.` or `..` component, backslash, NUL, drive/absolute prefix are invalid. Invalid `@link` name has primary `R-DIAG-LINK-001`; invalid `@abi`/`@header` name has `R-DIAG-FFI-003`.

<a id="R-FFI-0053"></a>

**R-FFI-0053** — Platform loader may execute external library initialization before R module initialization and finalization after R drops. Such code is outside R abstract machine but shall satisfy external-code contract: it shall not access an uninitialized/destroyed R object, unwind into R or race with R without synchronization.

<a id="R-FFI-0054"></a>

**R-FFI-0054** — Outside an extern block, `extern "C"` is valid only on a non-`protected` function definition satisfying R-FFI-0024. Protected definition, body-less prototype, object declaration либо any other top-level `extern` use is a constraint violation; imports shall be written inside verified `extern "C"` block.

<a id="R-FFI-0055"></a>

**R-FFI-0055** — C-origin thread entering any exported `extern "C"` R function shall be attached by entry trampoline or explicit `std.c` protocol before any R execution, whether or not declaration bears `@callback`. Attachment has per-thread owner and nesting depth. Entry on already attached thread increments depth and shall not detach an outer explicit/R-created context. A trampoline-created temporary attachment detaches only after its outermost C-origin entry returns; explicit attachment persists until explicit detach. Detach at depth zero requires no live R frame/borrow/value retained by that attachment generation and performs its R thread-local drops exactly once. While runtime and module remain live, a later entry on the same C thread may create a fresh temporary attachment generation with fresh thread-local instances. Failure to establish a required temporary attachment aborts before safe R argument materialization or exported body. Provider shall stop and join future entry sources before R module destruction; entry after runtime shutdown or provider/module unload violates external contract.

<a id="R-FFI-0056"></a>

**R-FFI-0056** — Value entering R by value from C shall first occupy an internal unchecked ABI slot. Backend shall recursively validate non-null pointers/function pointers, R-declared enum discriminants and every nested `@repr(C)` field carrying such a semantic invariant before constructing ordinary R value. C ABI `c_bool`, `c_char`, `c_wchar` and `c_wint` are already C semantic values and are not checked as R bool/Unicode char. A noncanonical/trap machine representation that cannot be read through its correct C type is foreign-C UB before portable R ingress and requires target-specific external shim if mediation is needed. Invalid detectable imported return/object read causes `contract_violation` panic; invalid argument to an exported `extern "C"` entry aborts before exported R body per R-ERR-0006. This gate does not replace API-specific length, ownership, aliasing or error-code checks.

<a id="R-FFI-0057"></a>

**R-FFI-0057** — Any spelling emitted by backend as a C identifier shall not be a C17 keyword. Exported C identifier shall not belong to an identifier class reserved to the C implementation by ISO C17 7.1.3. Imported symbol or type spelling in a reserved class shall not be redeclared by generated code: it requires declaration from a verified `@header` and a dedicated bridge that includes that header and exposes a unique non-reserved private name. An ABI record alone does not authorize such redeclaration; without the header-compatible bridge user shall provide an external default-C-ABI shim. A `@c_constant` substituted solely from verified ABI record emits no C identifier and is unaffected by the redeclaration restriction.

<a id="R-FFI-0058"></a>

**R-FFI-0058** — Change of floating-point environment made by imported C code shall not affect a later R floating operation and R-generated flags shall not leak into C. Before every outbound C-ABI call, whether direct import or call through raw C function pointer, boundary saves host environment and establishes canonical C-entry environment: `FE_DFL_ENV`, `FE_TONEAREST`, all supported standard exception flags clear, default trap/non-stop state, and manifest-normalized extra controls such as gradual-underflow mode. It restores exact saved R host environment on every normal return. Every C-origin entry into an exported `extern "C"` R function saves exact foreign rounding, flags, trap and extra state, establishes the same canonical environment before R code, and restores the foreign state on normal return. R 0.1 does not expose inherited or persistent foreign fenv across calls; API requiring it shall use one external C shim enclosing all dependent C operations. Target manifest selects R-IDB-020; a proven immutable already-canonical environment may use no-op. If no mechanism exists, only a direct import with machine-readable R-FFI-0059 assertion is accepted; raw C function-pointer calls and every exported `extern "C"` definition require `R-DIAG-FFI-001`. An implementation using a verified external isolation shim shall classify it as R-IDB-020 verified runtime-helper mode rather than unavailable fallback. Failure to save or establish canonical state before outbound C causes `contract_violation` panic without the call only if host state was unchanged or exact restoration succeeded; otherwise it shall abort immediately. Failure to restore after foreign execution, or any save/establish/restore failure at a C-origin entry boundary, shall abort before further R/C execution.

<a id="R-FFI-0059"></a>

**R-FFI-0059** — Optional singleton `@fenv("preserve")` on imported C function is a machine-readable external-contract assertion that function neither observes nor changes rounding mode, exception flags, trap state or any manifest-listed extra floating control. Absence means `may_change_or_observe`. On R-IDB-020 unavailable target only direct imported function declaration bearing `preserve` is permitted; call through arbitrary raw C function pointer and every exported `extern "C"` definition are rejected with `R-DIAG-FFI-001`. False `preserve` assertion violates external safety contract and is UB. Any other value/scope requires `R-DIAG-FFI-003`.

<a id="R-FFI-0060"></a>

**R-FFI-0060** — C library may retain одну shared-R strong ownership obligation только через unsafe adapter вокруг matching `std.arc::into_raw` либо `std.rc::into_raw`. Exported non-null C representation — `raw const void*`; conversion к/из internal `raw const (T)*` является explicit unsafe zero-offset object-pointer conversion, preserving allocation identity. Copying pointer bits не создаёт obligation. Каждый retain shall создать один R strong clone и одну additional `into_raw` obligation; каждый release shall reconstruct и drop ровно одну obligation. Все obligations shall быть released до defining-module/runtime shutdown, если runtime не удерживает module, его drop glue и allocator live до последней obligation. `arc` obligation may пересекать C threads только если T Send+Sync и C transfer establishes required synchronization. `rc` obligation никогда не пересекает originating R thread; на C-origin thread она may пережить calls только в той же live explicit attachment generation и не может пережить temporary attachment. Каждый C-origin managed-token adapter shall attach по R-FFI-0055 либо equivalent trampoline до pointer reconstruction, count mutation или R drop; attachment/fenv failure aborts до изменения token/count state, а temporary detach происходит только когда reconstructed R value больше не live. Forged, repeated, wrong-family, wrong-T, late или wrong-thread/generation use нарушает unsafe external contract. Raw pointer, borrowed from T без strong obligation, may retained только пока separately documented owner remains live.

<a id="standard-library"></a>

## 24. Интеграция со стандартной библиотекой R

<a id="R-LIBREF-0001"></a>

**R-LIBREF-0001** — R Standard Library Specification 0.1, named R-REF-0005, является normative companion library standard для этой language revision. Ему принадлежат complete rules `R-LIB-0001` through `R-LIB-0024`, перенесённые из прежнего combined draft, и каждый rule `R-SLIB-...`. Reference этого документа на один из этих identifiers resolves в companion document.

<a id="R-LIBREF-0002"></a>

**R-LIBREF-0002** — Compiler-recognized standard types, constructors, qualified names, outcome variants и type-operand calls syntactically admitted только Annex A. Их exact signatures, ownership, allocation, panic, cancellation, deadline и error contracts определяет companion library standard. Library edition shall not добавлять source syntax либо ослаблять любое Core requirement ownership, borrow, evaluation-order, Send/Sync, FFI или diagnostics.

<a id="R-LIBREF-0003"></a>

**R-LIBREF-0003** — Selected target profile определяет available standard modules. `hosted-native-async` требует runtime `std.async` и complete native asynchronous I/O surface; target, у которого отсутствует хотя бы один mandatory backend либо обязательная cancellation/deadline capability, shall reject этот profile во время selection. Generated application code остаётся strict C17, а verified target-specific asynchronous adapters остаются внутри runtime boundary Annex F.

<a id="implementation-limits"></a>

## 25. Implementation limits

<a id="R-LIMIT-0001"></a>

**R-LIMIT-0001** — Implementation may impose finite documented limits but shall accept at least: 127 nested blocks; 127 declarator/type nesting levels; 127 parameters and arguments; 1023 struct fields; 1023 enum variants; 4095 identifiers with external/module linkage per program; 4095 bytes in one string literal after concatenation; 127 imported modules per module; 256 significant Unicode scalars per identifier; 127 non-empty `extern "C"` blocks per module; 1023 C declarations per block; 127 logical libraries per program; and 255 UTF-8 bytes per logical library/header/ABI-record name.

<a id="R-LIMIT-0002"></a>

**R-LIMIT-0002** — Source file of at least 1 MiB, module graph of at least 1024 modules and array object of at least 65535 bytes shall be supported when target address space can represent them.

<a id="R-LIMIT-0003"></a>

**R-LIMIT-0003** — Exceeding implementation limit shall produce `R-DIAG-LIMIT-001` at translation time if input was received completely. Silent miscompilation, wraparound counters or uncontrolled recursion are non-conforming.

<a id="R-LIMIT-0004"></a>

**R-LIMIT-0004** — Runtime stack/heap exhaustion shall cause defined panic or documented process termination without executing invalid memory access. Exact resource quantities remain environmental, not language limits.

<a id="R-LIMIT-0005"></a>

**R-LIMIT-0005** — Conforming implementation shall expose machine-readable target manifest containing actual limits, `usize` width, endian, alignments, panic strategy, atomic lock-free properties, C compiler/ABI identity, selected profile и, когда applicable, record native asynchronous backend R-IDB-021.

<a id="bounded-implementation-0-1"></a>

### 25.1 Границы реализации callable и provenance (informative)

Эталонный компилятор понижает checked и async closures, замыкания внутри generic-функций и синхронные generic-представления с заимствованиями через обычные HIR, MIR и C17. Interface schema 31 записывает ресурсные контракты, значимость и отбрасываемость результата и происхождение заимствований. Обычный результат и каждая checked error имеют отдельные границы регионов; вложенные projections консервативно используют соответствующее объединение.

Контракты не сериализуют исполняемые generic-тела: между модулями нужны исходные определения. Отдельный исходный prototype не восполняет отсутствующее происхождение заимствованного результата. Borrowed captures и views не становятся Send или unborrowed и не могут оставаться живыми через await вне займа группы задач либо передаваться в unscoped task/thread. Ресурсные callable-контракты не делают async start, allocator internals или текущий mutex-based executor свободными от аллокаций и блокировок. `@must_use` проверяет непрочитанные bindings, а не линейный протокол, а `@discardable` ослабляет лишь отбрасывание собственного результата в позиции оператора. Ограничения target/profile и реализации сохраняются; неподдерживаемая конструкция требует диагностики, а не непроверенного понижения.

Вычисление при трансляции (R-FUNC-0023, R-EXPR-0032) интерпретирует проверенный HIR эталонного компилятора. Его документированные лимиты — 4000000 шагов вычисления и 64 MiB значений на одно вычисление; значение больше 256 скалярных элементов не подставляется в тело функции, а объект модуля или static-объект хранит значение любого размера как один статический initializer. Объявление уровня модуля, которому нужно такое значение, проверяется после разведочного прохода по тем же исходникам; тот же проход вычисляет константные условия `@if` уровня модуля (R-META-0002) до выбора объявлений, вычисляемые формулы generic-инстансов, закрытых на уровне модуля (R-TYPE-0047), и связанные константы, которые объявления уровня модуля используют до проверки реализаций (R-TYPE-0050). Каждый проход вычисляет значения, ставшие доступными после предыдущих; программа, чьи значения уровня модуля продолжают прибавляться после 32 проходов, требует `R-DIAG-LIMIT-001`. `long double` цели, формат которого не binary64, остаётся run-time значением, а dictionary с ключами с плавающей точкой или `std.string::string` не замораживается в образ программы. Interface schema 31 записывает значение экспортируемого объекта каноническим термом его проверенного инициализатора, поэтому изменение экспортируемого const-значения (R-MOD-0005) меняет интерфейс.

<a id="annex-a"></a>

## Annex A — Normative EBNF grammar

### A.1 Notation

В grammar ниже terminal заключён в кавычки, `[ X ]` означает optional X, `{ X }` — zero or more repetitions, `X, Y` — sequence, `X | Y` — choice, а `;` завершает production. Precedence от higher к lower: grouping/postfix `[]`/`{}`; comma sequence; choice `|`. Choice associates left only for notation and does not change generated alternatives. Внутри quoted EBNF terminal metacharacter `\` escapes только `\` и `"`: `\\` denotes one U+005C and `\"` one U+0022; в иных случаях code points literal. `identifier` и literals являются lexical terminals. Whitespace и comments разделяют tokens. Semantic constraints основного текста дополняют grammar. `quotation-mark` denotes U+0022. `unicode-XID-Start`, `unicode-XID-Continue` и terminals `unicode-scalar-except-...` являются meta-terminals, определёнными R-LEX-0001, R-LEX-0006 и указанной Unicode version, а не пропущенными productions.

<a id="R-GRAM-A001"></a>

**R-GRAM-A001** — Реализация shall принимать каждую token sequence, выводимую из этой grammar и удовлетворяющую constraints, и shall reject sequence, не выводимую из grammar, в strict mode.

### A.2 Lexical grammar

```ebnf
identifier          = identifier-start, { identifier-continue } ;
identifier-start    = "_" | unicode-XID-Start ;
identifier-continue = unicode-XID-Continue ;
binary-digit        = "0" | "1" ;
octal-digit         = "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" ;
decimal-digit       = octal-digit | "8" | "9" ;
hex-digit           = decimal-digit | "a" | "b" | "c" | "d" | "e" | "f"
                    | "A" | "B" | "C" | "D" | "E" | "F" ;
decimal-digits      = decimal-digit, { [ "_" ], decimal-digit } ;
hex-digits          = hex-digit, { [ "_" ], hex-digit } ;

decimal-literal     = decimal-digit, { [ "_" ], decimal-digit }, [ integer-suffix ] ;
binary-literal      = "0b", binary-digit, { [ "_" ], binary-digit }, [ integer-suffix ] ;
octal-literal       = "0o", octal-digit, { [ "_" ], octal-digit }, [ integer-suffix ] ;
hex-literal         = "0x", hex-digit, { [ "_" ], hex-digit }, [ integer-suffix ] ;
integer-literal     = decimal-literal | binary-literal | octal-literal | hex-literal ;
integer-suffix      = "i8" | "u8" | "i16" | "u16" | "i32" | "u32"
                    | "i64" | "u64" | "isize" | "usize" ;

floating-literal    = decimal-float | hexadecimal-float ;
decimal-float       = decimal-digits, ".", decimal-digits, [ decimal-exponent ], [ float-suffix ]
                    | decimal-digits, decimal-exponent, [ float-suffix ] ;
hexadecimal-float   = "0x", hex-digits, [ ".", [ hex-digits ] ], binary-exponent, [ float-suffix ] ;
decimal-exponent    = ( "e" | "E" ), [ "+" | "-" ], decimal-digits ;
binary-exponent     = ( "p" | "P" ), [ "+" | "-" ], decimal-digits ;
float-suffix        = "f32" | "f64" ;

character-literal   = "'", character-element, "'" ;
string-literal      = quotation-mark, { string-element }, quotation-mark ;
formatted-literal   = "f", quotation-mark, { format-element }, quotation-mark ;
format-element      = unicode-scalar-except-double-quote-backslash-newline-braces
                    | escape-sequence | "{{" | "}}" | format-slot ;
format-slot         = "{", ( format-path | format-index ), [ ":", format-spec ], "}" ;
format-path         = identifier, { ( "." | "->" ), identifier } ;
format-index        = decimal-digit, { decimal-digit } ;
format-spec         = format-width, [ format-presentation ] | format-presentation ;
format-width        = decimal-digit, { decimal-digit } ;
format-presentation = "d" | "x" | "X" | "b" | "o"
                    | ".", decimal-digit, { decimal-digit } ;
format-sequence     = { string-literal }, formatted-literal,
                      { string-literal | formatted-literal } ;
format-suffix       = ".", "format", "(", argument-list, ")" ;
character-element   = unicode-scalar-except-quote-backslash-newline | escape-sequence ;
string-element      = unicode-scalar-except-double-quote-backslash-newline | escape-sequence ;
escape-sequence     = "\\\\" | "\\\"" | "\\'" | "\\n" | "\\r" | "\\t" | "\\0"
                    | "\\x", hex-digit, hex-digit
                    | "\\u{", hex-digit, { hex-digit }, "}" ;
```

U+005F is already a member of Unicode `XID_Continue`, поэтому отдельная continuation alternative не требуется.

Keywords include all words from R-LEX-0008 and all primitive/type words:

```
bool char i8 u8 i16 u16 i32 u32 i64 u64 isize usize ai8 ai16 ai32 ai64 aisize au8 au16 au32 au64 ausize f32 f64 str constexpr
o array list dict task async await catch finally throw throws fn opaque dyn
c_char c_schar c_uchar c_short c_ushort c_int c_uint c_long c_ulong
c_llong c_ullong c_bool c_wchar c_wint c_int8 c_uint8 c_int16 c_uint16
c_int32 c_uint32 c_int64 c_uint64 c_intptr c_uintptr c_intmax c_uintmax
c_float c_double c_long_double c_size c_ptrdiff
```

### A.3 Translation units and declarations

```ebnf
translation-unit    = [ module-declaration ], { import-declaration }, { external-declaration } ;
module-declaration  = { attribute }, "module", module-path, ";" ;
module-path         = identifier, { ".", identifier } ;
import-declaration  = "import", module-path,
                      [ "::", "{", identifier, { ",", identifier }, [ "," ], "}" ], ";" ;

external-declaration = static-if-declaration | { attribute },
                       ( generic-header, { attribute },
                         ( [ "protected" ], ( aggregate-declaration | function-declaration )
                         | impl-declaration | drop-definition )
                       | [ "protected" ], ( aggregate-declaration | function-declaration
                                          | module-object-declaration | extern-block
                                          | trait-declaration )
                       | impl-declaration | drop-definition ) ;

static-if-declaration = "@", "if", "(", static-predicate, ")", static-declarations,
                        [ "@", "else", ( static-declarations | static-if-declaration ) ] ;
static-declarations = "{", { external-declaration }, "}" ;
static-predicate    = static-conjunction, { "||", static-conjunction } ;
static-conjunction  = static-atom, { "&&", static-atom } ;
static-atom         = "!", static-atom | "(", static-predicate, ")"
                    | type, "is", ( type | generic-constraint )
                    | "core", "::", "profile", "is", static-profile
                    | "core", "::", "target", "is", string-literal
                    | static-value-condition ;
static-value-condition = "true" | "false"
                    | condition-value-expression, comparison-operator,
                      condition-value-expression
                    | membership-expression ;
static-profile      = "freestanding" | "allocation" | "hosted" | "hosted-thread"
                    | "hosted-native-async" ;

generic-header      = "@", "generic", "<", generic-parameter,
                      { ",", generic-parameter }, ">" ;
generic-parameter   = identifier, [ "..." ], [ ":", generic-constraint,
                      { "&", generic-constraint } ]
                    | "const", constant-parameter-type, identifier
                    | identifier, "::", identifier, ":", generic-constraint,
                      { "&", generic-constraint } ;
constant-parameter-type = "i8" | "i16" | "i32" | "i64" | "isize" | "u8" | "u16" | "u32"
                    | "u64" | "usize" | "bool" ;
generic-constraint  = "copy" | "pod" | "send" | "sync" | "key" | "error" | "unborrowed"
                    | "json_encode" | "json_decode" | "errors" | "clone" | trait-application
                    | callable-constraint ;
trait-name          = identifier | module-path, "::", identifier | core-trait-name ;
trait-application   = trait-name, [ "<", type, { ",", type }, ">" ] ;
core-trait-name     = "core", "::", ( "Iterator" | "Contains" ) ;
callable-constraint = [ "async" ], "fn", { callable-resource-attribute }, [ callable-mode ],
                      "(", [ callable-parameters ], ")", "->", type, [ callable-throws ] ;
callable-parameters = parameter-type, { ",", parameter-type }, [ "..." ] ;
callable-resource-attribute = "@", ( "noalloc" | "nonblocking" ) ;
callable-throws     = "throws", "(", type, { ",", type }, ")" ;
callable-mode       = "shared" | "mut" | "once" ;

attribute           = "@", identifier, [ "(", [ attribute-argument,
                      { ",", attribute-argument } ], ")" ] | json-attribute ;
attribute-argument  = [ identifier, "=" ], attribute-value ;
attribute-value     = identifier | integer-literal | string-literal | type ;

json-attribute      = "@", "json", "(", [ json-argument,
                      { ",", json-argument } ], ")" ;
json-argument       = ( "name" | "case" ), "=", string-literal
                    | "skip" | "omitempty" | "omitzero" | "string" | "embed"
                    | "optional" | "omitnone" | "default", "=", json-default ;
json-default        = [ "-" ], ( integer-literal | floating-literal )
                    | character-literal | string-literal | "true" | "false"
                    | identifier | qualified-name ;

aggregate-declaration = struct-declaration | enum-declaration | error-declaration ;
error-declaration   = "error", identifier, [ ":", aggregate-type-name ], "{",
                      { field-declaration }, "}", ";"
                    | "error", identifier, [ ":", enum-underlying-type ], "{",
                      enum-variant, { ",", enum-variant }, [ "," ], "}", ";" ;
struct-declaration  = "struct", identifier, "{", { field-declaration }, "}", ";" ;
field-declaration   = { attribute }, [ "protected" ], type, identifier,
                      [ "=", initializer ], ";" ;

enum-declaration    = "enum", identifier, [ ":", enum-underlying-type ], "{",
                      enum-variant, { ",", enum-variant }, [ "," ], "}", ";" ;
enum-variant        = { attribute }, identifier, [ "=", constant-expression ]
                    | { attribute }, identifier, "(", type, ")"
                    | { attribute }, identifier, "{", field-declaration,
                      { field-declaration }, "}" ;

module-object-declaration = [ "thread_local" ], type, identifier, "=", initializer, ";" ;
object-declaration  = [ storage-specifier ], ( type | "auto" ), identifier, "=",
                      initializer, ";"
                    | destructuring-declaration ;
destructuring-declaration = "auto", "(", identifier, { ",", identifier }, ")", "=",
                            initializer, ";" ;
storage-specifier   = "static" | "thread_local" ;
initializer         = expression | aggregate-initializer | dict-expression ;
aggregate-initializer = "{", [ initializer-item, { ",", initializer-item }, [ "," ] ], "}"
                      | "{", { initializer-item, "," }, spread-argument, [ "," ], "}" ;
initializer-item    = initializer | ".", identifier, "=", initializer ;
dict-expression     = "{", dict-entry, { ",", dict-entry }, [ "," ], "}"
                    | "{", dict-entry, comprehension-for, { comprehension-clause }, "}" ;
dict-entry          = expression, ":", expression ;

function-declaration = [ "unsafe" ], [ "async" ], [ "extern", string-literal ],
                       ( type | opaque-result ), function-name, "(", parameter-list, ")",
                       [ throws-clause ],
                       ( ";" | block ) ;
opaque-result       = "opaque", "(", opaque-contract, { "&", opaque-contract }, ")" ;
opaque-contract     = generic-constraint | identifier, "=", type ;
function-name       = identifier | method-name ;
method-name         = ( aggregate-type-name | generic-type ), "::", identifier ;
parameter-list      = [ receiver-parameter, { ",", parameter }
                      | parameter, { ",", parameter } ] ;
parameter           = ( "out", type | type, [ "..." ] | [ "const" ], "null_t" ), identifier ;
parameter-type      = [ "out" ], type ;
receiver-parameter  = [ "const" ], self-type, "*", "this" | self-type, "this" ;
self-type           = "Self" | aggregate-type-name | generic-type ;

trait-declaration   = "trait", identifier, [ ":", generic-constraint,
                      { "&", generic-constraint } ], "{", { trait-member }, "}", ";" ;
trait-member        = trait-method-declaration | associated-type-declaration
                    | associated-constant-declaration ;
trait-method-declaration = { attribute }, [ "unsafe" ], [ "async" ], type, identifier,
                      "(", parameter-list, ")", [ throws-clause ], ( ";" | block ) ;
associated-type-declaration = "type", identifier, [ associated-type-parameters ],
                              [ ":", generic-constraint, { "&", generic-constraint } ], ";" ;
associated-type-parameters = "<", associated-type-parameter,
                             { ",", associated-type-parameter }, ">" ;
associated-type-parameter = identifier, [ ":", generic-constraint,
                            { "&", generic-constraint } ] ;
associated-constant-declaration = "const", constant-parameter-type, identifier,
                                  [ "=", expression ], ";" ;
impl-declaration    = "impl", trait-application, "for", type, "{",
                      { impl-member }, "}", ";" ;
impl-member         = impl-method-definition | associated-type-binding
                    | associated-constant-binding ;
impl-method-definition = { attribute }, [ "unsafe" ], [ "async" ], type, identifier,
                      "(", parameter-list, ")", [ throws-clause ], block ;
associated-type-binding = "type", identifier, [ "<", identifier, { ",", identifier }, ">" ],
                          "=", type, ";" ;
associated-constant-binding = "const", constant-parameter-type, identifier, "=", expression,
                              ";" ;
throws-clause       = "throws", error-type, { ",", error-type } ;
error-type          = aggregate-type-name | generic-type | standard-parametric-type ;

drop-definition     = "drop", "(", borrow-type, identifier, ")", block ;

extern-block        = "extern", string-literal, "{", { c-declaration }, "}" ;
c-declaration       = { attribute },
                      ( c-function-declaration | c-object-declaration
                      | c-constant-declaration | c-opaque-declaration
                      | struct-declaration | enum-declaration ) ;
c-function-declaration = [ "unsafe" ], type, identifier, "(",
                         c-parameter-list, ")", ";" ;
c-object-declaration = [ "thread_local" ], type, identifier, ";" ;
c-constant-declaration = type, identifier, "=", constant-expression, ";" ;
c-opaque-declaration = "opaque", "struct", identifier, ";" ;
c-parameter-list    = [ parameter, { ",", parameter }, [ ",", "..." ] ] ;
```

`extern` string literal shall содержать ровно `C` в R 0.1. Function definition inside extern block запрещено; `extern "C"` definition экспортируется по умолчанию и shall not быть `protected`.

### A.4 Types

```ebnf
type                = direct-type | borrow-type | dyn-borrow-type | owning-pointer-type
                    | shared-owner-type | weak-owner-type | raw-pointer-type
                    | array-type | slice-type | constexpr-string-type | atomic-type
                    | raw-function-type | function-type | "(", type, ")"
                    | tuple-type ;
tuple-type          = "(", type, ",", type, { ",", type }, ")"
                    | "(", { type, "," }, identifier, "...", ")" ;

direct-type         = [ "const" ], direct-value-type ;
direct-value-type   = type-atom | option-type
                    | container-type | task-type | standard-direct-type
                    | standard-parametric-type ;
type-atom           = primitive-type | predefined-type-name | aggregate-type-name | generic-type
                    | "str" | "void" | "never" | "Self" | associated-type-projection ;
associated-type-projection = ( "Self" | identifier ), "::", identifier,
                             [ "<", generic-argument, { ",", generic-argument }, ">" ] ;
predefined-type-name = "bytes" ;
aggregate-type-name = identifier | module-path, "::", identifier ;
generic-type        = aggregate-type-name, "<", generic-argument, { ",", generic-argument }, ">" ;
generic-argument    = type | constant-expression ;
explicit-generic-arguments = "::", "<", explicit-generic-argument,
                             { ",", explicit-generic-argument }, ">" ;
explicit-generic-argument  = generic-argument
                           | "throws", "(", [ type, { ",", type } ], ")" ;
pointer-pointee     = type-atom | container-type | standard-parametric-type
                    | "(", type, ")" ;
sequence-element    = type-atom | "(", type, ")" ;
managed-pointee     = type-atom | dyn-type | "(", type, ")" ;

borrow-type         = [ "const" ], pointer-pointee, "*", [ "?" ] ;
dyn-borrow-type     = [ "const" ], dyn-type, "*", [ "?" ] ;
dyn-type            = "dyn", "(", opaque-contract, { "&", opaque-contract }, ")" ;
owning-pointer-type = "own", ( pointer-pointee | dyn-type ), "*", [ "?" ] ;
shared-owner-type   = ( "arc" | "rc" ), managed-pointee ;
weak-owner-type     = "weak", shared-owner-type ;
raw-pointer-type    = "raw", [ "const" ], pointer-pointee, "*", [ "?" ] ;

array-type          = [ "const" ], sequence-element, "[", constant-expression, "]",
                      { "[", constant-expression, "]" } ;
slice-type          = [ "const" ], sequence-element, "[", "]" ;
constexpr-string-type = [ "const" ], "constexpr", "str" ;

option-type         = "o", "<", type, ">" ;
task-type           = "task", "<", type, [ throws-clause ], ">" ;
container-type      = "array", "<", type, ">"
                    | "list", "<", type, ">"
                    | "dict", "<", type, ",", type, ">" ;
standard-direct-type = "std", ".", "async", "::", "start_error" ;
standard-parametric-type = standard-effect-type
                         | standard-one-type-name, "<", type, ">"
                         | standard-two-type-name, "<", type, ",", type, ">" ;
standard-effect-type = "std", ".", "thread", "::",
                       ( "join_handle" | "scoped_join_handle" ),
                       "<", type, [ throws-clause ], ">" ;
standard-one-type-name = "core", "::", "atomic_compare_exchange_result"
                       | "std", ".", "alloc", "::", "new_error"
                       | "std", ".", "json", "::", ( "decoder" | "reader" | "detached" )
                       | "std", ".", ( "arc" | "rc" ), "::", "try_unwrap_result"
                       | "std", ".", "thread", "::", "join_result"
                       | "std", ".", "sync", "::",
                         ( "mutex" | "rw_lock" | "lock_result" | "try_lock_result"
                         | "read_lock_result" | "try_read_lock_result"
                         | "write_lock_result" | "try_write_lock_result"
                         | "mutex_guard" | "rw_read_guard" | "rw_write_guard"
                         | "once_lock" | "set_result" | "channel" | "sync_channel"
                         | "sender" | "sync_sender" | "receiver" | "send_result"
                         | "try_send_result" | "recv_result" | "try_recv_result" )
                       | "std", ".", "array", "::", "push_error"
                       | "std", ".", "list", "::", ( "push_error" | "iter" ) ;
standard-two-type-name = "std", ".", "dict", "::",
                         ( "insert_error" | "iter" | "entry_ref" ) ;
atomic-type         = "atomic", atomic-base-type
                    | "ai8" | "ai16" | "ai32" | "ai64" | "aisize"
                    | "au8" | "au16" | "au32" | "au64" | "ausize" ;
raw-function-type   = "raw", "fn", { callable-resource-attribute }, [ "?" ],
                      "(", function-type-parameters, ")", "->", type ;
function-type       = [ "async" ], "fn", { callable-resource-attribute },
                      "(", [ parameter-type, { ",", parameter-type } ], ")",
                      "->", type, [ callable-throws ] ;
function-type-parameters = [ function-parameter-type,
                           { ",", function-parameter-type } ] ;
function-parameter-type = non-void-direct-type | borrow-type | owning-pointer-type
                        | shared-owner-type | weak-owner-type
                        | raw-pointer-type | array-type | slice-type
                        | constexpr-string-type
                        | atomic-type | raw-function-type
                        | "(", function-parameter-type, ")" ;
non-void-direct-type = [ "const" ], non-void-direct-value-type ;
non-void-direct-value-type = non-void-type-atom | option-type
                           | container-type | task-type | standard-direct-type
                           | standard-parametric-type ;
non-void-type-atom  = primitive-type | predefined-type-name | aggregate-type-name | generic-type
                    | "str" | "never" | "Self" ;

primitive-type      = integer-type | floating-type | "bool" | "char" | c-abi-type ;
integer-type        = "i8" | "u8" | "i16" | "u16" | "i32" | "u32"
                    | "i64" | "u64" | "isize" | "usize" ;
enum-underlying-type = integer-type | c-integer-type ;
floating-type       = "f32" | "f64" ;
atomic-base-type    = integer-type | "bool" | atomic-raw-pointer-type ;
atomic-raw-pointer-type = "raw", [ "const" ], pointer-pointee, "*", "?" ;
c-integer-type      = "c_char" | "c_schar" | "c_uchar" | "c_short" | "c_ushort"
                    | "c_int" | "c_uint" | "c_long" | "c_ulong" | "c_llong"
                    | "c_ullong" | "c_wchar" | "c_wint"
                    | "c_int8" | "c_uint8" | "c_int16" | "c_uint16"
                    | "c_int32" | "c_uint32" | "c_int64" | "c_uint64"
                    | "c_intptr" | "c_uintptr" | "c_intmax" | "c_uintmax"
                    | "c_size" | "c_ptrdiff" ;
c-abi-type          = "c_char" | "c_schar" | "c_uchar" | "c_short" | "c_ushort"
                    | "c_int" | "c_uint" | "c_long" | "c_ulong" | "c_llong"
                    | "c_ullong" | "c_bool" | "c_wchar" | "c_wint"
                    | "c_int8" | "c_uint8" | "c_int16" | "c_uint16"
                    | "c_int32" | "c_uint32" | "c_int64" | "c_uint64"
                    | "c_intptr" | "c_uintptr" | "c_intmax" | "c_uintmax"
                    | "c_float" | "c_double" | "c_long_double"
                    | "c_size" | "c_ptrdiff" ;
```

### A.5 Statements

```ebnf
statement           = block | object-declaration | lambda-declaration
                    | expression-statement | if-statement | switch-statement | while-statement | for-statement
                    | for-in-statement | labeled-loop-statement | static-if-statement | deadline-statement
                    | budget-statement
                    | thread-scope-statement | task-scope-statement | select-statement | jump-statement
                    | drop-statement | await-object-declaration | await-statement | try-statement
                    | throw-statement | unsafe-block ;
block               = "{", { statement }, "}" ;
expression-statement = [ expression ], ";" ;
if-statement        = "if", "(", statement-condition, ")", block,
                      [ "else", block ] ;
statement-condition = condition-expression | pattern-test ;
pattern-test        = condition-value-expression, "is", match-pattern ;
static-if-statement = "@", "if", "(", static-predicate, ")", block,
                      [ "@", "else", ( block | static-if-statement ) ] ;
while-statement     = "while", "(", statement-condition, ")", block ;
labeled-loop-statement = identifier, ":", ( while-statement | for-statement
                                          | for-in-statement ) ;
for-statement       = "for", "(", [ for-init ], ";", condition-expression, ";",
                      [ expression ], ")", block ;
for-init            = object-declaration-no-semicolon | expression ;
for-in-statement    = "for", "(", ( type | "auto" ), identifier, "in", iterable, ")", block ;
iterable            = expression | expression, "..", expression ;
object-declaration-no-semicolon = [ storage-specifier ], ( type | "auto" ), identifier, "=",
                                  initializer ;
lambda-declaration  = [ "async" ], "fn", { callable-resource-attribute }, [ callable-mode ],
                      type, identifier, "(", parameter-list, ")",
                      [ "move", "(", identifier, { ",", identifier }, ")" ], [ throws-clause ], block ;
await-object-declaration = type, identifier, "=", await-operation, ";" ;
await-statement     = await-operation, ";" ;
await-operation     = "await", ( "move", identifier | await-call ) ;
await-call          = ( identifier | qualified-primary ), [ explicit-generic-arguments ],
                      "(", argument-list, ")"
                    | primary-expression, { postfix-suffix }, method-call-suffix ;

switch-statement    = "switch", "(", expression, ")", "{",
                      { switch-clause }, "}" ;
switch-clause       = case-clause | default-clause ;
case-clause         = "case", case-pattern, ":",
                      { case-body-statement }, [ clause-terminator ] ;
case-pattern        = constant-expression
                    | "variant", ( qualified-name | standard-container-variant-name
                                 | standard-owner-variant-name
                                 | standard-async-variant-name ),
                      [ "(", [ ( identifier | "move", identifier ) ], ")" ] ;
default-clause      = "default", ":",
                      { case-body-statement }, [ clause-terminator ] ;

case-body-statement = static-if-statement | block | object-declaration | expression-statement
                    | if-statement | switch-statement | while-statement
                    | for-statement | for-in-statement | labeled-loop-statement
                    | thread-scope-statement | task-scope-statement | select-statement
                    | drop-statement | await-object-declaration | await-statement | try-statement
                    | unsafe-block | conditional-throw-statement | deadline-statement
                    | budget-statement ;
clause-terminator   = "break", [ identifier ], ";" | "continue", [ identifier ], ";"
                    | "return", [ return-operand ], ";"
                    | "throw", [ throw-operand ], ";"
                    | conditional-throw-else-statement
                    | "fallthrough", ";" ;

jump-statement      = "break", [ identifier ], ";" | "continue", [ identifier ], ";"
                    | "return", [ return-operand ], ";" ;
return-operand      = expression | aggregate-initializer ;
try-statement       = "try", block,
                      ( catch-clause, { catch-clause }, [ finally-clause ]
                      | finally-clause ) ;
catch-clause        = "catch", "(", error-type, identifier, ")", block ;
finally-clause      = "finally", block ;
throw-statement     = "throw", [ throw-operand ], ";"
                    | conditional-throw-statement | conditional-throw-else-statement ;
conditional-throw-statement = "throw", "(", condition-expression, ")",
                              throw-operand, ";" ;
conditional-throw-else-statement = "throw", "(", condition-expression, ")",
                                   throw-operand, "else", throw-operand, ";" ;
throw-operand       = expression | aggregate-initializer ;
drop-statement      = "drop", expression, ";" ;
thread-scope-statement = "thread_scope", block ;
task-scope-statement = "task_scope", "(", constant-expression, ")", identifier, block ;
deadline-statement  = "deadline", "(", expression, ")", block ;
budget-statement    = "budget", "(", expression, ")", block ;
select-statement    = "select", "(", identifier, ")", "{", select-clause, { select-clause },
                      "}" ;
select-clause       = "case", select-branch, ":",
                      { case-body-statement }, [ clause-terminator ] ;
select-branch       = ( type | "auto" ), identifier, "=", await-operation
                    | await-operation
                    | "until", "(", expression, ")" ;
unsafe-block        = "unsafe", block ;
```

<a id="R-GRAM-A003"></a>

**R-GRAM-A003** — Productions `if-statement`, `while-statement`, `for-statement`, `thread-scope-statement`, `deadline-statement` и `budget-statement` consume explicit blocks. Block delimiters form branch/loop boundaries directly in parse tree; optional `else` belongs to the single preceding completed `if-statement` production. Productions `case-clause`, `default-clause` and `select-clause` instead end at their explicit terminator or, without one, before the next label or the closing brace, and form implicit lexical scopes.

=== A.6 Expressions

Уровни записаны от меньшего precedence к большему. Assignment не ассоциативен; остальные binary уровни left-associative, conditional right-associative.

```ebnf
expression          = assignment-expression ;
assignment-expression = conditional-expression,
                      [ assignment-operator, conditional-expression ] ;
assignment-operator = "=" | "+=" | "-=" | "*=" | "/=" | "%="
                    | "&=" | "|=" | "^=" | "<<=" | ">>=" ;

conditional-expression = logical-or-expression
                       | condition-expression, "?", expression, ":",
                         conditional-expression ;

condition-expression    = condition-or-expression ;
condition-or-expression = condition-and-expression,
                          { "||", condition-and-expression } ;
condition-and-expression = condition-primary, { "&&", condition-primary } ;
condition-primary       = "true" | "false"
                        | condition-value-expression, comparison-operator,
                          condition-value-expression
                        | membership-expression
                        | "(", condition-expression, ")" ;
membership-expression   = shift-expression, [ "not" ], "in",
                          ( shift-expression | range-expression ) ;
range-expression        = shift-expression, "..", shift-expression ;
comparison-operator     = "==" | "!=" | "<" | "<=" | ">" | ">=" ;
condition-value-expression = condition-bitwise-or-expression ;
condition-bitwise-or-expression = condition-bitwise-xor-expression,
                                  { "|", condition-bitwise-xor-expression } ;
condition-bitwise-xor-expression = condition-bitwise-and-expression,
                                   { "^", condition-bitwise-and-expression } ;
condition-bitwise-and-expression = shift-expression, { "&", shift-expression } ;

logical-or-expression   = logical-and-expression, { "||", logical-and-expression } ;
logical-and-expression  = bitwise-or-expression, { "&&", bitwise-or-expression } ;
bitwise-or-expression   = bitwise-xor-expression, { "|", bitwise-xor-expression } ;
bitwise-xor-expression  = bitwise-and-expression, { "^", bitwise-and-expression } ;
bitwise-and-expression  = equality-expression, { "&", equality-expression } ;
equality-expression     = relational-expression, { ( "==" | "!=" ), relational-expression } ;
relational-expression   = shift-expression, { ( "<" | "<=" | ">" | ">=" ), shift-expression }
                        | membership-expression ;
shift-expression        = additive-expression, { ( "<<" | ">>" ), additive-expression } ;
additive-expression     = multiplicative-expression, { ( "+" | "-" ), multiplicative-expression } ;
multiplicative-expression = cast-expression, { ( "*" | "/" | "%" ), cast-expression } ;
cast-expression         = unary-expression, { "as", type } ;

unary-expression      = postfix-expression
                      | ( "+" | "-" | "!" | "~" | "*" | "&" | "++" | "--" ), unary-expression
                      | "move", unary-expression | await-operation
                      | "sizeof", "(", type, ")"
                      | "alignof", "(", type, ")"
                      | "new", type,
                        ( "(", expression, ")" | aggregate-initializer ) ;

postfix-expression    = primary-expression, { postfix-suffix },
                        [ method-call-suffix, { postfix-suffix | method-call-suffix } ] ;
method-call-suffix    = ( "." | "->" ), identifier, [ explicit-generic-arguments ],
                        "(", argument-list, ")" ;
postfix-suffix        = format-suffix | "(", argument-list, ")"
                      | "[", expression, "]"
                      | "[", [ expression ], "..", [ expression ], "]"
                      | ".", ( identifier | integer-literal )
                      | "->", ( identifier | integer-literal )
                      | "++" | "--" ;
argument-list         = [ argument, { ",", argument } ] ;
argument              = expression | spread-argument | out-argument | pack-expansion ;
pack-expansion        = identifier, "..." ;
out-argument          = "out", expression ;
spread-argument       = "...", expression ;

primary-expression   = format-sequence | identifier, [ explicit-generic-arguments ]
                     | qualified-primary, [ explicit-generic-arguments ]
                     | literal | "true" | "false" | "null"
                     | "(", expression, ")" | tuple-expression | aggregate-constructor | panic-call
                     | standard-type-call-expression | reflection-constant-expression
                     | collection-expression | match-expression
                     | associated-constant-name ;
associated-constant-name = "Self", "::", identifier ;
match-expression     = "match", "(", expression, ")", "{", match-arm, { match-arm }, "}" ;
match-arm            = "case", match-pattern, [ "if", "(", condition-expression, ")" ],
                       ":", ( expression, ";" | throw-statement )
                     | "default", ":", ( expression, ";" | throw-statement ) ;
match-pattern        = identifier | "move", identifier | constant-expression
                     | "variant", qualified-primary, [ "(", match-pattern, ")" | match-fields ]
                     | match-fields ;
match-fields         = "{", [ match-field, { ",", match-field }, [ "," ] ], "}" ;
match-field          = ".", ( identifier | integer-literal ), "=", match-pattern ;
tuple-expression     = "(", expression, ",", { expression, "," },
                       ( expression | spread-argument ), ")" ;
collection-expression = "[", [ expression, { ",", expression }, [ "," ] ], "]"
                      | "[", expression, comprehension-for, { comprehension-clause }, "]" ;
comprehension-clause  = comprehension-for | comprehension-if ;
comprehension-for     = "for", "(", ( type | "auto" ), identifier, "in", iterable, ")" ;
comprehension-if      = "if", "(", condition-expression, ")" ;
qualified-primary    = qualified-name | standard-json-qualified-name | standard-owner-qualified-name
                     | standard-container-qualified-name
                     | standard-async-qualified-name ;
qualified-head       = identifier | "o" ;
qualified-name       = qualified-head, { ".", identifier }, "::", identifier,
                       { "::", identifier }
                     | generic-type, "::", identifier ;
standard-json-qualified-name = "std", ".", "json", "::", ( "null" | "array" )
                             | "std", ".", "json", "::", "value_kind", "::", ( "null" | "array" ) ;
standard-owner-qualified-name = "std", ".", ( "arc" | "rc" ), "::",
                                ( "clone" | "clone_weak" | "downgrade" | "upgrade"
                                | "get_mut" | "try_unwrap" | "strong_count"
                                | "weak_count" | "ptr_eq" | "into_raw" | "from_raw" )
                              | standard-owner-variant-name ;
standard-owner-variant-name = "std", ".", ( "arc" | "rc" ), "::",
                              "try_unwrap_result", "::", ( "unwrapped" | "shared" ) ;
standard-container-qualified-name = "std", ".", "array", "::",
                                    ( "create" | "with_capacity" | "capacity"
                                    | "reserve" | "push" | "pop" | "remove"
                                    | "get" | "get_mut" | "as_slice"
                                    | "as_slice_mut" | "clear" )
                                  | "std", ".", "array", "::", "push_error", "::",
                                    "allocation_failed"
                                  | "std", ".", "list", "::",
                                    ( "create" | "push_front" | "push_back"
                                    | "insert_before" | "insert_after"
                                    | "front" | "back" | "front_mut" | "back_mut"
                                    | "get" | "get_mut" | "remove"
                                    | "pop_front" | "pop_back" | "clear"
                                    | "iter" | "next" )
                                  | "std", ".", "list", "::", "push_error", "::",
                                    "allocation_failed"
                                  | "std", ".", "dict", "::",
                                    ( "create" | "with_capacity" | "reserve" | "insert"
                                    | "contains" | "get" | "get_mut" | "remove"
                                    | "clear" | "iter" | "next" )
                                  | "std", ".", "dict", "::", "insert_error", "::",
                                    "allocation_failed" ;
standard-container-variant-name = "std", ".", "array", "::", "push_error", "::",
                                  "allocation_failed"
                                | "std", ".", "list", "::", "push_error", "::",
                                  "allocation_failed"
                                | "std", ".", "dict", "::", "insert_error", "::",
                                  "allocation_failed" ;
standard-async-qualified-name = "std", ".", "async", "::", ( "cancel" | "detach" )
                              | standard-async-variant-name ;
standard-async-variant-name = "std", ".", "async", "::", "start_error", "::",
                              ( "allocation_failed" | "runtime_stopping" | "scope_full"
                              | "budget_exhausted" ) ;
aggregate-constructor = aggregate-constructor-head, aggregate-initializer ;
aggregate-constructor-head = identifier | qualified-name | generic-type ;
panic-call           = "panic", "(", expression, ")" ;
standard-type-call-expression = "std", ".", "sync", "::", "channel",
                                [ "::", "<", type, ">" ], "(", ")"
                              | "std", ".", "sync", "::", "sync_channel",
                                [ "::", "<", type, ">" ], "(", expression, ")"
                              | "std", ".", "sync", "::", "once_lock",
                                [ "::", "<", type, ">" ], "(", ")"
                              | "std", ".", "async", "::", "broadcast",
                                [ "::", "<", type, ">" ], "(", expression, ")"
                              | "std", ".", "array", "::", "create",
                                [ "::", "<", type, ">" ], "(", ")"
                              | "std", ".", "array", "::", "with_capacity",
                                [ "::", "<", type, ">" ], "(", expression, ")"
                              | "std", ".", "list", "::", "create",
                                [ "::", "<", type, ">" ], "(", ")"
                              | "std", ".", "dict", "::", "create",
                                [ "::", "<", type, ",", type, ">" ], "(", ")"
                              | "std", ".", "dict", "::", "with_capacity",
                                [ "::", "<", type, ",", type, ">" ], "(", expression, ")"
                              | "core", "::", ( "enum_at" | "enum_from_name" ),
                                "::", "<", type, ">", "(", expression, ")" ;
reflection-constant-expression = "core", "::",
                                 ( "enum_count" | "enum_min" | "enum_max" | "enum_variants"
                                 | "variant_count" | "field_count" | "type_name" ),
                                 "::", "<", type, ">", "(", ")"
                               | "core", "::", "field_name",
                                 "::", "<", type, ">", "(", constant-expression, ")" ;
literal              = integer-literal | floating-literal | character-literal | string-literal ;
constant-expression  = conditional-expression ;

call-free-expression = call-free-assignment-expression ;
call-free-assignment-expression = call-free-conditional-expression,
                                  [ assignment-operator,
                                    call-free-conditional-expression ] ;
call-free-conditional-expression = call-free-logical-or-expression
                                 | call-free-condition-expression, "?",
                                   call-free-expression, ":",
                                   call-free-conditional-expression ;
call-free-condition-expression = call-free-condition-or-expression ;
call-free-condition-or-expression = call-free-condition-and-expression,
                                    { "||", call-free-condition-and-expression } ;
call-free-condition-and-expression = call-free-condition-primary,
                                     { "&&", call-free-condition-primary } ;
call-free-condition-primary = "true" | "false"
                            | call-free-condition-value-expression,
                              comparison-operator,
                              call-free-condition-value-expression
                            | "(", call-free-condition-expression, ")" ;
call-free-condition-value-expression = call-free-condition-bitwise-or-expression ;
call-free-condition-bitwise-or-expression = call-free-condition-bitwise-xor-expression,
                                            { "|", call-free-condition-bitwise-xor-expression } ;
call-free-condition-bitwise-xor-expression = call-free-condition-bitwise-and-expression,
                                             { "^", call-free-condition-bitwise-and-expression } ;
call-free-condition-bitwise-and-expression = call-free-shift-expression,
                                             { "&", call-free-shift-expression } ;
call-free-logical-or-expression = call-free-logical-and-expression,
                                  { "||", call-free-logical-and-expression } ;
call-free-logical-and-expression = call-free-bitwise-or-expression,
                                   { "&&", call-free-bitwise-or-expression } ;
call-free-bitwise-or-expression = call-free-bitwise-xor-expression,
                                  { "|", call-free-bitwise-xor-expression } ;
call-free-bitwise-xor-expression = call-free-bitwise-and-expression,
                                   { "^", call-free-bitwise-and-expression } ;
call-free-bitwise-and-expression = call-free-equality-expression,
                                   { "&", call-free-equality-expression } ;
call-free-equality-expression = call-free-relational-expression,
                                { ( "==" | "!=" ),
                                  call-free-relational-expression } ;
call-free-relational-expression = call-free-shift-expression,
                                  { ( "<" | "<=" | ">" | ">=" ),
                                    call-free-shift-expression } ;
call-free-shift-expression = call-free-additive-expression,
                             { ( "<<" | ">>" ), call-free-additive-expression } ;
call-free-additive-expression = call-free-multiplicative-expression,
                                { ( "+" | "-" ),
                                  call-free-multiplicative-expression } ;
call-free-multiplicative-expression = call-free-cast-expression,
                                      { ( "*" | "/" | "%" ),
                                        call-free-cast-expression } ;
call-free-cast-expression = call-free-unary-expression, { "as", type } ;
call-free-unary-expression = call-free-postfix-expression
                           | ( "+" | "-" | "!" | "~" | "*" | "&" | "++" | "--" ),
                             call-free-unary-expression
                           | "move", call-free-unary-expression
                           | "sizeof", "(", type, ")"
                           | "alignof", "(", type, ")"
                           | "new", type,
                             ( "(", call-free-expression, ")"
                             | call-free-aggregate-initializer ) ;
call-free-postfix-expression = call-free-primary-expression,
                               { call-free-postfix-suffix }, [ method-call-suffix ] ;
call-free-postfix-suffix = format-suffix | "[", call-free-expression, "]"
                         | "[", [ call-free-expression ], "..",
                           [ call-free-expression ], "]"
                         | ".", ( identifier | integer-literal )
                         | "->", ( identifier | integer-literal )
                         | "++" | "--" ;
call-free-primary-expression = format-sequence | identifier | call-free-qualified-primary
                             | literal | "true" | "false" | "null"
                             | "(", call-free-expression, ")"
                             | call-free-aggregate-constructor
                             | reflection-constant-expression | associated-constant-name ;
call-free-qualified-primary = qualified-name | standard-json-qualified-name | standard-owner-qualified-name
                            | standard-container-qualified-name
                            | standard-async-qualified-name ;
call-free-aggregate-constructor = aggregate-constructor-head,
                                  call-free-aggregate-initializer ;
call-free-initializer = call-free-expression | call-free-aggregate-initializer ;
call-free-aggregate-initializer = "{",
    [ call-free-initializer-item, { ",", call-free-initializer-item }, [ "," ] ], "}" ;
call-free-initializer-item = call-free-initializer
                           | ".", identifier, "=", call-free-initializer ;
```

<a id="R-GRAM-A002"></a>

**R-GRAM-A002** — Comma token используется productions `argument-list`, initializers, enum variants и selected imports как separator. Expression productions consume operands через named operators и завершаются перед separator comma.

<a id="annex-b"></a>

## Annex B — Required diagnostics

Каждая строка задаёт стабильный diagnostic code. Implementation may добавить notes/suggestions, но shall emit code и normative rule anchor.

| Code | Условие | Основные rules |
|----|----|----|
| `R-DIAG-LEX-001` | Malformed UTF-8, forbidden scalar/BOM | R-LEX-0001..0003 |
| `R-DIAG-LEX-002` | Unterminated/invalid comment or literal/escape | R-LEX-0004, R-LEX-0012..0014 |
| `R-DIAG-LEX-003` | Invalid identifier or non-NFC spelling | R-LEX-0006..0008 |
| `R-DIAG-LEX-004` | Unknown/forbidden whitespace, character or punctuator, including `#` | R-LEX-0004, R-LEX-0015..0016 |
| `R-DIAG-SYN-001` | Token sequence not derivable from Annex A | R-GRAM-0001, R-GRAM-0005, R-STMT-0002 |
| `R-DIAG-SYN-002` | Unknown/malformed/misplaced non-FFI attribute | R-GRAM-0007, R-AGG-0012, R-FUNC-0026 |
| `R-DIAG-ASYNC-001` | Invalid async declaration, task type/frame, await context or task lifecycle operation | R-TYPE-0029, R-BORROW-0024, R-STMT-0012, R-STMT-0017..0021, R-FUNC-0010..0012, R-FUNC-0026, R-MEM-0031, R-TYPE-0054 |
| `R-DIAG-ALLOC-001` | Panicking allocation под policy deny-panic-allocation | R-OBJ-0012 |
| `R-DIAG-PROFILE-001` | Selected profile or mandatory native capability is unavailable | R-CONF-0005, R-LIBREF-0003, R-CONF-G005 |
| `R-DIAG-NAME-001` | Unresolved, ambiguous or inaccessible name | R-NAME-0003, R-AGG-0007, R-FUNC-0016, R-TYPE-0050, R-TYPE-0054 |
| `R-DIAG-NAME-002` | Duplicate declaration/definition | R-NAME-0004, R-NAME-0010, R-TYPE-0045, R-TYPE-0050, R-OWN-0020, R-AGG-0012, R-STMT-0004 |
| `R-DIAG-NAME-003` | Forbidden shadowing | R-NAME-0009 |
| `R-DIAG-NAME-004` | Invalid visibility or storage specifier | R-NAME-0007..0008 |
| `R-DIAG-INIT-001` | Missing initializer or non-constant static initializer | R-NAME-0005..0008, R-NAME-0011 |
| `R-DIAG-INIT-002` | Invalid aggregate initializer, missing non-default field | R-INIT-0003..0005 |
| `R-DIAG-TYPE-001` | Type mismatch, invalid access/implicit conversion/enum form/managed owner/standard type/key contract/`constexpr str`, checked-error type/set либо catch | R-TYPE-0007..0008, R-TYPE-0012, R-TYPE-0014..0017, R-TYPE-0025..0030, R-OBJ-0006, R-INIT-0002, R-INIT-0012, R-EXPR-0004, R-EXPR-0013..0015, R-EXPR-0021, R-EXPR-0024, R-STMT-0011, R-FUNC-0009, R-ERR-0002..0003, R-AGG-0007, R-AGG-0009..0010, R-LIB-0019..0023, R-FUNC-0013..0017, R-TYPE-0041, R-TYPE-0043..0046, R-NAME-0011, R-STMT-0014, R-STMT-0021, R-EXPR-0029, R-EXPR-0030, R-FUNC-0018, R-REFL-0001..0004, R-TYPE-0036, R-TYPE-0050..0051, R-STMT-0018..0020, R-OWN-0019..0020, R-AGG-0012, R-TYPE-0054, R-TYPE-0055, R-INIT-0004, R-STMT-0022 |
| `R-DIAG-EFFECT-001` | Необработанный checked effect или запрещённая propagation из finally | R-ERR-0001..0003, R-FUNC-0026 |
| `R-DIAG-EFFECT-002` | Недопустимая категория ошибки, throw operand, rethrow или неоднозначный выведенный тип ошибки; номинальные типы требуют объявления `error` | R-TYPE-0012, R-ERR-0002..0003, R-AGG-0001 |
| `R-DIAG-EFFECT-003` | Повтор типа в throws set или catch | R-TYPE-0012, R-ERR-0003 |
| `R-DIAG-USE-001` | Неиспользованное значимое значение или недопустимый контракт результата | R-FUNC-0020, R-FUNC-0021, R-FUNC-0022, R-STMT-0018 |
| `R-DIAG-USE-002` | Безусловная замена локального значения | R-INIT-0014 |
| `R-DIAG-RESOURCE-001` | Отсутствие heap-аллокаций не доказано | R-FUNC-0019 |
| `R-DIAG-RESOURCE-002` | Неблокирующее исполнение не доказано | R-FUNC-0019 |
| `R-DIAG-META-001` | Неразрешимый статический предикат или несовместимые предположения ветви | R-META-0001, R-META-0002, R-META-0003 |
| `R-DIAG-TYPE-002` | Non-bool operand оператора `!`, `&&` или `\|\|` | R-EXPR-0010 |
| `R-DIAG-TYPE-003` | Invalid explicit conversion, transmute или unsafe core-intrinsic type | R-EXPR-0016..0019, R-EXPR-0023, R-UNSAFE-0007..0008 |
| `R-DIAG-CONST-001` | Literal/constant overflow, division by zero, invalid shift/conversion | R-LEX-0010..0011, R-TYPE-0011, R-INIT-0002, R-EXPR-0005..0007, R-EXPR-0016, R-EXPR-0026, R-STMT-0006, R-AGG-0004, R-REFL-0003 |
| `R-DIAG-CONST-002` | Expression required to be constant is not constant | R-EXPR-0022, R-EXPR-0032, R-FUNC-0023, R-META-0002 |
| `R-DIAG-CONST-003` | Translation-time evaluation panics or throws | R-EXPR-0032, R-META-0002 |
| `R-DIAG-MOVE-001` | Named Move value consumed without `move` | R-OWN-0003, R-OWN-0013, R-EXPR-0014, R-STMT-0014 |
| `R-DIAG-MOVE-002` | Use/double-move of moved or destroyed place | R-AM-0007, R-OWN-0004, R-STMT-0018 |
| `R-DIAG-MOVE-003` | Partial/atomic/payload move, nested staged-call move, move while borrowed либо invalid reinitialization | R-INIT-0013, R-OWN-0002, R-OWN-0005..0006, R-STMT-0010, R-FUNC-0010, R-MEM-0013 |
| `R-DIAG-BORROW-001` | Conflicting shared/exclusive borrow, overlap staged-move reservation либо mutable access к immutable storage | R-TYPE-0008, R-BORROW-0002..0004, R-BORROW-0020, R-EXPR-0021, R-FUNC-0010, R-FUNC-0016, R-MEM-0013, R-OWN-0010, R-OWN-0014, R-LIB-0019..0023, R-OWN-0019, R-TYPE-0055 |
| `R-DIAG-BORROW-002` | Borrow escapes, применяет address-of к non-place, использует temporary storage root, crosses async suspension либо нарушает lifetime relation | R-BORROW-0007..0010, R-BORROW-0016..0024, R-FUNC-0018, R-TYPE-0045 |
| `R-DIAG-BORROW-003` | Ambiguous borrow kind or nullable safe dereference unproven | R-BORROW-0001, R-BORROW-0005 |
| `R-DIAG-DROP-001` | Invalid drop definition/call or destructor state | R-INIT-0009..0011 |
| `R-DIAG-BOUNDS-001` | Compile-time provable invalid index/range | R-EXPR-0021 |
| `R-DIAG-FLOW-001` | Missing return, unhandled checked effect, invalid throw/rethrow, jump/fallthrough либо finally transfer, conditional expression в аргументе вызова либо операнде return | R-STMT-0003..0013, R-STMT-0018, R-FUNC-0003, R-FUNC-0008, R-ERR-0001..0003, R-EXPR-0014 |
| `R-DIAG-SWITCH-001` | Duplicate/non-exhaustive/invalid switch/pattern | R-STMT-0006..0010 |
| `R-DIAG-UNSAFE-001` | Unsafe operation outside unsafe context | R-UNSAFE-0001..0002 |
| `R-DIAG-UNSAFE-002` | Missing/empty safety contract | R-UNSAFE-0003 |
| `R-DIAG-STACK-001` | Recursive call chain либо recursive ownership при static stack discipline | R-FUNC-0004, R-FUNC-0007, R-FUNC-0026, R-OWN-0020, R-TYPE-0054 |
| `R-DIAG-TRAIT-001` | Неполная, несовпадающая, orphan либо дублирующая trait implementation, несвязанный associated type или constant | R-TYPE-0041..0042, R-TYPE-0045..0046, R-TYPE-0050..0051, R-AGG-0012, R-TYPE-0055 |
| `R-DIAG-MEM-001` | Potential data race, invalid Send/Sync transfer or escaped scoped-thread/task capability | R-MEM-0002..0004, R-MEM-0010..0031, R-LIB-0010..0017 |
| `R-DIAG-ATOMIC-001` | Invalid constant memory ordering | R-MEM-0007 |
| `R-DIAG-MOD-001` | Invalid import, module identity/cycle or interface mismatch | R-MOD-0001..0007 |
| `R-DIAG-FFI-001` | Non-ABI-compatible scalar/signature/layout, checked effect or boundary capability | R-TYPE-0022..0024, R-OWN-0018, R-FFI-0002..0006, R-FFI-0017, R-FFI-0021, R-FFI-0058 |
| `R-DIAG-FFI-002` | Invalid variadic argument or C symbol attribute | R-FFI-0005, R-FFI-0007, R-FFI-0045, R-FFI-0057 |
| `R-DIAG-FFI-003` | Opaque/unsupported C declaration or invalid FFI form/attribute | R-FFI-0015..0020, R-FFI-0050..0052, R-FFI-0054, R-FFI-0057, R-FFI-0059 |
| `R-DIAG-FFI-004` | Header or ABI-record verification mismatch | R-FFI-0039..0044 |
| `R-DIAG-FFI-005` | Callback/function-pointer signature mismatch | R-FFI-0012, R-FFI-0024..0026 |
| `R-DIAG-FFI-006` | Non-empty extern block lacks `@header` or `@abi` | R-FFI-0038 |
| `R-DIAG-LINK-001` | Missing/incompatible target library manifest or artifact | R-FFI-0028..0033 |
| `R-DIAG-LINK-002` | Conflicting library identity or unresolved static dependency cycle | R-FFI-0034..0035 |
| `R-DIAG-LINK-003` | Missing/ambiguous/wrong-provider required symbol or load dependency | R-FFI-0036..0037, R-CMAP-0031 |
| `R-DIAG-FORMAT-001` | Недопустимый слот, спецификатор, тип аргумента или хранение шаблона | R-EXPR-0028 |
| `R-DIAG-JSON-001` | Некорректный контракт JSON-поля или недоступная схема преобразования | R-JSON-0001 |
| `R-DIAG-LIMIT-001` | Documented implementation limit exceeded | R-LIMIT-0001..0003, R-EXPR-0032, R-META-0002, R-STMT-0022 |
<a id="R-DIAG-0001"></a>

**R-DIAG-0001** — После любой required diagnostic implementation shall not emit an executable marked conforming. Recovery AST/IR shall retain poisoned state, чтобы последующая code generation не использовала guessed semantics.

<a id="R-DIAG-0002"></a>

**R-DIAG-0002** — Для каждого code Annex B conformance suite shall содержать хотя бы один negative program; для каждой основной feature — positive counterpart.

<a id="annex-c"></a>

## Annex C — Implementation-defined behavior

Этот каталог закрыт для R 0.1. Каждый conforming target manifest shall указать выбор для каждой строки; иной implementation-defined choice требует новой версии стандарта.

| ID | Choice | Обязательная документация |
|----|----|----|
| `R-IDB-001` | Width `usize`/`isize`: 32 или 64 | Width, ranges, corresponding C types |
| `R-IDB-002` | Byte order raw object representation | Little, big либо mixed с полным описанием |
| `R-IDB-003` | Default R aggregate layout | Size/alignment/field-offset algorithm per target |
| `R-IDB-004` | Module path to source mapping | Canonical roots, case sensitivity, separator rules |
| `R-IDB-005` | Panic strategy | `abort` или `unwind`; status and diagnostic sink |
| `R-IDB-006` | Stack exhaustion response | Panic category либо immediate documented termination |
| `R-IDB-007` | Hosted process status mapping | Mapping i32 `main` result to environment |
|`R-IDB-023` |Статусы неявных ошибок main |Диапазоны приложения и подложки, полный маппинг portable domain, статус invalid_main_status и allocation-free diagnostic sink |
| `R-IDB-008` | C17 implementation and ABI | Compiler identity/version, data model, calling convention |
| `R-IDB-009` | `c_*` scalar representations/availability | Size, alignment, signedness, IEC format/rounding/operation support and optional exact-width typedefs |
| `R-IDB-010` | Actual implementation limits | Every limit at or above section 25 minima |
| `R-IDB-011` | Atomic lock-free properties | For each supported atomic type |
| `R-IDB-012` | Allocation maximum/alignment | Maximum object, default allocator and OOM action |
| `R-IDB-013` | Thread scheduling facilities | Available thread profile and scheduling guarantees, if any |
| `R-IDB-014` | External symbol spelling constraints | Accepted character set, maximum length, collision policy |
| `R-IDB-015` | Lone CR source handling warning class | Warning code and whether warnings fail strict build policy |
| `R-IDB-016` | Implicit C runtime import set | Exact symbols usable without `@link` for target/profile |
| `R-IDB-017` | Dynamic `link_load_failure` termination | Process status and diagnostic sink before `main` |
| `R-IDB-018` | Dynamic loader deployment policy | How manifest runtime identity is installed/resolved without host fallback |
| `R-IDB-019` | Pointer/integer exposure support | Supported address widths and provenance-restoration API, or unsupported |
| `R-IDB-020` | C-boundary floating-environment isolation | Immutable canonical state, working C17 save/canonicalize/restore, verified runtime helper, or unavailable contract-only fallback; all extra controls and failure behavior |
| `R-IDB-021` | Native asynchronous backend | Backend identity/version, executor model, cancellation acknowledgement and deadline facilities, а также count filesystem adapter lane, admission list и cancellation matrix; unavailable, когда `hosted-native-async` невозможно реализовать полностью |
| `R-IDB-022` | Native process argument mapping | Exact native-to-Unicode/UTF-8 conversion, representable domain, source element zero либо synthesis executable spelling для zero arguments, а также emergency diagnostic sink и distinct nonzero process status для каждой из `argument_encoding_failure` и startup-snapshot `allocation_failure`; values shall agree with R-SLIB-IDB-0010 |

<a id="R-IDB-0001"></a>

**R-IDB-0001** — Implementation-defined choice shall be stable for one target manifest. Changing it invalidates interface fingerprints and requires full rebuild.

<a id="annex-d"></a>

## Annex D — Unspecified behavior

Этот каталог также закрыт. Все варианты остаются defined and safe.

| ID | Unspecified choice | Разрешённые варианты |
|----|----|----|
| `R-USB-001` | Numeric addresses of objects/allocations | Any suitably aligned non-overlapping placement |
| `R-USB-002` | Padding byte values | Any values; safe R cannot observe them |
| `R-USB-003` | NaN payload/sign after operation | Any quiet NaN allowed by operand format |
| `R-USB-004` | Which allocation request fails first | Any request consistent with available resources |
| `R-USB-005` | Thread interleaving | Any interleaving consistent with happens-before |
| `R-USB-006` | Order among concurrent relaxed atomics | Any values allowed by memory model/modification order |
| `R-USB-007` | Diagnostic wording and secondary spans | Any wording retaining required code, primary span and rule ID |
| `R-USB-008` | Internal start representation of empty slice | Null or aligned non-dereferenceable sentinel |
| `R-USB-009` | C padding received through valid FFI | Any C-provided bytes; not observable as R value |
| `R-USB-010` | Formatting of panic report | Any target-appropriate format preserving category |
| `R-USB-011` | Attached runtime context для detached-outcome cleanup | Any fresh live cleanup context по R-MEM-0017; никогда context после начала thread-local teardown |

<a id="R-USB-0001"></a>

**R-USB-0001** — Implementation shall not use an unspecified choice to alter deterministic expression order, drop order, runtime check point or required result.

<a id="annex-e"></a>

## Annex E — Unsafe operations and safety contracts

### E.1 General rule

<a id="R-SAFETY-0001"></a>

**R-SAFETY-0001** — Every unsafe operation below has all listed preconditions. Caller shall establish them for the complete duration stated. Violating any one precondition produces UB; satisfying all yields the defined effect.

### E.2 Closed operation table

| Contract ID | Operation | Safety contract and defined effect |
|----|----|----|
| `R-SAFETY-RAW-DEREF` | Read/write through `raw T*` | Pointer is non-null, aligned, provenance-valid for a live initialized T, within bounds; read has no conflicting exclusive/write access; write additionally targets mutable storage with exclusive effective access. Effect equals typed T access. |
| `R-SAFETY-RAW-ARITH` | Raw `p + n`, `p - n`, indexing | p points to element or one-past a live array object; mathematical result remains from first element through one-past; offset is representable. One-past result shall not be dereferenced. Effect preserves allocation provenance. |
| `R-SAFETY-RAW-DIFF` | Raw pointer subtraction/order | Both pointers have provenance of the same live array, including one-past, and difference is representable in isize. Effect is element difference or array order. |
| `R-SAFETY-RAW-CAST` | Cast raw object pointer type/change raw constness | Source is null or carries preserved allocation identity/byte offset; target uses a manifest-compatible C object-pointer representation. Effect preserves null, identity and offset but creates no borrow, alignment, effective-type or mutation permission. Any later access independently satisfies R-SAFETY-RAW-DEREF; removing `const` never makes immutable storage writable. |
| `R-SAFETY-RAW-BORROW` | `pointer as const T*` или `pointer as T*` | Raw pointer satisfies deref contract for entire declared lifetime; shared/exclusive alias rule is established before creation and maintained until borrow ends. Effect creates corresponding provenance-bearing borrow. |
| `R-SAFETY-BORROW-RAW` | `borrow as raw const T*` или `borrow as raw T*`, включая borrow, derived from owner | Source object remains live and unmoved through every raw access; raw access never exceeds source permission or range. Effect exposes pointer without extending lifetime. |
| `R-SAFETY-PTR-INT` | Pointer/integer conversion | Target manifest supports exposed addresses; integer round-trip uses unmodified sufficiently wide `usize`, allocation remains live, and implementation API restores provenance. Arbitrary integer is not dereferenceable without an independently established platform mapping. |
| `R-SAFETY-SLICE-PARTS` | `core::slice_from_raw_parts(pointer, length)` или `core::slice_from_raw_parts_mut(pointer, length)`, либо anchored `core::slice_from_raw_parts_in(anchor, pointer, length)` или `core::slice_from_raw_parts_in_mut(anchor, pointer, length)` | При length больше zero pointer является non-null и identifies length последовательных initialized aligned elements T; byte size representable. Shared form не имеет conflicting write либо exclusive access, а mutable form имеет exclusive access для всего fresh inferred result region, а для anchored form — пока referent anchor live и unmoved. При zero length pointer является null либо valid aligned sentinel и никогда не dereferenced. Effect создаёт ровно соответствующий slice descriptor без copying element либо extending storage lifetime. |
| `R-SAFETY-TRANSMUTE` | Representation transmute `source as D` | Source/target satisfy R-UNSAFE-0007 Copy-only, no-`constexpr str`-target restriction and equal size; copied bits are a valid initialized target value; operation does not fabricate borrow provenance, expose padding as value or create invalid enum/bool/char/pointer. Source remains an ordinary Copy value. |
| `R-SAFETY-VOLATILE` | `core::volatile_load(address)` или `core::volatile_store(address, value)` | Address является non-null, valid и aligned для одного live initialized object T и exact access width; store дополнительно designates writable storage. Target device permits access, а concurrent semantics externally synchronized. Effect выполняет ровно один observable volatile read либо write T; он не является atomic synchronization. |
| `R-SAFETY-ADOPT` | `core::adopt(pointer)` | Pointer является exact non-null aligned base pointer одной live allocation, содержащей один initialized T и созданной implementation-documented allocator, compatible со standard unique-owner drop и deallocation. Не существует другого owner, drop duty либо deallocator duty. Effect создаёт `own T*` над тем же object и transfers его exact drop и deallocation duty в R без allocation либо construction другого T. |
| `R-SAFETY-RELEASE` | `core::release(move owner)` | Owner является одним live unique owner, а recipient accepts его exact T drop, allocation и compatible-deallocator contract и shall discharge каждую duty ровно один раз. Effect consumes owner без drop или deallocation, возвращает exact non-null allocation base как `raw T*`, сохраняет T initialized и transfers все duties caller. |
| `R-SAFETY-SHARED-INTO-RAW` | Convert `arc T` or `rc T` strong owner into raw token | Owner является одним valid initialized strong owner и consumed ровно один раз. Effect suppresses drop этого handle без изменения strong count и возвращает exact non-null stable base pointer к T как `raw const (T)*`. Он создаёт одну abstract outstanding obligation для (runtime, allocation identity, family, T). Copying raw pointer bits не copies obligation; raw shared read её не consumes. Каждый отдельно выполненный `into_raw`, включая applied to clone, создаёт одну obligation, даже когда pointer bits equal. `rc` obligation остаётся на originating R thread и, для C-origin thread, в его attachment generation. |
| `R-SAFETY-SHARED-FROM-RAW` | Reconstruct `arc T` или `rc T` from raw token | Pointer является exact base pointer, returned `into_raw` того же family/T, возможно после provenance-preserving object-pointer/`void`-pointer round trip; существует минимум одна unmatched obligation для этого tuple; runtime, defining module, drop glue и allocator domain остаются live. Cross-thread `arc` use дополнительно требует T Send+Sync и verified transfer/synchronization contract. `rc` требует originating R thread и для C-origin execution ту же live explicit attachment generation. Effect consumes ровно одну obligation и recreates один strong owner без изменения count. Pointer copy не создаёт obligation. Forged/interior/wrong-family/wrong-T pointer, over-consumption либо repetition после last obligation нарушает unsafe contract и является UB. |
| `R-SAFETY-ASSUME` | `core::assume(condition)` | Condition is true at this execution point for all abstract-machine states. Effect supplies fact to optimizer; false is immediate UB. |
| `R-SAFETY-EXTERN-CALL` | Call imported C function/function pointer | Associated library is loaded and remains loaded; symbol/function pointer is non-null and has verified compatible prototype/calling convention; all C/application preconditions, lifetimes, nullability, initialization, effective types and thread rules hold; C does not unwind/longjmp across boundary or retain data beyond declared contract; floating environment is isolated per R-FFI-0058. |
| `R-SAFETY-EXTERN-OBJECT` | Read/write/address imported C object | Required symbol is resolved, library initialization completed, object/TLS representation is valid for declared type, access is permitted by C declaration and all external/concurrent accesses are excluded or synchronized for full operation. |
| `R-SAFETY-EXTERN-CALLBACK` | C invokes exported R callback | Function pointer/prototype/calling convention match; R runtime is initialized; current thread is already attached or may be temporarily attached by the entry trampoline; every argument and pointed range satisfies R validity for complete callback; callback code remains loaded; no foreign unwind or longjmp crosses callback. |
| `R-SAFETY-EXTERN-RETAIN` | C retains pointer, callback or userdata | Every retained allocation/code object remains live, stable and correctly synchronized until documented release callback/event; any transferred owner has exactly one receiver; no R borrow expires or owner drops earlier. |
| `R-SAFETY-CSTRING` | Use NUL-terminated C string | Pointer is non-null unless API permits null; readable range contains a terminator before declared bound; bytes meet required encoding; buffer remains stable for complete call/retention time; mutable destination has sufficient writable capacity. |
| `R-SAFETY-MUT-STATIC` | Access mutable R static storage | R initialization is complete and all conflicting thread/interrupt/signal accesses are excluded or synchronized. Effect is ordinary typed access; imported external objects use R-SAFETY-EXTERN-OBJECT instead. |

<a id="R-SAFETY-0002"></a>

**R-SAFETY-0002** — Contract attribute syntax shall be `@safety("CONTRACT-ID", "preconditions")`. Standard contracts may cite table IDs; project-defined unsafe function shall use globally unique identifier and complete preconditions rather than merely state “caller is responsible”.

<a id="R-SAFETY-0003"></a>

**R-SAFETY-0003** — Inline assembly, direct compiler intrinsic not standardized here, setjmp/longjmp across live R objects and foreign exception unwinding through R frames are not unsafe operations R 0.1; they are unsupported extensions.

<a id="annex-f"></a>

## Annex F — Normative mapping to ISO C17

### F.1 Mapping principle

<a id="R-CMAP-0001"></a>

**R-CMAP-0001** — C17 backend shall generate strictly conforming C17 plus a runtime that reproduces R abstract machine. It shall not rely on signed overflow, invalid shift, null/invalid dereference, unsequenced side effects, inactive union reads, misalignment, out-of-bounds pointer arithmetic, data races or strict-aliasing UB for any conforming safe R execution.

<a id="R-CMAP-0002"></a>

**R-CMAP-0002** — Generated C shall compile in strict mode equivalent to `-std=c17 -pedantic` without compiler extensions. Target-specific ABI adaptation shall be isolated in configured headers validated by static assertions.

### F.2 Type mapping

| R type | Preferred C17 representation | Required condition |
|----|----|----|
| `bool` | `_Bool` | Generated R creates only 0/1; unreadable foreign trap representation is outside portable ingress |
| `i8..i64` | `int8_t..int64_t` | Exact typedef exists; otherwise conforming fixed-width emulation |
| `u8..u64` | `uint8_t..uint64_t` | Exact width, no padding |
| `isize` | `ptrdiff_t` or exact signed type | Width matches target manifest |
| `usize` | `size_t` or exact unsigned type | Represents all object sizes |
| `char` | `uint32_t` | Every construction validates Unicode scalar range |
| `f32`, `f64` | `float`, `double` | Only if IEC 60559 semantics match; otherwise runtime/software representation |
| `c_*` scalar | Corresponding C fundamental/standard typedef | Exact selected target C ABI; optional typedef shall exist |
| Borrow/raw pointer | C object pointer | R metadata/checks preserve lifetime, range, provenance, nullability |
| Opaque extern struct | Incomplete C tag or typedef | Only raw pointer use; `@c_type` selects spelling kind |
| `raw fn(P...) -> R` | C function pointer | Exact C prototype and calling convention |
| `own T*` | T pointer plus static ownership state | Exactly one cleanup path; representation not public ABI |
| `arc T` | One non-null pointer to private atomic control block | Strong/weak protocol R-CMAP-0032..0034; never a C ABI type |
| `rc T` | One non-null pointer to private thread-local control block | Plain checked counts; static non-Send/non-Sync enforcement |
| `weak arc T`, `weak rc T` | Same-family control-block pointer | Never dereferenced; block outlives T until last weak release |
| `T[N]` | C array or wrapper struct | No decay visible to R; element order preserved |
| `T[]` | Generated `{ T *data; size_t len; }` | Not C ABI unless explicit wrapper contract |
| `array<T>` | Private `{ T *data; size_t len; size_t capacity; }`-equivalent owner | Contiguous storage, fallible growth и reverse-order destruction соответствуют R-LIB-0019; never a C ABI type |
| `list<T>` | Private owner doubly linked stable nodes | Node identity и address stability соответствуют R-LIB-0022..R-LIB-0023; never a C ABI type |
| `dict<K,V>` | Private entry sequence плюс power-of-two sparse index table | Insertion order и exact linear-probing/load-factor invariants соответствуют R-LIB-0020..R-LIB-0021; never a C ABI type |
| `str` | Generated `{ const uint8_t *data; size_t len; }` | Сохраняются UTF-8 invariant и lifetime ordinary borrow |
| `constexpr str` | Тот же descriptor, pointing only into immutable program-image bytes либо program-lifetime empty sentinel | Доказаны UTF-8 и program-long backing storage; descriptor не имеет runtime-storage drop |
| R struct | Generated C struct | Field order preserved; generated padding/layout private |
| Fieldless enum | Fixed integer wrapper | Reject invalid discriminants before safe R |
| Payload enum | Tag plus C union/struct storage | Read only active variant; cleanup switches on tag |
| `o<T>` | Tag plus payload storage | Niche optimizations allowed only when unobservable and ABI-private |
| Function completion для `T throws E...` | Private tag plus T/error-alternative storage | Не является source value type; checked propagation и exactly-once cleanup preserved |
| `task<T throws E...>` | Private pointer/handle к executor-owned frame и completion state | Unique observation right, checked completion, cancellation lifetime и edges R-MEM-0031 preserved; never C ABI type |
| `atomic T` | Matching `_Atomic` type or locked runtime | C memory order at least as strong as R order |
| Standard thread/sync/channel/container resource | Private generated wrapper around runtime state | Move/drop и borrow state tracked; no public C ABI representation |

<a id="R-CMAP-0003"></a>

**R-CMAP-0003** — Fixed-width typedef absence shall not cause substitution by a different-width C type. Implementation shall emulate or report target unsupported.

### F.3 Expressions and checks

<a id="R-CMAP-0004"></a>

**R-CMAP-0004** — Backend shall linearize R left-to-right evaluation into C temporaries/statements; it shall not place multiple R side effects into C expression whose evaluation order is unspecified or unsequenced.

<a id="R-CMAP-0005"></a>

**R-CMAP-0005** — Signed addition/subtraction/multiplication shall be range-checked before performing signed C operation or computed in proven safe unsigned/runtime form. Compiler overflow builtins may not be required by portable output.

<a id="R-CMAP-0006"></a>

**R-CMAP-0006** — Division shall guard zero and signed MIN/-1 before C `/` or `%`. Shift shall guard count before shift; signed left shift shall use checked arithmetic; signed right shift shall be implemented independently of C implementation choice.

<a id="R-CMAP-0007"></a>

**R-CMAP-0007** — Array/slice access shall check R bounds before C address formation, unless compiler proves bounds from R semantics. It shall not form an out-of-range C pointer speculatively on a path where R operation panics.

<a id="R-CMAP-0008"></a>

**R-CMAP-0008** — Checked cast shall validate range/NaN/infinity before C cast. Generated C shall never execute a floating-to-integer conversion outside C range. Representation transmute shall copy source object representation через `memcpy` либо equivalent compiler operation в distinct target storage, затем expose D только после установления validity contract R-SAFETY-TRANSMUTE. Generated C shall not использовать union или pointer type-punning, treat padding как R value либо нарушать C effective-type rules.

<a id="R-CMAP-0009"></a>

**R-CMAP-0009** — Nullable pointer check shall dominate C dereference. Borrow/raw metadata may erase only after compiler has proved all lifetime/alias constraints. Raw-parts intrinsic constructs только ordinary pointer-and-length slice descriptor и не выполняет element access; его nullable zero-length pointer остаётся subject empty- slice mapping R-ARRAY-0003. Volatile load либо store lowers ровно в один access через appropriately volatile-qualified C lvalue compatible T либо в один separately verified runtime primitive с той же width и observable effect; backend shall not duplicate, merge либо remove этот access. `core::adopt` и `core::release` сохраняют exact base pointer и меняют только R ownership/drop obligation; ни одна operation не allocates, constructs, drops либо deallocates T. `core::assume` не требует C runtime side effect на contract-satisfying execution и не требует non-C17 construct в generated application C.

### F.4 Drop and control flow

<a id="R-CMAP-0010"></a>

**R-CMAP-0010** — Backend shall emit one logically unique cleanup action for every initialized Move object on each normal/early/unwind edge. Initialization flags or structured cleanup blocks shall prevent double drop after move.

<a id="R-CMAP-0011"></a>

**R-CMAP-0011** — Backend may использовать внутренние C cleanup labels и jumps при условии, что они не входят в scope через C variably modified object, не пропускают initialization и не изменяют R drop/evaluation order.

<a id="R-CMAP-0012"></a>

**R-CMAP-0012** — Payload enum C union may be accessed only through member matching current tag and effective type. Backend shall not type-pun; `memcpy` may copy only Copy representations with all validity invariants re-established.

### F.5 Translation and validation

<a id="R-CMAP-0013"></a>

**R-CMAP-0013** — Generated identifiers shall avoid C reserved identifier classes, be unique after target linker truncation and not depend on Unicode C identifiers.

<a id="R-CMAP-0014"></a>

**R-CMAP-0014** — Generated translation units shall include only standard C17 headers plus versioned R runtime headers. Dedicated ABI verifier/bridge translation unit may include only headers selected through verified manifest and shall itself compile in strict C17 mode. Such header remains compatibility evidence, not a semantic source that injects C declarations into R.

<a id="R-CMAP-0015"></a>

**R-CMAP-0015** — Conformance shall compile generated C with selected production C17 compiler and warnings treated as errors. When available for target, it shall add an independent C17 compiler and ASan/UBSan; concurrent subset additionally uses TSan or equivalent race validation. Each compiler has its own manifest and repeats R-CMAP-0024 ABI verification. Unavailable tool shall be recorded with a documented substitute or explicit validation limitation; absence does not relax language semantics.

<a id="R-CMAP-0016"></a>

**R-CMAP-0016** — If target C makes a pointer value indeterminate after deallocation, backend shall preserve separate allocation identity/offset metadata for R raw equality and shall not evaluate an indeterminate C pointer merely to implement R-TYPE-0021.

### F.6 External C libraries

<a id="R-CMAP-0017"></a>

**R-CMAP-0017** — Each imported function/object shall use exactly one compatible C17 declaration path. Direct path emits one `extern` declaration with selected `@link_name` as actual C identifier and is allowed only when identifier is non-reserved and no header-owned type spelling is required. Bridge path of R-CMAP-0026 obtains actual declaration from verified header and exposes a unique private bridge declaration to generated R translation unit; it shall not also redeclare the actual symbol there. Neither path shall generate a definition or tentative definition of the actual imported entity. Bridge may define only its private forwarding helper prescribed by R-CMAP-0026 and shall introduce no automatic or mirrored storage for imported object.

<a id="R-CMAP-0018"></a>

**R-CMAP-0018** — `@c_type` shall select C typedef/tag spelling without embedding arbitrary source tokens. Opaque type produces only compatible incomplete declaration. For complete `@repr(C)` struct or enum, backend shall either use the exact header-owned declaration through the verified include/bridge or emit from ABI record the complete normalized declaration one-for-one when every emitted identifier may legally be declared. It shall never emit a subset declaration, and a complete enum emission shall contain every verified enumerator with its verified name and value; enumerator order may differ as permitted by C17 compatibility. Reserved or otherwise header-owned spelling requires the header-compatible bridge of R-FFI-0057/R-CMAP-0026.

<a id="R-CMAP-0019"></a>

**R-CMAP-0019** — `@c_constant` shall substitute verified R constant and shall not emit symbol reference. A verifier translation unit shall compare it to header/ABI integer value with `_Static_assert` and shall obtain/check compatible expanded- expression type, rank and signedness through C17 type probes plus normalized compiler inventory. Cross-target record shall retain exact type, enum identity if any, rank, signedness and mathematical value.

<a id="R-CMAP-0020"></a>

**R-CMAP-0020** — Every exported `extern "C"` function shall use a C ABI entry trampoline or equivalent boundary stub whenever thread attachment, panic barrier, representation validation or fenv isolation is required, whether or not declaration bears `@callback`. C-visible entry shall not let R unwind cross C and shall preserve exact function type.

<a id="R-CMAP-0021"></a>

**R-CMAP-0021** — Imported external/TLS object access shall compile to actual symbol access only inside unsafe operation after any required runtime library guard. C object shall not be copied into hidden R global that changes identity.

<a id="R-CMAP-0022"></a>

**R-CMAP-0022** — Backend driver shall translate logical `@link` graph to target linker inputs exclusively from selected manifest, preserving dependency order, artifact identity and static/dynamic/framework kind. It shall not append discovered host search result or silently switch static/dynamic kind.

<a id="R-CMAP-0023"></a>

**R-CMAP-0023** — Dynamic/framework artifact and required symbol availability shall be checked by platform loader or generated startup guard before R module initialization. Failure follows R-FFI-0037 and shall never reach a null/incorrect call.

<a id="R-CMAP-0024"></a>

**R-CMAP-0024** — ABI verifier shall be compiled for target with same C compiler identity/options/sysroot/feature macros as generated C. Running a host executable to infer target layout is forbidden in cross build; compile-time assertions or target ABI record shall be used.

<a id="R-CMAP-0025"></a>

**R-CMAP-0025** — Generated C, ABI probe, link manifest and, when requested, exported C header shall be reproducible from identical normalized inputs. Hashes of every artifact actually emitted shall be recorded in conformance artifact; this rule does not itself require optional C-header generation.

<a id="R-CMAP-0026"></a>

**R-CMAP-0026** — If exact compatible declaration depends on header-defined typedef, reserved imported identifier, or tag expansion that portable standalone C17 cannot safely redeclare, backend may generate a strict-C17 bridge translation unit that includes verified headers. An equivalent declaration emitted only from immutable ABI record is allowed for non-reserved identifiers; reserved identifier requires the declaring header. Bridge calls/addresses original symbol and exposes a unique private non-reserved default-C ABI already proven equivalent to R declaration. Header requiring a nonstandard language extension, calling convention or decorated symbol is rejected unless user supplies an external default-C-ABI shim outside generated conforming backend. Guessing typedef underlying type, using compiler attributes или erasing actual pointer type imported declaration to `void*` is forbidden. Это не запрещает explicit user-visible managed-token adapter, declared C ABI которого является `const void*` по R-FFI-0060/R-CMAP-0037. Object bridge shall address actual C object/TLS and shall not introduce mirrored storage.

<a id="R-CMAP-0027"></a>

**R-CMAP-0027** — Backend/runtime shall implement R stack-exhaustion behavior through stack probes/checks, managed stack or equivalent preflight before unsafe C stack growth. It shall not rely on recovering after generated C has overflowed its stack or otherwise executed invalid memory access.

<a id="R-CMAP-0028"></a>

**R-CMAP-0028** — Generated C shall preserve each R `f32`/`f64` operation and cast at the declared precision and rounding rule. Excess precision, contraction, reassociation, reciprocal approximation, finite-math assumptions and other fast-math transformations that can change an R result are forbidden; backend may use runtime helpers when target C evaluation cannot provide the required result.

<a id="R-CMAP-0029"></a>

**R-CMAP-0029** — C ABI ingress shall store return/object/C-origin-entry argument representations in compatible C temporaries without treating them as initialized safe R values, perform R-FFI-0056 checks, and only then materialize R representation. Optimizer shall not assume R enum, non-null or aggregate validity before the dominating gate.

<a id="R-CMAP-0030"></a>

**R-CMAP-0030** — Backend shall implement C-boundary isolation R-FFI-0058 with working C17 `<fenv.h>` operations, verified runtime helper, or proven immutable canonical no-op as recorded by R-IDB-020. Software floating operations may ensure R arithmetic independently but do not by themselves save/canonicalize/restore host fenv and satisfy boundary only with the contract-only fallback of R-FFI-0058. Backend shall compile R floating operations under environment-access rules that prevent ambient rounding-mode assumptions. Generated R operation shall have round-to-nearest ties-to-even semantics independently of C code called before it; every C-origin return restores foreign environment before control reaches C. Return codes/failures are handled exactly as R-FFI-0058, never ignored. C17 path shall use `fegetenv`/`fesetenv`, `fesetround(FE_TONEAREST)` and `feclearexcept(FE_ALL_EXCEPT)` as applicable; target helper normalizes/restores any additional value-affecting or trap-control state not represented by portable C17.

<a id="R-CMAP-0031"></a>

**R-CMAP-0031** — Strict C17 has no portable DLL/framework visibility syntax. Required visibility/rooting of every exported `extern "C"` symbol shall therefore be a verified target build/link-manifest property or be supplied by external shim; the backend shall not insert an unverified compiler attribute. Missing export in final typed symbol inventory requires `R-DIAG-LINK-003`.

### F.7 Reference counting, threads, tasks и synchronization

<a id="R-CMAP-0032"></a>

**R-CMAP-0032** — Каждый `arc` или `rc` strong/weak handle lowers в один non-null C object pointer на opaque suitably aligned control block. Block содержит T storage или private pointer к нему, strong/weak-liveness counters, drop glue и allocator identity. `arc` block использует C17 `_Atomic` counters exact unsigned C representation, selected для R `usize`, либо runtime-locked counter с теми же width, limit и semantics. `rc` block использует ordinary counters того же exact type, confined одному R thread. Coallocation/split allocation unobservable.

<a id="R-CMAP-0033"></a>

**R-CMAP-0033** — Portable atomic lowering `arc` shall implement: checked strong clone через CAS increment с relaxed success/failure; `downgrade`/`clone_weak` через checked weak-liveness CAS increment с relaxed success/failure и retry во время installed uniqueness sentinel; strong release через release decrement и, если prior value one, acquire fence до drop T; weak release через release decrement и acquire fence до block deallocation; weak upgrade через CAS из nonzero strong count с acquire success/relaxed failure; `try_unwrap` через acq_rel CAS one-to-zero с relaxed failure. После successful unwrap CAS одна cleanup duty moves T в alternative `unwrapped`, ровно один раз releases implicit weak-liveness duty и deallocates block iff нет explicit weak; каждый exceptional exit owns ровно одно из T/result и эту cleanup duty. Overflow проверяется до каждого successful write. Fetch-add с последующей overflow check non-conforming, поскольку может transiently wrap. Decrement-and-fence sequences либо runtime-locked alternative R-CMAP-0032 shall реализовать каждое synchronizes-with edge R-MEM-0028 до destruction T, move-out T либо control-block deallocation.

<a id="R-CMAP-0034"></a>

**R-CMAP-0034** — `arc get_mut` shall быть linearizable относительно strong clone, weak downgrade/clone, upgrade и release. Portable implementation may reserve максимальное значение, представимое типом `usize`, как locked sentinel: по R-OWN-0017 normal physical range идёт от zero до единицы плюс половина этого максимума, rounded down. Она may acquire-CAS sole implicit weak value в этот sentinel, acquire-check strong equals one, затем release-restore implicit weak value; weak creators retry while sentinel installed. Два independent count loads без exclusion non-conforming. `weak_count` также retries и никогда не reports sentinel. Last-strong cleanup shall release implicit weak duty ровно один раз после ordinary drop или first unwind; immediate abort при second panic следует R-ERR-0008. Successful uniqueness reservation и strong-count check shall реализовать synchronizes-with edges `get_mut` из R-MEM-0028 до return mutable borrow.

<a id="R-CMAP-0035"></a>

**R-CMAP-0035** — Spawn lowering shall stage explicit arguments в одном typed private capture record с per-field initialization state. Direct named Move argument является одним provisional capture field; argument temporaries никогда не recursively provisional. До commit field, полученный из direct named Move place, хранит provisional reference и exclusive reservation token, covering этот всё ещё initialized source и всё overlapping storage; lowering shall reject любой конфликтующий с token access последующего argument. Temporary arguments принадлежат caller-side full expression. Runtime резервирует полное состояние child и completion до единого atomic commit, converting каждый token в move его именованного source в capture. Затем successful runtime publication делает child sole cleanup owner до начала entry. Failed creation и unwind до commit освобождают каждый token, не commit ни одного named move, не уничтожают ни одного named source и уничтожает каждый staged temporary ровно один раз в reverse construction order. Abort до commit следует R-ERR-0005 и не выполняет дополнительный R cleanup. При normal entry completion child stages returned value либо exact declared checked-error payload и начинает свои thread-local drops. При unwind strategy panic, достигший thread root, вместо этого создаёт owned panic report после applicable cleanup. Если первый panic начинается в thread-local drop после normal return, lowering завершает remaining duties failing instance по R-ERR-0008, ровно один раз destroys staged returned value, drops remaining Live thread-local instances по exact stack algorithm R-OBJ-0008 и только затем forms и commits replacement panic report; panic во время любой subsequent cleanup является second panic по R-ERR-0008 и aborts. Только после applicable thread-local drops child commits ровно один returned, thrown либо panicked completion outcome. При abort strategy любой first panic aborts по R-ERR-0005/R-ERR-0009 без commit outcome и дальнейших R drops; no-panic child всё равно commits после thread-local drops. Child publishes committed completion release operation. Join consumes handle и observes completion acquire semantics до move result. Detached cleanup duty также observes child completion release с acquire до доступа к result. Detached completion destroys unobserved result ровно один раз через attached runtime cleanup duty R-MEM-0017. Её ordinary no-panic path завершает thread-local cleanup этого context до last abstract completion action, observed R-AM-0013. При unwind strategy любой contained либо newly produced panic report также передаётся hook и destroyed до этого action; при abort strategy первый panic немедленно aborts и subsequent action не требуется. Ни один payload drop не выполняется в target context после начала его thread-local teardown. Lowering retains отдельные target и observation/cleanup references completion state и выполняет sole last-reference release completion storage и runtime/OS thread resource в exact point R-MEM-0011; никакой moved, joined либо detached state не может release его дважды. `std.thread::thread` descriptor использует checked atomic либо runtime-locked identity/runtime- reference count exact R `usize` representation, enforces limit R-LIB-0013 до change и реализует last-reference acquire edge R-MEM-0011 до release descriptor storage.

<a id="R-CMAP-0036"></a>

**R-CMAP-0036** — Generated strict C17 shall access mutexes, condition variables, barriers, once objects, thread lifecycle и channels только через versioned R runtime functions, verified contracts которых реализуют section 21. Target с usable C17 `<threads.h>` may реализовать runtime через него; target-specific runtime may использовать иные facilities, но они shall not leak non-C17 syntax в generated translation units. Каждый guard, endpoint и thread handle имеет один explicit moved/active/completed state, предотвращающий double unlock/close/join/result drop. Successful scoped publication также records child в region supervisor независимо от handle и создаёт один non-observing registration liveness reference на completion storage. Каждое scoped outcome right имеет одно linearizable state из HandleOwned, SupervisorOwned и Consumed: explicit join меняет HandleOwned на Consumed, drop handle меняет HandleOwned на SupervisorOwned, а scope close после всех target waits меняет каждый remaining HandleOwned на SupervisorOwned до любого outcome staging. Scope-close transition still-live handle одновременно создаёт один non-observing tombstone reference для этого shell; drop handle transfers right без такого reference, поскольку shell не остаётся. Только still-live scoped handle, revoked этим scope-close transition, имеет tombstone; остаётся ли его right SupervisorOwned либо уже стал Consumed, его destruction только releases этот reference. Ordinary handle drop не оставляет shell либо tombstone release. Transition и tombstone state принадлежат completion storage, поэтому правило применяется без scan либо rewrite handle, nested в другом outcome или aggregate, и это storage остаётся live до destruction shell. Для каждого SupervisorOwned right phase-two acquire, который moves его outcome в staging sequence supervisor, меняет SupervisorOwned на Consumed ровно один раз и затем releases registration reference этой entry. Entry, уже Consumed explicit join, releases свой registration reference при visit в том же reverse- order phase-two sweep; supervisor больше не обращается к этой entry. После phase-three outcome consumption last-reference duty R-MEM-0011 runs только когда выполнены остальные её preconditions и все tombstone/registration releases.

<a id="R-CMAP-0037"></a>

**R-CMAP-0037** — Ни одна C declaration shall not воспроизводить managed-owner, control-block или standard-resource layout. Managed-token adapter существует только как explicitly declared exported `extern "C"` R function по R-FFI-0024/0060; exported name и exact C-compatible prototype объявляются user, и ни один adapter symbol не synthesized implicitly. Его C token parameters/results используют только `const void *` и выполняют explicit pointer round trip R-FFI-0060. Такой adapter set may предоставлять operation patterns create, retain и release; R 0.1 не определяет weak raw token, поэтому C upgrade pattern отсутствует. Create allocates owner, затем применяет `into_raw`. Retain attaches, reconstructs одну valid obligation, один раз clones и converts оба resulting owners обратно в две obligations без net loss input obligation. Release attaches, reconstructs и drops одну obligation. Arbitrary `raw T*` нельзя adopt как `arc T` или `rc T`. Каждый adapter завершает attachment/fenv establishment до pointer reconstruction/count mutation и сохраняет module/drop-glue lifetime до last outstanding obligation.

<a id="R-CMAP-0038"></a>

**R-CMAP-0038** — `async` body shall lower в private C17 state machine и owned frame либо observationally equivalent runtime representation. Каждый complete direct named Move argument до commit представлен provisional source reference и exclusive overlap-reservation token, который enforces R-FUNC-0010. Start path reserves complete frame/completion/executor state, затем atomically converts все tokens в argument moves и publishes ровно один раз; каждый returning либо unwind path до commit освобождает tokens и сохраняет named Move operands. Abort path следует R-ERR-0005 без дополнительного R cleanup. Suspension stores continuation state без prohibited borrow, а terminal completion выполняет одну release publication, matched acquire operation R-MEM-0031. Cancel, detach, task drop и completion races разделяют одно checked terminal state и один exactly-once cleanup path. Generated application translation units остаются strict C17 и calls только versioned R runtime entry points. Target-specific event-loop, filesystem, socket и process primitives confined в verified runtime adapters; они shall not leak non-C17 syntax либо native resource representations в generated C или public R types.

<a id="R-CMAP-0039"></a>

**R-CMAP-0039** — Target hosted-native-async may использовать filesystem adapter lane только для descriptor-relative open/close, query metadata, seek общей position, enumeration directory, создания directory, unlink, atomic no-replace rename и durability file/directory. Выбранный target manifest shall задавать точные native entry points; ни один незарегистрированный call не may исполняться на lane. Submission в lane является bounded и никогда не блокирует worker executor. Cancellation либо expiry deadline удаляет operation, которая всё ещё находится в queue. После входа thread lane в native call этот call не preempted: runtime сохраняет каждый handle, buffer и frame до return, не публикует outcome до acknowledgement и выполняет cleanup ровно один раз. Cancellation либо deadline may выбрать сообщаемый outcome только до документированной non-cancellable external commit point operation. Operation, успешно достигшая commit, возвращает success даже при более позднем observation cancellation. Payload byte transfer, console, DNS, sockets, child-process waiting и R execution shall not направляться через lane.

<a id="annex-g"></a>

## Annex G — Requirements for a conforming implementation

### G.1 Source of truth

<a id="R-CONF-G001"></a>

**R-CONF-G001** — При конфликте authority order: published specification version; corrigenda; normative EBNF; conformance suite; reference translator; tutorials and examples. Implementation behavior shall not silently redefine the language.

### G.2 Required implementation components

<a id="R-CONF-G002"></a>

**R-CONF-G002** — Conforming release shall provide: translator identity/version; strict mode; target manifest; diagnostics Annex B; machine-readable table mapping every `R-*` rule to tests or documented non-automatable review; reproducible standard-library interface; compatibility policy.

<a id="R-CONF-G003"></a>

**R-CONF-G003** — Translator shall process arbitrary input bytes without crash, out-of-bounds access or uncontrolled recursion up to documented resource exhaustion. Invalid source is data, not permission for translator UB.

<a id="R-CONF-G004"></a>

**R-CONF-G004** — Required test classes: lexical/grammar positive and negative; type/name; initialization; ownership/move; borrow/lifetime; runtime checks; drop order; panic; reference counting/weak lifetime; thread transfer/scoped join; locks/poison/channels; async start/await/cancel/detach; unsafe boundary; memory model; modules; C ABI; generated C; determinism and implementation limits.

### G.3 Profiles

| Capability | Freestanding | Hosted |
|----|----|----|
| Core syntax/types/ownership | Required | Required |
| Panic | Handler supplied by environment | Runtime abort/unwind |
| Allocation and `new` | May be unsupported with compile-time diagnostic | Required |
| `arc`/`rc`, `array<T>`/`list<T>`/`dict<K,V>` и R-LIB-0012/0019..0023 | Required iff allocation/new supported; иначе каждая construction/operation диагностируется | Required |
| Environment and non-blocking process state | Not required | Required |
| `async`/`await`/`task<T>` execution | Not required; use is diagnosed when unavailable | Required only in profile `hosted-native-async`; other hosted profiles may omit it |
| Native asynchronous I/O | Not required | Complete filesystem, console, network and child-process surface required by `hosted-native-async`; no synchronous alias |
| Threads | Not required | Required if target supports threads; otherwise documented hosted-single-thread profile |
| `std.sync` | Not required | Required with hosted-thread profile |
| C ABI | Required when target has conforming C17 implementation | Required |

<a id="R-CONF-G005"></a>

**R-CONF-G005** — Profile selection shall occur before translation and form part of interface fingerprint. Unsupported profile feature shall be diagnosed, not linked to a trap stub. `hosted-native-async` является hosted allocation profile, requiring process executor, task runtime и complete native asynchronous library contract R-REF-0005. Selection shall fail с `R-DIAG-PROFILE-001`, если у target отсутствует хотя бы один required backend либо обязательная cancellation acknowledgement/deadline facility. Его runtime shall not реализовывать potentially blocking I/O на worker executor. Разрешённые blocking adapters — filesystem adapter lane из R-TERM-0014/R-CMAP-0039 и R-REF-0005, пул блокирующих вызовов из R-TERM-0015, который исполняет только entries, переданные `std.async::blocking`, и адаптер файловых передач из R-TERM-0016, который исполняет только payload byte transfer обычных файлов.

=== G.4 Minimal normative examples

.Positive: ownership, borrow and deterministic drop

```r
module example.owner;

protected struct Buffer {
    own u8* data;
    usize size;
};

drop(Buffer* self) {
    // self.data is dropped after this body.
}

protected usize size_of(const Buffer* buffer) {
    return buffer->size;
}

i32 main() {
    own u8* byte = new u8(0u8);
    Buffer b = { .data = move byte, .size = 1usize };
    const Buffer* view = &b;
    usize n = size_of(view);
    return n as i32;
}
```

.Negative: named owner copied without move

```r
void rejected_owner_copy() {
    own u8* first = new u8(1u8);
    own u8* second = first; // R-DIAG-MOVE-001
}
```

.Negative: conflicting borrow

```r
i32 rejected_conflicting_borrow() {
    i32 value = 1;
    const i32* shared = &value;
    i32* exclusive = &value; // R-DIAG-BORROW-001 while shared is live
    return *shared + *exclusive;
}
```

.Positive: explicit `arc` clone transferred to a typed thread

```r
protected struct Payload {
    i32 value;
};

protected i32 read_payload(arc Payload payload) {
    return payload->value;
}

std.thread::join_result<i32> run_worker() throws std.thread::thread_error {
    arc Payload shared = new arc Payload { .value = 41i32 };
    arc Payload child = std.arc::clone(&shared);
    std.thread::join_handle<i32> handle = std.thread::spawn(read_payload, move child);
    std.thread::join_result<i32> joined = std.thread::join(move handle);
    return move joined;
}
```

.Positive: fast single-thread `rc` with explicit clone

```r
bool same_local_allocation() {
    rc i32 first = new rc i32(7i32);
    rc i32 second = std.rc::clone(&first);
    bool same = std.rc::ptr_eq(&first, &second);
    drop second;
    return same;
}
```

.Negative: `rc` cannot cross a thread boundary

```r
void consume_local(rc i32 value) {
    drop value;
}

void rejected_rc_transfer() throws std.thread::thread_error {
    rc i32 local = new rc i32(7i32);
    std.thread::join_handle<void> started =
        std.thread::spawn(consume_local, move local); // R-DIAG-MEM-001
}
```

.Positive: scoped thread borrows an automatic local

```r
i32 read_borrowed(const i32* value) {
    return *value;
}

void scoped_read() throws std.thread::thread_error {
    i32 value = 7i32;
    thread_scope {
        std.thread::scoped_join_handle<i32> started =
            std.thread::spawn_scoped(read_borrowed, &value);
    }
    value += 1i32;
}
```

.Positive: checked error propagation

```r
error Error {
    i32 code;
};

u8 read_first(const u8[] bytes) throws Error {
    if (len(bytes) == 0usize) {
        throw { .code = 1i32 };
    }
    return bytes[0usize];
}

u8 twice_first(const u8[] bytes) throws Error {
    u8 value = read_first(bytes);
    return (value + value) as u8;
}
```

.Positive: range-for по range, sequence и core iterator с membership test

```r
module example.iteration;

struct Counter {
    i32 next_value;
    i32 limit;
};

impl core::Iterator for Counter {
    type Item = i32;
    o<i32> next(Counter* this) {
        if (this->next_value >= this->limit) { return o::none; }
        i32 value = this->next_value;
        this->next_value += 1;
        return o::some(value);
    }
};

i32 main() {
    i32 total = 0;
    i32[3] fixed = {1, 2, 3};
    for (i32 i in 0..3) { total += i; }
    for (const i32* x in &fixed) { total += *x; }
    Counter counter = Counter { .next_value = 0, .limit = 3 };
    for (i32 v in move counter) { total += v; }
    if (total not in 0..100) { return 1; }
    return total - 12;
}
```

.Positive: collection expressions, comprehension и variadic sum

```r
module example.collections;

i32 sum(i32... values) {
    i32 total = 0;
    for (const i32* v in &values) { total += *v; }
    return total;
}

i32 main() {
    try {
        array<i32> small = [1, 2, 3];
        array<i32> squares = [x * x for (i32 x in 0..6) if (x % 2 == 0)];
        dict<i32, i32> doubles = {*x: *x * 2 for (const i32* x in &small)};
        i32 two = 2;
        if (two not in doubles) { return 1; }
        i32 packed = sum(1, 2, 3);
        i32 spread = sum(...small);
        i32 total = 0;
        for (i32 s in &squares) { total += s; }
        return packed + spread + total - 32;
    } catch (std.array::push_error<i32> failure) {
        failure as void;
        return 2;
    } catch (std.dict::insert_error<i32, i32> failure) {
        failure as void;
        return 3;
    }
}
```

.Positive: tuple result и recursion по type pack

```r
module example.packs;

trait Shown { i32 show(const Self* this); };
impl Shown for i32 { i32 show(const i32* this) { return *this; } };
impl Shown for bool {
    i32 show(const bool* this) {
        if (*this == true) { return 1; }
        return 0;
    }
};

@generic<Head: Shown, Tail...: Shown>
i32 total(Head head, Tail... tail) {
    i32 first = head.show();
    @if (len(Tail...) != 0usize) { return first + total(...move tail); }
    return first;
}

(i32, bool) split(i32 value) { return (value / 2, value % 2 == 0); }

i32 main() {
    (i32, bool) half = split(8);
    i32 sum = total(half.0, half.1, 3);
    return sum - 8;
}
```

.Positive: builder с методами @chain

```r
module example.builder;

struct Options { u16 port; usize capacity; };

Options Options::create() { return Options { .port = 8080u16, .capacity = 64usize }; }

@chain Options Options::with_port(Options this, u16 value) {
    this.port = value;
    return move this;
}

@chain Options Options::with_capacity(Options this, usize value) {
    this.capacity = value;
    return move this;
}

i32 main() {
    Options options = Options::create().with_port(9000u16).with_capacity(128usize);
    if (options.port == 9000u16) { return 0; }
    return 1;
}
```

.Positive: строковые метки, ветвь с throw и clauses без break

```r
module example.labels;

error Usage { str message; };

i32 width(str kind) throws Usage {
    return match (kind) {
        case "i8": 1;
        case "i16": 2;
        case "i32": 4;
        default: throw Usage { .message = "unknown type" };
    };
}

i32 main() {
    try {
        i32 bytes = width("i16");
        i32 code = 0;
        switch (bytes) {
        case 2: code = 0;
        default: code = 1;
        }
        return code;
    } catch (Usage failure) { return 2; }
}
```

.Positive: family ошибок, ближайший ancestor и повторный throw

```r
module example.errors;

error io_error { i32 code; };
error net_error : io_error { u16 port; };

i32 port_of(io_error failure) {
    try {
        throw move failure;
    } catch (net_error e) {
        return e.port as i32;
    } catch (io_error e) {
        return 0;
    }
}

i32 main() {
    try {
        throw net_error { .code = 7, .port = 80u16 };
    } catch (io_error failure) {
        i32 code = failure.code;
        return port_of(move failure) - 80 + code - 7;
    }
}
```

.Positive: checked errors, значения с плавающей точкой и замороженный dictionary при трансляции

```r
module spec.translation;

error bad_port { u32 digit; };

u16 parse_port(str text) throws bad_port {
    u32 value = 0u32;
    for (usize index = 0usize; index < len(text); index += 1usize) {
        u8 byte = text[index];
        throw (byte < 48u8 || byte > 57u8) bad_port {.digit = index as u32};
        value = value * 10u32 + ((byte - 48u8) as u32);
    }
    return value as u16;
}

u16 port_or(str text, u16 fallback) {
    try {
        return parse_port(text);
    } catch (bad_port failure) {
        return fallback;
    }
}

f64 kelvin(f64 celsius) { return celsius + 273.15; }

dict<str, u16> default_ports() throws std.alloc::alloc_error, std.dict::insert_error<str, u16> {
    dict<str, u16> ports = std.dict::create::<str, u16>();
    o<u16> first = ports.insert("http", 80u16);
    first as void;
    o<u16> second = ports.insert("https", 443u16);
    second as void;
    return move ports;
}

const u16 DEFAULT_PORT = parse_port("8080");     // 8080; "80a0" stops the build
const u16 PROXY_PORT = port_or("none", 3128u16); // 3128, caught during translation
const f64 FREEZING = kelvin(0.0);                // bit for bit as at run time
const dict<str, u16> PORTS = default_ports();    // frozen into the program image

i32 main() {
    str name = "https";
    o<const u16*> found = PORTS.get(&name);
    u16 port = match (found) {
        case variant o::some(value): *value;
        case variant o::none: DEFAULT_PORT;
    };
    i32 failures = port == 443u16 && PROXY_PORT == 3128u16 ? 0 : 1;
    failures += FREEZING == 273.15 ? 0 : 1;
    return failures;
}
```

.Positive: блок дедлайна ограничивает запущенные в нём операции

```r
module example.deadline;

async usize size(std.fs::path path) throws std.fs::fs_error, std.async::start_error {
    array<u8> data = await std.fs::read_file(&path, 65536usize);
    return len(data);
}

async usize total(std.fs::path first, std.fs::path second)
    throws std.fs::fs_error, std.async::start_error, std.time::time_error {
    std.time::instant start = std.time::monotonic_now();
    // Every read started in the block, also by the tasks it starts, ends within two seconds.
    deadline (start.add(std.time::duration_from_seconds(2i64))) {
        usize here = await size(move first);
        usize there = await size(move second);
        return here + there;
    }
}
```

.Positive: форматирование пользовательских типов через core::Format

```r
module example.formatting;

@derive(format)
enum Shape { circle(f64), rect { i32 w; i32 h; }, empty };

struct Celsius { f64 degrees; };

impl core::Format for Celsius {
    void format(const Celsius* this, std.format::builder* out) throws std.alloc::alloc_error {
        std.string::string text = f"{this->degrees} C";
        std.format::append_str(out, text.as_str());
    }
};

@generic<T: core::Format>
std.string::string labelled(str label, const T* value) throws std.alloc::alloc_error {
    return f"{label}={value:8}";
}

std.string::string report() throws std.alloc::alloc_error {
    Celsius today = Celsius {.degrees = 21.5};
    Shape[2] shapes = {Shape::circle(1.5), Shape::rect {.w = 2, .h = 3}};
    // "today=  21.5 C [circle(1.5), rect { w: 2, h: 3 }]"
    std.string::string first = labelled("today", &today);
    return f"{first} {shapes}";
}
```

.Positive: инициализаторы полей и вариант по умолчанию

```r
module example.defaults;

enum Level { quiet, @default normal, verbose };

struct Retry {
    u32 attempts = 3;
    std.time::duration delay = std.time::duration_from_seconds(1i64);
    Level level;
};

u32 total(u32 extra) {
    Retry standard = Retry {};              // 3 attempts, one second, normal
    Retry patient = Retry {.attempts = 10}; // the other fields keep their initializers
    Retry previous = core::take(&patient);  // patient becomes Retry {} again
    return standard.attempts + previous.attempts + patient.attempts + extra;
}
```

.Negative: unsafe operation outside boundary

```r
i32 rejected_raw_deref(raw i32* p) {
    return *p; // R-DIAG-UNSAFE-001
}
```

.Positive: dynamically linked C library with verified header

```r
module example.zlib;

@link(name = "zlib", kind = "dynamic")
@header("zlib.h")
extern "C" {
    @link_name("zlibVersion")
    @safety("ZLIB-VERSION",
            "zlib is loaded; result is a non-null NUL-terminated static string")
    raw const c_char* zlib_version();

    @link_name("compressBound")
    @safety("ZLIB-COMPRESS-BOUND", "source_len is accepted by selected zlib ABI")
    c_ulong zlib_compress_bound(c_ulong source_len);
}

raw const c_char* read_zlib_version() {
    unsafe {
        raw const c_char* version = zlib_version();
        return version;
    }
}
```

.Positive: opaque C handle, output pointer and verified C constant

```r
@link(name = "sqlite3", kind = "dynamic")
@header("sqlite3.h")
extern "C" {
    @c_type(name = "sqlite3", kind = "typedef")
    opaque struct SQLite;

    @c_constant(name = "SQLITE_OK")
    const c_int SQLITE_OK = 0i32 as c_int;

    @link_name("sqlite3_open")
    @safety("SQLITE-OPEN",
            "filename is a live NUL-terminated UTF-8 string; out_db is writable")
    c_int sqlite_open(raw const c_char* filename,
                      raw (raw SQLite*?)* out_db);

    @link_name("sqlite3_close")
    @safety("SQLITE-CLOSE", "db is a unique live sqlite3 handle and is consumed")
    c_int sqlite_close(raw SQLite* db);
}
```

.Positive: exported callback has an exact raw function-pointer type

```r
@callback
@export_name("r_compare")
@safety("EXAMPLE-COMPARE-CALLBACK",
        "runtime is initialized; trampoline may attach the calling thread; left and right satisfy comparator userdata contract")
extern "C" c_int compare(raw const void* left, raw const void* right) {
    // A real comparator validates and dereferences pointers only inside unsafe.
    return 0i32 as c_int;
}

void prepare_callback() {
    raw fn(raw const void*, raw const void*) -> c_int callback = compare;
    callback as void;
}
```

.Negative: source embeds a host-specific library path

```r
@link(name = "/usr/local/lib/libz.dylib", kind = "dynamic") // R-DIAG-LINK-001
@header("zlib.h")
extern "C" { }
```

.Negative: non-empty block has no ABI evidence

```r
@link(name = "zlib", kind = "dynamic")
extern "C" {
    @safety("ZLIB-VERSION", "result is a static C string")
    raw const c_char* zlibVersion(); // R-DIAG-FFI-006
}
```

<a id="R-CONF-G006"></a>

**R-CONF-G006** — Examples illustrate rules but do not override grammar or normative paragraphs. Conformance repository shall expand each Annex B code into isolated positive/negative test pairs and shall record expected rule IDs. C FFI matrix shall cover the mandatory static-library, global, verification and callback cases of R-CONF-G009; dynamic/framework/system linking, TLS and C-origin threads follow the conditional target-capability matrix stated there. Core syntax/type fixtures shall cover bare и qualified aggregate type names, resolution constructor context, включая colliding ordinary name и каждый inadmissible braced head, direct-braced aggregate allocation, parenthesized expression allocation, declaration-wins parsing, default export и rejection доступа к protected declarations, contextual representable и out-of-range integer literals в object/payload initialization, simple assignment, by-value arguments, ordinary return и checked throw paths, а также default literal typing вне этих contexts и identity каждой standardized atomic shorthand с её expanded spelling. Они shall доказывать, что `bytes` и `array<u8>` имеют единые canonical type identity, layout, capability set и interface fingerprint; принимать любое spelling там, где expected другое; принимать allocation-free empty initializer через оба spellings; отклонять declaration либо shadowing в aggregate type-name space; и по-прежнему принимать `bytes` как ordinary value, function, module, variant либо member name. Fixtures direct call shall передавать каждый source, допустимый R-EXPR-0027, в один exact parameter `const u8[]`, доказывать byte-for-byte zero-copy observation и borrow provenance первоначального source, а также отклонять ту же contextual conversion owner/fixed-array в contexts initialization, assignment, return и conditional common-type. Они shall также отклонять non-u8 sequence и любой move source либо structural mutation, перекрывающиеся derived shared borrow. Они также shall покрывать literal typing `constexpr str`, runtime selection, aggregate storage, passing, return и weakening to ordinary `str`; rejection promotion из ordinary string и `constexpr` для любого другого type; а также только lowercase spellings type и variants `o`. Conditional-expression fixtures shall отклонять conditional expression непосредственно и на глубине в каждой форме аргумента вызова и операнде return из R-EXPR-0014 и принимать его в object initializer и assignment. Explicit-generic fixtures shall закрывать result-only, константные параметры и параметры `errors` через `name::<...>` в простых, квалифицированных, owner-prefixed вызовах, вызовах через receiver и в function items, выбирать один член перегрузки, образовывать function items методов с receiver первым параметром и отклонять списки неверной длины или формы, списки после не-generic имён и вызовы с невыведенным параметром. Fixtures кортежей и пакетов shall строить, читать, разбирать шаблоном и раскрывать spread кортежи Copy- и Move-элементов, инстанцировать функцию с пакетом для нескольких длин, включая ноль, выполнять рекурсию по пакету под `@if (len(T...) != 0usize)`, передавать дальше и расширять пакет, вызывать callable с пакетом в конце сигнатуры и уничтожать каждую часть spread ровно один раз, в том числе после отклонённого async-старта. Translation-time fixtures shall вычислять таблицу, размер для типа уровня модуля и значение enumerator, заменять вызов с константными аргументами его значением, отклонять в обязательном контексте паникующий вызов, вызов сверх лимитов трансляции и вызов run-time функции с `R-DIAG-CONST-003`, `R-DIAG-LIMIT-001` и `R-DIAG-CONST-002`, а паникующий вызов вне такого контекста выполнять во время выполнения. Они также shall бросать, ловить и повторно бросать при трансляции checked errors constant values и family ошибки и отклонять ошибку, покидающую обязательную константу, с `R-DIAG-CONST-003`, вычислять значения binary32 и binary64 бит в бит как во время выполнения, строить и читать arrays, lists, dictionaries, strings, options и tagged values и замораживать owners в `const`-объекты модуля и static-объекты, которые программа читает без allocation. Static-condition fixtures shall выбирать ветвь оператора и объявления модуля по константным условиям, включая условие модуля над объявлением, выбранным другим условием, решать условие над константным generic-параметром для каждой инстанциации и отклонять неконстантное или паникующее условие. Computed-formula fixtures shall закрывать generic-поле, сигнатуру, локальную переменную и константный аргумент с вызовом функции над параметрами для каждого инстанса, включая инстанс, закрытый на уровне модуля, и отклонять формулу, которая паникует, невычислима для инстанса или измеряет свой собственный инстанс. Checked-error fixtures shall покрывать exact nominal error types, canonicalized error sets, `throw`, rethrow, multiple typed catches и `finally`. Async fixtures shall покрывать declaration marker, logical return type, immediate effect `std.async::start_error`, value `task<T throws E...>`, exact closed paths `std.async`, композицию await в инициализаторах, return, аргументах, условиях и выражениях циклов, а также отклонение await в синхронных функциях, статических инициализаторах и телах finally.

### G.5 Canonical syntax and paired examples

| Decision | Accepted R 0.1 | Rejected form / diagnostic |
|----|----|----|
| Aggregate type spelling | `struct S { i32 x; }; S value = { .x = 0 };` | `struct S value = { .x = 0 };` / `R-DIAG-SYN-001` |
| No typedef declaration | `S value = { .x = 0 };` после definition S | `typedef i32 Id;` / `R-DIAG-SYN-001` |
| Type-name-aware declaration | `S* value = source;`, когда S разрешается в aggregate type | Та же form, когда S не является type / `R-DIAG-NAME-001` |
| Namespace braced constructor | `example.api::S value = example.api::S { .x = 0 };` | Braces после ordinary function name / `R-DIAG-TYPE-001` |
| Direct-braced aggregate allocation | `own S* value = new S{ .x = 0 };` | `new S({ .x = 0 })` / `R-DIAG-SYN-001` |
| Explicit cast uses `as` | `u32 y = x as u32;` | `u32 y = (u32)x;` / `R-DIAG-SYN-001` |
| Результат compound assignment малого integer | `u8 x = 255; x += 1;` даёт ноль | `u8 y = x + 1;` без `as u8` / `R-DIAG-TYPE-001` |
| Raw arithmetic requires unsafe | `unsafe { raw i32* q = p + 1usize; }` | Same initializer outside unsafe / `R-DIAG-UNSAFE-001` |
| Module composition | `import platform.api;` | `#include "api.h"` / `R-DIAG-LEX-004` |
| Default export and protected helper | `i32 api(); protected i32 helper();` | Access к `helper` из другого module / `R-DIAG-NAME-001` |
| C variadic import | Verified custom C declaration `c_int log_values(raw const c_char* format, ...);` with `@safety` | R definition with `...` / `R-DIAG-SYN-001` |
| Payload alternatives | `enum E { I(i32), F(f32) };` | `union U { i32 i; f32 f; };` / `R-DIAG-SYN-001` |
| Braced branch body | `if (a == 1) { b += 1; }` | `if (a == 1) b += 1;` / `R-DIAG-SYN-001` |
| Explicit condition | `if (a == 1) { }` | `if (a) { }` / `R-DIAG-SYN-001` |
| Tuple and type pack (R-TYPE-0052, R-TYPE-0053) | `(i32, bool) t = (1, true); i32 n = t.0;` and `@generic<T...> usize count(T... values)` | `T values` for a pack `T` / `R-DIAG-TYPE-001` |
| Method chain (R-FUNC-0024) | `@chain Options Options::with_port(Options this, u16 value)` and `Options::create().with_port(80u16).with_capacity(4usize)` | a suffix after a method without `@chain` / `R-DIAG-SYN-001` |
| Explicit switch transfer | `switch (x) { case 0: fallthrough; default: break; }` | Clause достигает следующего label / `R-DIAG-SYN-001` |
| Вложенный вызов в аргументе (R-EXPR-0020) | `outer(inner());` либо `i32 value = inner(); outer(value);` при совместимых типах | Результат `inner()` несовместим с типом параметра `outer` / `R-DIAG-TYPE-001` |
| Вызов в return (R-EXPR-0020, R-STMT-0005) | `return compute();` либо `i32 value = compute(); return value;` при совместимых типах | Результат `compute()` несовместим с типом результата содержащей вызов функции / `R-DIAG-TYPE-001` |
| Eager asynchronous start | `task<u32> started = compute_async();` в function с `throws std.async::start_error`, для `async u32 compute_async()` | Использование call result как `u32` / `R-DIAG-TYPE-001` |
| Explicit task observation | `u32 value = await move pending;` внутри async function | `return await move pending;` либо `consume(await move pending);` / `R-DIAG-SYN-001` |
| Void task observation | `await move pending;` для `task<void>` | Standalone await `task<u32>` / `R-DIAG-ASYNC-001` |
| Direct-call task observation | `u32 value = await compute_async();` and `await write_output(move data);` inside async functions with handled call and completion errors | `await pending;` or `await move compute_async();` / `R-DIAG-SYN-001` |
| Asynchronous entry point | `async i32 main()` в `hosted-native-async` | Async main в profile без его runtime / `R-DIAG-PROFILE-001` |
| Checked-error declaration | `u32 f() throws Error { return 1; }` | `u32 f() throws i32 { return 1; }` / `R-DIAG-TYPE-001` |
| Direct checked throw | `u32 f() throws Error { throw { .code = 1 }; }`, когда Error — unique matching error type | Braced operand, matching несколько либо ни одного permitted error type / `R-DIAG-TYPE-001` |
| Typed handling and cleanup | `try { work(); } catch (Error error) { handle(move error); } finally { cleanup(); }` | Duplicate catch type либо `await`/outward transfer в finally / `R-DIAG-FLOW-001` |
| Конец function без payload | `void finish() { work(); }` не требует final `return;` | Достижение конца `i32` / `R-DIAG-FLOW-001` |
| Именованная развилка перед return | `i32 status = (count == 0) ? 1 : 2; return status;` | `return (count == 0) ? 1 : 2;` либо `f((count == 0) ? 1 : 2);` / `R-DIAG-FLOW-001` |
| Явные generic-аргументы | `array<i32> values = make::<i32>(); auto read = Point::get;` | `make()`, когда `T` есть только в результате, либо `make::<i32, i32>()` / `R-DIAG-TYPE-001` |
| Nullable marked `?` | `const i32*? p = null;` | `const i32* p = null;` / `R-DIAG-TYPE-001` |
| Named move explicit | `own u8* b = move a;` | `own u8* b = a;` / `R-DIAG-MOVE-001` |
| Atomic shared owner | `arc S a = new arc S { .x = 0 };` | `arc S a = null;` / `R-DIAG-TYPE-001` |
| Fast local shared owner | `rc i32 a = new rc i32(1);` | Transfer of `a` to unscoped thread / `R-DIAG-MEM-001` |
| Reference clone explicit | `arc S b = std.arc::clone(&a);` | `arc S b = a;` / `R-DIAG-MOVE-001` |
| Weak owner explicit | `weak arc S w = std.arc::downgrade(&a);` | Dereference of `w` / `R-DIAG-TYPE-001` |
| Scoped borrowed thread | `thread_scope { std.thread::scoped_join_handle<void> h = std.thread::spawn_scoped(worker, &value); }` в function с `throws std.thread::thread_error` | Scoped handle or borrow escaping block / `R-DIAG-MEM-001` |
| Atomic shorthand | `au32 flags = 0;` | Arithmetic directly on flags / `R-DIAG-TYPE-001` |
| Contextual array and byte literals | `u8[3] bytes = { 0xcb, 0x48, 0xcd };` | `u8[1] bytes = { 0x100 };` / `R-DIAG-CONST-001` |
| Complete array type до name | `Entry[MAX_ENTRIES] entries;` | C split declarator `Entry entries[MAX_ENTRIES];` / `R-DIAG-SYN-001` |
| Contextual integer literals | `u8 value = 1; value = 2; if (value < 0x80) { take_u8(3); }` | Out-of-range literal для required destination или case type / `R-DIAG-CONST-001` |
| Zero parameters | `Status initial_status()` | Native R `Status initial_status(void)` / `R-DIAG-SYN-001` |
| Строка из program image | `constexpr str text = count == 0 ? "yes" : "no";` | Conversion ordinary `str` в `constexpr str` / `R-DIAG-TYPE-001` |
| Lowercase tagged type constructors | `o<i32> value = o::some(1);` | `Option<i32> value = o::some(1);` / `R-DIAG-SYN-001` |
| Lowercase built-in variants | `o<i32> value = o::some(1);` | `o<i32> value = o::Some<1>;` / `R-DIAG-NAME-001` |
| Dynamic array | `array<i32> values = std.array::create::<i32>();` | Copy `values` без `move` / `R-DIAG-MOVE-001` |
| Owned byte alias | `bytes data = {};` является в точности `array<u8> data = {};`; отдельные ordinary, module, variant и field name spaces также могут использовать это spelling | Declaration aggregate type `struct bytes { };` / `R-DIAG-NAME-003` |
| Contextual read-only byte view | Exact parameter `const u8[]` напрямую принимает `str`, `constexpr str`, `bytes`, `array<u8>`, `u8[N]`, `u8[]` либо `const u8[]` без copy | Использование этой contextual owner/array conversion для variable initializer либо передача `array<u16>` / `R-DIAG-TYPE-001` |
| Stable-node list | `list<i32> values = std.list::create::<i32>();` | Copy `values` без `move` / `R-DIAG-MOVE-001` |
| Associated dictionary key contract | `u64 Key::hash(const Key* value); bool Key::equal(const Key* left, const Key* right);` с matching definitions | Только одна operation либо mismatched signature / `R-DIAG-TYPE-001` |
| Linear-probing dictionary | `dict<Key,u32> counts = std.dict::create::<Key, u32>();` после key contract | Nominal key без key contract / `R-DIAG-TYPE-001` |
| Inferred returned-borrow origin | `const i32* id(const i32* x) { return x; }` | Source-only borrowed-output declaration без definition/interface metadata / `R-DIAG-BORROW-002` |
| C library is logical | `@link(name = "z", kind = "dynamic")` | Absolute/path-like library name / `R-DIAG-LINK-001` |
| C ABI is verified | `@link(name = "api", kind = "static") @header("api.h") @abi("api.abi") extern "C" { @safety("API-VERSION", "no preconditions") c_int api_version(); }` | Non-empty unverified block / `R-DIAG-FFI-006` |
| Opaque C type is pointer-only | `@link(name = "api", kind = "static") @header("api.h") @abi("api.abi") extern "C" { @c_type(name = "Handle", kind = "struct") opaque struct Handle; @safety("HANDLE-OPEN", "returns null or a live handle") raw Handle*? open_handle(); }` | Opaque `Handle` by value / `R-DIAG-FFI-003` |
| Callback type is exact | `raw fn?(c_int) -> c_int callback = null;` | Mismatched callback prototype / `R-DIAG-FFI-005` |

<a id="R-CONF-G007"></a>

**R-CONF-G007** — Each rejected form in this table shall be represented by a negative conformance test with the listed primary diagnostic; each accepted form shall have a positive parse/type counterpart.

### G.6 Release criteria

<a id="R-CONF-G008"></a>

**R-CONF-G008** — R 0.1 implementation may claim complete only when every normative rule is implemented or explicitly classified non-automatable, no known safe test reaches C UB, generated C is reproducible, and exported ABI manifests compare equal across repeated builds on same target.

<a id="R-CONF-G009"></a>

**R-CONF-G009** — Implementation claiming external C library support shall test a real minimal C17 static library: function, global, opaque handle, complete repr© struct with hidden-member-in-padding mismatch test, C integer constant plus same-value/wrong-type constant rejection, valid promoted variadic call plus unpromoted/variadic-definition rejection, retained userdata, portable null-for-non-null and invalid-enum ingress, omitted extra C enumerator, unused but representable enum value, missing artifact or typed symbol, header/ABI-record disagreement, wrong-provider evidence root, record target/options/artifact mismatch, two-provider same-spelling collision and cross-target host-fallback rejection. Mutable save/canonicalize/restore R-IDB-020 modes test C observation, change and exact restoration of rounding mode, exception flags and applicable extra state in outbound calls and C-origin entries. Proven immutable-canonical mode tests that attempted foreign change has no effect and every observation remains canonical. Unavailable contract-only mode instead tests acceptance of an `@fenv("preserve")` direct import and rejection of an unmarked/fenv-sensitive import, raw function-pointer call or exported `extern "C"` definition without executing a violated contract. Every implementation supporting C-origin entry additionally tests synchronous nested same-thread re-entry while already attached. Dynamic/framework/system, runtime-load failure, TLS and entry from a newly C-created thread run only for supported target capabilities. An unsupported link kind is a negative test when the target has one; absent optional `c_wint` is a negative test when unavailable, and present `c_wint` receives a positive manifest/range test. All C compiles use warnings as errors; sanitizer/substitute/non-applicability follows R-CMAP-0015.

<a id="R-CONF-G010"></a>

**R-CONF-G010** — Английский и русский файлы одной редакции shall иметь identical последовательности anchors, multisets нормативных rule ID и source/EBNF blocks. Английский файл является нормативным. Release shall обозначать русский файл как перевод; любое расхождение разрешается в пользу английского текста.

<a id="R-CONF-G011"></a>

**R-CONF-G011** — Каждая implementation с allocation/new shall test managed ownership независимо от thread support: move без count change; explicit strong/weak clone; exactly-once T drop и block lifetime до last weak; allocator balance после successful/failing `try_unwrap`; remaining weak handles после unwrap; uniqueness success/failure и non-observable sentinel; injected strong/explicit-weak overflow до mutation; balanced same-thread `into_raw`/`from_raw` round trips, включая два equal pointer values с двумя distinct obligations. При unwind strategy она shall дополнительно verify allocator balance после initializer и first drop panic. При abort strategy subprocess tests вместо этого verify process abort и отсутствие subsequent R drops; post-panic allocator balance не требуется. Defined strong-cycle leak и weak-backedge reclamation shall test, когда profile предоставляет standardized safe interior-mutation facility, способную построить эти graphs; иначе suite records эти два cases как not applicable для profile.

Каждый allocation-supporting profile shall дополнительно test contiguous order `array<T>`, fallible geometric growth, returned Move payloads и reverse-order destruction. Он shall доказывать, что last use каждого array element либо slice borrow предшествует structural mutation, включая reallocation и removal со shift later elements. Dictionary tests shall покрывать predefined и nominal key contracts; exact signatures и closed effects `K::hash`/`K::equal`; equal-key hash consistency; collisions, linear probing, tombstones, load-factor boundary, rehash failure и insertion order; recovery обоих staged Move inputs после failed insertion. List tests shall сохранять node borrows через insertion и removal proven-distinct node, проверять неизменность addresses, требовать exclusive borrow removed node, enforce iterator/mutation conflicts и verify back-to-front destruction и allocation balance.

Каждая `hosted-native-async` implementation shall inject обе task-start failures и доказать, что ни одна body instruction не выполняется, ни один named Move operand не меняет state, а каждый provisional runtime retain balanced. Negative cases shall stage `move value`, затем в последующем argument пытаться читать, писать, borrow, повторить `move value` либо обращаться к overlapping subobject; каждый case shall требовать `R-DIAG-BORROW-001`. При unwind panic во время evaluation последующего disjoint argument и каждая injected start failure shall освобождать reservation и оставлять original named source немедленно usable; subprocess abort strategy shall вместо этого terminate по R-ERR-0005 без assertion cleanup. Отдельный negative case shall отклонять nested `Payload { .owner = move value }` с `R-DIAG-MOVE-003`, принимая отдельно initialized named Payload, переданный как complete direct Move argument. Successful start shall commit каждый by-value Move argument один раз, copy либо retain каждый другой argument как требует его signature, стать independently runnable eager и publish все inputs до body access. Tests shall проверять сочетания named consuming await и direct-call await по R-STMT-0012 и контролируемые заимствования scoped-задач по R-STMT-0017; отклонять borrowed parameters и borrow/slice/`str` values, live через suspension в обычных async-функциях; verify Send derivation `task<T throws E...>`, включая каждый E, и unconditional non-Sync; observe release/acquire completion edge. По R-STMT-0018 tests shall проверять гонку таймера с native I/O и с вычислением, select среди одновременно завершённых участников, наблюдение одной из нескольких ошибок детей при однократном уничтожении остальных и отложенное подтверждение отмены с единственным cleanup. По R-STMT-0017 tests shall показывать, что отсоединённый участник освобождает слот, когда он завершён, что выход из группы отменяет и дожидается отсоединённого участника, который ещё выполняется, и что его исход уничтожается один раз. По R-STMT-0019 tests shall показывать, что задача, запущенная внутри блока дедлайна, участник группы и стандартная операция, вызванная без аргумента дедлайна, сообщают `timed_out`, когда дедлайн блока прошёл, что явный более поздний дедлайн и вложенный более поздний блок его не продлевают, что задача, запущенная до блока, и ожидания без дедлайна им не ограничиваются и что любой выход из блока восстанавливает прежний дедлайн. Cancel, ordinary drop, detach, deadline и completion races shall достигать одного terminal outcome, retain все frame/native resources до cancellation acknowledgement и выполнять exactly-once result/frame cleanup. При unwind observed panic shall re-raise в await, а unobserved panic shall достигать configured handler. Async-main termination shall cancel и drain каждую remaining task до module destruction. Injected root executor/frame reservation failure shall выполнять zero main-body instructions, report exact start-error category, выбирать documented nonzero status и не выполнять R static/module destruction. Runtime tracing shall демонстрировать, что potentially blocking I/O не выполняется как blocking C call на executor worker. Каждая hosted implementation shall inject startup-snapshot allocation failure и передать один native argument вне representable domain R-IDB-022 для каждой из `main()` и `main(const str[] args)`; каждая `hosted-native-async` implementation shall повторить эти cases для `async i32 main()` и `async i32 main(const str[] args)`. Cases shall доказать соответственно, что `allocation_failure` либо `argument_encoding_failure` сообщается до любой R static initialization или main-body instruction, ни один invalid `str` не materialized, а используется distinct documented nonzero status. Fixture с zero native arguments и argument-taking main shall наблюдать documented synthesized element zero и тот же byte-identical nonempty snapshot через main args и `std.env::arguments()`. Combined fixture invalid argument/allocation failure shall сообщать `argument_encoding_failure`; combined provider/argument failure shall сообщать `link_load_failure`. Asynchronous fixture с valid snapshot shall отдельно inject root executor/frame start failure и сообщать его start-error category, доказывая, что он достиг последующей phase.

Каждый profile shall exercise `core::validate_utf8` на valid и invalid instances каждого RFC 3629 sequence class, verify exact index первого invalid byte, zero allocator calls и отсутствие source mutation при обоих outcomes, а также prove, что successful view из mutable storage запрещает mutation до last use возвращённого `str`.

Каждый profile shall type-check семь universal signatures R-UNSAFE-0008 и требовать `R-DIAG-UNSAFE-001` для каждой operation, а также для representation transmute `source as D`, вне unsafe context. Valid raw-parts fixtures shall создавать shared и mutable slices nonempty live allocation, а также empty slice из null pointer; verify exact length и alias effect; доказывать отсутствие element copy и allocation; и сохранять backing storage live для всего fresh inferred result region. Anchored fixtures shall возвращать views из methods anchoring object, хранить один из них в struct, anchor к slice и к `str` и требовать diagnostics для moved, dropped либо assigned anchor, а также для read anchor mutable form, пока view live, для anchor, не являющегося place или borrow place, и для anchor без требуемого permission. Volatile fixtures над instrumented compatible object shall наблюдать ровно один load и один store T и отсутствие atomic либо synchronizes-with edge; cases с Move, borrow-bearing, string-bearing и atomic T shall требовать `R-DIAG-TYPE-003`. Valid same-size Copy fixture `source as D` shall сохранять source и создавать exact valid target representation; unequal size, excluded component и direct либо recursive target `constexpr str` shall требовать `R-DIAG-TYPE-003`. `core::assume(true)` shall type-check и не вводить observable runtime effect. Null nonempty raw parts, invalid transmute bits и `core::assume(false)` являются unsafe- contract violations: suite shall inspect их published contract metadata и shall not выполнять их как required conformance tests.

Каждый allocation-supporting profile shall дополнительно type-check обе adoption/release signatures, отклонять их use вне unsafe context и выполнять fixture compatible single-T allocation, который adopts её exact base pointer и proves один ordinary owner drop/deallocation. Второй fixture shall adopt, затем выполнить `core::release(move owner)`, prove identity возвращённого base pointer, отсутствие R drop или deallocation при release и exactly-once выполнение каждой transferred duty external fixture; named owner shall быть moved и unusable. Duplicate либо non-base adoption и incorrect released-pointer duty являются unsafe-contract violations: suite shall inspect их published contract metadata и shall not выполнять их как required conformance tests.

Каждый profile shall test, что `void` function может достигать closing brace, включая async logical-return form, тогда как reachable end value-returning function требует `R-DIAG-FLOW-001`. Он проверяет вложенные вызовы и await в операндах return, порядок вычисления и cleanup при checked-ошибке и отмене.

Checked-error fixtures shall принимать exact named complete объявления `error` в форме структуры и перечисления, включая явно зарегистрированные standard error types и permitted closed nominal instantiations; отклонять обычные struct/enum types в throws, throw, catch и наборах ошибок task/thread с `R-DIAG-EFFECT-002`; проверять контекстные идентификаторы `error`, forward references, imports, обе грамматики членов и запрет смешивания форм; отклонять void, scalar, pointer, borrow, slice, runtime `str`, anonymous structural, incomplete и uninhabited candidates; отклонять duplicate error type после canonical qualification; и доказывать, что разные source orders образуют один compatible checked-error set и interface fingerprint. Они shall принимать named Copy и Move operands `throw`, требуя `move` для последнего, и direct in-place braced construction только когда outgoing checked-error set содержит ровно один error type и этот единственный type является complete struct. Они shall отклонять implicit error conversion и narrowing. Effect call, `await` либо `join` shall быть caught enclosing exact typed catch либо входить в throws set enclosing declaration; unchecked/undeclared propagation требует `R-DIAG-FLOW-001`. Использование `try` как expression prefix вместо начала try statement shall требовать syntax diagnostic.

Try-statement fixtures shall покрывать multiple distinct catches, exact type selection, source-order-independent matching, unmatched error и error, thrown catch, bypassing его sibling catches. Они shall покрывать `throw;` из catch, rejection вне catch и после move/drop catch binding, а также selection только nested либо outer try. Finally fixtures shall выполнять exactly once после normal completion, handled/unmatched error, rethrow, return, break и continue, inner-to-outer при nesting. При unwind strategy они shall дополнительно доказывать exactly-once выполнение finally во время panic unwind. При abort strategy isolated subprocess shall вместо этого доказывать, что first panic aborts без выполнения любого ещё не начатого finally. Они shall отклонять duplicate catch types; outward return, throw, rethrow, break или continue из finally; каждый residual checked effect; и каждый `await` внутри finally. Они shall доказывать staged return/error ownership и exactly-once payload/local cleanup, включая panic самого cleanup при selected panic strategy. Они shall проверять, что `expression as void` следует R-EXPR-0023: операнд вычисляется один раз; Copy-операнд сохраняет источник пригодным для использования; Move-операнд потребляется с однократным уничтожением, включая user drop и вложенный cleanup. Они shall отклонять именованный Move-операнд без `move` и использование потреблённого источника до повторной инициализации. Effects операнда и drop, panic, checked errors и обязательства по разрешению task shall сохраняться. Separate translation shall принимать declaration каждого `ai8`, `ai16`, `ai32`, `ai64`, `aisize`, `au8`, `au16`, `au32`, `au64` и `ausize`, matched definition с expanded atomic spelling, а хотя бы один fixture shall вкладывать matched shorthand/expanded spellings в pointer, fixed array и raw-function signature и проверять единый canonical interface fingerprint. Suite shall отклонять direct arithmetic, atomic-suffix literals, nested `atomic ai32` и использование C `_Atomic` ABI. Для каждого permitted `atomic i8`, `atomic u8`, `atomic i16` и `atomic u16` fetch fixtures shall проверять возвращённый прежний T, unsigned modulo final conversion и signed overflow до modification. Cross-module fixtures shall доказывать default export, отклонять доступ к protected declaration и mismatched visibility prototype/definition, принимать bare self-referential и qualified aggregate type names и отклонять прежние type-use spellings `struct Name`/ `enum Name`. Suite shall типизировать каждый string literal как `constexpr str`; принимать runtime conditional selection между literals, а также storage, copy, argument passing и return через runtime aggregate; и принимать implicit weakening к ordinary `str` с inferred source region. Она shall отклонять conversion ordinary, allocated или foreign-backed strings в `constexpr str` и `constexpr` для любого другого type. Она shall принимать `const constexpr str` как immutable descriptor object, безопасно читать module или block-static object этого type, принимать reassignment non-`const` local descriptor между literal-derived values и отклонять reassignment `const` descriptor. Она shall отклонять direct address-of string-literal descriptor с `R-DIAG-BORROW-002`, одновременно принимая shared borrow in-bounds indexed literal byte, включая conditional либо returned descriptor temporary. Она shall отклонять exclusive borrow или write этого byte с `R-DIAG-BORROW-001`, string range subscript с `R-DIAG-TYPE-001` и chained literal-to-byte-slice conversion с тем же code, принимая две named staging declarations, required R-EXPR-0015, и direct contextual literal argument, допустимый R-EXPR-0027. Она shall отклонять conditional common type, который требует ту же two-edge conversion, принимать literal argument для by-value parameter `const constexpr str` и отклонять outermost object `const` function return type с `R-DIAG-TYPE-001`. Она shall принимать `const str` для parameter или local descriptor с сохранением compiler-inferred origin, проверять снятие только outermost object `const` при Copy member access из aggregate value и то же снятие для Copy-place arms conditional expression. Она shall проверять, что member place сохраняет effective `const` своего field либо aggregate path, отклоняя write или exclusive borrow с `R-DIAG-BORROW-001` и принимая соответствующие operations mutable place. Она shall также construct и extract Copy payload `o`, component которого имеет outermost object `const`, producing unqualified value, и отклонять move outermost-`const` Move payload с `R-DIAG-TYPE-001`. Она shall принимать только lowercase built-in spellings `o<T>`, `o::some` и `o::none` и отклонять их прежние uppercase spellings. Empty `constexpr str` shall использовать program-lifetime sentinel, required R-TYPE-0028. Каждый nesting depth C signature, включая pointee raw pointer, shall отклонять оба R string descriptor type. Direct либо recursively contained transmute targets `constexpr str` shall отклоняться с `R-DIAG-TYPE-003`.

Hosted-thread profile дополнительно shall inject thread-creation failure и доказать, что entry не вызывается, каждый именованный Move operand остаётся initialized и неизменным, а каждый staged temporary уничтожается ровно один раз. Spawn argument fixtures shall повторить staged `move value` overlap rejections, required выше, и shall доказать освобождение reservation после later-argument panic при unwind и после creation failure; subprocess abort strategy shall verify termination без assertion cleanup. Они также shall повторить rejection nested `Payload { .owner = move value }` и acceptance отдельно initialized payload. Она также shall test safe `rc` transfer rejection, `arc` Send/Sync derivation, contended clone/drop с одним last drop, upgrade vs last release, unwrap vs upgrade, scoped-borrow escape rejection и join на каждом exit, local `lock_result<State>`, чей contained guard carries inferred region creating lock, rejection, когда этот guard escapes либо попадает в forbidden storage, и local `scoped_join_handle<R throws E...>`, carrying inferred thread-scope region, same-region scoped-handle transfer и nested handle return с supervisor revocation без double observation, completion всех scoped target waits до implicit outcome-drop panic, unwind `panicked` join result против abort-strategy process abort без join result, guard non-Send/poison recovery и channel FIFO, rendezvous, disconnect, returned unsent value, factory-drop и queued-value drop. Concurrent tests используют TSan либо substitute/limitation record R-CMAP-0015. При unwind strategy fixture shall сохранить `constexpr str`, возвращённый `panic_category`, уничтожить originating `panic_report`, а затем успешно наблюдать то же category value.

Implementation с managed-owner C adapters shall statically reject direct/nested managed signatures, `@repr(C)` fields и transmute. Её conformance fixture shall explicitly declare user-named exported `extern "C"` create, retain и release adapters для одного `arc` T; ни одно name не synthesized. Fixture shall execute balanced sequence: create даёт одну obligation, retain даёт вторую без consuming первой, а два release reclaim allocation. Если newly C-created-thread entry supported и R-IDB-020 не unavailable, один release shall выполняться на таком attached thread с Send+Sync T, attach до decrement и release всех obligations до shutdown. Если эта thread capability отсутствует, cross-thread subcase records not applicable без rejection test; если capability есть, но R-IDB-020 unavailable, suite выполняет boundary rejection test R-CONF-G009. Если claimed `rc` C adapters, corresponding balanced sequence shall выполняться целиком в одной live explicit attachment generation. Forged, repeated, wrong-family, wrong-T и wrong-thread/generation tokens являются unsafe-contract violations и shall not исполняться как required conformance tests; suite проверяет их published contract metadata и отдельно compile-rejects safe `rc` cross-thread transfer. Hardened adapter may дополнительно test documented rejection behavior.

<a id="R-CONF-G012"></a>

**R-CONF-G012** — Target, заявляющий `hosted-native-async` с filesystem adapter lane, shall доказать runtime trace, что lane имеет ровно четыре threads, отделён от workers executor, допускает только manifest operations, разрешённые R-CMAP-0039, и никогда не исполняет R code либо payload I/O. Conformance tests shall отменять либо завершать по deadline operation в queue и внутри каждого разрешённого семейства native calls, проверять, что queued work не входит в native call, и проверять acknowledgement, retention и exactly-once cleanup для begun work. Tests namespace и durability shall покрывать cancellation до и после документированной commit point и наблюдать committed success после late cancellation. Saturation lane не должно препятствовать progress ready R continuations, console, network, DNS, timer либо child-process completions. Target, предоставляющий пул блокирующих вызовов R-TERM-0015, shall так же доказать, что пул никогда не запускает больше threads, чем записано в его manifest, отделён от workers executor и от lane, никогда не входит в call, отменённый в queue, и подтверждает начатый call только после его return; saturation пула не должно препятствовать progress ready R continuations либо native completions. Target, предоставляющий адаптер файловых передач R-TERM-0016, shall доказать, что в нём никогда не начато больше transfers, чем записано в его manifest, что он исполняет только payload transfer обычных файлов через entry points manifest и никогда не на worker executor, никогда не входит в transfer, отменённый либо истёкший до admission, и подтверждает начатый transfer только после return его call; saturation адаптера не должно препятствовать progress ready R continuations либо native completions.

[appendix]

<a id="annex-h"></a>

## Annex H — Portability recommendations (non-normative)

This annex is informative.

- Prefer fixed-width R integers for serialized/on-disk/network data and `c_*` types only at an FFI boundary.

- Do not persist default R aggregate representation; define an explicit byte encoding or a reviewed `@repr(C)` protocol.

- Treat signed/unsigned mixing as a review finding even though R defines it.

- Объявляйте recoverable allocation и I/O failures через `throws`; оставляйте `new` для code, policy которого — panic при allocation failure.

- Keep unsafe blocks minimal and place a comment citing an Annex E or project safety contract beside each unsafe operation.

- Avoid depending on NaN payload, allocation address, padding, thread scheduling, atomic lock-freedom or diagnostic prose.

- Prefer `rc T` для proven single-thread shared ownership и `arc T` только когда owner должен пересекать threads; ни одна form не синхронизирует mutation T.

- Use weak back edges для reference-counted graphs и test reclamation; strong cycles намеренно не collected.

- Expose C ownership with paired create/destroy functions and opaque raw handles; never expose default R layout as stable ABI.

- Keep physical library/header paths in per-target link manifest; use the same logical `@link` name across Linux, macOS and Windows and pin non-system digests.

- Treat header verification as ABI evidence, not source import. Commit generated R bindings and re-verify them whenever header, feature macro, compiler or sysroot changes.

- Package required dynamic library beside application according to manifest and test a clean-machine launch; successful link does not prove runtime loadability.

- Test generated C with multiple optimization levels and C17 compilers. A sanitizer pass complements but does not prove the abstract-machine mapping.

- Pin Unicode data, C compiler, re2c and formatter versions in reproducible builds.

- Сохраняйте каждую exported borrow-origin relation в interface metadata и включайте её в API compatibility review.

### H.1 Compatibility checklist

| Question | Portable answer | Reason |
|----|----|----|
| Does code depend on `usize == 64`? | No | R-IDB-001 permits 32 or 64 |
| Does code inspect struct bytes? | No | Default layout and padding vary |
| Does code depend on pointer order? | No | Safe R defines only equality |
| Can panic cross C? | No | Boundary aborts by definition |
| Can array be passed as pointer implicitly? | No | Use explicit borrow/slice/raw conversion |
| Can C `long` be represented by `i64`? | No assumption | Use `c_long` |
| Can a shared borrow outlive its owner? | No | Lifetime constraint |
| Can relaxed atomics be used for publication? | No | Use release/acquire or stronger |
| Can `rc T` be moved to another thread? | No | `rc` and `weak rc` are never Send or Sync |
| Does `arc T` make T’s fields thread-safe? | No | It protects lifetime only; use atomics, mutex or rw_lock |
| Does observing strong count one grant mutation? | No | Only linearizable `get_mut` grants an exclusive borrow |
| May a scoped handle escape `thread_scope`? | No | Its hidden inferred region is bound to the block and every child is joined |
| Can `@link` contain `.so/.dylib/.lib` path? | No | Logical name resolves through target manifest |
| Does `@header` import C names/macros into R? | No | It only drives ABI verification |
| Does successful static link prove dynamic deployment? | No | Clean startup must resolve manifest runtime identity |

### H.2 Index of normative rule families

| Prefix | Subject |
|----|----|
| `R-GEN`, `R-REF`, `R-TERM`, `R-CONF` | General, references, terminology, conformance |
| `R-AM` | Abstract machine |
| `R-LEX`, `R-GRAM` | Source, lexical elements, grammar |
| `R-NAME`, `R-TYPE`, `R-OBJ` | Names, types, objects |
| `R-INIT`, `R-OWN`, `R-BORROW` | Initialization, ownership, borrowing |
| `R-EXPR`, `R-STMT`, `R-FUNC` | Expressions, statements, functions |
| `R-AGG`, `R-ARRAY`, `R-ERR` | Aggregates, arrays/strings, error behavior |
| `R-UNSAFE`, `R-SAFETY` | Unsafe boundary and contracts |
| `R-MEM`, `R-MOD`, `R-FFI` | Concurrency, modules, C interoperability |
| `R-LIBREF`, `R-LIMIT` | Standard-library integration and limits |
| `R-REFL` | Static reflection |
| `R-DIAG`, `R-IDB`, `R-USB`, `R-CMAP` | Annex catalogs and C17 mapping |

*End of R Core Language Specification 0.1 draft.*
