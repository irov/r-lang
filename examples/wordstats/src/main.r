module example.wordstats.main;
import std.console;
import example.wordstats.words;

/* wordstats WORD...: prints the number of words and of distinct words, the most frequent word
   with its count, the longest word with its length, the number of words shorter than four
   letters, the words by size and the repeated words. A word that is not made of ASCII letters
   exits with 65. */
async i32 main(const str[] arguments) {
    try {
        usize count = len(arguments);
        if (count == 1usize) {
            await std.console::print(std.string::from_str("wordstats WORD...\n"));
            return 0;
        }
        std.string::string report =
            example.wordstats.words::summarize(arguments[1usize..count]);
        await std.console::print(move report);
        return 0;
    } catch (example.wordstats.words::NotAWord failure) {
        return 65;
    } catch (std.array::push_error<example.wordstats.words::Entry> failure) {
        return 71;
    } catch (std.dict::insert_error<str, usize> failure) {
        return 71;
    }
}
