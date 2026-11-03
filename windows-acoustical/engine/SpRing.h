// SpRing.h — Anillo float SPSC lock-free (un productor / un consumidor).
//
// Sustituye al antiguo anillo de AcousticalEngine (std::deque<float> +
// std::mutex ringMutex_). El deque era el primer candidato a matar al DAW:
// pushSamples() corre en el HILO DE AUDIO (processBlock del plugin /
// AudioCallback de la app) y el hilo del motor sostenía el mismo mutex
// DURANTE todo su tick (con la FFT de hasta 8192 muestras dentro). El
// callback de audio se bloqueaba en la mutex hasta que el motor terminaba su
// tick, y un host sin watchdog de hilo de audio (Cubase) se queda congelado
// entero y muere.
//
// El contrato nuevo:
//   - Productor = el hilo de audio (processBlock / AudioCallback): push().
//     Sin mutex y sin heap: escribe en un buffer fijo y avanza un contador de
//     64 bits con un store release. (std::atomic<int64_t> es lock-free en
//     x86 32/64 — cmpxchg64 — y en ARM64.)
//   - Consumidor = el hilo del motor (engineLoop): copyLatest(). Solo lee
//     slots <= al contador que obtuvo con un load acquire — nunca un slot que
//     el productor no haya publicado. El consumidor NUNCA escribe (es uno
//     solo, el hilo del motor).
//   - Si el anillo se llena el productor descarta lo más antiguo (avanza el
//     puntero del consumidor): el lector sigue viendo la cola más fresca, que
//     es exactamente lo que hacía el deque (pop_front al llenarse).
//
// La capacidad es POTENCIA DE 2 (enmascaramiento del índice). 8192*4 =
// 32768 floats ≈ 0,68 s a 48 kHz: la misma capacidad que el deque viejo.
//
// C++17 puro, sin dependencias: compila con el mismo código en MSVC
// (plugin + app), el NDK (Android) y el gate dsp de la CI (g++ Linux).
// La semántica de memoria es la misma de los anillos SPSC que ya usa el
// resto del plugin (InstanceLink) y la app (PreRing/RemoteAudioLink).
#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <new>

namespace acoustical {

class SpRing {
public:
    explicit SpRing(size_t capacityPow2)
        : mask_(capacityPow2 - 1),
          data_(static_cast<float*>(::operator new(sizeof(float) * capacityPow2))) {
        std::memset(data_, 0, sizeof(float) * capacityPow2);
    }

    ~SpRing() { ::operator delete(data_); }
    SpRing(const SpRing&) = delete;
    SpRing& operator=(const SpRing&) = delete;

    size_t capacity() const { return mask_ + 1; }

    /* (Hilo de audio) Añade n muestras. Si el anillo no cabe todo, descarta
     * lo más antiguo para que el consumidor vea siempre la cola más fresca.
     * Sin locks, sin malloc: un memcpy (una o dos tramas) y un store release.
     *
     * La publicación es atómica a nivel de muestra: los samples se escriben
     * ANTES del store release de tail_, y el consumidor solo lee slots
     * estrictamente por debajo del tail_ que carga con acquire — por lo que
     * nunca lee una muestra a medias. */
    void push(const float* s, int n) {
        if (n <= 0) return;
        const int64_t cap = (int64_t)mask_ + 1;
        int64_t tail = tail_.load(std::memory_order_relaxed);
        int64_t head = head_.load(std::memory_order_acquire);
        if (tail - head + n > cap) {
            // El consumidor no escribe head_: solo el productor lo avanza
            // (descartando lo antiguo), así el "load" de arriba es para
            // calcular el hueco, no una sincronización.
            head_ = (head = tail + n - cap);
        }
        const size_t base = (size_t)tail & mask_;
        const size_t first = (base + (size_t)n > (size_t)cap) ? (size_t)cap - base : (size_t)n;
        std::memcpy(data_ + base, s, first * sizeof(float));
        if (first < (size_t)n)
            std::memcpy(data_, s + first, ((size_t)n - first) * sizeof(float));
        tail_.store(tail + n, std::memory_order_release);
    }

    /* (Hilo del motor) Copia en dst las últimas hasta `need` muestras (menos
     * si no hay tantas). Devuelve cuántas copió. Sin locks: solo atomicos.
     * Un solo consumidor (el hilo del motor) llama a esto; si la UI/tests
     * quieren leer, copian desde su propio contexto (el test lo hace así). */
    int copyLatest(float* dst, int need) const {
        if (need <= 0) return 0;
        const int64_t tail = tail_.load(std::memory_order_acquire);
        const int64_t head = head_.load(std::memory_order_relaxed);
        const int64_t avail = tail - head;
        if (avail <= 0) return 0;
        const int64_t take = avail < need ? avail : (int64_t)need;
        const int64_t base = tail - take;
        for (int64_t i = 0; i < take; ++i)
            dst[i] = data_[((size_t)(base + i)) & mask_];
        return (int)take;
    }

    /* (Cualquier hilo; estado/tests) muestras pendientes en el anillo. */
    int64_t size() const {
        const int64_t tail = tail_.load(std::memory_order_acquire);
        const int64_t head = head_.load(std::memory_order_acquire);
        return tail > head ? tail - head : 0;
    }

private:
    const size_t mask_;
    float* const data_;
    // alignas(64): cada contador en su propia línea de caché (no false
    // sharing entre el store del productor y el load del consumidor).
    alignas(64) std::atomic<int64_t> head_{0};
    alignas(64) std::atomic<int64_t> tail_{0};
};

}  // namespace acoustical
