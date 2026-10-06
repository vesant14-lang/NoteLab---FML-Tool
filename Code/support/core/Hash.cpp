#include "Hash.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace fml {
namespace {

constexpr std::array<std::uint32_t, 64> kTable = {{
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
}};

inline std::uint32_t rotr(std::uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32u - bits));
}

struct Sha256Context {
    std::array<std::uint32_t, 8> state = {{
        0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u
    }};
    std::array<std::uint8_t, 64> buffer{};
    size_t buffered = 0;
    std::uint64_t totalBytes = 0;

    void block(const std::uint8_t* data) {
        std::array<std::uint32_t, 64> words{};
        for (size_t i = 0; i < 16; ++i) {
            const size_t p = i * 4u;
            words[i] = (static_cast<std::uint32_t>(data[p]) << 24u) |
                       (static_cast<std::uint32_t>(data[p + 1]) << 16u) |
                       (static_cast<std::uint32_t>(data[p + 2]) << 8u) |
                        static_cast<std::uint32_t>(data[p + 3]);
        }
        for (size_t i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(words[i - 15], 7) ^ rotr(words[i - 15], 18) ^
                                     (words[i - 15] >> 3);
            const std::uint32_t s1 = rotr(words[i - 2], 17) ^ rotr(words[i - 2], 19) ^
                                     (words[i - 2] >> 10);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }

        std::uint32_t a=state[0], b=state[1], c=state[2], d=state[3];
        std::uint32_t e=state[4], f=state[5], g=state[6], h=state[7];
        for (size_t i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t choice = (e & f) ^ ((~e) & g);
            const std::uint32_t t1 = h + s1 + choice + kTable[i] + words[i];
            const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = s0 + majority;
            h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
        state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
    }

    void update(const void* input, size_t size) {
        const auto* data = static_cast<const std::uint8_t*>(input);
        totalBytes += size;
        while (size > 0) {
            const size_t take = std::min(size, buffer.size() - buffered);
            std::memcpy(buffer.data() + buffered, data, take);
            buffered += take;
            data += take;
            size -= take;
            if (buffered == buffer.size()) {
                block(buffer.data());
                buffered = 0;
            }
        }
    }

    std::string finish() {
        const std::uint64_t bitLength = totalBytes * 8u;
        const std::uint8_t marker = 0x80u;
        updateWithoutCount(&marker, 1);
        const std::uint8_t zero = 0;
        while (buffered != 56u) updateWithoutCount(&zero, 1);
        std::uint8_t length[8]{};
        for (int shift = 56, i = 0; shift >= 0; shift -= 8, ++i)
            length[i] = static_cast<std::uint8_t>((bitLength >> shift) & 0xffu);
        updateWithoutCount(length, sizeof length);

        char output[65] = {};
        for (size_t i = 0; i < state.size(); ++i)
            std::snprintf(output + i * 8u, 9u, "%08x", state[i]);
        return output;
    }

private:
    void updateWithoutCount(const void* input, size_t size) {
        const std::uint64_t before = totalBytes;
        update(input, size);
        totalBytes = before;
    }
};

}  // namespace

std::string sha256Hex(const std::string& bytes) {
    Sha256Context context;
    context.update(bytes.data(), bytes.size());
    return context.finish();
}

std::optional<std::string> sha256File(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::nullopt;
    Sha256Context context;
    // Mantenerlo muy por debajo del stack de 1 MiB por defecto de MSVC.
    std::array<char, 64u * 1024u> buffer{};
    while (stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize read = stream.gcount();
        if (read > 0) context.update(buffer.data(), static_cast<size_t>(read));
    }
    if (!stream.eof()) return std::nullopt;
    return context.finish();
}

}  // namespace fml
