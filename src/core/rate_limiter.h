#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>

namespace dm {

// Limite de velocidade por "balde de fichas": cada byte recebido gasta uma ficha; as fichas voltam
// no ritmo do limite. Pode ser compartilhado por várias conexões (limite total) ou usado por um download.
class RateLimiter {
public:
    using Clock = std::chrono::steady_clock;

    // 0 = sem limite.
    void setRate(int64_t bytesPerSecond);
    int64_t rate() const;

    // Registra bytes recebidos e devolve quanto tempo esperar antes de ler mais.
    std::chrono::nanoseconds consume(int64_t bytes, Clock::time_point now = Clock::now());

private:
    mutable std::mutex mutex_;
    int64_t rate_ = 0;
    double available_ = 0;
    Clock::time_point last_{};
    bool started_ = false;
};

// Janela do agendador em minutos do dia (0..1439). Se o fim for antes do início, atravessa a meia-noite
// (ex.: 23:00 às 07:00). Início igual ao fim = o dia inteiro.
bool scheduleAllows(bool enabled, int startMinute, int endMinute, int nowMinute);

}  // namespace dm
