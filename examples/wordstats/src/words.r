module example.wordstats.words;

/* A word that is not made of ASCII letters, by its position among the words. */
error NotAWord { usize position; };

/* Reports a broken invariant; a call never returns, so it may stand wherever a value is
   expected (Core R-FUNC-0003, R-EXPR-0015). */
never impossible(str what) {
    panic(what);
}

/* Whether a word is made of ASCII letters only. */
bool is_word(str word) {
    if (len(word) == 0usize) {
        return false;
    }
    for (usize index = 0usize; index < len(word); index += 1usize) {
        u8 letter = word[index];
        bool lower = (letter >= 97u8) && (letter <= 122u8);
        bool upper = (letter >= 65u8) && (letter <= 90u8);
        if ((lower == false) && (upper == false)) {
            return false;
        }
    }
    return true;
}

/* Word counts by length; lengths from 15 letters on share the last bucket. */
struct Lengths { u32[16] buckets; };

/* The bucket of a length, as an exclusive borrow of that bucket (Core R-BORROW-0004). */
u32* Lengths::bucket(Lengths* this, usize size) {
    usize index = size;
    if (index > 15usize) {
        index = 15usize;
    }
    return &this->buckets[index];
}

/* A distinct word and how often it occurs. An entry holds a view of a command-line word, so an
   array of entries carries the origin of the words (Core R-BORROW-0018). */
struct Entry { str word; u32 count; };

/* The position an optional borrow designates (Core R-TYPE-0012). */
usize position_of(o<const usize*> position) {
    switch (position) {
    case variant o::some(item):
        return **item;
    case variant o::none:
        return impossible("a counted word has no entry");
    }
}

/* Appends `word:count` for each repeated word, `-` when no word repeats. */
void append_repeated(std.string::string* output, const list<const Entry*>* repeated)
    throws std.alloc::alloc_error {
    if (len(*repeated) == 0usize) {
        output->append("-");
        return;
    }
    bool first = true;
    for (const (const Entry*)* item in repeated) {
        if (first == false) {
            output->append(",");
        }
        first = false;
        str word = (*item)->word;
        u32 count = (*item)->count;
        std.string::string piece = f"{word}:{count}";
        str piece_text = piece.as_str();
        output->append(piece_text);
    }
}

/* Sorts the entries by count, the most frequent first and in first-seen order among equal
   counts. An entry read out of the array keeps its view of a command-line word while other
   entries are rewritten through the same parameter: what the parameter designates has a region
   of its own (Core R-BORROW-0018, R-BORROW-0019). */
void sort_by_count(array<Entry>* entries) {
    for (usize index = 1usize; index < len(*entries); index += 1usize) {
        Entry moving = {.word = "", .count = 0u32};
        {
            const array<Entry>* shared = entries;
            switch (std.array::get(shared, index)) {
            case variant o::some(entry):
                moving = **entry;
                break;
            case variant o::none:
                impossible("an entry position is out of range");
                break;
            }
        }
        usize slot = index;
        while (slot > 0usize) {
            Entry previous = {.word = "", .count = 0u32};
            {
                const array<Entry>* shared = entries;
                switch (std.array::get(shared, slot - 1usize)) {
                case variant o::some(entry):
                    previous = **entry;
                    break;
                case variant o::none:
                    impossible("an entry position is out of range");
                    break;
                }
            }
            if (previous.count >= moving.count) {
                break;
            }
            switch (std.array::get_mut(entries, slot)) {
            case variant o::some(target):
                **target = previous;
                break;
            case variant o::none:
                impossible("an entry position is out of range");
                break;
            }
            slot -= 1usize;
        }
        switch (std.array::get_mut(entries, slot)) {
        case variant o::some(target):
            **target = moving;
            break;
        case variant o::none:
            impossible("an entry position is out of range");
            break;
        }
    }
}

/* Appends `word:count` for the first three sorted entries. */
void append_ranking(std.string::string* output, const array<Entry>* entries)
    throws std.alloc::alloc_error {
    usize shown = 0usize;
    for (const Entry* entry in entries) {
        if (shown == 3usize) {
            break;
        }
        if (shown != 0usize) {
            output->append(",");
        }
        str word = entry->word;
        u32 count = entry->count;
        std.string::string piece = f"{word}:{count}";
        str piece_text = piece.as_str();
        output->append(piece_text);
        shown += 1usize;
    }
}

