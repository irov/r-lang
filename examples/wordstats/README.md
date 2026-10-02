# Word statistics over borrowed command-line words

Count the words given on the command line without copying them: the containers hold views of
the words themselves and carry their origins (Core R-BORROW-0018).

```sh
ctest --test-dir build-debug -R 'example_wordstats|wordstats_commands' --output-on-failure
build-debug/tests/codegen_example_wordstats alpha beta alpha gamma
```

`wordstats WORD...` prints
`words=N distinct=D top=W:C longest=L:S short=K sizes=S/M/L repeated=W:C,... ranking=W:C,...`:
the number of words and of distinct words, the first most frequent word with its count, the
first longest word with its length, the number of words shorter than four letters, the number
of words by size (shorter than four letters, four to seven letters, eight letters or more), the
words that occur more than once with their counts in first-seen order (`-` when none repeats)
and the three most frequent words, the first seen first among equal counts. A word that is not
made of ASCII letters exits with 65.

[words.r](src/words.r) takes the words as a `const str[]` and keeps one `Entry` per distinct
word in an `array<Entry>`, with a `dict<str, usize>` from a word to its entry. An entry holds a
view of a command-line word, so the array carries the origin of the words and may live exactly
as long as they do:

```r
struct Entry { str word; u32 count; };
array<Entry> entries = std.array::create::<Entry>();
dict<str, usize> positions = std.dict::create::<str, usize>();
```

A repeated word is counted through an exclusive borrow of its entry; the element borrow reaches
the storage of the array, while the word the entry holds stays a shared loan of the
command-line word (Core R-BORROW-0002, R-LIB-0019):

```r
switch (std.array::get_mut(&entries, position)) {
case variant o::some(entry): (*entry)->count += 1u32; break;
...
}
```

Word sizes are counted through an array of exclusive borrows of three counters. While the array
holds the borrows, the counters cannot be read; once it is cleared, they are read into the
report (Core R-BORROW-0002):

```r
array<u32*> sizes = std.array::create::<u32*>();
std.array::push(&sizes, &short_size);
...
switch (std.array::get_mut(&sizes, size)) {
case variant o::some(counter): ***counter += entry->count; break;
...
}
std.array::clear(&sizes);
```

The repeated words are kept as shared borrows of their entries in a `list<const Entry*>`
beyond the loop that found them (Core R-BORROW-0018):

```r
list<const Entry*> repeated = std.list::create::<const Entry*>();
for (const Entry* entry in &entries) {
    if (entry->count > 1u32) { std.list::push_back(&repeated, entry) as void; }
}
```

The ranking sorts the entries in place through a parameter. An entry copied out of the array
keeps its view of a command-line word while other entries are rewritten through the same
parameter: what a parameter holds or designates has a region of its own, apart from the storage
it designates (Core R-BORROW-0018, R-BORROW-0019):

```r
void sort_by_count(array<Entry>* entries) {
    ...
    case variant o::some(entry): moving = **entry; break;
    ...
    case variant o::some(target): **target = previous; break;
    ...
}
```

The length histogram is reached through a method whose result is an exclusive borrow of one
bucket, written directly through the temporary borrow (Core R-BORROW-0004):

```r
u32* Lengths::bucket(Lengths* this, usize size) { ... return &this->buckets[index]; }
*lengths.bucket(len(word)) += 1u32;
```

The position of a word is read through `o<const usize*>`, the optional borrow `std.dict::get`
returns, passed to an ordinary parameter (Core R-TYPE-0012). A broken invariant is reported by
a `never` function, which may stand where a value is expected (Core R-FUNC-0003, R-EXPR-0015):

```r
never impossible(str what) { panic(what); }
case variant o::none: return impossible("a counted word has no entry");
```
