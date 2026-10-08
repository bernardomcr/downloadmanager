#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace dm {

// Faixa [start, end) do arquivo baixada por uma conexão por vez.
// written: até onde os bytes já foram gravados em disco (é o que vai para o estado de retomada).
// reserved: até onde a conexão dona já pegou bytes para gravar (>= written).
struct Segment {
    int64_t start = 0;
    int64_t end = 0;
    int64_t written = 0;
    int64_t reserved = 0;
    bool owned = false;

    int64_t remaining() const { return end - reserved; }
    bool complete() const { return written >= end; }
};

// Divisão dinâmica de segmentos (o "segredo" do IDM): o download começa com um único
// segmento; cada conexão nova que fica livre corta ao meio o segmento com mais bytes
// faltando e assume a segunda metade. Assim todas as conexões trabalham até o fim.
class SegmentPlanner {
public:
    struct Claim {
        int64_t offset = 0;
        int64_t length = 0;
    };

    SegmentPlanner(int64_t totalSize, int64_t minSplitSize);

    // Retomada: substitui os segmentos pelos salvos. Nenhum fica com dono.
    void restore(std::vector<Segment> segments);

    // Download novo: o arquivo inteiro como segmento 0, já com dono (a conexão que sondou o servidor).
    size_t acquireFirst();

    // Próximo trabalho para uma conexão livre: um segmento pendente sem dono, ou a metade
    // final do maior segmento em andamento. Vazio quando não há mais nada que valha dividir.
    std::optional<size_t> acquire();

    // A conexão recebeu `wanted` bytes; devolve onde gravar e quantos cabem no segmento
    // (o fim do segmento pode ter encolhido por uma divisão).
    Claim reserve(size_t index, int64_t wanted);
    // Os bytes reservados foram gravados em disco.
    void commit(size_t index, int64_t length);
    // A conexão largou o segmento (erro ou pausa); reservas não gravadas são descartadas.
    void release(size_t index);

    bool reachedEnd(size_t index) const;
    int64_t position(size_t index) const;
    int64_t end(size_t index) const;

    // Muda o mínimo para dividir (o motor ajusta pela velocidade: dividir o que a conexão dona termina em
    // poucos segundos sai mais caro que abrir uma conexão nova). Nunca abaixo do mínimo do construtor.
    void setMinSplit(int64_t bytes);

    bool allComplete() const;
    int64_t bytesWritten() const;
    int64_t totalSize() const { return total_; }
    std::vector<Segment> snapshot() const;

private:
    mutable std::mutex mutex_;
    std::vector<Segment> segments_;
    int64_t total_;
    int64_t baseMinSplit_;
    int64_t minSplit_;
};

}  // namespace dm
