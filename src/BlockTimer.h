// Chronomètre de bloc — mesure J2 du coût par appel de processBlock.
// Préalloué, sans allocation ni verrou dans le thread audio. Le calcul des
// statistiques (copie + tri) se fait hors audio.
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

namespace plug
{
    class BlockTimer
    {
    public:
        static constexpr size_t kCapacity = 1u << 18; // 262 144 blocs, soit 11,6 min à 128 / 48 kHz

        // Allocation unique à la construction (hors thread audio).
        BlockTimer() : samples (kCapacity, 0u) {}

        struct Stats
        {
            size_t count = 0;      // blocs retenus (les kCapacity derniers)
            size_t total = 0;      // blocs vus depuis le reset
            double meanUs = 0, p50Us = 0, p99Us = 0, p999Us = 0, maxUs = 0;
            uint32_t minBlock = 0, maxBlock = 0;
        };

        void reset() noexcept
        {
            writeIndex = 0; total = 0; minBlockSize = UINT32_MAX; maxBlockSize = 0;
        }

        // Thread audio : deux appels par bloc, aucune allocation.
        void begin() noexcept { start = std::chrono::steady_clock::now(); }

        void end (uint32_t blockSize) noexcept
        {
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now() - start).count();
            samples[writeIndex] = (uint32_t) std::min<long long> (ns, (long long) UINT32_MAX);
            writeIndex = (writeIndex + 1) % kCapacity;
            ++total;
            minBlockSize = std::min (minBlockSize, blockSize);
            maxBlockSize = std::max (maxBlockSize, blockSize);
        }

        // Hors audio uniquement.
        Stats compute() const
        {
            Stats s;
            const size_t n = std::min (total, kCapacity);
            s.total = total;
            if (n == 0) return s;

            std::vector<uint32_t> sorted (samples.begin(), samples.begin() + (std::ptrdiff_t) n);
            std::sort (sorted.begin(), sorted.end());

            double sum = 0;
            for (auto v : sorted) sum += v;

            auto pct = [&] (double p) { return sorted[std::min (n - 1, (size_t) (p * (double) n))] / 1000.0; };

            s.count = n;
            s.meanUs = sum / (double) n / 1000.0;
            s.p50Us = pct (0.5); s.p99Us = pct (0.99); s.p999Us = pct (0.999);
            s.maxUs = sorted.back() / 1000.0;
            s.minBlock = minBlockSize; s.maxBlock = maxBlockSize;
            return s;
        }

    private:
        std::vector<uint32_t> samples;
        std::chrono::steady_clock::time_point start;
        size_t writeIndex = 0, total = 0;
        uint32_t minBlockSize = UINT32_MAX, maxBlockSize = 0;
    };
}
