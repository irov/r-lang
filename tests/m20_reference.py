"""Reference values for M20: SplitMix64-seeded xoshiro256**, below/range/fill/shuffle, and
HMAC vectors of RFC 4231 (computed with the Python standard library)."""
import hashlib, hmac

MASK = (1 << 64) - 1

def rotl(x, k):
    return ((x << k) | (x >> (64 - k))) & MASK

class Generator:
    def __init__(self, seed):
        x = seed & MASK
        words = []
        for _ in range(4):
            x = (x + 0x9e3779b97f4a7c15) & MASK
            y = ((x ^ (x >> 30)) * 0xbf58476d1ce4e5b9) & MASK
            z = ((y ^ (y >> 27)) * 0x94d049bb133111eb) & MASK
            words.append(z ^ (z >> 31))
        self.s = words
    def next_u64(self):
        s0, s1, s2, s3 = self.s
        result = (rotl((s1 * 5) & MASK, 7) * 9) & MASK
        t = (s1 << 17) & MASK
        s2 ^= s0; s3 ^= s1; s1 ^= s2; s0 ^= s3; s2 ^= t; s3 = rotl(s3, 45)
        self.s = [s0, s1, s2, s3]
        return result
    def next_u32(self):
        return self.next_u64() >> 32
    def below(self, bound):
        limit = ((1 << 64) - bound) % bound
        while True:
            x = self.next_u64()
            if x >= limit:
                return x % bound
    def range(self, low, high):
        return low + self.below(high - low)
    def fill(self, n):
        out = bytearray()
        while len(out) < n:
            v = self.next_u64()
            take = min(8, n - len(out))
            out += v.to_bytes(8, 'little')[:take]
        return bytes(out)
    def shuffle(self, items):
        items = list(items)
        i = len(items)
        while i > 1:
            i -= 1
            j = self.below(i + 1)
            items[i], items[j] = items[j], items[i]
        return items

if __name__ == '__main__':
    g = Generator(0)
    print('seed0 u64:', [hex(g.next_u64()) for _ in range(4)])
    g = Generator(42)
    print('seed42 u64:', [g.next_u64() for _ in range(3)])
    print('seed42 u32:', [g.next_u32() for _ in range(2)])
    print('seed42 below(10):', [g.below(10) for _ in range(5)])
    print('seed42 range(100,200):', [g.range(100, 200) for _ in range(3)])
    print('seed42 fill(11):', list(g.fill(11)))
    print('seed42 shuffle(0..9):', g.shuffle(range(10)))
    cases = [
        (b'\x0b' * 20, b'Hi There'),
        (b'Jefe', b'what do ya want for nothing?'),
        (b'\xaa' * 20, b'\xdd' * 50),
        (bytes(range(1, 26)), b'\xcd' * 50),
        (b'\x0c' * 20, b'Test With Truncation'),
        (b'\xaa' * 131, b'Test Using Larger Than Block-Size Key - Hash Key First'),
        (b'\xaa' * 131, b'This is a test using a larger than block-size key and a larger than block-size data. The key needs to be hashed before being used by the HMAC algorithm.'),
    ]
    for i, (k, m) in enumerate(cases, 1):
        print(i, hmac.new(k, m, hashlib.sha256).hexdigest(), hmac.new(k, m, hashlib.sha512).hexdigest())