/* Counts the words and formats the report. The containers hold views of the command-line
   words and carry their origins (Core R-BORROW-0018); they live only during this call. */
std.string::string summarize(const str[] input) throws NotAWord, std.array::push_error<Entry>,
    std.dict::insert_error<str, usize>, std.alloc::alloc_error {
    array<Entry> entries = std.array::create::<Entry>();
    dict<str, usize> positions = std.dict::create::<str, usize>();
    Lengths lengths = {};
    for (usize index = 0usize; index < len(input); index += 1usize) {
        str word = input[index];
        if (is_word(word) == false) {
            throw NotAWord {.position = index + 1usize};
        }
        *lengths.bucket(len(word)) += 1u32;
        if (std.dict::contains(&positions, &word) == true) {
            /* A repeated word is counted through an exclusive borrow of its entry
               (Core R-BORROW-0002, R-LIB-0019). */
            usize position = position_of(std.dict::get(&positions, &word));
            switch (std.array::get_mut(&entries, position)) {
            case variant o::some(entry):
                (*entry)->count += 1u32;
                break;
            case variant o::none:
                return impossible("a counted word has no entry");
            }
        } else {
            std.dict::insert(&positions, word, len(entries)) as void;
            std.array::push(&entries, Entry {.word = word, .count = 1u32});
        }
    }
    /* Word sizes, counted through an array of exclusive borrows of three counters; the counters
       are read again once the array is cleared (Core R-BORROW-0002). */
    u32 short_size = 0u32;
    u32 medium_size = 0u32;
    u32 long_size = 0u32;
    array<u32*> sizes = std.array::create::<u32*>();
    try {
        std.array::push(&sizes, &short_size);
        std.array::push(&sizes, &medium_size);
        std.array::push(&sizes, &long_size);
    } catch (std.array::push_error<u32*> failure) {
        move failure as void;
        return impossible("three counters do not fit");
    }
    str top = "";
    u32 top_count = 0u32;
    str longest_word = "";
    list<const Entry*> repeated = std.list::create::<const Entry*>();
    for (const Entry* entry in &entries) {
        if (entry->count > top_count) {
            top_count = entry->count;
            top = entry->word;
        }
        if (len(entry->word) > len(longest_word)) {
            longest_word = entry->word;
        }
        usize size = 2usize;
        if (len(entry->word) < 4usize) {
            size = 0usize;
        } else {
            if (len(entry->word) < 8usize) {
                size = 1usize;
            }
        }
        switch (std.array::get_mut(&sizes, size)) {
        case variant o::some(counter):
            ***counter += entry->count;
            break;
        case variant o::none:
            return impossible("a size class has no counter");
        }
        if (entry->count > 1u32) {
            /* A shared borrow of the entry is kept past this iteration (Core R-BORROW-0018). */
            try {
                std.list::push_back(&repeated, entry) as void;
            } catch (std.list::push_error<const Entry*> failure) {
                move failure as void;
                return impossible("a repeated word does not fit");
            }
        }
    }
    std.array::clear(&sizes);
    usize longest_size = len(longest_word);
    u32 short_words = lengths.buckets[1] + lengths.buckets[2] + lengths.buckets[3];
    usize total = len(input);
    usize unique = len(positions);
    std.string::string report = f"words={total} distinct={unique} top={top}:{top_count} ";
    std.string::string longest_text = f"longest={longest_word}:{longest_size} short={short_words} ";
    str longest_view = longest_text.as_str();
    report.append(longest_view);
    std.string::string sizes_text = f"sizes={short_size}/{medium_size}/{long_size} repeated=";
    str sizes_view = sizes_text.as_str();
    report.append(sizes_view);
    append_repeated(&report, &repeated);
    sort_by_count(&entries);
    report.append(" ranking=");
    append_ranking(&report, &entries);
    report.append("\n");
    return move report;
}
