#include "r_std_hash.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static uint8_t r_test_hex_digit(char value) {
    if ((value >= '0') && (value <= '9')) {
        return (uint8_t)(value - '0');
    }
    return (uint8_t)(value - 'a' + 10);
}

static _Bool
r_test_digest_equal(const uint8_t *actual, size_t actual_length, const char *expected_hex) {
    size_t index;

    if (strlen(expected_hex) != (actual_length * 2U)) {
        return 0;
    }
    for (index = 0U; index < actual_length; ++index) {
        const uint8_t high = r_test_hex_digit(expected_hex[index * 2U]);
        const uint8_t low = r_test_hex_digit(expected_hex[(index * 2U) + 1U]);
        const uint8_t expected = (uint8_t)((uint8_t)(high << 4U) | low);

        if (actual[index] != expected) {
            return 0;
        }
    }
    return 1;
}

static int r_test_md5_vector(const uint8_t *source, size_t length, const char *expected) {
    const RStdHashMd5Digest digest = r_std_hash_md5((RStdHashByteView){source, length});

    R_TEST_CHECK(r_test_digest_equal(digest.bytes, sizeof(digest.bytes), expected));
    return 0;
}

static int r_test_hashes(void) {
    static const struct {
        size_t length;
        const char *digest;
    } md5_boundaries[] = {
        {55U, "ef1772b6dff9a122358552954ad0df65"},
        {56U, "3b0c8ac703f828b04c6c197006d17218"},
        {63U, "b06521f39153d618550606be297466d5"},
        {64U, "014842d480b571495a4a0363793f7367"},
        {65U, "c743a45e0d2e6a95cb859adae0248435"},
    };
    const uint8_t digits[] = "123456789";
    const uint8_t abc[] = "abc";
    const uint8_t md5_message[] = "message digest";
    const uint8_t alphabet[] = "abcdefghijklmnopqrstuvwxyz";
    const uint8_t alpha_numeric[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    const uint8_t repeated_digits[] =
        "12345678901234567890123456789012345678901234567890123456789012345678901234567890";
    const uint8_t long_sha256[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    const uint8_t long_sha512[] = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                                  "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    uint8_t binary[256];
    uint8_t md5_padding[65];
    RStdHashSha1Digest sha1;
    RStdHashSha256Digest sha256;
    RStdHashSha512Digest sha512;
    size_t index;

    for (index = 0U; index < sizeof(binary); ++index) {
        binary[index] = (uint8_t)index;
    }
    (void)memset(md5_padding, 'a', sizeof(md5_padding));

    R_TEST_CHECK(r_std_hash_crc32((RStdHashByteView){NULL, 0U}) == UINT32_C(0));
    R_TEST_CHECK(r_std_hash_crc32((RStdHashByteView){digits, sizeof(digits) - 1U}) ==
                 UINT32_C(0xcbf43926));
    R_TEST_CHECK(r_std_hash_crc32((RStdHashByteView){binary, sizeof(binary)}) ==
                 UINT32_C(0x29058c73));

    R_TEST_CHECK(r_test_md5_vector(NULL, 0U, "d41d8cd98f00b204e9800998ecf8427e") == 0);
    R_TEST_CHECK(r_test_md5_vector((const uint8_t *)"a", 1U, "0cc175b9c0f1b6a831c399e269772661") ==
                 0);
    R_TEST_CHECK(r_test_md5_vector(abc, sizeof(abc) - 1U, "900150983cd24fb0d6963f7d28e17f72") == 0);
    R_TEST_CHECK(r_test_md5_vector(md5_message,
                                   sizeof(md5_message) - 1U,
                                   "f96b697d7cb7938d525a2f31aaf161d0") == 0);
    R_TEST_CHECK(
        r_test_md5_vector(digits, sizeof(digits) - 1U, "25f9e794323b453885f5181f1b624d0b") == 0);
    R_TEST_CHECK(r_test_md5_vector(
                     alphabet, sizeof(alphabet) - 1U, "c3fcd3d76192e4007dfb496cca67e13b") == 0);
    R_TEST_CHECK(r_test_md5_vector(alpha_numeric,
                                   sizeof(alpha_numeric) - 1U,
                                   "d174ab98d277d9f5a5611c2c9f419d9f") == 0);
    R_TEST_CHECK(r_test_md5_vector(repeated_digits,
                                   sizeof(repeated_digits) - 1U,
                                   "57edf4a22be3c955ac49da2e2107b67a") == 0);
    R_TEST_CHECK(r_test_md5_vector(binary, sizeof(binary), "e2c865db4162bed963bfaa9ef6ac18f0") ==
                 0);
    for (index = 0U; index < sizeof(md5_boundaries) / sizeof(md5_boundaries[0]); ++index) {
        R_TEST_CHECK(r_test_md5_vector(md5_padding,
                                       md5_boundaries[index].length,
                                       md5_boundaries[index].digest) == 0);
    }

    sha1 = r_std_hash_sha1((RStdHashByteView){NULL, 0U});
    R_TEST_CHECK(r_test_digest_equal(
        sha1.bytes, sizeof(sha1.bytes), "da39a3ee5e6b4b0d3255bfef95601890afd80709"));
    sha1 = r_std_hash_sha1((RStdHashByteView){abc, sizeof(abc) - 1U});
    R_TEST_CHECK(r_test_digest_equal(
        sha1.bytes, sizeof(sha1.bytes), "a9993e364706816aba3e25717850c26c9cd0d89d"));
    sha1 = r_std_hash_sha1((RStdHashByteView){long_sha256, sizeof(long_sha256) - 1U});
    R_TEST_CHECK(r_test_digest_equal(
        sha1.bytes, sizeof(sha1.bytes), "84983e441c3bd26ebaae4aa1f95129e5e54670f1"));
    sha1 = r_std_hash_sha1((RStdHashByteView){binary, sizeof(binary)});
    R_TEST_CHECK(r_test_digest_equal(
        sha1.bytes, sizeof(sha1.bytes), "4916d6bdb7f78e6803698cab32d1586ea457dfc8"));

    sha256 = r_std_hash_sha256((RStdHashByteView){NULL, 0U});
    R_TEST_CHECK(
        r_test_digest_equal(sha256.bytes,
                            sizeof(sha256.bytes),
                            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    sha256 = r_std_hash_sha256((RStdHashByteView){abc, sizeof(abc) - 1U});
    R_TEST_CHECK(
        r_test_digest_equal(sha256.bytes,
                            sizeof(sha256.bytes),
                            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    sha256 = r_std_hash_sha256((RStdHashByteView){long_sha256, sizeof(long_sha256) - 1U});
    R_TEST_CHECK(
        r_test_digest_equal(sha256.bytes,
                            sizeof(sha256.bytes),
                            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    sha256 = r_std_hash_sha256((RStdHashByteView){binary, sizeof(binary)});
    R_TEST_CHECK(
        r_test_digest_equal(sha256.bytes,
                            sizeof(sha256.bytes),
                            "40aff2e9d2d8922e47afd4648e6967497158785fbd1da870e7110266bf944880"));

    sha512 = r_std_hash_sha512((RStdHashByteView){NULL, 0U});
    R_TEST_CHECK(
        r_test_digest_equal(sha512.bytes,
                            sizeof(sha512.bytes),
                            "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
                            "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e"));
    sha512 = r_std_hash_sha512((RStdHashByteView){abc, sizeof(abc) - 1U});
    R_TEST_CHECK(
        r_test_digest_equal(sha512.bytes,
                            sizeof(sha512.bytes),
                            "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2"
                            "192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"));
    sha512 = r_std_hash_sha512((RStdHashByteView){long_sha512, sizeof(long_sha512) - 1U});
    R_TEST_CHECK(
        r_test_digest_equal(sha512.bytes,
                            sizeof(sha512.bytes),
                            "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
                            "501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909"));
    sha512 = r_std_hash_sha512((RStdHashByteView){binary, sizeof(binary)});
    R_TEST_CHECK(
        r_test_digest_equal(sha512.bytes,
                            sizeof(sha512.bytes),
                            "1e7b80bc8edc552c8feeb2780e111477e5bc70465fac1a77b29b35980c3f0ce4"
                            "a036a6c9462036824bd56801e62af7e9feba5c22ed8a5af877bf7de117dcac6d"));
    for (index = 0U; index < sizeof(binary); ++index) {
        R_TEST_CHECK(binary[index] == (uint8_t)index);
    }
    return 0;
}

static int r_test_padding_boundaries(void) {
    static const struct {
        size_t length;
        const char *sha1;
        const char *sha256;
        const char *sha512;
    } vectors[] = {
        {55U,
         "c1c8bbdc22796e28c0e15163d20899b65621d65a",
         "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318",
         "b0220c772cbf6c1822e2cb38a437d0e1d58772417a4bbb21c961364f8b6143e05aa6316dca8d1d7b19e164484"
         "19076395f6086cb55101fbd6d5497b148e1745f"},
        {56U,
         "c2db330f6083854c99d4b5bfb6e8f29f201be699",
         "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a",
         "962b64aae357d2a4fee3ded8b539bdc9d325081822b0bfc55583133aab44f18bafe11d72a7ae16c79ce2ba620"
         "ae2242d5144809161945f1367f41b3972e26e04"},
        {63U,
         "03f09f5b158a7a8cdad920bddc29b81c18a551f5",
         "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34",
         "c1b0f5c6d3b03dfe4a2602e67242f54e344090b66e01100a469b129f583f016c7e27dddeaa438393dcc7ec54b"
         "0b57c9ba7af007f9b56db5f6fb677d972a31362"},
        {64U,
         "0098ba824b5c16427bd7a1122a5a442a25ec644d",
         "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb",
         "01d35c10c6c38c2dcf48f7eebb3235fb5ad74a65ec4cd016e2354c637a8fb49b695ef3c1d6f7ae4cd74d78cc9"
         "c9bcac9d4f23a73019998a7f73038a5c9b2dbde"},
        {65U,
         "11655326c708d70319be2610e8a57d9a5b959d3b",
         "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0",
         "b83086cd8494e55708ad7ecd82dfb4bca1bda61ecbb7caf0c68967902e709345e5d8305eb7ac0d588afc6cbb7"
         "5161aa9c8c7e0ea986bd833dafe5e1ccd37345a"},
        {111U,
         "ac877859d427d9192054eea8feb3b8a403ef83a5",
         "6374f73208854473827f6f6a3f43b1f53eaa3b82c21c1a6d69a2110b2a79baad",
         "fa9121c7b32b9e01733d034cfc78cbf67f926c7ed83e82200ef86818196921760b4beff48404df811b9538282"
         "74461673c68d04e297b0eb7b2b4d60fc6b566a2"},
        {112U,
         "689993727ba37386bb032495e9dbdfb4dd1ba744",
         "f54353008a2553262ecdc4a34749563ba0950e8b0fc8652780b0a614b99683c1",
         "c01d080efd492776a1c43bd23dd99d0a2e626d481e16782e75d54c2503b5dc32bd05f0f1ba33e568b88fd2d97"
         "0929b719ecbb152f58f130a407c8830604b70ca"},
        {127U,
         "89d95fa32ed44a7c610b7ee38517ddf57e0bb975",
         "c57e9278af78fa3cab38667bef4ce29d783787a2f731d4e12200270f0c32320a",
         "828613968b501dc00a97e08c73b118aa8876c26b8aac93df128502ab360f91bab50a51e088769a5c1eff4782a"
         "ce147dce3642554199876374291f5d921629502"},
        {128U,
         "ad5b3fdbcb526778c2839d2f151ea753995e26a0",
         "6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e",
         "b73d1929aa615934e61a871596b3f3b33359f42b8175602e89f7e06e5f658a243667807ed300314b95cacdd57"
         "9f3e33abdfbe351909519a846d465c59582f321"},
        {129U,
         "d96debf1bdcbc896e6c134ea76e8141f40d78536",
         "c12cb024a2e5551cca0e08fce8f1c5e314555cc3fef6329ee994a3db752166ae",
         "4f681e0bd53cda4b5a2041cc8a06f2eabde44fb16c951fbd5b87702f07aeab611565b19c47fde30587177ebb8"
         "52e3971bbd8d3fd30da18d71037dfbd98420429"},
    };
    uint8_t source[129];
    size_t index;

    (void)memset(source, 'a', sizeof(source));
    for (index = 0U; index < sizeof(vectors) / sizeof(vectors[0]); ++index) {
        const RStdHashByteView view = {source, vectors[index].length};
        const RStdHashSha1Digest sha1 = r_std_hash_sha1(view);
        const RStdHashSha256Digest sha256 = r_std_hash_sha256(view);
        const RStdHashSha512Digest sha512 = r_std_hash_sha512(view);

        R_TEST_CHECK(r_test_digest_equal(sha1.bytes, sizeof(sha1.bytes), vectors[index].sha1));
        R_TEST_CHECK(
            r_test_digest_equal(sha256.bytes, sizeof(sha256.bytes), vectors[index].sha256));
        R_TEST_CHECK(
            r_test_digest_equal(sha512.bytes, sizeof(sha512.bytes), vectors[index].sha512));
    }
    for (index = 0U; index < sizeof(source); ++index) {
        R_TEST_CHECK(source[index] == (uint8_t)'a');
    }
    return 0;
}

int main(void) {
    R_TEST_CHECK(r_test_hashes() == 0);
    return r_test_padding_boundaries();
}
